// sqnice/base.cc
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


#include "sqnice/base.hh"
#include "sqnice/database.hh"

#ifdef SQNICE_LOADABLE_EXTENSION
#  include <sqlite3ext.h>
SQLITE_EXTENSION_INIT1
#else
#  include <sqlite3.h>
#endif

namespace sqnice {
    using namespace std;

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

    static_assert(int(function_flags::deterministic) == SQLITE_DETERMINISTIC);
    static_assert(int(function_flags::direct_only)   == SQLITE_DIRECTONLY);
    static_assert(int(function_flags::subtype)       == SQLITE_SUBTYPE);
    static_assert(int(function_flags::innocuous)     == SQLITE_INNOCUOUS);


    database_error::database_error(char const* msg, status rc)
    : runtime_error(msg)
    , error_code(rc) {
    }


    checking::checking(database &db)
    :checking(db.db_, db.exceptions_)
    { }


    shared_ptr<sqlite3> checking::check_get_db() const {
        if (shared_ptr<sqlite3> db = weak_db_.lock()) [[likely]]
            return db;
        else
            throw logic_error("database is no longer open");
    }


    status checking::check(status rc) const {
        if (!ok(rc)) [[unlikely]] {
            if (exceptions_ || rc == status::misuse)
                if (rc != status::done && rc != status::row)
                    raise(rc);
        }
        return rc;
    }


    void checking::raise(status rc) const {
        if (auto db = weak_db_.lock())
            raise(rc, sqlite3_errmsg(db.get()));
        else
            raise(rc, "");
    }

    void checking::raise(status rc, const char* msg) {
        switch (int(rc)) {
            case SQLITE_INTERNAL:
                throw logic_error(msg);
            case SQLITE_NOMEM:
                throw bad_alloc();
            case SQLITE_RANGE:
            case SQLITE_MISUSE:
                throw invalid_argument(msg);
            case SQLITE_OK:
            case SQLITE_NOTICE:
            case SQLITE_WARNING:
            case SQLITE_ROW:
            case SQLITE_DONE:
                throw logic_error("invalid call to throw_, err=" + to_string(int(rc)));
            default:
                throw database_error(msg, rc);
        }
    }


    void checking::log_warning(const char* format, ...) noexcept {
        va_list args;
        va_start(args, format);
        char* message = sqlite3_vmprintf(format, args);
        va_end(args);
        sqlite3_log(SQLITE_WARNING, message);
        sqlite3_free(message);
    }

}
