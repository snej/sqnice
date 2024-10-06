// sqnice/database.hh
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

#pragma once
#ifndef SQNICE_DATABASE_H
#define SQNICE_DATABASE_H

#include "sqnice/base.hh"
#include <cstdint>
#include <functional>
#include <optional>
#include <span>
#include <tuple>

ASSUME_NONNULL_BEGIN

struct sqlite3;
struct sqlite3_context;
struct sqlite3_value;

namespace sqnice {

    class command;
    class context;
    class database;
    class function_args;
    class function_result;
    class query;
    template <class STMT> class statement_cache;


    /** Flags used when opening a database; equivalent to `SQLITE_OPEN_...` macros in sqlite3.h. */
    enum class open_flags : unsigned {
        readonly        = 0x00000000,   ///< Not really a flag, just the absence of `readwrite`
        readwrite       = 0x00000002,   ///< Open database file as writeable, if possible
        create          = 0x00000004,   ///< Create database file if it doesn't exist
        uri             = 0x00000040,   ///< filename may be a `file:` URI (see docs)
        memory          = 0x00000080,   ///< Open a temporary in-memory database; filename ignored.
        nomutex         = 0x00008000,   ///< Use the "multi-thread" threading mode (see docs)
        fullmutex       = 0x00010000,   ///< Use the "serialized" threading mode (see docs)
        nofollow        = 0x01000000,   ///< Symbolic links in path will not be followed
        // nonstandard flags:
        delete_first    = 0x80000000,   ///< Delete any pre-existing file; requires `create`
        temporary       = 0x40000000,   ///< Create temporary file, deleted on close
#ifdef __APPLE__
        // Apple-specific flags; add one of these to specify an iOS file protection mode.
        fileprotection_complete             = 0x00100000,
        fileprotection_complete_unless_open = 0x00200000,
        fileprotection_complete_until_auth  = 0x00300000,
        fileprotection_none                 = 0x00400000,
#endif
        defaults        = readwrite | create,   ///< Default flags: `readwrite` and `create`
    };

    inline constexpr open_flags operator| (open_flags a, open_flags b) {return open_flags(int(a) | int(b));}
    inline constexpr open_flags operator& (open_flags a, open_flags b) {return open_flags(int(a) & int(b));}
    inline constexpr open_flags operator- (open_flags a, open_flags b) {return open_flags(int(a) & ~int(b));}
    inline constexpr open_flags& operator|= (open_flags& a, open_flags b) {a = a | b; return a;}
    inline constexpr bool operator!(open_flags a) {return !int(a);}
    open_flags normalize(open_flags);

    /** Per-database size/quantity limits that can be adjusted. */
    enum class limit : int {
        row_length      =  0,
        sql_length      =  1,
        columns         =  2,
        function_args   =  6,
        worker_threads  = 11,
    };


    enum class function_flags : int {
        none            = 0,
        deterministic   = 0x000000800,  // same args will always return the same result
        direct_only     = 0x000080000,  // cannot be used in VIEWs or TRIGGERs
        subtype         = 0x000100000,  // implementation gets or sets subtypes of values
        innocuous       = 0x000200000,  // no side effects, accesses nothing but its args
    };
    inline function_flags operator| (function_flags a, function_flags b) {
        return function_flags(int(a) | int(b));}


    /** A SQLite database connection. */
    class database : public checking, noncopyable {
    public:
        /// Constructs a `database` and calls `open` with the same arguments; see the docs of that
        /// method for details.
        ///
        /// Exceptions are enabled by default! If you want to open a database without potentially
        /// throwing an exception, use the no-arguments constructor instead, then call
        /// `exceptions(false)` and then `open(filename,...)`.
        explicit database(std::string_view filename,
                          open_flags flags          = open_flags::defaults,
                          const char* _Nullable vfs = nullptr);

        /// Constructs an instance that isn't connected to any database.
        /// You must call `open` before doing anything else with it.
        database() noexcept;

        /// Constructs an instance that uses an already-open SQLite database handle.
        /// The destructor, and the `close` method, will not close this handle.
        explicit database(sqlite3*) noexcept;

        database(database&& db) noexcept;
        database& operator=(database&& db) noexcept;
        ~database() noexcept;

        /// Opens (connects to) a database file. Any existing connection is closed first.
        /// @param filename  The filesystem path to the database, including any extension.
        ///                  An empty string represents a temporary on-disk database.
        ///                  This parameter is ignored if the `memory` flag is given.
        /// @param flags Specifies how the database is opened; see documentation of each flag.
        /// @param vfs  Name of Virtual File-System to use, if any.
        /// @returns  Result status.
        /// @throws database_error if exceptions are enabled.
        status open(std::string_view filename,
                    open_flags flags          = open_flags::defaults,
                    const char* _Nullable vfs = nullptr);

