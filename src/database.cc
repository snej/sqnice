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
#include <cstring>
#include <cassert>
#include <cstdio>

#ifdef SQNICE_LOADABLE_EXTENSION
#  include <sqlite3ext.h>
SQLITE_EXTENSION_INIT1
#else
#  include <sqlite3.h>
#endif

namespace sqnice {

    static_assert(int(status::ok)               == SQLITE_OK);
    static_assert(int(status::error)            == SQLITE_ERROR);
    static_assert(int(status::perm)             == SQLITE_PERM);
    static_assert(int(status::abort)            == SQLITE_ABORT);
    static_assert(int(status::busy)             == SQLITE_BUSY);
    static_assert(int(status::locked)           == SQLITE_LOCKED);
    static_assert(int(status::readonly)         == SQLITE_READONLY);
    static_assert(int(status::interrupt)        == SQLITE_INTERRUPT);
    static_assert(int(status::ioerr)            == SQLITE_IOERR);
    static_assert(int(status::corrupt)          == SQLITE_CORRUPT);
    static_assert(int(status::cantopen)         == SQLITE_CANTOPEN);
    static_assert(int(status::constraint)       == SQLITE_CONSTRAINT);
    static_assert(int(status::mismatch)         == SQLITE_MISMATCH);
    static_assert(int(status::misuse)           == SQLITE_MISUSE);
    static_assert(int(status::auth)             == SQLITE_AUTH);
    static_assert(int(status::range)            == SQLITE_RANGE);
    static_assert(int(status::done)             == SQLITE_DONE);
    static_assert(int(status::row)              == SQLITE_ROW);

    static_assert(int(open_flags::readonly)     == SQLITE_OPEN_READONLY);
    static_assert(int(open_flags::readwrite)    == SQLITE_OPEN_READWRITE);
    static_assert(int(open_flags::create)       == SQLITE_OPEN_CREATE);
    static_assert(int(open_flags::uri)          == SQLITE_OPEN_URI);
    static_assert(int(open_flags::memory)       == SQLITE_OPEN_MEMORY);
    static_assert(int(open_flags::nomutex)      == SQLITE_OPEN_NOMUTEX);
    static_assert(int(open_flags::fullmutex)    == SQLITE_OPEN_FULLMUTEX);
    static_assert(int(open_flags::nofollow)     == SQLITE_OPEN_NOFOLLOW);
    static_assert(int(open_flags::exrescode)    == SQLITE_OPEN_EXRESCODE);
#ifdef __APPLE__
    static_assert(int(open_flags::fileprotection_complete)
                  == SQLITE_OPEN_FILEPROTECTION_COMPLETE);
    static_assert(int(open_flags::fileprotection_complete_unless_open)
                  == SQLITE_OPEN_FILEPROTECTION_COMPLETEUNLESSOPEN);
    static_assert(int(open_flags::fileprotection_complete_until_auth) 
                  == SQLITE_OPEN_FILEPROTECTION_COMPLETEUNTILFIRSTUSERAUTHENTICATION);
    static_assert(int(open_flags::fileprotection_none) 
                  == SQLITE_OPEN_FILEPROTECTION_NONE);
#endif

    static_assert(int(limit::row_length)        == SQLITE_LIMIT_LENGTH);
    static_assert(int(limit::sql_length)        == SQLITE_LIMIT_SQL_LENGTH);
    static_assert(int(limit::columns)           == SQLITE_LIMIT_COLUMN);
    static_assert(int(limit::function_args)     == SQLITE_LIMIT_FUNCTION_ARG);
    static_assert(int(limit::worker_threads)    == SQLITE_LIMIT_WORKER_THREADS);


    database_error::database_error(char const* msg, status rc)
    : std::runtime_error(msg)
    , error_code(rc) {
    }


    database_error::database_error(database& db, status rc)
    : database_error(sqlite3_errmsg(db.db_), rc)
    { }


    checking::checking(database &db)
    :checking(db, db.exceptions_)
    { }


    status checking::check(status rc) const {
        if (!ok(rc) && exceptions_)
            throw_(rc);
        return rc;
    }


    void checking::throw_(status rc) const {
        assert(!ok(rc));
        throw database_error(db_, rc);
    }


#pragma mark - DATABASE:


    database::database(std::string_view dbname, open_flags flags, char const* vfs)
    : checking(*this, kExceptionsByDefault)
    {
        connect(dbname, flags, vfs);
    }

    database::database() noexcept
    : checking(*this, kExceptionsByDefault)
    { }

    database::database(sqlite3* pdb) noexcept
    : checking(*this, kExceptionsByDefault)
    , db_(pdb)
    , borrowing_(true)
    { }

    database::database(database&& db) noexcept
    : checking(*this, db.exceptions_)
    , db_(std::move(db.db_))
    , borrowing_(std::move(db.borrowing_))
    , bh_(std::move(db.bh_))
    , ch_(std::move(db.ch_))
    , rh_(std::move(db.rh_))
    , uh_(std::move(db.uh_))
    , ah_(std::move(db.ah_))
    {
        db.db_ = nullptr;
    }

