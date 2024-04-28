// sqnice/database.cc
//
// The MIT License
//
// Copyright (c) 2015 Wongoo Lee (iwongu at gmail dot com)
// Copyright (c) 2024 Jens Alfke (Github: snej)
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
// THE SOFTWARE.


#include "sqnice/database.hh"
#include "sqnice/query.hh"
#include "sqnice/statement_cache.hh"
#include <cstdio>
#include <cstring>
#include <cassert>
#include <mutex>

#ifdef SQNICE_LOADABLE_EXTENSION
#  include <sqlite3ext.h>
SQLITE_EXTENSION_INIT1
#else
#  include <sqlite3.h>
#endif

namespace sqnice {
    using namespace std;


#pragma mark - DATABASE:


    database::database() noexcept
    : checking(kExceptionsByDefault)
    { }

    database::database(string_view dbname, open_flags flags, char const* vfs)
    : checking(kExceptionsByDefault)
    {
        open(dbname, flags, vfs);
    }

    database::database(sqlite3* pdb) noexcept
    : checking(kExceptionsByDefault)
    , db_(pdb, [](sqlite3*) { })    // do nothing when the last shared_ptr ref is gone
    {
        weak_db_ = db_;
    }

    database::database(database&& db) noexcept
    : checking(db.exceptions_)
    , db_(std::move(db.db_))
    , bh_(std::move(db.bh_))
    , ch_(std::move(db.ch_))
    , rh_(std::move(db.rh_))
    , uh_(std::move(db.uh_))
    , ah_(std::move(db.ah_))
    {
        weak_db_ = db_;
        db.weak_db_ = {};
    }

    database& database::operator=(database&& db) noexcept {
        static_cast<checking&>(*this) = static_cast<checking&&>(db);
        set_db(db.db_);
        set_db(std::move(db.db_));
        db.weak_db_ = {};
        bh_ = std::move(db.bh_);
        ch_ = std::move(db.ch_);
        rh_ = std::move(db.rh_);
        uh_ = std::move(db.uh_);
        ah_ = std::move(db.ah_);

        return *this;
    }

    database::~database() noexcept {
        if (db_)
            tear_down();
    }

    status database::open(string_view dbname_, open_flags flags, char const* vfs) {
        close();

        if (!!(flags & open_flags::memory) && !(flags & (open_flags::readwrite | open_flags::readonly)))
            flags |= open_flags::readwrite;

        string dbname(dbname_);
        // "It is recommended that when a database filename actually does begin with a ":" character
        // you should prefix the filename with a pathname such as "./" to avoid ambiguity."
        if (dbname.starts_with(":") && dbname != ":memory:" && !(flags & open_flags::uri))
            dbname = "./" + dbname; //FIXME: Is this OK on Windows?

        sqlite3* db = nullptr;
        auto rc = status{sqlite3_open_v2(dbname.c_str(), &db,
                                         int(flags) | SQLITE_OPEN_EXRESCODE,
                                         vfs)};
        if (ok(rc)) {
            // deleter function for the shared_ptr:
            auto close_db = [](sqlite3* db) {
                auto rc = status{sqlite3_close(db)};
                if (rc == status::busy) {
                    fprintf(stderr, "**SQLITE WARNING**: A `sqnice::database` object at %p"
                            "is being destructed while there are still open query iterators, blob"
                            " streams or backups. This is bad! (For more information, read the docs for"
                            "`sqnice::database::close`.)\n", (void*)db);
                    sqlite3_db_config(db, SQLITE_DBCONFIG_NO_CKPT_ON_CLOSE, 1, nullptr);
                    (void)sqlite3_close_v2(db);
                }
            };
            set_db(shared_ptr<sqlite3>(db, close_db));
            temporary_ = !!(flags & open_flags::memory) || dbname.empty() || dbname == ":memory:";
            posthumous_error_ = nullptr;

        } else {
            string message = db ? sqlite3_errmsg(db) : "can't open database";
            (void)sqlite3_close_v2(db);
            if (exceptions())
                raise(rc, message.c_str());
            else
                posthumous_error_ = make_unique<database_error>(message.c_str(), rc);
        }
        return rc;
    }

    status database::open_temporary(bool on_disk) {
        // "If the filename is an empty string, then a private, temporary on-disk database will
        // be created [and] automatically deleted as soon as the database connection is closed."
        string_view name;
        open_flags flags = open_flags::readwrite;
        if (!on_disk) {
            name = "temporary";
            flags |= open_flags::memory;
        }
        return open(name, flags);
    }

    status database::close(bool immediately) {
        if (db_) {
            if (immediately && db_.use_count() > 1)
                return check(status::busy);
            tear_down();
            set_db(nullptr);
        }
        return status::ok;
    }