        /// Opens a new, temporary and anonymous SQLite database.
        /// Any existing connection is closed first.
        /// @param on_disk  If false, the database is stored in memory;
        ///                 if true, in a temporary file on disk (deleted on close.)
        status open_temporary(bool on_disk = false);

        /// Closes the database connection. (If there is none, does nothing.)
        ///
        /// SQLite cannot close the connection while any `query::iterator` objects are still active,
        /// `blob_stream`s are open, or backups are running. In this situation, the `immediately`
        /// parameter comes into play:
        /// - `immediately==true` (default): The method will return or throw an error status,
        ///     without closing the connection.
        /// - `immediately==false`: The method will return `ok` regardless, and the `database`
        ///     instance is no longer connected; however, SQLite itself keeps the database file
        ///     open until the last query/blob/backup is closed.
        ///
        /// @warning If you are going to delete the database files, **do not pass `false`**.
        ///     The effects of SQLite having an open connection to a deleted database file can
        ///     be dangerous, especially if that file is re-created before SQLite closes it;
        ///     this is one of the known ways to corrupt a SQLite database.
        status close(bool immediately = true);

        /// Closes the database and deletes its file(s).
        /// @warning  There must be no other open connections to the same database file!
        status close_and_delete();

        /// Deletes a database file at `path`, and any associated "-wal" or "-shm" files.
        /// @returns `ok` if deleted, `cantopen` if file doesn't exist, else other error status.
        /// @throws database_error if `exceptions` is true and status is not `ok` or `cantopen`.
        /// @warning  There must be no open connections to the same database file!
        static status delete_file(std::string_view path, bool exceptions = true);

        /// The filename (path) of the open database, as passed to the constructor or `connect`.
        [[nodiscard]] const char* filename() const noexcept;

        /// True if a database connection is open.
        [[nodiscard]] bool is_open() const noexcept         {return db_ != nullptr;}

        /// True if the database is writeable, false if read-only.
        /// This depends on the database file's permissions as well as the flags used to open it.
        [[nodiscard]] bool is_writeable() const noexcept;

        /// True if the database is in-memory or in a temporary directory (or closed.)
        [[nodiscard]] bool is_temporary() const noexcept    {return temporary_ || !db_;}

        /// The raw SQLite database handle, for use if you need to call a SQLite API yourself.
        [[nodiscard]] sqlite3* handle() const noexcept      {return db_.get();}

        /// Like `handle`, but throws an exception instead of returning `nullptr`.
        /// @throws std::logic_error
        sqlite3* check_handle() const;


#pragma mark - CONFIGURATION:

        /// Returns the runtime version number of the SQLite library as {major, minor, patch},
        /// e.g. {3, 43, 1}.
        static std::tuple<int,int,int> sqlite_version() noexcept;

        /// Configures this database **connection** according to current best practices.
        /// This is optional, but recommended. It does the following things:
        /// * Enables foreign key checks.
        /// * Sets a busy timeout of 5 seconds.
        /// * Enables "defensive mode", preventing some ways of corrupting a database through SQL.
        /// * Disallows the use of double-quotes for SQL strings (an old misfeature.)
        /// * Sets the `synchronous` pragma to `normal`, or to `off` for a temporary database.
        status setup_connection();

        /// Configures the database **file** according to current best practices.
        /// This is optional, but recommended. It should be called immedidately after opening
        /// a database. The auto-vacuum mode setting will only take effect if `setup` is the
        /// first call made to a newly-created database file.
        ///
        /// If the database file has already been configured by an earlier call to `setup`,
        /// no changes are made to it.
        ///
        /// This method first calls `setup_connection`. Then, if the database is writeable, it also:
        /// * Sets the journal mode to WAL
        /// * Enables incremental auto-vacuum mode
        status setup();

        /// Sets the database connection's maximum RAM cache size, in kilobytes.
        /// SQLite's default is 20,000 (20MB).
        status set_cache_size_KB(size_t kb);

        /// Enables/disables enforcement of foreign-key constraints. The default is off, but
        /// the `setup_connection` method turns it on.
        status enable_foreign_keys(bool enable = true);

        /// Sets how long SQLite will wait to acquire the lock of a busy database, before writing.
        /// If the timeout expires (or is set to 0) the operation will fail with `status::busy`.
        /// The default is 0. `setup_connection` sets it to 5000 (five seconds.)
        /// @param ms  The timeout, in _milliseconds_, or 0 to disable waiting.
        status set_busy_timeout(int ms);

