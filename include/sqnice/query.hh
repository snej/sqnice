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
#ifndef SQNICE_QUERY_H
#define SQNICE_QUERY_H

#include "sqnice/base.hh"
#include <concepts>
#include <cstddef>
#include <iterator>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <cassert>

ASSUME_NONNULL_BEGIN

struct sqlite3_stmt;

namespace sqnice {
    class arg_value;
    class database;
    class statement;
    class column_value;

    /** SQLite's data types. (Values are equal to SQLITE_INT, etc.) */
    enum class data_type : int {
        integer         = 1,
        floating_point  = 2,
        text            = 3,
        blob            = 4,
        null            = 5,
    };


    /** The concept `bindable` identifies custom types that can be bound to statement parameters.
        Declaring a function `sqlite3cpp::bind_helper(statement&, int, T)` allows the type T to be
        used in a parameter binding. This function should call statement::bind(). */
    template <typename T>
    concept bindable = requires(statement& stmt, T value) {
        {bind_helper(stmt, 1, value)} -> std::same_as<status>;
    };


    struct null_type {};
    /** A singleton value representing SQL `NULL`, for use with the `getstream` API. */
    extern null_type ignore;  //FIXME: This is awkward; get rid of it or make it constexpr


    /** Specifies how string and blob args are bound. `copy` is safer, `nocopy` faster.*/
    enum copy_semantic { copy, nocopy };


    /** A blob value to bind to a statement parameter. */
    struct blob {
        const void* _Nullable   data  = nullptr;
        size_t                  size  = 0;
        copy_semantic           fcopy = copy;
    };


    /** Abstract base class of `command` and `query`. */
    class statement : public checking {
    public:
        enum persistence {nonpersistent, persistent};

        /// Initializes or replaces the SQL of the statement.
        status prepare(std::string_view sql, persistence = nonpersistent);

        /// Tears down the `sqlite3_stmt`. This instance is no longer prepared and can't be used.
        void finish() noexcept;

        /// True if the statement contains SQL and can be executed.
        bool prepared() const                           {return impl_ != nullptr;}
        explicit operator bool() const                  {return prepared();}

        /// The original SQL of the compiled statement.
        /// @note The string remains valid until `finish`, `prepare` or the destructor are called.
        std::string_view sql() const;

        /// The SQL of the compiled statement,
        /// with parameters (like "?1") replaced by their current bindings.
        std::string expanded_sql() const;

        /// True if the statement is running; `reset` clears this.
        bool busy() const noexcept;

        /// Stops the execution of the statement.
        /// @note  This does not clear bindings.
        void reset() noexcept;

        /// Clears all the parameter bindings to `NULL`.
        void clear_bindings();

        class bindref;
        /// A reference to the `idx`'th parameter; assign a value to bind it.
        [[nodiscard]] inline bindref operator[] (int idx);
        /// A reference to a named parameter; assign a value to bind it.
        [[nodiscard]] bindref operator[] (char const *name);

        class bindstream;
        /// Returns a sort of output stream, on which each `<<` binds the next numbered parameter.
        bindstream binder(int idx = 1);

        /// Returns the (1-based) index of a named parameter, or 0 if none exists.
        int parameter_index(const char* name) const noexcept;

        /// Returns the (1-based) index of a named parameter, or throws if none exists.
        /// @throws std::invalid_argument
        int check_parameter_index(const char* name) const;

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

        status bind(int idx, std::floating_point auto v)    {return bind_double(idx, v);}

        status bind(int idx, char const* _Nullable value, copy_semantic = copy);
        status bind(int idx, std::string_view value, copy_semantic = copy);

        status bind(int idx, blob value);
        status bind(int idx, std::span<const std::byte> value, copy_semantic = copy);

        status bind(int idx, null_type)                     {return bind(idx, nullptr);}
        status bind(int idx, nullptr_t);

        status bind(int idx, arg_value);

        template <bindable T>
        status bind(int idx, T&& v) {
            return bind_helper(*this, idx, std::forward<T>(v));
        }

