// sqlite3pp.h
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
#ifndef SQLITE3PP_H
#define SQLITE3PP_H

#define SQLITE3PP_VERSION "2.0.0"
#define SQLITE3PP_VERSION_MAJOR 2
#define SQLITE3PP_VERSION_MINOR 0
#define SQLITE3PP_VERSION_PATCH 0

#include <concepts>
#include <functional>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <tuple>
#include <unordered_map>

#ifdef SQLITE3PP_LOADABLE_EXTENSION
#  include <sqlite3ext.h>
    SQLITE_EXTENSION_INIT1
#else
#  include <sqlite3.h>
#endif


namespace sqlite3pp {
    
    class database;

    // Defined in sqlite3ppext.hh
    namespace ext {
        class function;
        class aggregate;
        database borrow(sqlite3*);
    }

    /** Classes inheriting from this cannot be copied, but can be moved. */
    class noncopyable {
    protected:
        noncopyable() = default;
        ~noncopyable() = default;

        noncopyable(noncopyable&&) = default;
        noncopyable& operator=(noncopyable&&) = default;

        noncopyable(noncopyable const&) = delete;
        noncopyable& operator=(noncopyable const&) = delete;
    };

    /** A SQLite error code. */
    enum class status : int {
        ok          = SQLITE_OK,
        busy        = SQLITE_BUSY,
        locked      = SQLITE_LOCKED,
        constraint  = SQLITE_CONSTRAINT,
        misuse      = SQLITE_MISUSE,
        done        = SQLITE_DONE,
        row         = SQLITE_ROW,
    };

    /// Masks out other bits set in extended status codes
    inline status basic_status(status s)                    {return status{int(s) & 0xff};}
    /// True if a `status` is successful (not an error.)
    inline bool ok(status s)                                {return s == status::ok;}

    /** Exception class thrown by this API. */
    class database_error : public std::runtime_error {
    public:
        explicit database_error(char const* msg, status rc);
        explicit database_error(database& db, status rc);
        explicit database_error(char const* msg, int rc)    :database_error(msg,status{rc}) { }
        explicit database_error(database& db, int rc)       :database_error(db,status{rc}) { }

        status const error_code;
    };


    /** A base class that handles exceptions by throwing or returning an error code.
        Most of the other classes derive from this. */
    class checking {
    public:
        /// Enables or disables exceptions. 
        /// For a `database`, this defaults to false.
        /// For other objects, it defaults to the value in the `database` they are created from.
        void exceptions(bool x)     {exceptions_ = x;}
        /// True if exceptions are enabled.
        bool exceptions()           {return exceptions_;}
    protected:
        checking(database &db, bool x)              :db_(db), exceptions_(x) { }
        explicit checking(database &db);
        status check(status rc) const;
        status check(int rc) const                  {return check(status{rc});}
        [[noreturn]] void throw_(status rc) const;
        [[noreturn]] void throw_(int rc) const      {throw_(status{rc});}

        database&   db_;
        bool        exceptions_ = false;
    };


#pragma mark - DATABASE:


    /** A SQLite database connection. */
    class database : public checking, noncopyable {
    public:
        explicit database(char const* dbname = nullptr,
                          int flags = SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE,
                          const char* vfs = nullptr);

        database(database&& db);
        database& operator=(database&& db);

        ~database();

        status connect(char const* dbname, int flags, const char* vfs = nullptr);
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
        char const* error_msg() const;

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

    private:
        friend class statement;
        friend class database_error;
        friend class blob_handle;
        friend class ext::function;
        friend class ext::aggregate;
        friend database ext::borrow(sqlite3* pdb);

        database(sqlite3* pdb);

    private:
        sqlite3*            db_;
        bool                borrowing_;
        busy_handler        bh_;
        commit_handler      ch_;
        rollback_handler    rh_;
        update_handler      uh_;
        authorize_handler   ah_;
    };


#pragma mark - STATEMENT:

    class statement;