    database& database::operator=(database&& db) noexcept {
        exceptions_ = db.exceptions_;
        db_ = std::move(db.db_);
        db.db_ = nullptr;
        borrowing_ = std::move(db.borrowing_);
        bh_ = std::move(db.bh_);
        ch_ = std::move(db.ch_);
        rh_ = std::move(db.rh_);
        uh_ = std::move(db.uh_);
        ah_ = std::move(db.ah_);

        return *this;
    }

    database::~database() noexcept {
        if (!borrowing_) {
            if (auto rc = status{sqlite3_close(db_)}; !ok(rc)) {
                // Closing the database failed: there are still SQLite statements (queries) open,
                // probably `command` or `query` objects that haven't been destructed yet.
                // Unfortunately in a destructor, unlike `close()`, we can't throw an exception
                // to make the operation fail. The best we can do is to call `sqlite3_close_v2`
                // which will defer the close until the last open handle is gone.
                // As an additional safeguard, prevent SQLite from checkpointing the WAL when it
                // does finally close: if this happens after the file has been reopened or deleted,
                // it can overwrite a mismatched WAL, causing data corruption.
                fprintf(stderr, "**SQLITE WARNING**: A `sqnice::database` object at %p"
                        "is being destructed while there are still open query iterators, blob"
                        " streams or backups. This is bad! (For more information, read the docs for"
                        "`sqnice::database::close`.)\n", db_);
                sqlite3_db_config(db_, SQLITE_DBCONFIG_NO_CKPT_ON_CLOSE, 1, nullptr);
                (void) sqlite3_close_v2(db_);
            }
        }
    }

    status database::connect(std::string_view dbname, open_flags flags, char const* vfs) {
        close();

        auto rc = status{sqlite3_open_v2(std::string(dbname).c_str(), &db_, int(flags), vfs)};
        if (!ok(rc)) {
            if (exceptions_) {
                std::string message;
                if (db_) {
                    message = sqlite3_errmsg(db_);
                    (void)sqlite3_close_v2(db_);
                } else {
                    message = "can't open database";
                }
                throw database_error(message.c_str(), rc);
            } else {
                (void)sqlite3_close_v2(db_);
            }
            db_ = nullptr;
        }
        return check(rc);
    }

    status database::close(bool immediately) {
        if (borrowing_)
            return status::ok;
        auto rc = status{immediately ? sqlite3_close(db_) : sqlite3_close_v2(db_)};
        if (ok(rc))
            db_ = nullptr;
        return check(rc);
    }

    status database::execute(std::string_view sql) {
        auto rc = status{sqlite3_exec(db_, std::string(sql).c_str(), nullptr, nullptr, nullptr)};
        if (rc == status::error && exceptions_)
            throw std::invalid_argument(error_msg());
        return check(rc);
    }

    status database::executef(char const* sql, ...) {
        va_list ap;
        va_start(ap, sql);
        std::shared_ptr<char> msql(sqlite3_vmprintf(sql, ap), sqlite3_free);
        va_end(ap);

        return execute(msql.get());
    }


#pragma mark - DATABASE CONFIGURATION:


    std::tuple<int,int,int> database::sqlite_version() noexcept {
        auto v = sqlite3_libversion_number();
        return {v / 1'000'000, v / 1'000, v % 1'000};
    }

    status database::enable_foreign_keys(bool enable) {
        return check(sqlite3_db_config(db_, SQLITE_DBCONFIG_ENABLE_FKEY, enable ? 1 : 0, nullptr));
    }

    status database::enable_triggers(bool enable) {
        return check(sqlite3_db_config(db_, SQLITE_DBCONFIG_ENABLE_TRIGGER, enable ? 1 : 0, nullptr));
    }

    status database::enable_extended_result_codes(bool enable) {
        return check(sqlite3_extended_result_codes(db_, enable ? 1 : 0));
    }

    status database::set_busy_timeout(int ms) {
        return check(sqlite3_busy_timeout(db_, ms));
    }


    status database::setup() {
        status rc = enable_extended_result_codes(true);
        if (!ok(rc)) {return rc;}
        rc = enable_foreign_keys();
        if (!ok(rc)) {return rc;}
        rc = set_busy_timeout(5000);
        if (ok(rc) && writeable()) {
            rc = execute("PRAGMA auto_vacuum = incremental;" // must be the first statement executed
                         "PRAGMA journal_mode = WAL;"
                         "PRAGMA synchronous=normal");
        }
        return rc;
    }


    int64_t database::pragma(const char* pragma) {
        return query(*this, std::string("PRAGMA \"") + pragma + "\"").single_value_or<int>(0);
    }

    std::string database:: string_pragma(const char* pragma) {
        return query(*this, std::string("PRAGMA \"") + pragma + "\"").single_value_or<std::string>("");
    }

