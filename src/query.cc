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


#include "sqnice/query.hh"
#include "sqnice/database.hh"
#include "sqnice/functions.hh"
#include "statement_cache.hh"
#include <cassert>

#ifdef SQNICE_LOADABLE_EXTENSION
#  include <sqlite3ext.h>
SQLITE_EXTENSION_INIT1
#else
#  include <sqlite3.h>
#endif

namespace sqnice {
    using namespace std;

    static_assert(int(data_type::integer)       == SQLITE_INTEGER);
    static_assert(int(data_type::floating_point)== SQLITE_FLOAT);
    static_assert(int(data_type::text)          == SQLITE_TEXT);
    static_assert(int(data_type::blob)          == SQLITE_BLOB);
    static_assert(int(data_type::null)          == SQLITE_NULL);


    null_type ignore;


#pragma mark - STATEMENT:


    statement::impl::~impl()    {
        sqlite3_finalize(stmt);
    }


    statement::statement(checking const& ck, shared_ptr<impl> impl) noexcept
    :checking(ck),
    impl_(std::move(impl))
    { }


    statement::statement(checking const& ck, string_view stmt, persistence persistence)
    :statement(ck)
    {
        exceptions_ = true;
        prepare(stmt, persistence);
        exceptions_ = ck.exceptions();
    }

    statement::statement(statement const& other) noexcept
    :checking(other)
    ,impl_(other.impl_)
    { 
        clear_bindings();
    }

    statement& statement::operator=(statement const& other) noexcept {
        if (this != &other) {
            finish();
            this->checking::operator=(other);
            impl_ = other.impl_;
            clear_bindings();
        }
        return *this;
    }


    statement::statement(statement&& other) noexcept
    :checking(std::move(other))
    ,impl_(std::move(other.impl_))
    {
        impl_->transfer_owner(&other, this);
    }

    statement& statement::operator=(statement&& other) noexcept {
        if (this != &other) {
            finish();
            this->checking::operator=(std::move(other));
            impl_ = std::move(other.impl_);
            impl_->transfer_owner(&other, this);
        }
        return *this;
    }


    statement::~statement() noexcept {
        (void) finish();
    }

    status statement::prepare(string_view sql, persistence persistence) {
        sqlite3_stmt* stmt = nullptr;
        const char* tail = nullptr;
        unsigned flags = (persistence ? SQLITE_PREPARE_PERSISTENT : 0);
        auto db = check_get_db();
        auto rc = status{sqlite3_prepare_v3(db.get(), sql.data(), int(sql.size()), flags,
                                            &stmt, &tail)};
        if (ok(rc) && tail != nullptr && tail < sql.data() + sql.size()) {
            // Multiple statements are not supported.
            sqlite3_finalize(stmt);
            if (exceptions_)
                throw invalid_argument("multiple SQL statements are not allowed");
            rc = status::error;
        } else if (rc == status::error && exceptions_) {
            // Throw a better exception:
            throw invalid_argument(format("%s, in SQL statement \"%s\"",
                                          sqlite3_errmsg(db.get()), string(sql).c_str()));
        } else if (ok(rc)) {
            finish();
            impl_ = shared_ptr<impl>(new impl(stmt));
        }
        return check(rc);
    }

    int statement::parameter_count() const noexcept {
        return sqlite3_bind_parameter_count(any_stmt());
    }

    int statement::parameter_index(const char* name) const noexcept {
        return sqlite3_bind_parameter_index(any_stmt(), name);
    }

    int statement::check_parameter_index(const char* name) const {
        if (int idx = sqlite3_bind_parameter_index(any_stmt(), name); idx >= 1)
            return idx;
        else [[unlikely]]
            throw invalid_argument(format("unknown binding name \"%s\" for: %s",
                                          name, sqlite3_sql(any_stmt())));
    }

    void statement::finish() noexcept {
        if (impl_) {
            if (impl_->transfer_owner(this, nullptr))
                sqlite3_reset(impl_->stmt);
            impl_ = nullptr;
        }
    }


    // Lets another object use the impl.
    // Throws if I have none, or someone else is already using it.
    shared_ptr<statement::impl> statement::give_impl(const void* newOwner) {
        if (!impl_) [[unlikely]]
            throw logic_error("command or query is not prepared");
        else if (impl_->transfer_owner(nullptr, newOwner) || impl_->transfer_owner(this, newOwner))
            return impl_;
        else
            throw logic_error("command or query is in use by another iterator");
    }


