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
        /// Constructs an instance that opens a SQLite database file. Details depend on the flags.
        explicit database(std::string_view filename,
                          open_flags flags           = open_flags::readwrite | open_flags::create,
                          const char*  _Nullable vfs = nullptr);

        /// Constructs an instance that uses an already-open SQLite database handle.
        /// Its destructor will not close this handle.
        explicit database(sqlite3*);

        /// Constructs an instance that isn't connected to any database.
        /// You must call `connect` before doing anything else with it.
        database();

        ~database();
        database(database&& db);
        database& operator=(database&& db);

        /// Closes any existing connection and opens a new database file.
        status connect(std::string_view filename,
                       open_flags flags           = open_flags::readwrite | open_flags::create,
                       const char*  _Nullable vfs = nullptr);

        /// Closes any existing database connection.
        status disconnect();

        /// The filename (path) of the open database.
        const char* filename() const;

        /// True if the database is writeable, false if read-only.
        /// This depends on the database file's permissions as well as the flags used to open it.
        bool writeable() const;

        /// The raw SQLite database handle, for use if you need to call a SQLite API yourself.
        sqlite3* handle() const                         {return db_;}

        /// Configures the database following current best practices; this is optional,
        /// and should be the first call after opening a database. It:
        /// * Enables extended result codes.
        /// * Enables foreign key checks.
        /// * Sets a busy timeout of 5 seconds.
        /// If the database is writeable, it also:
        /// * Enables incremental auto-vacuum mode
        /// * Sets the journal mode to WAL
        /// * Sets the `synchronous` pragma to `normal`
        status setup();

        status enable_foreign_keys(bool enable = true);
        status enable_triggers(bool enable = true);
        status enable_extended_result_codes(bool enable = true);
        status set_busy_timeout(int ms);

        /// Returns the current value of a limit. (See the `limits` enum.)
        unsigned get_limit(limit) const;
        /// Sets the value of a limit, returning the previous value.
        unsigned set_limit(limit, unsigned);

        /// Executes `PRAGMA name`, returning its value as an int.
        /// @note  For pragmas that return textual results, use `string_pragma`.
        /// @note  For pragmas that return multiple values, like `database-list`,
        ///         you'll have to run your own query.
        [[nodiscard]] int64_t pragma(const char* name);

        /// Executes `PRAGMA name`, returning its value as a string.
        [[nodiscard]] std::string string_pragma(const char* name);

        /// Executes `PRAGMA name = value`.
        status pragma(const char* name, int64_t value);
        /// Executes `PRAGMA name = value`.
        status pragma(const char* name, std::string_view value);

        /** Executes a (non-`SELECT`) statement, or multiple statements separated by `;`. */
        status execute(std::string_view sql);

        /** Same as `execute` but uses `printf`-style formatting to produce the SQL string.
            @warning If using `%s`, be **very careful** not to introduce SQL injection attacks! */
        status executef(char const* sql, ...)
#ifdef SQLITE3PP_TYPECHECK_EXECUTEF
                                                __attribute__((__format__ (__printf__, 2, 3)))
#endif
        ;

        /// True if a transaction or savepoint is active.
        bool in_transaction() const;

        /// The `rowid` of the last row inserted by an `INSERT` statement.
        int64_t last_insert_rowid() const;

        /// The number of rows changed by the last `execute` call or by a `command` object.
        int changes() const;

        /// The total number of rows changed since the connection was opened.
        int64_t total_changes() const;

        status error_code() const;
        status extended_error_code() const;
        char const* _Nullable error_msg() const;

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

        /// Runs `PRAGMA incremental_vacuum(N)`. This causes up to N free pages to be removed from
        /// the database, reducing its file size.
        /// @param always  If false, the operation is only performed if the size of the freelist is
        ///                either 25% of the database or 10MB, whichever is less.
        ///                If true, vacuuming always occurs, and as a bonus, the WAL is truncated
        ///                to save even more disk space.
        /// @param nPages  The maximum number of pages to free; or if 0, unlimited.
        /// @returns  The number of pages freed, or `nullopt` if no vacuuming took place.
        /// @note Has no effect if the database is not in `auto_vacuum=incremental` mode,
        ///       or if it's not writeable.
        /// @note See <https://blogs.gnome.org/jnelson/2015/01/06/sqlite-vacuum-and-auto_vacuum/>
        std::optional<int64_t> incremental_vacuum(bool always = true, int64_t nPages = 0);

        /// Runs `PRAGMA optimize`. This "is usually a no-op but it will occasionally run ANALYZE
        /// if it seems like doing so will be useful to the query planner."
        status optimize();

        using backup_handler = std::function<void (int, int, status)>;

        status backup(database& destdb, backup_handler h = {});
        status backup(std::string_view dbname,
                      database& destdb,
                      std::string_view destdbname,
                      backup_handler h,
                      int step_page = 5);

    private:
        friend class statement;
        friend class database_error;
        friend class blob_stream;
        friend class ext::function;
        friend class ext::aggregate;
        friend database ext::borrow(sqlite3* pdb);

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