    // Declaring a function `sqlite3cpp::bind_helper(statement&, int, T)` allows the type T to be
    // used in a parameter binding. The function should call statement::bind().
    template <typename T>
    concept bindable = requires(statement& stmt, T value) {
        {bind_helper(stmt, 1, value)} -> std::same_as<status>;
    };

    class null_type {};
    /** A singleton value representing SQL `NULL`, for use with the `bind` API. */
    constexpr null_type ignore;

    /** Specifies how string and blob values are bound. `copy` is safer, `nocopy` faster.*/
    enum copy_semantic { copy, nocopy };

    /** A blob value to bind to a statement parameter. */
    struct blob {
        const void* data;
        size_t size;
        copy_semantic fcopy;
    };


    /** Abstract base class of `command` and `query`. */
    class statement : public checking, noncopyable {
    public:
        /// Stops the execution of the statement.
        /// @note  The does not clear bindings.
        status reset();

        /// Clears all the parameter bindings to `NULL`.
        status clear_bindings();

        class bindref;
        /// A reference to the `idx`'th parameter; assign a value to bind it.
        [[nodiscard]] inline bindref operator[] (int idx);
        /// A reference to a named parameter; assign a value to bind it.
        [[nodiscard]] bindref operator[] (char const *name);

        class bindstream;
        /// Returns a sort of output stream, on which each `<<` binds the next numbered parameter.
        bindstream binder(int idx = 1);

        // The following methods bind values to numbered parameters (`?` params start at 1)
        status bind(int idx, std::signed_integral auto v) {
            static_assert(sizeof(v) <= 8);
            if constexpr (sizeof(v) <= 4)
                return bind_int(idx, v);
            else
                return bind_int64(idx, v);
        }

        status bind(int idx, std::unsigned_integral auto v) {
            static_assert(sizeof(v) <= 8);
            if constexpr (sizeof(v) < 4)
                return bind_int(idx, int(v));
            else if constexpr (sizeof(v) < 8)
                return bind_int64(idx, int64_t(v));
            else
                return bind_uint64(idx, v);
        }

        status bind(int idx, std::floating_point auto v)        {return bind_double(idx, v);}

        status bind(int idx, char const* value, copy_semantic = copy);
        status bind(int idx, std::string_view value, copy_semantic = copy);

        status bind(int idx, blob value);
        status bind(int idx, std::span<const std::byte> value, copy_semantic = copy);

        status bind(int idx, null_type)                         {return bind(idx, nullptr);}
        status bind(int idx, nullptr_t);

        template <bindable T>
        status bind(int idx, T&& v)             {return bind_helper(*this, idx, std::forward<T>(v));}

        /// Binds a value to a named parameter.
        template <typename T>
        status bind(char const* name, T&& v)    {return bind(bind_parameter_index(name), std::forward<T>(v));}

        /// Returns the (1-based) index of a named parameter, or 0 if none exists.
        int bind_parameter_index(const char* name) const   {return sqlite3_bind_parameter_index(stmt_, name);}

        /// Can be used to replace the SQL of the statement.
        status prepare(char const* sql);

        /// Tears down the `sqlite3_stmt`. The statement is no longer prepared and can't be used.
        status finish();

        /// True if the statement contains SQL and can be executed.
        bool prepared() const           {return stmt_ != nullptr;}
        operator bool() const           {return prepared();}

    protected:
        explicit statement(database& db, char const* stmt = nullptr);
        statement(statement&&) = default;
        ~statement();

        status bind_int(int idx, int value);
        status bind_int64(int idx, int64_t value);
        status bind_uint64(int idx, uint64_t value);
        status bind_double(int idx, double value);

        template <typename T, typename... Args>
        void _bind_args(int idx, T&& arg, Args... rest) {
            bind(idx, std::forward<T>(arg));
            if constexpr (sizeof...(rest) > 0)
                _bind_args(idx + 1, rest...);
        }