    sqlite3_stmt* statement::any_stmt() const {
        if (!impl_) [[unlikely]]
            throw logic_error("command or query is not prepared");
        else
            return impl_->stmt;
    }

    sqlite3_stmt* statement::stmt() const {
        if (!impl_) [[unlikely]]
            throw logic_error("command or query is not prepared");
        else if (impl_->set_owner(this)) [[likely]]
            return impl_->stmt;
        else
            throw logic_error("command or query is in use by an iterator");
    }

    string_view statement::sql() const {
        string_view result;
        if (const char* sql = sqlite3_sql(any_stmt()))
            result = sql;
        return result;
    }

    string statement::expanded_sql() const {
        string result;
        if (char* sql = sqlite3_expanded_sql(any_stmt())) {
            result = sql;
            sqlite3_free(sql);
        }
        return result;
    }

    bool statement::busy() const noexcept {
        return impl_ && sqlite3_stmt_busy(impl_->stmt);
    }

    void statement::reset() noexcept {
        if (impl_) [[likely]] {
            sqlite3_reset(stmt());
            impl_->transfer_owner(this, nullptr);
        }
    }

    void statement::clear_bindings() {
        if (impl_) [[likely]]
            sqlite3_clear_bindings(stmt());
    }

    status statement::check_bind(int rc, int idx) {
        if (exceptions_ && rc == SQLITE_RANGE) [[unlikely]]
            throw invalid_argument(format("parameter index %d out of range (max %d) for: %s",
                                          idx, parameter_count(), sqlite3_sql(any_stmt())));
        else
            return check(rc);
    }

    status statement::bind_int(int idx, int value) {
        return check_bind(sqlite3_bind_int(stmt(), idx, value), idx);
    }

    status statement::bind_double(int idx, double value) {
        return check_bind(sqlite3_bind_double(stmt(), idx, value), idx);
    }

    status statement::bind_int64(int idx, int64_t value) {
        return check_bind(sqlite3_bind_int64(stmt(), idx, value), idx);
    }

    status statement::bind_uint64(int idx, uint64_t value) {
        if (value > INT64_MAX) [[unlikely]]
            throw domain_error(format("uint64_t value 0x%llux is too large for SQLite",
                                      (unsigned long long)value));
        return bind_int64(idx, int64_t(value));
    }

    status statement::bind(int idx, string_view value) {
        return check_bind(sqlite3_bind_text64(stmt(), idx, value.data(), value.size(),
                                              SQLITE_TRANSIENT, SQLITE_UTF8), idx);
    }

    status statement::bind(int idx, uncopied_string value) {
        return check_bind(sqlite3_bind_text64(stmt(), idx, value.data(), value.size(),
                                              SQLITE_STATIC, SQLITE_UTF8), idx);
    }

    status statement::bind_blob(int idx, blob value, bool copy) {
        int rc;
        if (value.data)
            rc = sqlite3_bind_blob64(stmt(), idx, value.data, value.size,
                                                  copy ? SQLITE_TRANSIENT : SQLITE_STATIC);
        else
            rc = sqlite3_bind_zeroblob64(stmt(), idx, value.size);
        return check_bind(rc, idx);
    }

    status statement::bind_pointer(int idx, void* ptr, const char* type, pointer_destructor dtor) {
        return check_bind(sqlite3_bind_pointer(stmt(), idx, ptr, type, dtor), idx);
    }

    status statement::bind(int idx, nullptr_t) {
        return check_bind(sqlite3_bind_null(stmt(), idx), idx);
    }

    status statement::bind(int idx, arg_value v) {
        return check_bind(sqlite3_bind_value(stmt(), idx, v.value()), idx);
    }

    statement::bindref statement::operator[] (char const *name) {
        auto idx = sqlite3_bind_parameter_index(stmt(), name);
        if (idx < 1 && exceptions_) [[unlikely]]
            throw invalid_argument(format("unknown binding name \"%s\"", name));
        return bindref(*this, idx);
    }

    statement::bindstream statement::binder(int idx) {
        return bindstream(*this, idx);
    }


#pragma mark - COMMAND:


