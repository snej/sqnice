// sqlite3pp/database.hh
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

#pragma once
#ifndef SQLITE3PP_DATABASE_H
#define SQLITE3PP_DATABASE_H

#include "sqlite3pp/base.hh"
#include <functional>

ASSUME_NONNULL_BEGIN

struct sqlite3;

namespace sqlite3pp {

    class database;

    // Defined in sqlite3ppext.hh
    namespace ext {
        class function;
        class aggregate;
        database borrow(sqlite3*);
    }


    /** Flags used when opening a database; equivalent to `SQLITE_OPEN_...` macros in sqlite3.h. */
    enum class open_flags : int {
        readonly        = 0x00000001,   ///< Open database file as read-only
        readwrite       = 0x00000002,   ///< Open database file as writeable, if possible
        create          = 0x00000004,   ///< Create database file if it doesn't exist
        uri             = 0x00000040,   ///< filename may be a `file:` URI (see docs)
        memory          = 0x00000080,   ///< Open a temporary in-memory database
        nomutex         = 0x00008000,   ///< Use the "multi-thread" threading mode (see docs)
        fullmutex       = 0x00010000,   ///< Use the "serialized" threading mode (see docs)
        nofollow        = 0x01000000,   ///< Symbolic links in path will not be followed
        exrescode       = 0x02000000,   ///< Enable extended result codes
#ifdef __APPLE__
        // Add no more than one of these, to specify an iOS file protection mode.
        fileprotection_complete             = 0x00100000,
        fileprotection_complete_unless_open = 0x00200000,
        fileprotection_complete_until_auth  = 0x00300000,
        fileprotection_none                 = 0x00400000,
#endif
    };

    inline open_flags operator| (open_flags a, open_flags b) {return open_flags(int(a) | int(b));}
    inline open_flags operator& (open_flags a, open_flags b) {return open_flags(int(a) & int(b));}
    inline open_flags operator- (open_flags a, open_flags b) {return open_flags(int(a) & ~int(b));}
    inline open_flags& operator|= (open_flags& a, open_flags b) {a = a | b; return a;}

    /** Per-database size/quantity limits that can be adjusted. */
    enum class limit : int {
        row_length      =  0,
        sql_length      =  1,
        columns         =  2,
        function_args   =  6,
        worker_threads  = 11,
    };


    /** A SQLite database connection. */
    class database : public checking, noncopyable {
    public:
        explicit database(char const* dbname,
                          open_flags flags           = open_flags::readwrite | open_flags::create,
                          const char*  _Nullable vfs = nullptr);

        ~database();
        database(database&& db);
        database& operator=(database&& db);

        status connect(char const* dbname,
                       open_flags flags           = open_flags::readwrite | open_flags::create,
                       const char*  _Nullable vfs = nullptr);
        status disconnect();

        status attach(char const* dbname, char const* name);
        status detach(char const* name);

        const char* filename() const;

        sqlite3* handle() const         {return db_;}

        int64_t last_insert_rowid() const;

        status enable_foreign_keys(bool enable = true);
        status enable_triggers(bool enable = true);
        status enable_extended_result_codes(bool enable = true);

        int changes() const;
        int64_t total_changes() const;

        status error_code() const;
        status extended_error_code() const;
        char const* _Nullable error_msg() const;

        /** Executes a (non-`SELECT`) statement, or multiple statements separated by `;`. */
        status execute(char const* sql);

        /** Same as `execute` but uses `printf`-style formatting to produce the SQL string.
            @warning If using `%s`, be **very careful** not to introduce SQL injection attacks! */
        status executef(char const* sql, ...)
#ifdef SQLITE3PP_TYPECHECK_EXECUTEF
                                                __attribute__((__format__ (__printf__, 2, 3)))
#endif
        ;

        status set_busy_timeout(int ms);

        using busy_handler = std::function<status (int)>;
        using commit_handler = std::function<status ()>;
        using rollback_handler = std::function<void ()>;
        using update_handler = std::function<void (int, char const*, char const*, long long int)>;
        using authorize_handler = std::function<status (int, char const*, char const*, char const*, char const*)>;

        void set_busy_handler(busy_handler h);
        void set_commit_handler(commit_handler h);
        void set_rollback_handler(rollback_handler h);
        void set_update_handler(update_handler h);
        void set_authorize_handler(authorize_handler h);

        using backup_handler = std::function<void (int, int, status)>;

        status backup(database& destdb, backup_handler h = {});
        status backup(char const* dbname, database& destdb, char const* destdbname, backup_handler h, int step_page = 5);

        bool in_transaction() const;

        unsigned get_limit(limit) const;
        unsigned set_limit(limit, unsigned);

    private:
        friend class statement;
        friend class database_error;
        friend class blob_stream;
        friend class ext::function;
        friend class ext::aggregate;
        friend database ext::borrow(sqlite3* pdb);

        database(sqlite3* pdb);

    private:
        sqlite3* _Nullable  db_;
        bool                borrowing_;
        busy_handler        bh_;
        commit_handler      ch_;
        rollback_handler    rh_;
        update_handler      uh_;
        authorize_handler   ah_;
    };

}

ASSUME_NONNULL_END

#endif