        /// Returns the current value of a limit. (See the `limits` enum.)
        unsigned get_limit(limit) const noexcept;
        /// Sets the value of a limit, returning the previous value.
        unsigned set_limit(limit, unsigned) noexcept;

        /// Executes `PRAGMA name`, returning its value as an int.
        /// @note  For pragmas that return textual results, use `string_pragma`.
        /// @note  For pragmas that return multiple values, like `database-list`,
        ///         you'll have to run your own query.
        /// @warning NEVER pass an untrusted string; it can enable SQL injection attacks!
        [[nodiscard]] int64_t pragma(const char* name);

        /// Executes `PRAGMA name`, returning its value as a string.
        /// @warning NEVER pass an untrusted string; it can enable SQL injection attacks!
        [[nodiscard]] std::string string_pragma(const char* name);

        /// Executes `PRAGMA name = value`.
        /// @warning NEVER pass an untrusted string; it can enable SQL injection attacks!
        status pragma(const char* name, int64_t value);
        /// Executes `PRAGMA name = value`.
        /// @warning NEVER pass an untrusted string; it can enable SQL injection attacks!
        status pragma(const char* name, std::string_view value);

#pragma mark - SCHEMA MIGRATION:

        /// Returns the database's "user version", an app-defined version number. Initially 0.
        [[nodiscard]] int64_t user_version() const;
        
        /// Sets the database's "user version", an app-defined version number.
        status set_user_version(int64_t);

        /// Simple schema upgrades. If the database's user version is equal to `old_version`,
        /// executes the SQL command in `sql_command` and then sets the version to `new_version`.
        /// The SQL will presumably change the database's schema by adding tables or indexes,
        /// altering tables, etc.
        /// It's recommended that you wrap all your migrations in a single transaction.
        status migrate_from(int64_t old_version,
                            int64_t new_version,
                            std::string_view sql_command);

        /// Simple schema upgrades. If the database's user version is less than `new_version`,
        /// executes the SQL command(s) in `sql_command` and then sets the user version to
        /// `new_version`.
        /// The SQL will presumably change the database's schema by adding tables or indexes,
        /// altering tables, etc.
        /// It's recommended that you wrap all your migrations in a single transaction.
        status migrate_to(int64_t new_version,
                          std::string_view sql_command);

        /// Simple schema upgrades. If the database's user version is equal to `old_version`,
        /// calls `fn` and on success sets the user version to `new_version`.
        /// The function will presumably change the database's schema by adding tables or indexes,
        /// altering tables, etc.
        /// It's recommended that you wrap all your migrations in a single transaction.
        status migrate_from(int64_t old_version,
                            int64_t new_version,
                            const std::function<status(database&)>& fn);

        /// Simple schema upgrades. If the database's user version is less than `new_version`,
        /// calls `fn` and on success sets the user version to `new_version`.
        /// The function will presumably change the database's schema by adding tables or indexes,
        /// altering tables, etc.
        /// It's recommended that you wrap all your migrations in a single transaction.
        status migrate_to(int64_t new_version,
                          const std::function<status(database&)>& fn);

#pragma mark - STATUS:

        /// The status of the last operation on this database or its queries/commands/blob_handles.
        status last_status() const noexcept;

        /// The error message, if any, from the last database operation.
        char const* _Nullable error_msg() const noexcept;

        /// The `rowid` of the last row inserted by an `INSERT` statement.
        int64_t last_insert_rowid() const noexcept;

        /// The number of rows changed by the last `execute` call or by a `command` object.
        int changes() const noexcept;

        /// The total number of rows changed _by this connection_ since it was opened.
        /// @note If you care about changes made by other connections, use `global_changes` instead.
        int64_t total_changes() const noexcept;

        /// Returns the "data version", a number which changes when the database is altered by
        /// _any_ connection in _any_ process.
        uint32_t global_changes() const noexcept;

        /// True if a transaction or savepoint is active.
        bool in_transaction() const noexcept;

        /// The number of beginTransaction calls not balanced by endTransaction.
        int transaction_depth() const noexcept          {return txn_depth_;}

#pragma mark - EXECUTING:

        /** Executes a (non-`SELECT`) statement, or multiple statements separated by `;`. */
        status execute(std::string_view sql);

        /// Returns a `command` object that will run the given SQL statement.
        /// @note This object comes from an internal `command_cache`, so subsequent calls with the
        ///       same SQL string will use the precompiled statement instead of compiling it again.
        [[nodiscard]] sqnice::command command(std::string_view sql);