    status database::pragma(const char* pragma, int64_t value) {
        return executef("PRAGMA %s(%d)", pragma, value);
    }

    status database::pragma(const char* pragma, std::string_view value) {
        return executef("PRAGMA %s(%q)", pragma, std::string(value).c_str());
    }


    unsigned database::get_limit(limit lim) const noexcept {
        return sqlite3_limit(db_, int(lim), -1);
    }

    unsigned database::set_limit(limit lim, unsigned val) noexcept {
        return sqlite3_limit(db_, int(lim), int(val));
    }


#pragma mark - DATABASE PROPERTIES:


    const char* database::filename() const noexcept {
        return sqlite3_db_filename(db_, nullptr);
    }

    status database::error_code() const noexcept {
        return status{sqlite3_errcode(db_)};
    }

    status database::extended_error_code() const noexcept {
        return status{sqlite3_extended_errcode(db_)};
    }

    char const* database::error_msg() const noexcept {
        return sqlite3_errmsg(db_);
    }

    bool database::writeable() const noexcept {
        return ! sqlite3_db_readonly(db_, "main");
    }

    int64_t database::last_insert_rowid() const noexcept {
        return sqlite3_last_insert_rowid(db_);
    }

    int database::changes() const noexcept {
        return sqlite3_changes(db_);
    }

    int64_t database::total_changes() const noexcept {
        return sqlite3_total_changes(db_);
    }

    bool database::in_transaction() const noexcept {
        return !sqlite3_get_autocommit(db_);
    }


#pragma mark - BACKUP:


    status database::backup(database& destdb, backup_handler h) {
        return backup("main", destdb, "main", h);
    }

    status database::backup(std::string_view dbname,
                            database& destdb, std::string_view destdbname,
                            backup_handler h, int step_page)
    {
        sqlite3_backup* bkup = sqlite3_backup_init(destdb.db_,
                                                   std::string(destdbname).c_str(),
                                                   db_,
                                                   std::string(dbname).c_str());
        if (!bkup) {
            return error_code();
        }
        auto rc = status::ok;
        do {
            rc = status{sqlite3_backup_step(bkup, step_page)};
            if (h) {
                h(sqlite3_backup_remaining(bkup), sqlite3_backup_pagecount(bkup), rc);
            }
        } while (rc == status::ok || rc == status::busy || rc == status::locked);
        sqlite3_backup_finish(bkup);
        return check(rc);
    }


    status database::optimize() {
        /* "The optimize pragma is usually a no-op but it will occasionally run ANALYZE if it
         seems like doing so will be useful to the query planner. The analysis_limit pragma
         limits the scope of any ANALYZE command that the optimize pragma runs so that it does
         not consume too many CPU cycles. The constant "400" can be adjusted as needed. Values
         between 100 and 1000 work well for most applications."
         -- <https://sqlite.org/lang_analyze.html> */
        if (!writeable())
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


    std::optional<int64_t> database::incremental_vacuum(bool always, int64_t nPages) {
        // <https://blogs.gnome.org/jnelson/2015/01/06/sqlite-vacuum-and-auto_vacuum/>
        if (!writeable())
            return std::nullopt;
        int64_t pageCount = pragma("page_count");
        bool do_it = always;
        if (!always) {
            int64_t freePages = pragma("freelist_count");
            float freeFraction = pageCount ? (float)freePages / pageCount : 0;
            do_it = freeFraction >= kVacuumFractionThreshold
                    || freePages * pragma("page_size") >= kVacuumSizeThreshold;
        }
        if (!do_it)
            return std::nullopt;

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
        lh_ = h;
        auto callback = [](void* p, int errCode, const char* msg) noexcept {
            if ( (errCode & 0xFF) == SQLITE_SCHEMA )
                return;  // ignore harmless "statement aborts ... database schema has changed"
            static_cast<database*>(p)->lh_(status(errCode), msg);
        };
        sqlite3_config(SQLITE_CONFIG_LOG, (h ? callback : nullptr), this);
    }

    void database::set_busy_handler(busy_handler h) noexcept {
        bh_ = h;
        sqlite3_busy_handler(db_, bh_ ? busy_handler_impl : nullptr, &bh_);
    }

    void database::set_commit_handler(commit_handler h) noexcept {
        ch_ = h;
        sqlite3_commit_hook(db_, ch_ ? commit_hook_impl : nullptr, &ch_);
    }

    void database::set_rollback_handler(rollback_handler h) noexcept {
        rh_ = h;
        sqlite3_rollback_hook(db_, rh_ ? rollback_hook_impl : nullptr, &rh_);
    }

    void database::set_update_handler(update_handler h) noexcept {
        uh_ = h;
        sqlite3_update_hook(db_, uh_ ? update_hook_impl : nullptr, &uh_);
    }

    void database::set_authorize_handler(authorize_handler h) noexcept {
        ah_ = h;
        sqlite3_set_authorizer(db_, ah_ ? authorizer_impl : nullptr, &ah_);
    }

}