        using pointer_destructor = void(*)(void*);
        status bind_pointer(int idx, void* pointer, const char* type, pointer_destructor);

        /// Binds a value to a named parameter.
        template <typename T>
        status bind(char const* name, T&& v)            {
            return bind(check_parameter_index(name), std::forward<T>(v));
        }

        /// Binds a value to a named parameter, with a copy/nocopy flag.
        template <typename T>
        status bind(char const* name, T&& v, copy_semantic cp)            {
            return bind(check_parameter_index(name), std::forward<T>(v), cp);
        }

        sqlite3_stmt* stmt() const;
        sqlite3_stmt* any_stmt() const;

        statement(statement const&) noexcept;
        statement& operator=(statement const&) noexcept;
        statement(statement&&) noexcept;
        statement& operator=(statement&&) noexcept;

    protected:
        class impl {
        public:
            explicit impl(sqlite3_stmt* s)              :stmt(s) { }
            ~impl();
            bool owned() const                          {return owner_ != nullptr;}
            bool owned_by(const void* o) const          {return owner_ == o;}
            bool set_owner(const void* _Nullable newOwner) {
                return newOwner == owner_ || transfer_owner(nullptr, newOwner);
            }
            bool transfer_owner(const void* _Nullable oldOwner, const void* _Nullable newOwner) {
                if (owner_ == oldOwner) {
                    owner_ = newOwner;
                    return true;
                } else {
                    return false;
                }
            }

            sqlite3_stmt* const   stmt;
        private:
            const void* _Nullable owner_ = nullptr;
        };

        explicit statement(checking& ck) noexcept       :checking(ck) { }
        explicit statement(checking&, std::shared_ptr<impl> impl) noexcept;
        statement(checking&, std::string_view sql, persistence);
        ~statement() noexcept;

        std::shared_ptr<impl> give_impl(const void* _Nullable newOwner = nullptr);
        status check_bind(int rc);
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

    private:
        std::shared_ptr<impl> impl_;
    };


    /* A reference to a statement parameter; an implementation detail of statement::operator[]. */
    class statement::bindref : noncopyable {
    public:
        /// Assigning a value to a `bindref` calls `bind` on the `statement`.
        /// @note  This returns `status`, not `*this`, which is unusual for an assignment operator.
        template <class T>
        status operator= (T&& value)              {return stmt_.bind(idx_, std::forward<T>(value));}

    private:
        friend class statement;
        bindref(statement &stmt, int idx) noexcept      :stmt_(stmt), idx_(idx) { }
        statement&  stmt_;
        int const   idx_;
    };

    statement::bindref statement::operator[] (int idx)  {return bindref(*this, idx);}


    /** Produced by `statement::binder()`. Binds one statement parameter for each `<<` call.*/
    class statement::bindstream {
    public:
        template <class T>
        bindstream& operator << (T&& v)     {stmt_.bind(idx_++, std::forward<T>(v)); return *this;}

    private:
        friend class statement;
        explicit bindstream(statement& stmt, int idx =1) noexcept :stmt_(stmt), idx_(idx) { }
        statement& stmt_;
        int idx_;
    };


    /** A SQL statement that does not return rows of results,
        i.e. `INSERT`, `UPDATE`, `CREATE`... anything other than `SELECT`. */
    class command : public statement {
    public:
        /// Creates a command, compiling the SQL string.
        /// @throws database_error if the SQL is invalid.
        command(checking& ck, std::string_view sql, persistence = nonpersistent)
            :statement(ck, sql, persistent) { }

        /// Executes the statement.
        /// @note  To get the rowid of an INSERT, call `last_insert_rowid`.
        /// @note  To get the number of rows changed, call `changes`.
        status execute()                                {return check(try_execute());}

        /// Executes the statement, without throwing exceptions.
        /// This is useful if you want to handle constraint errors.
        [[nodiscard]] status try_execute() noexcept;

        /// Binds parameters to the arguments, then executes the statement.
        template <typename... Args>
        status execute(Args... args)                    {_bind_args(1, args...); return execute();}