    void database::tear_down() noexcept {
        commands_.reset();
        queries_.reset();
        set_busy_handler(nullptr);
        set_commit_handler(nullptr);
        set_rollback_handler(nullptr);
        set_update_handler(nullptr);
        set_authorize_handler(nullptr);
    }

    sqlite3* database::check_handle() const {
        if (!db_) [[unlikely]]
            throw logic_error("database is not open");
        return db_.get();
    }

    status database::execute(string_view sql) {
        auto rc = status{sqlite3_exec(check_handle(), string(sql).c_str(), nullptr, nullptr, nullptr)};
        if (rc == status::error && exceptions_)
            throw invalid_argument(error_msg());
        return check(rc);
    }

    status database::executef(char const* sql, ...) {
        va_list ap;
        va_start(ap, sql);
        shared_ptr<char> msql(sqlite3_vmprintf(sql, ap), sqlite3_free);
        va_end(ap);

        return execute(msql.get());
    }


    command database::command(string_view sql) {
        if (!commands_)
            commands_ = make_unique<command_cache>(*this);
        return commands_->compile(string(sql));
    }

    query database::query(string_view sql) {
        if (!queries_)
            queries_ = make_unique<query_cache>(*this);
        return queries_->compile(string(sql));
    }


#pragma mark - DATABASE CONFIGURATION:


    tuple<int,int,int> database::sqlite_version() noexcept {
        auto v = sqlite3_libversion_number();
        return {v / 1'000'000, v / 1'000, v % 1'000};
    }

    status database::enable_foreign_keys(bool enable) {
        return check(sqlite3_db_config(check_handle(), 
                                       SQLITE_DBCONFIG_ENABLE_FKEY, enable ? 1 : 0, nullptr));
    }

    status database::enable_triggers(bool enable) {
        return check(sqlite3_db_config(check_handle(),
                                       SQLITE_DBCONFIG_ENABLE_TRIGGER, enable ? 1 : 0, nullptr));
    }

    status database::set_busy_timeout(int ms) {
        return check(sqlite3_busy_timeout(check_handle(), ms));
    }

    status database::setup() {
        assert(is_open());
        status rc = enable_foreign_keys();
        if (!ok(rc)) {return rc;}
        rc = set_busy_timeout(5000);
        if (ok(rc) && is_writeable()) {
            rc = execute("PRAGMA auto_vacuum = incremental;" // must be the first statement executed
                         "PRAGMA journal_mode = WAL;"
                         "PRAGMA synchronous=normal");
        }
        auto dbc = db_.get();
        sqlite3_db_config(dbc, SQLITE_DBCONFIG_DEFENSIVE, 1, 0);  // disallow db corruption
        sqlite3_db_config(dbc, SQLITE_DBCONFIG_DQS_DML,   0, 0); // disallow double-quoted strs
        sqlite3_db_config(dbc, SQLITE_DBCONFIG_DQS_DDL,   0, 0);
        return rc;
    }


    int64_t database::pragma(const char* pragma) {
        return sqnice::query(*this, string("PRAGMA \"") + pragma + "\"").single_value_or<int>(0);
    }

    string database:: string_pragma(const char* pragma) {
        return sqnice::query(*this, string("PRAGMA \"") + pragma + "\"").single_value_or<string>("");
    }

    status database::pragma(const char* pragma, int64_t value) {
        return executef("PRAGMA %s(%d)", pragma, value);
    }

    status database::pragma(const char* pragma, string_view value) {
        return executef("PRAGMA %s(%q)", pragma, string(value).c_str());
    }


    unsigned database::get_limit(limit lim) const noexcept {
        return sqlite3_limit(check_handle(), int(lim), -1);
    }

    unsigned database::set_limit(limit lim, unsigned val) noexcept {
        return sqlite3_limit(check_handle(), int(lim), int(val));
    }


#pragma mark - DATABASE PROPERTIES:


    const char* database::filename() const noexcept {
        return sqlite3_db_filename(check_handle(), nullptr);
    }

    status database::last_status() const noexcept {
        if (db_)
            return status{sqlite3_extended_errcode(db_.get())};
        else if (posthumous_error_)
            return posthumous_error_->error_code;
        else
            return status::cantopen;
    }

    char const* database::error_msg() const noexcept {
        if (db_)
            return sqlite3_errmsg(db_.get());
        else if (posthumous_error_)
            return posthumous_error_->what();
        else
            return nullptr;
    }

    bool database::is_writeable() const noexcept {
        return ! sqlite3_db_readonly(check_handle(), "main");
    }

