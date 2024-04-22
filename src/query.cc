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
#include <cassert>

#ifdef SQNICE_LOADABLE_EXTENSION
#  include <sqlite3ext.h>
SQLITE_EXTENSION_INIT1
#else
#  include <sqlite3.h>
#endif

namespace sqnice {

    static_assert(int(data_type::integer)       == SQLITE_INTEGER);
    static_assert(int(data_type::floating_point)== SQLITE_FLOAT);
    static_assert(int(data_type::text)          == SQLITE_TEXT);
    static_assert(int(data_type::blob)          == SQLITE_BLOB);
    static_assert(int(data_type::null)          == SQLITE_NULL);


    null_type ignore;

    static sqlite3_destructor_type as_dtor(copy_semantic cs) {
        return cs == copy ? SQLITE_TRANSIENT : SQLITE_STATIC;
    }


#pragma mark - STATEMENT:


    statement::statement(database& db, std::string_view stmt, persistence persistence)
    : statement(db)
    {
        exceptions_ = true;
        prepare_impl(stmt, persistence);
        exceptions_ = db.exceptions();
    }

    statement::~statement() noexcept {
        (void) finish();
    }

    status statement::prepare_impl(std::string_view sql, persistence persistence) {
        assert(!stmt_);
        shared_ = false;
        const char* tail = nullptr;
        unsigned flags = (persistence ? SQLITE_PREPARE_PERSISTENT : 0);
        auto rc = status{sqlite3_prepare_v3(db_.db_, sql.data(), int(sql.size()), flags, &stmt_, &tail)};
        if (ok(rc) && tail != nullptr && tail < sql.data() + sql.size()) {
            // Multiple statements are not supported.
            finish_impl(stmt_);
            stmt_ = nullptr;
            if (exceptions_)
                throw std::invalid_argument("multiple SQL statements are not allowed");
            rc = status::error;
        } else if (rc == status::error && exceptions_) {
            // Throw a better exception:
            std::string message(db_.error_msg());
            message += ", in SQL statement \"" + std::string(sql) + "\"";
            throw std::invalid_argument(message);
        }
        return check(rc);
    }

    status statement::prepare(std::string_view sql, persistence persistence) {
        (void)finish();
        return prepare_impl(sql, persistence);
    }

    int statement::parameter_index(const char* name) const noexcept {
        return sqlite3_bind_parameter_index(stmt_, name);
    }

    int statement::check_parameter_index(const char* name) const {
        if (int idx = sqlite3_bind_parameter_index(stmt_, name); idx >= 1)
            return idx;
        else [[unlikely]]
            throw std::invalid_argument("unknown binding name");
    }

    status statement::finish() noexcept {
        auto rc = status::ok;
        if (stmt_) {
            if (shared_)
                rc = reset();
            else
                rc = finish_impl(stmt_);
            stmt_ = nullptr;
        }
        // "If the most recent evaluation of statement S failed, then sqlite3_finalize(S) returns
        // the appropriate error code." Since this is not a new error, don't call check().
        return rc;
    }

    status statement::finish_impl(sqlite3_stmt* stmt) noexcept {
        return status{sqlite3_finalize(stmt)};
    }

    status statement::step() noexcept {
        return status{sqlite3_step(stmt_)};
    }

    bool statement::busy() const noexcept {
        return stmt_ && sqlite3_stmt_busy(stmt_);
    }

    status statement::reset() noexcept {
        if (!stmt_) [[unlikely]]
            return status::ok;
        // "If the most recent call to sqlite3_step ... indicated an error, then sqlite3_reset
        // returns an appropriate error code." 
        // Since this is not a new error, don't call check().
        return status{sqlite3_reset(stmt_)};
    }

    status statement::clear_bindings() {
        return check(sqlite3_clear_bindings(stmt_));
    }

    status statement::check_bind(int rc) {
        if (exceptions_ && rc == SQLITE_RANGE) [[unlikely]]
            throw std::invalid_argument("bind index out of range");
        else
            return check(rc);
    }

    status statement::bind_int(int idx, int value) {
        return check_bind(sqlite3_bind_int(stmt_, idx, value));
    }

    status statement::bind_double(int idx, double value) {
        return check_bind(sqlite3_bind_double(stmt_, idx, value));
    }