        void share(const statement&);
        status prepare_impl(char const* stmt);
        status finish_impl(sqlite3_stmt* stmt);
        status step();

    protected:
        sqlite3_stmt*   stmt_;
        char const*     tail_;
        bool            shared_ = false;
    };


    /* A reference to a statement parameter; an implementation detail of statement::operator[]. */
    class statement::bindref : noncopyable {
    public:
        bindref(statement &stmt, int idx) :stmt_(stmt), idx_(idx) { }

        /// Assigning a value to a `bindref` calls `bind` on the `statement`.
        template <class T>
        void operator= (const T &value) {
            if (auto rc = stmt_.bind(idx_, value); rc != status::ok)
                throw database_error(stmt_.db_, rc);
        }
    private:
        statement&  stmt_;
        int const   idx_;
    };

    statement::bindref statement::operator[] (int idx)  {return bindref(*this, idx);}


    /** Produced by `statement::binder()`. Binds one statement parameter for each `<<` call.*/
    class statement::bindstream {
    public:
        explicit bindstream(statement& stmt, int idx =1)  :stmt_(stmt), idx_(idx) { }

        template <class T>
        bindstream& operator << (T&& v)     {stmt_.bind(idx_++, std::forward<T>(v)); return *this;}

    private:
        statement& stmt_;
        int idx_;
    };


    /** A SQL statement other than `SELECT`; i.e. `INSERT`, `UPDATE`, `CREATE`... */
    class command : public statement {
    public:
        explicit command(database& db, char const* stmt = nullptr);

        /// Executes the statement.
        /// @note  To get the rowid of an INSERT, call `last_insert_rowid`.
        /// @note  To get the number of rows changed, call `changes()`.
        status execute();

        /// Executes the statement, without throwing exceptions.
        /// This is useful if you want to handle constraint errors.
        [[nodiscard]] status try_execute();

        /// Binds parameters to the arguments, then executes the statement.
        template <typename... Args>
        status execute(Args... args)        {_bind_args(1, args...); return execute();}

        /// Binds parameters to the arguments, then executes the statement,
        /// but without throwing exceptions.
        template <typename... Args>
        [[nodiscard]] status try_execute(Args... args) {_bind_args(1, args...); return try_execute();}

        /// The last rowid inserted by this command, after executing it.
        int64_t last_insert_rowid() const   {return db_.last_insert_rowid();}

        /// The number of rows changed by this command, after executing it.
        int changes() const                 {return db_.changes();}

        /// Creates a new instance that shares the same underlying `sqlite3_stmt`.
        /// @warning  Do not use the copy after the original `command` is destructed!
        command shared_copy() const;

        [[deprecated]] status execute_all();
    };


    /** A `SELECT` statement, whose results can be iterated. */
    class query : public statement {
    public:
        explicit query(database& db, char const* stmt = nullptr);

        /// Creates a new instance that shares the same underlying `sqlite3_stmt`.
        /// @warning  Do not use the copy after the original `command` is destructed!
        query shared_copy() const;

        /// Binds its arguments to multiple query parameters starting at index 1.
        template <typename... Args>
        query& operator() (Args... args) & {_bind_args(1, args...); return *this;}

        template <typename... Args>
        [[nodiscard]] query operator() (Args... args) && {
            _bind_args(1, args...); return std::move(*this);
        }

        /// The number of columns the query will produce.
        int column_count() const;

        /// The name of the `idx`'th column.
        char const* column_name(int idx) const;
        
        /// The declared type of the `idx`th column in the table's schema. Not generally useful.
        char const* column_decltype(int idx) const;

        class iterator;
        class row;

        /// Runs the query and returns an iterator pointing to the first result row.
        [[nodiscard]] iterator begin();
        /// An empty iterator representing the end of the rows.
        [[nodiscard]] inline iterator end();

        /// A convenience method that runs the query and returns the value of the first row's
        /// first column. If there are no rows, returns `std::nullopt`.
        template <typename T>
        std::optional<T> single_value();