        /// Returns a `query` object that will run the given SQL statement.
        /// @note This object comes from an internal `command_cache`, so subsequent calls with the
        ///       same SQL string will use the precompiled statement instead of compiling it again.
        [[nodiscard]] sqnice::query query(std::string_view sql) const;

        /// Low-level transaction support: begins a transaction.
        /// Transactions can nest; nested transactions are implemented as savepoints.
        /// @note It's usually better to use the higher-level `transaction` class instead.
        /// @param immediate  If true, the database immediately acquires an exclusive lock.
        status begin_transaction(bool immediate);

        /// Low-level transaction support: ends a (possibly nested) transaction.
        /// @note It's usually better to use the higher-level `transaction` class instead.
        /// @param commit  If true, commits the transaction; if false, aborts.
        status end_transaction(bool commit);

#pragma mark - FUNCTIONS:

        using function_handler = std::function<void (function_args, function_result)>;
        using step_handler     = std::function<void (function_args)>;
        using finish_handler   = std::function<void (function_result)>;

        /// Registers a SQL function.
        /// @param name  The SQL function name to register.
        /// @param h  The function that will be called. It gets arg values from the `function_args`
        ///             argument, and returns a result by assigning to the `function_result`.
        /// @param nargs  The function's argument count, or -1 to accept any number.
        /// @param flags  Optional attributes of the function.
        status create_function(std::string_view name, 
                               function_handler h,
                               int nargs = -1,
                               function_flags flags = {});

        /// Registers a SQL function. This variant takes care of marshaling the args & return value.
        /// @note  You must include "sqnice/functions.hh" or you'll get compile errors.
        template <class F>
        status create_function(std::string_view name, 
                               std::function<F> h,
                               function_flags flags = {})
        {
            auto fh = new std::function<F>(std::move(h));
            auto destroy = [](void* pApp) {delete static_cast<std::function<F>*>(pApp);};
            return create_function_impl<F>()(*this, name, flags, fh, destroy);
        }

        /// Registers a SQL aggregate function.
        /// @param name  The SQL name of the function.
        /// @param step  A function that takes `nargs` arguments.
        /// @param finish  A function that takes no arguments and sets the aggregate's result.
        /// @param nargs  The number of arguments the function takes; -1 allows any number.
        status create_aggregate(std::string_view name,
                                step_handler step, finish_handler finish,
                                int nargs = -1,
                                function_flags = {});

        /// Registers an aggregate function.
        /// This variant takes care of marshaling the args & return values.
        /// The template argument `T` must be a class or struct with two public instance methods:
        /// - `step`, whose parameter types are the `Ps...` template args
        /// - `finish`, which takes no args and returns your aggregate's type.
        /// For examples, see the test case "SQNice aggregate functions" in testfunctions.cc.
        /// @note  You must include "sqnice/functions.hh" or you'll get compile errors.
        template <class T, class... Ps>
        status create_aggregate(std::string_view name,
                                function_flags flags = {}) {
            return register_function(name, sizeof...(Ps), flags, nullptr,
                                     nullptr, stepx_impl<T, Ps...>, finishN_impl<T>, nullptr);
        }

#pragma mark - MAINTENANCE

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

        status backup(database& destdb, const backup_handler& h = {});

        status backup(std::string_view dbname,
                      database& destdb,
                      std::string_view destdbname,
                      const backup_handler& h,
                      int step_page = 5);

#pragma mark - ENCRYPTED DATABASES:

        /// True if sqnice and SQLite were built with encryption support (SQLCipher or SEE.)
        /// The macro `SQLITE_HAS_CODEC` must have been defined when building sqnice.
        static const bool encryption_available;

        /// Unlocks an encrypted database, or makes a newly-created database encrypted,
        /// by providing the password to use to encrypt/decrypt data.
        ///
        /// This should be called immediately after opening the database, since no data can be
        /// read from an encrypted database without the key, and encryption can't be enabled in
        /// a new database once data has been written to it.
        ///
        /// SQLite Encryption Extension supports calling this function with an empty string.
        /// This does not encrypt a new database, but causes a few bytes to be reserved in every
        /// every page, which will improve the quality of any subsequent encryption.
        ///
        /// @warning Database encryption is a complex topic that needs more space than this comment!
        ///        Please read the [SQLCipher](https://www.zetetic.net/sqlcipher)
        ///        or [SQLite Encryption Extension](https://sqlite.org/com/see.html) docs first.
        ///
        /// @note  This function requires SQLCipher or the SQLite Encryption Extension.
        ///        Otherwise it returns/throws status::error.
        status use_password(std::string_view password);