    status statement::bind_int64(int idx, int64_t value) {
        return check_bind(sqlite3_bind_int64(stmt_, idx, value));
    }

    status statement::bind_uint64(int idx, uint64_t value) {
        if (value > INT64_MAX) [[unlikely]]
            throw std::domain_error("uint64_t value too large for SQLite");
        return bind_int64(idx, int64_t(value));
    }

    status statement::bind(int idx, char const* value, copy_semantic fcopy) {
        return check_bind(sqlite3_bind_text(stmt_, idx, value, int(std::strlen(value)),
                                            as_dtor(fcopy)));
    }

    status statement::bind(int idx, blob value) {
        return check_bind(sqlite3_bind_blob(stmt_, idx, value.data, int(value.size),
                                            as_dtor(value.fcopy)));
    }

    status statement::bind(int idx, std::span<const std::byte> value, copy_semantic fcopy) {
        return check_bind(sqlite3_bind_blob(stmt_, idx, value.data(), int(value.size_bytes()),
                                            as_dtor(fcopy) ));
    }

    status statement::bind(int idx, std::string_view value, copy_semantic fcopy) {
        return check_bind(sqlite3_bind_text(stmt_, idx, value.data(), int(value.size()),
                                            as_dtor(fcopy) ));
    }

    status statement::bind_pointer(int idx, void* ptr, const char* type, pointer_destructor dtor) {
        return check_bind(sqlite3_bind_pointer(stmt_, idx, ptr, type, dtor));
    }

    status statement::bind(int idx, nullptr_t) {
        return check_bind(sqlite3_bind_null(stmt_, idx));
    }

    statement::bindref statement::operator[] (char const *name) {
        auto idx = sqlite3_bind_parameter_index(stmt_, name);
        if (idx < 1 && exceptions_) [[unlikely]]
            throw std::invalid_argument("unknown binding name");
        return bindref(*this, idx);
    }

    statement::bindstream statement::binder(int idx) {
        return bindstream(*this, idx);
    }


#pragma mark - COMMAND:


    status command::try_execute() noexcept {
        auto rc = step();
        return rc == status::done ? status::ok : rc;
    }

    int64_t command::last_insert_rowid() const noexcept   {return db_.last_insert_rowid();}

    int command::changes() const noexcept                 {return db_.changes();}


#pragma mark - QUERY:


    int query::column_count() const noexcept {
        return sqlite3_column_count(stmt_);
    }

    unsigned query::check_idx(unsigned idx) const {
        if (idx >= column_count()) [[unlikely]]
            throw std::invalid_argument("invalid column index");
        return idx;
    }

    char const* query::column_name(unsigned idx) const {
        return sqlite3_column_name(stmt_, check_idx(idx));
    }

    char const* query::column_decltype(unsigned idx) const {
        return sqlite3_column_decltype(stmt_, check_idx(idx));
    }


#pragma mark - QUERY ROW:


    int query::row::column_count() const noexcept {
        return sqlite3_data_count(stmt_);
    }

    unsigned query::row::check_idx(unsigned idx) const {
        if (idx >= column_count()) [[unlikely]]
            throw std::invalid_argument("invalid column index");
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

    long long int column_value::get_int64() const noexcept {
        return sqlite3_column_int64(stmt_, idx_);
    }

    double column_value::get_double() const noexcept {
        return sqlite3_column_double(stmt_, idx_);
    }

    template<> char const* column_value::get() const noexcept {
        return reinterpret_cast<char const*>(sqlite3_column_text(stmt_, idx_));
    }

    template<> std::string_view column_value::get() const noexcept {
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
        return {data, size_t(size), copy};
    }


#pragma mark - QUERY ITERATOR:


    query::iterator::iterator(query* query)
    : query_(query)
    , rc_ (query->step())
    , cur_row_(query_->stmt_)
    {
        if (rc_ != status::row && rc_ != status::done)
            query_->raise(rc_);
    }

    query::iterator::~iterator() noexcept {
        if (rc_ != status::done && query_)
            query_->reset();
    }

    query::iterator& query::iterator::operator++() {
        rc_ = query_->step();
        if (rc_ != status::row && rc_ != status::done)
            query_->raise(rc_);
        return *this;
    }

}