        /// A convenience method that runs the query and returns the value of the first row's
        /// first column. If there are no rows, returns `defaultResult` instead.
        template <typename T>
        T single_value_or(T const& defaultResult);

        // (Disabled because `reset` invalidates the pointers in the `blob`; use string instead.)
        template <> std::optional<blob> single_value() = delete;
        template <> blob single_value_or(blob const&) = delete;
    };


    // Specializing the struct `column_helper<T>` and adding a static `get(row&, int idx) -> T`
    // allows a query result value to be converted to type T.
    template <typename T> struct column_helper{ };
    template <typename T>
    concept columnable = requires(query::row const& row) {
        {column_helper<T>::get(row, 1)} -> std::same_as<T>;
    };


    /** Represents the current row of a query being iterated.
        @note  Columns are numbered from 0. */
    class query::row {
    public:
        /// The number of columns in the row. (Same as `query::column_count`.)
        int column_count() const;

        /// The SQLite data type of the `idx`th column -- `SQLITE_INTEGER`, `SQLITE_TEXT`, etc.
        int column_type(int idx) const;

        /// True if the `idx`th column value is not `NULL`.
        bool not_null(int idx) const                {return column_type(idx) != SQLITE_NULL;}

        /// The length in bytes of a BLOB or a UTF-8 TEXT value.
        int column_bytes(int idx) const;

        /// Returns the value of the `idx`th column as C++ type `T`.
        template <class T> T get(int idx) const     {return get(idx, T());}

        template <columnable T> T get(int idx) const  {return column_helper<T>::get(*this, idx);}

        class column;
        /// Returns a lightweight object representing a column. This makes it easy to assign a
        /// column value to a variable; for example, `int n = row[0];`
        [[nodiscard]] inline column operator[] (int idx) const;

        class getstream;
        /// Returns a sort of input stream from which columns can be read using `>>`.
        getstream getter(int idx = 0) const;

    private:
        friend class query;
        friend class query::iterator;
        friend class column;

        explicit row(sqlite3_stmt* stmt);

        template <std::signed_integral T>
        T get(int idx, T) const {
            if constexpr (sizeof(T) <= sizeof(int))
                return static_cast<T>(get_int(idx));
            else
                return get_int64(idx);
        }

        template <std::unsigned_integral T>
        T get(int idx, T) const {
            if constexpr (sizeof(T) < sizeof(int))
                return static_cast<T>(get_int(idx));
            else
                return static_cast<T>(get_int64(idx));
        }

        double get(int idx, double) const;
        char const* get(int idx, char const*) const;
        std::string get(int idx, std::string) const;
        std::string_view get(int idx, std::string_view) const;
        void const* get(int idx, void const*) const;
        bool get(int idx, bool) const       {return get_int(idx) != 0;}
        blob get(int idx, blob) const;
        null_type get(int idx, null_type) const;

        int get_int(int idx) const;
        int64_t get_int64(int idx) const;
        double get_double(int idx) const;

    private:
        sqlite3_stmt* stmt_;
    };


    /** Represents a single column of a query row; returned by `query::row[]`. */
    class query::row::column : sqlite3pp::noncopyable {
    public:
        template <typename T>
        operator T() const          {return row_.get<T>(idx_);}

        int type() const            {return row_.column_type(idx_);}
        bool is_blob() const        {return type() == SQLITE_BLOB;}
        bool not_null() const       {return row_.not_null(idx_);}

    private:
        friend class query::row;
        column(query::row r, int idx) noexcept :row_(r), idx_(idx) { }

        query::row const    row_;
        int const           idx_;
    };

    query::row::column query::row::operator[] (int idx) const   {return column(*this, idx);}



    /** A sort of input stream over a query row's columns. Each `>>` stores the next value. */
    class query::row::getstream {
    public:
        getstream(row const* rws, int idx);