        /// Binds parameters to the arguments, then executes the statement,
        /// but without throwing exceptions.
        template <typename... Args>
        [[nodiscard]] status try_execute(Args... args) noexcept {
            _bind_args(1,args...);
            return try_execute();
        }

        /// The last rowid inserted by this command, after executing it.
        int64_t last_insert_rowid() const noexcept          {return last_rowid_;}

        /// The number of rows changed by this command, after executing it.
        int changes() const noexcept                        {return changes_;}

    private:
        int64_t last_rowid_ = -1;
        int     changes_ = 0;
    };


    /** A `SELECT` statement, whose results can be iterated. */
    class query : public statement {
    public:
        /// Creates a command, compiling the SQL string.
        /// @throws database_error if the SQL is invalid.
        query(checking& db, std::string_view sql, persistence = nonpersistent)
            :statement(db, sql, persistent) { }

        /// Binds its arguments to multiple query parameters starting at index 1.
        template <typename... Args>
        query& operator() (Args... args) & {_bind_args(1, args...); return *this;}

        template <typename... Args>
        [[nodiscard]] query operator() (Args... args) && {      // (variant avoids dangling ref to
            _bind_args(1, args...); return std::move(*this);    // rvalue by returning a copy)
        }

        /// The number of columns the query will produce.
        int column_count() const noexcept;

        /// The name of the `idx`'th column.
        /// @throws std::domain_error if the index is invalid.
        char const* column_name(unsigned idx) const;

        /// The declared type of the `idx`th column in the table's schema. (Not generally useful.)
        /// @throws std::domain_error if the index is invalid.
        char const* column_decltype(unsigned idx) const;

        class iterator;
        class row;

        /// Runs the query and returns an iterator pointing to the first result row.
        [[nodiscard]] inline iterator begin();
        /// An empty iterator representing the end of the rows.
        [[nodiscard]] inline iterator end() noexcept;

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

    private:
        unsigned check_idx(unsigned idx) const;
    };


    /** Represents the current row of a query being iterated.
        @note  Columns are numbered from 0. */
    class query::row {
    public:
        /// The number of columns in the row. (Same as `query::column_count`.)
        int column_count() const noexcept;

        /// Returns a lightweight object representing a column. This makes it easy to assign a
        /// column value to a variable or pass it as a function argument;
        /// for example: `int n = row[0];` or `sqrt(row[1])`.
        [[nodiscard]] inline column_value column(unsigned idx) const;
        [[nodiscard]] inline column_value operator[] (unsigned idx) const;

        /// Returns the value of the `idx`th column as C++ type `T`.
        template <class T> T get(unsigned idx) const;

        class getstream;
        /// Returns a sort of input stream from which columns can be read using `>>`.
        getstream getter(unsigned idx = 0) const noexcept;

    private:
        friend class query;
        friend class query::iterator;
        friend class column_value;

        row() = default;
        explicit row(sqlite3_stmt* stmt) noexcept       :stmt_(stmt) { }

    private:
        unsigned check_idx(unsigned idx) const;

        sqlite3_stmt* _Nullable stmt_ = nullptr;
    };


    /** Specializing the struct `sqnice::column_helper<T>` and adding a static method
        `get(row&, unsigned idx) -> T` allows a query column value to be converted to type T. */
    template <typename T> struct column_helper{ };
    template <typename T>

    /** The concept `columnable` identifies custom types column values can be converted to. */
    concept columnable = requires(column_value const& col) {
        {column_helper<T>::get(col)} -> std::same_as<T>;
    };


    /** Represents a single column of a query row; returned by `query::row[]`. */
    class column_value : sqnice::noncopyable {
    public:
        /// Gets the value as type `T`.
        template <typename T> T get() const noexcept;

        /// Implicit conversion to type `T`, for assignment or passing as a parameter.
        template <typename T> operator T() const noexcept  {return get<T>();}