    int64_t database::last_insert_rowid() const noexcept {
        return sqlite3_last_insert_rowid(check_handle());
    }

    int database::changes() const noexcept {
        return sqlite3_changes(check_handle());
    }

    int64_t database::total_changes() const noexcept {
        return sqlite3_total_changes(check_handle());
    }

    uint32_t database::global_changes() const noexcept {
        uint32_t n;
        sqlite3_file_control(check_handle(), "main",  SQLITE_FCNTL_DATA_VERSION, &n);
        return n;
    }

    bool database::in_transaction() const noexcept {
        return !sqlite3_get_autocommit(check_handle());
    }


#pragma mark - TRANSACTIONS:


    status database::begin_transaction(bool immediate) {
        if (txn_depth_ == 0) {
            if (immediate) {
                if (in_transaction())
                    throw logic_error("unexpectedly already in a transaction");
                // Create an immediate txn, otherwise SAVEPOINT defaults to DEFERRED
                if (auto rc = command("BEGIN IMMEDIATE").execute(); !ok(rc))
                    return rc;
            }
            txn_immediate_ = immediate;
        }

        char sql[30];
        snprintf(sql, sizeof(sql), "SAVEPOINT sp_%d", txn_depth_ + 1);
        if (auto rc = command(sql).execute(); !ok(rc)) {
            if (txn_depth_ == 0 && immediate)
                (void)command("ROLLBACK").execute();
            return rc;
        }

        ++txn_depth_;
        return status::ok;
    }


    status database::end_transaction(bool commit) {
        if (txn_depth_ <= 0) [[unlikely]]
            throw logic_error("transaction underflow");
        char sql[50];
        if (!commit) {
            /// "Instead of cancelling the transaction, the ROLLBACK TO command restarts the
            /// transaction again at the beginning. All intervening SAVEPOINTs are canceled,
            /// however." --https://sqlite.org/lang_savepoint.html
            snprintf(sql, sizeof(sql), "ROLLBACK TO SAVEPOINT sp_%d", txn_depth_);
            if (auto rc = command(sql).execute(); !ok(rc))
                return rc;
            /// Thus we also have to call RELEASE to pop the savepoint from the stack...
        }
        snprintf(sql, sizeof(sql), "RELEASE SAVEPOINT sp_%d", txn_depth_);
        if (auto rc = command(sql).execute(); !ok(rc))
            return rc;

        --txn_depth_;
        if (txn_depth_ == 0) {
            if (txn_immediate_) {
                if (!in_transaction())
                    throw logic_error("unexpectedly not in a transaction");
                if (auto rc = command(commit ? "COMMIT" : "ROLLBACK").execute(); !ok(rc)) {
                    ++txn_depth_;
                    return rc;
                }
            }
        }
        return status::ok;
    }


#pragma mark - BACKUP:


    status database::backup(database& destdb, backup_handler h) {
        return backup("main", destdb, "main", h);
    }

    status database::backup(string_view dbname,
                            database& destdb, 
                            string_view destdbname,
                            backup_handler handler,
                            int step_page)
    {
        auto rc = status::ok;
        sqlite3_backup* bkup = sqlite3_backup_init(destdb.check_handle(),
                                                   string(destdbname).c_str(),
                                                   check_handle(),
                                                   string(dbname).c_str());
        if (!bkup) {
            // "If an error occurs within sqlite3_backup_init, then ... an error code and error
            // message are stored in the destination database connection"
            rc = destdb.last_status();
            if (exceptions_)
                raise(rc, destdb.error_msg());
            return rc;
        }

        // Run the backup incrementally:
        do {
            rc = status{sqlite3_backup_step(bkup, step_page)};
            if (handler)
                handler(sqlite3_backup_remaining(bkup), sqlite3_backup_pagecount(bkup), rc);
        } while (rc == status::ok || rc == status::busy || rc == status::locked);

        // Finish:
        auto end_rc = status{sqlite3_backup_finish(bkup)};
        if (rc == status::done)
            rc = end_rc;
        return check(rc);
    }


    status database::optimize() {
        /* "The optimize pragma is usually a no-op but it will occasionally run ANALYZE if it
         seems like doing so will be useful to the query planner. The analysis_limit pragma
         limits the scope of any ANALYZE command that the optimize pragma runs so that it does
         not consume too many CPU cycles. The constant "400" can be adjusted as needed. Values
         between 100 and 1000 work well for most applications."
         -- <https://sqlite.org/lang_analyze.html> */
        if (!is_writeable())
            return status::ok;
        status rc = pragma("analysis_limit", 400);
        if (ok(rc))
            rc = pragma("optimize", 0xfffe);
        return rc;
    }