        template <class T>
        getstream& operator >> (T& value) {
            value = rws_->get(idx_++, T());
            return *this;
        }

    private:
        row const*  rws_;
        int         idx_;
    };


    /** Iterator over a query's result rows. */
    class query::iterator {
    public:
        iterator() = default;
        explicit iterator(query* cmd);

        bool operator==(iterator const& other) const    {return rc_ == other.rc_;}

        iterator& operator++();

        const row& operator*() const                    {return rows_;}
        const row* operator->() const                   {return &rows_;}

        /// True if the iterator is valid; false if it's at the end.
        explicit operator bool() const                  {return rc_ != status::done;}

        /// A convenience for accessing a column of the current row.
        row::column operator[] (int idx) const          {return rows_[idx];}

        using iterator_category = std::input_iterator_tag;
        using value_type = row;
        using difference_type = std::ptrdiff_t;
        using pointer = row*;
        using reference = row&;

    private:
        query*  query_  {0};
        status  rc_     {status::done};
        row     rows_   {nullptr};
    };


    query::iterator query::end() {
        return iterator();
    }

    template <typename T>
    std::optional<T> query::single_value() {
        std::optional<T> result;
        if (auto i = begin())
            result = i->get<T>(0);
        reset();
        return result;
    }

    template <typename T>
    T query::single_value_or(T const& defaultResult) {
        if (auto i = begin()) {
            T result = i->get<T>(0);
            reset();
            return result;
        } else {
            reset();
            return defaultResult;
        }
    }


#pragma mark - STATEMENT CACHE:


    /** A cache of pre-compiled `query` or `command` objects. This has three benefits:
        1. Reusing a compiled statement is faster than compiling a new one.
        2. If you work around 1 by keeping statement objects around, you have to remember to reset
           them when done with them, else they hang onto database resources.
        3. Destructing the cache destructs all the statements automatically.*/
    template <class STMT>
    class statement_cache {
    public:
        explicit statement_cache(database &db) :db_(db) { }

        /// Compiles a STMT, or returns a copy of an already-compiled one with the same SQL string.
        /// @warning  If two returned STMTs with the same string are in scope at the same time,
        ///           they won't work right because they are using the same `sqlite3_stmt`.
        STMT compile(const std::string &sql) {
            const STMT* stmt;
            if (auto i = stmts_.find(sql); i != stmts_.end()) {
                stmt = &i->second;
            } else {
                auto x = stmts_.emplace(std::piecewise_construct,
                                        std::tuple<std::string>{sql},
                                        std::tuple<database&,const char*>{db_, sql.c_str()});
                stmt = &x.first->second;
            }
            return stmt->shared_copy();
        }

        STMT operator[] (const std::string &sql)    {return compile(sql);}
        STMT operator[] (const char *sql)           {return compile(sql);}

        /// Empties the cache, freeing all statements.
        void clear()                                {stmts_.clear();}

    private:
        database& db_;
        std::unordered_map<std::string,STMT> stmts_;
    };

    /** A cache of pre-compiled `command` objects. */
    using command_cache = statement_cache<command>;

    /** A cache of pre-compiled `query` objects. */
    using query_cache = statement_cache<query>;


#pragma mark - TRANSACTIONS:


    /** An RAII wrapper around SQLite's `BEGIN` and `COMMIT`/`ROLLBACK` commands.
        @note  These do not nest! If you need nesting, use `savepoint` instead. */
    class transaction : public checking, noncopyable {
    public:
        /// Begins a transaction.
        /// @param db  The database.
        /// @param autocommit  If true, the transaction will automatically commit when
        ///     the destructor runs. Not recommended because destructors should not throw
        ///     exceptions, so you won't know if it succeeded or not.
        /// @param immediate  If true, uses `BEGIN IMMEDIATE`, which immediately grabs
        ///     the database lock. If false, the first write you make in the transaction
        ///     will try to grab the lock but will fail if another transaction is active.
        /// @throws database_error if a transaction is already active.
        explicit transaction(database& db,
                             bool autocommit = false,
                             bool immediate = true);
        transaction(transaction&&);
        ~transaction();