        /// The data type of the column value.
        data_type type() const noexcept;
        bool not_null() const noexcept                  {return type() != data_type::null;}
        bool is_blob() const noexcept                   {return type() == data_type::blob;}

        /// The length in bytes of a text or blob value.
        size_t size_bytes() const noexcept;

        // The following are just the specializations of get<T>() ...

        template <std::signed_integral T>
        T get() const noexcept {
            if constexpr (sizeof(T) <= sizeof(int))
                return static_cast<T>(get_int());
            else
                return get_int64();
        }

        template <std::unsigned_integral T>
        T get() const noexcept {
            // pin negative values to 0 instead of returning bogus huge numbers
            if constexpr (sizeof(T) < sizeof(int))
                return static_cast<T>(std::max(0, get_int()));
            else
                return static_cast<T>(std::max(int64_t(0), get_int64()));
        }

        template<std::floating_point T>
        T get() const noexcept                          {return static_cast<T>(get_double());}

        template<> bool get() const noexcept            {return get_int() != 0;}
        template<> char const* get() const noexcept;
        template<> std::string get() const noexcept   {return std::string(get<std::string_view>());}
        template<> std::string_view get() const noexcept;
        template<> void const* get() const noexcept;
        template<> blob get() const noexcept;
        template<> null_type get() const noexcept       {return ignore;}

        template <columnable T> T get() const noexcept  {return column_helper<T>::get(*this);}

    private:
        friend class query::row;

        column_value(query::row const& r, unsigned idx) noexcept :stmt_(r.stmt_), idx_(idx) { }
        column_value(column_value&&) = delete;
        column_value& operator=(column_value&&) = delete;

        int get_int() const noexcept;
        int64_t get_int64() const noexcept;
        double get_double() const noexcept;

        sqlite3_stmt*  stmt_;
        int const      idx_;
    };


    /** A sort of input stream over a query row's columns. Each `>>` stores the next value. */
    class query::row::getstream {
    public:
        template <class T>
        getstream& operator >> (T& value) {
            value = row_->column(idx_++).get<T>();
            return *this;
        }

    private:
        friend class query::row;
        getstream(row const* rws, unsigned idx) noexcept     :row_(rws), idx_(idx) { }
        row const*  row_;
        int         idx_;
    };


    /** Typical C++ iterator over a query's result rows. */
    class query::iterator {
    public:
        const row& operator*() const noexcept           {return cur_row_;}
        const row* operator->() const noexcept          {return &cur_row_;}

        bool operator==(iterator const& other) const noexcept    {return rc_ == other.rc_;}

        iterator& operator++();

        /// True if the iterator is valid; false if it's at the end.
        explicit operator bool() const noexcept         {return rc_ != status::done;}

        /// A convenience for accessing a column of the current row.
        column_value operator[] (unsigned idx) const    {return cur_row_[idx];}

        ~iterator() noexcept;

        using iterator_category = std::input_iterator_tag;
        using value_type = row;
        using difference_type = std::ptrdiff_t;
        using pointer = row*;
        using reference = row&;

    private:
        friend class query;
        iterator() = default;
        explicit iterator(query*);

        std::shared_ptr<statement::impl> impl_;
        status              rc_     {status::done};
        row                 cur_row_;
    };



    // some template & method implementations

    template <typename T>
    std::optional<T> query::single_value() {
        std::optional<T> result;
        if (auto i = begin())
            result = i->get<T>(0);
        return result;
    }

    template <typename T>
    T query::single_value_or(T const& defaultResult) {
        if (auto i = begin()) {
            return i->get<T>(0);
        } else {
            return defaultResult;
        }
    }

    column_value query::row::column(unsigned idx) const {return column_value(*this,check_idx(idx));}
    column_value query::row::operator[] (unsigned idx) const   {return column(idx);}
    template <class T> T query::row::get(unsigned idx) const   {return column(idx).get<T>();}

    query::iterator query::begin()                      {return iterator(this);}
    query::iterator query::end() noexcept               {return iterator();}

}

ASSUME_NONNULL_END

#endif