    command::command(database& db, std::string_view sql, persistence p)
    :statement(db, sql, p) { }

    command::command(statement_cache<command>& sc, std::string_view sql, persistence p)
    :statement(sc, sql, p) { }

    status command::try_execute() noexcept {
        auto db = check_get_db();
        sqlite3_stmt* stmtPointer = stmt();
        auto rc = status{sqlite3_step(stmtPointer)};
        if (rc == status::done) {
            last_rowid_ = sqlite3_last_insert_rowid(db.get());
            changes_ = sqlite3_changes(db.get());
            rc = status::ok;
        } else {
            last_rowid_ = -1;
            changes_ = 0;
        }
        reset();
        return rc;
    }

    
#pragma mark - QUERY:


    unsigned query::column_count() const noexcept {
        return sqlite3_column_count(any_stmt());
    }

    unsigned query::check_idx(unsigned idx) const {
        if (idx >= column_count()) [[unlikely]]
            throw invalid_argument(format("invalid column index %u (max %u)",
                                          idx, column_count()));
        return idx;
    }

    char const* query::column_name(unsigned idx) const {
        return sqlite3_column_name(any_stmt(), check_idx(idx));
    }

    char const* query::column_decltype(unsigned idx) const {
        return sqlite3_column_decltype(any_stmt(), check_idx(idx));
    }


#pragma mark - QUERY ROW:


    int query::row::column_count() const noexcept {
        return sqlite3_data_count(stmt_);
    }

    unsigned query::row::check_idx(unsigned idx) const {
        if (idx >= column_count()) [[unlikely]]
            throw invalid_argument("invalid column index");
        return idx;
    }

    query::row::getstream query::row::getter(unsigned idx) const noexcept {
        return getstream(this, check_idx(idx));
    }


#pragma mark - QUERY COLUMN VALUE:


    data_type column_value::type() const noexcept {
        return data_type{sqlite3_column_type(stmt_, idx_)};
    }

    size_t column_value::size_bytes() const noexcept {
        return sqlite3_column_bytes(stmt_, idx_);
    }

    int column_value::get_int() const noexcept {
        return sqlite3_column_int(stmt_, idx_);
    }

    int64_t column_value::get_int64() const noexcept {
        return sqlite3_column_int64(stmt_, idx_);
    }

    double column_value::get_double() const noexcept {
        return sqlite3_column_double(stmt_, idx_);
    }

    template<> char const* column_value::get() const noexcept {
        return reinterpret_cast<char const*>(sqlite3_column_text(stmt_, idx_));
    }

    template<> string_view column_value::get() const noexcept {
        char const* cstr = get<const char*>();
        if (!cstr)
            return {};
        return {cstr, size_bytes()};
    }

    template<> void const* column_value::get() const noexcept {
        return sqlite3_column_blob(stmt_, idx_);
    }

    template<> blob column_value::get() const noexcept {
        // It's important to make the calls in this order,
        // so we get the size of the blob value, not the string value.
        auto data = sqlite3_column_blob(stmt_, idx_);
        auto size = sqlite3_column_bytes(stmt_, idx_);
        return blob{data, size_t(size)};
    }


#pragma mark - QUERY ITERATOR:


    query::iterator::iterator(query* query)
    : impl_(query->give_impl(this))
    , cur_row_(impl_->stmt)
    , exceptions_(query->exceptions())
    {
        ++(*this); // go to 1st row
    }

    query::iterator::~iterator() noexcept {
        if (impl_ && impl_->transfer_owner(this, nullptr))
            sqlite3_reset(impl_->stmt);
    }

    query::iterator& query::iterator::operator++() {
        assert(impl_->owned_by(this));
        switch (rc_ = status{sqlite3_step(impl_->stmt)}) {
            case status::row:
            case status::ok:
                break;
            case status::done:
                cur_row_.clear();
                sqlite3_reset(impl_->stmt);
                impl_->transfer_owner(this, nullptr); // return sqlite3_stmt to query
                break;
            default:
                cur_row_.clear();
                if (exceptions_) {
                    const char* msg = sqlite3_errmsg(sqlite3_db_handle(impl_->stmt));
                    checking::raise(rc_, msg);
                }
        }
        return *this;
    }

}