        /// Commits the transaction.
        status commit();
        /// Rolls back (aborts) the transaction.
        status rollback();

    private:
        bool active_;
        bool autocommit_;
    };

    
    /** A savepoint is like a transaction but can be nested. */
    class savepoint : public checking, noncopyable {
    public:
        /// Begins a transaction.
        /// @param db  The database.
        /// @param autocommit  If true, the transaction will automatically commit when
        ///     the destructor runs. Not recommended because destructors should not throw
        ///     exceptions, so you won't know if it succeeded or not.
        explicit savepoint(database& db, bool autocommit = false);
        savepoint(savepoint&&);
        ~savepoint();

        /// Commits the savepoint.
        /// @note  Changes made in a nested savepoint are not actually persisted until
        ///     the outermost savepoint is committed.
        status commit();

        /// Rolls back (aborts) the savepoint. All changes made since the savepoint was
        ///     created are removed.
        status rollback();

    private:
        status execute(char const *cmd);

        bool active_;
        bool autocommit_;
    };


#pragma mark - BLOB HANDLE:


    /** Random access to the data in a blob. */
    class blob_handle : public checking, noncopyable {
    public:
        /// Opens a handle for reading a blob.
        /// @note  If the blob doesn't exist, the behavior depends on the database's `exceptions`
        ///     status. If they're enabled, this will throw an exception. If not, it will return
        ///     normally, but `status` will return the error, and all reads will fail.
        /// @param db  The `database` handle.
        /// @param database  The "symbolic name" of the database:
        ///     - For the main database file: "main".
        ///     - For TEMP tables: "temp".
        ///     - For attached databases: the name that appears after `AS` in the ATTACH statement.
        /// @param table  The name of the table.
        /// @param column  The name of the table column.
        /// @param rowid  The row ID containing the blob.
        /// @param writeable  True if you want to write to the data.
        blob_handle(database& db,
                    const char *database,
                    const char* table,
                    const char *column,
                    int64_t rowid,
                    bool writeable);

        ~blob_handle()                      {if (blob_) sqlite3_blob_close(blob_);}

        /// The status of the last operation: opening the blob handle, or the last read.
        /// @note  If exceptions are enabled,
        status status() const               {return status_;}

        /// The size in bytes of the blob.
        /// @note  This API uses `int` internally so blobs are limited to 2^31 bytes (~2GB.)
        uint64_t size() const               {return size_;}

        /// Reads from the blob.
        /// @note  It is not an error to read past the end of the blob; the read will be truncated
        ///     and the byte count returned will be less than you asked for. But it _is_ an error
        ///     for the read to start past the end of the blob.
        /// @param dst  The destination address to copy data to.
        /// @param len  The number of bytes to read.
        /// @param offset  The offset in the blob to start reading at.
        /// @returns  The number of bytes actually read, or -1 on error; check `status()`.
        /// @throws database_error  on error if exceptions are enabled.
        [[nodiscard]] ssize_t pread(void *dst, size_t len, uint64_t offset) const;

        /// Writes to the blob.
        /// @note  Unlike reads, it _is_ an error to write past the end of a blob, since this may
        ///     cause data loss. (The blob's length cannot be changed.)
        /// @param src  The address of the data to write.
        /// @param len  The number of bytes to write.
        /// @param offset  The offset in the blob to start writing at.
        /// @returns  The number of bytes actually written, or -1 on error; check `status()`.
        /// @throws database_error  on error if exceptions are enabled.
        [[nodiscard]] ssize_t pwrite(const void *src, size_t len, uint64_t offset);

    private:
        int range_check(size_t len, uint64_t offset) const;

        sqlite3_blob*       blob_ = nullptr;
        uint64_t            size_ = 0;
        mutable enum status status_;
    };

} // namespace sqlite3pp

#endif