    // If this fraction of the database is composed of free pages, vacuum it on close
    static constexpr float kVacuumFractionThreshold = 0.25;
    // If the database has many bytes of free space, vacuum it on close
    static constexpr int64_t kVacuumSizeThreshold = 10'000'000;


    optional<int64_t> database::incremental_vacuum(bool always, int64_t nPages) {
        // <https://blogs.gnome.org/jnelson/2015/01/06/sqlite-vacuum-and-auto_vacuum/>
        if (!is_writeable())
            return nullopt;
        int64_t pageCount = pragma("page_count");
        bool do_it = always;
        if (!always) {
            int64_t freePages = pragma("freelist_count");
            float freeFraction = pageCount ? (float)freePages / pageCount : 0;
            do_it = freeFraction >= kVacuumFractionThreshold
                    || freePages * pragma("page_size") >= kVacuumSizeThreshold;
        }
        if (!do_it)
            return nullopt;

        pragma("incremental_vacuum", nPages);
        if (always) {
            // On explicit compact, truncate the WAL file to save more disk space:
            pragma("wal_checkpoint", "TRUNCATE");
        }
        return pageCount - pragma("page_count");
    }



#pragma mark - HOOKS:

    
    namespace {
        int busy_handler_impl(void* p, int attempts) noexcept {
            auto h = static_cast<database::busy_handler*>(p);
            return (*h)(attempts);
        }

        int commit_hook_impl(void* p) noexcept {
            auto h = static_cast<database::commit_handler*>(p);
            return int((*h)());
        }

        void rollback_hook_impl(void* p) noexcept {
            auto h = static_cast<database::rollback_handler*>(p);
            (*h)();
        }

        void update_hook_impl(void* p, int opcode, char const* dbname, char const* tablename,
                              long long int rowid) noexcept {
            auto h = static_cast<database::update_handler*>(p);
            (*h)(opcode, dbname, tablename, rowid);
        }

        int authorizer_impl(void* p, int action, char const* p1, char const* p2,
                            char const* dbname, char const* tvname) noexcept {
            auto h = static_cast<database::authorize_handler*>(p);
            return int((*h)(action, p1, p2, dbname, tvname));
        }

    } // namespace


    void database::set_log_handler(log_handler h) noexcept {
        static mutex sLogMutex;
        static log_handler sLogHandler;

        lock_guard lock(sLogMutex);
        sLogHandler = std::move(h);
        if (h) {
            auto callback = [](void* p, int errCode, const char* msg) noexcept {
                if ( (errCode & 0xFF) == SQLITE_SCHEMA ) {
                    // ignore noisy and harmless "statement aborts ...database schema has changed"
                    // warning when a sqlite_stmt is executed after a schema change. SQLite
                    // automatically recompiles the statement, so this is not an error.
                    return;
                }
                lock_guard lock(sLogMutex);
                if (sLogHandler)
                    sLogHandler(status{errCode}, msg);
            };
            sqlite3_config(SQLITE_CONFIG_LOG, callback, nullptr);
        } else {
            sqlite3_config(SQLITE_CONFIG_LOG, nullptr, nullptr);
        }
    }

    void database::set_busy_handler(busy_handler h) noexcept {
        bh_ = h;
        sqlite3_busy_handler(check_handle(), bh_ ? busy_handler_impl : nullptr, &bh_);
    }

    void database::set_commit_handler(commit_handler h) noexcept {
        ch_ = h;
        sqlite3_commit_hook(check_handle(), ch_ ? commit_hook_impl : nullptr, &ch_);
    }

    void database::set_rollback_handler(rollback_handler h) noexcept {
        rh_ = h;
        sqlite3_rollback_hook(check_handle(), rh_ ? rollback_hook_impl : nullptr, &rh_);
    }

    void database::set_update_handler(update_handler h) noexcept {
        uh_ = h;
        sqlite3_update_hook(check_handle(), uh_ ? update_hook_impl : nullptr, &uh_);
    }

    void database::set_authorize_handler(authorize_handler h) noexcept {
        ah_ = h;
        sqlite3_set_authorizer(check_handle(), ah_ ? authorizer_impl : nullptr, &ah_);
    }

    status database::register_function(string_view name, 
                                       int nArgs,
                                       function_flags flags,
                                       void* _Nullable pApp,
                                       callFn _Nullable call,
                                       callFn _Nullable step,
                                       finishFn _Nullable finish,
                                       destroyFn _Nullable destroy)
    {
        return check( sqlite3_create_function_v2(check_handle(),
                                                 string(name).c_str(),
                                                 nArgs,
                                                 SQLITE_UTF8 | int(flags),
                                                 pApp, 
                                                 call, step, finish, destroy) );
    }

}
