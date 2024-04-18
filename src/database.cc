// sqlite3pp/database.cc
//
// The MIT License
//
// Copyright (c) 2015 Wongoo Lee (iwongu at gmail dot com)
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


#include "sqlite3pp/database.hh"
#include <cstring>
#include <cassert>

#ifdef SQLITE3PP_LOADABLE_EXTENSION
#  include <sqlite3ext.h>
SQLITE_EXTENSION_INIT1
#else
#  include <sqlite3.h>
#endif

namespace sqlite3pp {

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


    database::database(char const* dbname, open_flags flags, char const* vfs)
    : checking(*this, false)
    , db_(nullptr)
    , borrowing_(false)
    {
        assert(dbname);
        if (auto rc = connect(dbname, flags, vfs); rc != status::ok) {
            std::string message;
            if (db_) {
                message = sqlite3_errmsg(db_);
                sqlite3_close_v2(db_);
            } else {
                message = "can't open database";
            }
            throw database_error(message.c_str(), rc);
        }
    }

    database::database(sqlite3* pdb)
    : checking(*this, false)
    , db_(pdb)
    , borrowing_(true)
    { }

    database::database(database&& db) 
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

    database& database::operator=(database&& db) {
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

    database::~database() {
        if (!borrowing_) {
            disconnect();
        }
    }

    const char* database::filename() const {
        return sqlite3_db_filename(db_, nullptr);
    }

    status database::connect(char const* dbname, open_flags flags, char const* vfs) {
        if (!borrowing_)
            disconnect();
        return check(sqlite3_open_v2(dbname, &db_, int(flags), vfs));
    }

    status database::disconnect() {
        auto rc = status::ok;
        if (db_) {
            rc = status{sqlite3_close(db_)};
            if (rc == status::ok) {
                db_ = nullptr;
            }
        }

        return check(rc);
    }

    status database::attach(char const* dbname, char const* name) {
        return executef("ATTACH '%q' AS '%q'", dbname, name);
    }

    status database::detach(char const* name) {
        return executef("DETACH '%q'", name);
    }

    status database::backup(database& destdb, backup_handler h) {
        return backup("main", destdb, "main", h);
    }

    status database::backup(char const* dbname, database& destdb, char const* destdbname, backup_handler h, int step_page) {
        sqlite3_backup* bkup = sqlite3_backup_init(destdb.db_, destdbname, db_, dbname);
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


    status database::execute(char const* sql) {
        return check(sqlite3_exec(db_, sql, 0, 0, 0));
    }

    status database::executef(char const* sql, ...) {
        va_list ap;
        va_start(ap, sql);
        std::shared_ptr<char> msql(sqlite3_vmprintf(sql, ap), sqlite3_free);
        va_end(ap);

        return execute(msql.get());
    }


#pragma mark - DATABASE CONFIGURATION:


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

    unsigned database::get_limit(limit lim) const {
        return sqlite3_limit(db_, int(lim), -1);
    }

    unsigned database::set_limit(limit lim, unsigned val) {
        return sqlite3_limit(db_, int(lim), int(val));
    }


#pragma mark - DATABASE PROPERTIES:


    status database::error_code() const {
        return status{sqlite3_errcode(db_)};
    }

    status database::extended_error_code() const {
        return status{sqlite3_extended_errcode(db_)};
    }

    char const* database::error_msg() const {
        return sqlite3_errmsg(db_);
    }

    int64_t database::last_insert_rowid() const {
        return sqlite3_last_insert_rowid(db_);
    }

    int database::changes() const {
        return sqlite3_changes(db_);
    }

    int64_t database::total_changes() const {
        return sqlite3_total_changes(db_);
    }

    bool database::in_transaction() const {
        return !sqlite3_get_autocommit(db_);
    }


#pragma mark - HOOKS:

    
    namespace {
        int busy_handler_impl(void* p, int cnt) {
            auto h = static_cast<database::busy_handler*>(p);
            return int((*h)(cnt));
        }

        int commit_hook_impl(void* p) {
            auto h = static_cast<database::commit_handler*>(p);
            return int((*h)());
        }

        void rollback_hook_impl(void* p) {
            auto h = static_cast<database::rollback_handler*>(p);
            (*h)();
        }

        void update_hook_impl(void* p, int opcode, char const* dbname, char const* tablename, long long int rowid) {
            auto h = static_cast<database::update_handler*>(p);
            (*h)(opcode, dbname, tablename, rowid);
        }

        int authorizer_impl(void* p, int evcode, char const* p1, char const* p2, char const* dbname, char const* tvname) {
            auto h = static_cast<database::authorize_handler*>(p);
            return int((*h)(evcode, p1, p2, dbname, tvname));
        }

    } // namespace


    void database::set_busy_handler(busy_handler h) {
        bh_ = h;
        sqlite3_busy_handler(db_, bh_ ? busy_handler_impl : 0, &bh_);
    }

    void database::set_commit_handler(commit_handler h) {
        ch_ = h;
        sqlite3_commit_hook(db_, ch_ ? commit_hook_impl : 0, &ch_);
    }

    void database::set_rollback_handler(rollback_handler h) {
        rh_ = h;
        sqlite3_rollback_hook(db_, rh_ ? rollback_hook_impl : 0, &rh_);
    }

    void database::set_update_handler(update_handler h) {
        uh_ = h;
        sqlite3_update_hook(db_, uh_ ? update_hook_impl : 0, &uh_);
    }

    void database::set_authorize_handler(authorize_handler h) {
        ah_ = h;
        sqlite3_set_authorizer(db_, ah_ ? authorizer_impl : 0, &ah_);
    }

}