        /// Changes the encryption password of an existing database.
        ///
        /// With SQLite Encryption Extension, this method will encrypt or decrypt a database
        /// in place. (Pass an empty string to decrypt.)
        ///
        /// SQLCipher **does not** support in-place encryption or decryption, so this function works
        /// only with an already-encrypted database, and the password must not be empty. To learn
        /// how to encrypt or decrypt an existing database, see:
        /// https://discuss.zetetic.net/t/how-to-encrypt-a-plaintext-sqlite-database/868
        ///
        /// @note  This function requires SQLCipher or the SQLite Encryption Extension.
        ///        Otherwise it returns/throws status::error.
        status change_password(std::string_view newPassword);

#pragma mark - LOGGING

        using log_handler = std::function<void (status, const char* message)>;

        /// Registers a global callback function that will be passed every message SQLite logs.
        static void set_log_handler(log_handler) noexcept;


#pragma mark - CALLBACKS

        // For details, see the SQLite docs for sqlite3_busy_handler(), etc.

        using busy_handler = std::function<bool (int attempts)>;
        using commit_handler = std::function<bool ()>;
        using rollback_handler = std::function<void ()>;
        using update_handler = std::function<void (int op, char const*dbName, char const*tableName,
                                                   int64_t rowid)>;
        using authorize_handler = std::function<status (int action,
                                                        char const* _Nullable arg1,
                                                        char const* _Nullable arg2,
                                                        char const* _Nullable dbName,
                                                        char const* _Nullable triggerOrView)>;

        void set_busy_handler(busy_handler) noexcept;
        void set_commit_handler(commit_handler) noexcept;
        void set_rollback_handler(rollback_handler) noexcept;
        void set_update_handler(update_handler) noexcept;
        void set_authorize_handler(authorize_handler) noexcept;

        using argv_t = sqlite3_value* _Nullable * _Nullable;
        using callFn = void (*)(sqlite3_context*, int, argv_t);
        using finishFn = void (*)(sqlite3_context*);
        using destroyFn = void (*)(void*);

        /// Lowest-level API for defining a SQL function. You probably want to use
        /// `create_function` or `create_aggregate` instead.
        status register_function(std::string_view name,
                                 int nArgs,
                                 function_flags,
                                 void* _Nullable pApp,
                                 callFn _Nullable call,
                                 callFn _Nullable step,
                                 finishFn _Nullable finish,
                                 destroyFn _Nullable destroy);

    private:
        friend class checking;
        friend class pool;

        void set_db(db_handle db) {
            db_ = std::move(db);
            weak_db_ = db_;
        }
        void tear_down() noexcept;
        void set_borrowed(bool b) const noexcept            {borrowed_ = b;}
        status executef(char const* sql, ...)   sqnice_printflike(2, 3);
        status _use_password(std::string_view password, int (*fn)(sqlite3*,const void*,int));

        // internal gunk used by create_function and create_aggregate.
        // Implementations in functions.hh.
        using pfunction_base = std::shared_ptr<void>;
        template <class R, class... Ps> static void functionx_impl(sqlite3_context*, int, argv_t);
        template <class T, class... Ps>static void stepx_impl(sqlite3_context*, int, argv_t);
        template <class T> static void finishN_impl(sqlite3_context*);
        template<class R, class... Ps> struct create_function_impl;
        template<class R, class... Ps> struct create_function_impl<R (Ps...)> {
            status operator()(database& db, std::string_view name, function_flags flags, 
                              void* fh, destroyFn destroy) const {
                return db.register_function(name, sizeof...(Ps), flags, fh, functionx_impl<R, Ps...>,
                                            nullptr, nullptr, destroy);
            }
        };

    private:
        db_handle           db_;                    // shared_ptr<sqlite3>
        int                 txn_depth_ = 0;         // Transaction nesting level
        bool                txn_immediate_ = false; // True if outer txn is immediate
        bool                temporary_ = false;     // True if db is temporary
        bool mutable        borrowed_ = false;      // True if checked out from a `pool`
        std::unique_ptr<database_error> posthumous_error_;
        std::unique_ptr<statement_cache<sqnice::command>> commands_;
        std::unique_ptr<statement_cache<sqnice::query>> mutable queries_;
        busy_handler        bh_;
        commit_handler      ch_;
        rollback_handler    rh_;
        update_handler      uh_;
        authorize_handler   ah_;
    };

}

ASSUME_NONNULL_END

#endif
