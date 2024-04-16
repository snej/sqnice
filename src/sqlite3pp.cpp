// sqlite3pp.cpp
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

#include "sqlite3pp.h"
#include <cstring>
#include <memory>
#include <assert.h>

namespace sqlite3pp {

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


    database::database(char const* dbname, int flags, char const* vfs)
    : checking(*this, false)
    , db_(nullptr)
    , borrowing_(false)
    {
        if (dbname) {
            auto rc = connect(dbname, flags, vfs);
            if (rc != status::ok) {
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

    status database::connect(char const* dbname, int flags, char const* vfs) {
        if (!borrowing_) {
            disconnect();
        }

        return check(sqlite3_open_v2(dbname, &db_, flags, vfs));
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

    int64_t database::last_insert_rowid() const {
        return sqlite3_last_insert_rowid(db_);
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

    int database::changes() const {
        return sqlite3_changes(db_);
    }

    int64_t database::total_changes() const {
        return sqlite3_total_changes(db_);
    }

    status database::error_code() const {
        return status{sqlite3_errcode(db_)};
    }

    status database::extended_error_code() const {
        return status{sqlite3_extended_errcode(db_)};
    }

    char const* database::error_msg() const {
        return sqlite3_errmsg(db_);
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

    status database::set_busy_timeout(int ms) {
        return check(sqlite3_busy_timeout(db_, ms));
    }


#pragma mark - STATEMENT:


    statement::statement(database& db, char const* stmt) 
    : checking(db)
    , stmt_(0)
    , tail_(0)
    {
        if (stmt) {
            auto rc = prepare(stmt);
            if (rc != status::ok)
                throw_(rc);
        }
    }

    statement::~statement() {
        // finish() can return error. If you want to check the error, call
        // finish() explicitly before this object is destructed.
        finish();
    }

    status statement::prepare(char const* stmt) {
        auto rc = finish();
        if (rc != status::ok)
            return check(rc);

        return check(prepare_impl(stmt));
    }

    status statement::prepare_impl(char const* stmt) {
        shared_ = false;
        return status{sqlite3_prepare_v2(db_.db_, stmt, int(std::strlen(stmt)), &stmt_, &tail_)};
    }

    void statement::share(const statement& other) {
        finish();
        stmt_ = other.stmt_;
        shared_ = true;
        clear_bindings();
    }

    status statement::finish() {
        auto rc = status::ok;
        if (stmt_) {
            if (shared_) {
                reset();
            } else {
                rc = finish_impl(stmt_);
            }
            stmt_ = nullptr;
        }
        tail_ = nullptr;

        return check(rc);
    }

    status statement::finish_impl(sqlite3_stmt* stmt) {
        return status{sqlite3_finalize(stmt)};
    }


    status statement::step() {
        auto rc = status{sqlite3_step(stmt_)};
        if (rc != status::done && rc != status::row)
            check(rc);
        return rc;
    }

    status statement::reset() {
        // "If the most recent call to sqlite3_step ... indicated an error, then sqlite3_reset
        // returns an appropriate error code." Since this is not a new error, don't call check().
        return status{sqlite3_reset(stmt_)};
    }

    status statement::clear_bindings() {
        return check(sqlite3_clear_bindings(stmt_));
    }

    status statement::bind(int idx, int value) {
        return check(sqlite3_bind_int(stmt_, idx, value));
    }

    status statement::bind(int idx, double value) {
        return check(sqlite3_bind_double(stmt_, idx, value));
    }

    status statement::bind(int idx, long int value) {
        return check(sqlite3_bind_int64(stmt_, idx, value));
    }

    status statement::bind(int idx, long long int value) {
        return check(sqlite3_bind_int64(stmt_, idx, value));
    }

    status statement::bind(int idx, char const* value, copy_semantic fcopy) {
        return check(sqlite3_bind_text(stmt_, idx, value, int(std::strlen(value)), fcopy == copy ? SQLITE_TRANSIENT : SQLITE_STATIC ));
    }

    status statement::bind(int idx, blob value) {
        return check(sqlite3_bind_blob(stmt_, idx, value.data, int(value.size), 
                                       value.fcopy == copy ? SQLITE_TRANSIENT : SQLITE_STATIC ));
    }

    status statement::bind(int idx, std::span<const std::byte> value, copy_semantic fcopy) {
        return check(sqlite3_bind_blob(stmt_, idx, value.data(), int(value.size_bytes()),
                                       fcopy == copy ? SQLITE_TRANSIENT : SQLITE_STATIC ));
    }

    status statement::bind(int idx, std::string_view value, copy_semantic fcopy) {
        return check(sqlite3_bind_text(stmt_, idx, value.data(), int(value.size()),
                                       fcopy == copy ? SQLITE_TRANSIENT : SQLITE_STATIC ));
    }

    status statement::bind(int idx) {
        return check(sqlite3_bind_null(stmt_, idx));
    }

    status statement::bind(char const* name, int value) {
        auto idx = sqlite3_bind_parameter_index(stmt_, name);
        return bind(idx, value);
    }

    status statement::bind(char const* name, double value) {
        auto idx = sqlite3_bind_parameter_index(stmt_, name);
        return bind(idx, value);
    }

    status statement::bind(char const* name, long int value) {
        auto idx = sqlite3_bind_parameter_index(stmt_, name);
        return bind(idx, value);
    }

    status statement::bind(char const* name, long long int value) {
        auto idx = sqlite3_bind_parameter_index(stmt_, name);
        return bind(idx, value);
    }

    status statement::bind(char const* name, char const* value, copy_semantic fcopy) {
        auto idx = sqlite3_bind_parameter_index(stmt_, name);
        return bind(idx, value, fcopy);
    }

    status statement::bind(char const* name, std::span<const std::byte> value, copy_semantic fcopy) {
        auto idx = sqlite3_bind_parameter_index(stmt_, name);
        return bind(idx, value, fcopy);
    }

    status statement::bind(char const* name, std::string_view value, copy_semantic fcopy) {
        auto idx = sqlite3_bind_parameter_index(stmt_, name);
        return bind(idx, value, fcopy);
    }

    status statement::bind(char const* name) {
        auto idx = sqlite3_bind_parameter_index(stmt_, name);
        return bind(idx);
    }

    statement::bindref statement::operator[] (char const *name) {
        auto idx = sqlite3_bind_parameter_index(stmt_, name);
        return bindref(*this, idx);
    }


#pragma mark - COMMAND:


    command::command(database& db, char const* stmt) 
    : statement(db, stmt)
    {
        exceptions(db.exceptions());
    }

    statement::bindstream statement::binder(int idx) {
        return bindstream(*this, idx);
    }

    command command::shared_copy() const {
        command cmd(db_);
        cmd.share(*this);
        return cmd;
    }

    status command::execute() {
        auto rc = step();
        if (rc == status::done) rc = status::ok;

        return check(rc);
    }

    status command::execute_all() {
        auto rc = execute();
        if (rc != status::ok) return rc;

        char const* sql = tail_;

        while (std::strlen(sql) > 0) { // sqlite3_complete() is broken.
            sqlite3_stmt* old_stmt = stmt_;

            if ((rc = prepare_impl(sql)) != status::ok) break;

            //FIXME: sqlite3_transfer_bindings is deprecated; replace with something else
            if ((rc = status{sqlite3_transfer_bindings(old_stmt, stmt_)}) != status::ok) break;

            if ((rc = finish_impl(old_stmt)) != status::ok) break;

            if ((rc = execute()) != status::ok) break;

            sql = tail_;
        }

        return check(rc);
    }


#pragma mark - QUERY:


    query::query(database& db, char const* stmt) 
    : statement(db, stmt)
    { }

    query query::shared_copy() const {
        query q(db_);
        q.share(*this);
        return q;
    }

    int query::column_count() const {
        return sqlite3_column_count(stmt_);
    }

    char const* query::column_name(int idx) const {
        return sqlite3_column_name(stmt_, idx);
    }

    char const* query::column_decltype(int idx) const {
        return sqlite3_column_decltype(stmt_, idx);
    }


    query::iterator query::begin() {
        return iterator(this);
    }

    query::iterator query::end() {
        return iterator();
    }

#pragma mark - QUERY ROW:


    query::row::getstream::getstream(row const* rws, int idx)
    : rws_(rws)
    , idx_(idx)
    { }

    query::row::row(sqlite3_stmt* stmt) : stmt_(stmt) {
    }

    int query::row::column_count() const {
        return sqlite3_data_count(stmt_);
    }

    int query::row::column_type(int idx) const {
        return sqlite3_column_type(stmt_, idx);
    }

    int query::row::column_bytes(int idx) const {
        return sqlite3_column_bytes(stmt_, idx);
    }

    int query::row::get(int idx, int) const {
        return sqlite3_column_int(stmt_, idx);
    }

    double query::row::get(int idx, double) const {
        return sqlite3_column_double(stmt_, idx);
    }

    long long int query::row::get(int idx, long int) const {
        return sqlite3_column_int64(stmt_, idx);
    }

    long long int query::row::get(int idx, long long int) const {
        return sqlite3_column_int64(stmt_, idx);
    }

    char const* query::row::get(int idx, char const*) const {
        return reinterpret_cast<char const*>(sqlite3_column_text(stmt_, idx));
    }

    std::string query::row::get(int idx, std::string) const {
        char const* cstr = get(idx, (char const*)0);
        if (!cstr)
            return {};
        return {cstr, size_t(column_bytes(idx))};
    }

    std::string_view query::row::get(int idx, std::string_view) const {
        char const* cstr = get(idx, (char const*)0);
        if (!cstr)
            return {};
        return {cstr, size_t(column_bytes(idx))};
    }

    void const* query::row::get(int idx, void const*) const {
        return sqlite3_column_blob(stmt_, idx);
    }

    blob query::row::get(int idx, blob) const {
        // It's important to make the calls in this order, so we get the size of the blob value, not the string value.
        auto data = sqlite3_column_blob(stmt_, idx);
        auto size = sqlite3_column_bytes(stmt_, idx);
        return {data, size_t(size), copy};
    }

    null_type query::row::get(int /*idx*/, null_type) const {
        return ignore;
    }

    query::row::getstream query::row::getter(int idx) const {
        return getstream(this, idx);
    }

#pragma mark - QUERY ITERATOR:


    query::iterator::iterator() : cmd_(0), rows_(nullptr) {
        rc_ = status::done;
    }

    query::iterator::iterator(query* cmd) : cmd_(cmd), rows_(cmd_->stmt_) {
        rc_ = cmd_->step();
        if (rc_ != status::row && rc_ != status::done)
            cmd_->throw_(rc_);
    }

    bool query::iterator::operator==(query::iterator const& other) const {
        return rc_ == other.rc_;
    }

    bool query::iterator::operator!=(query::iterator const& other) const {
        return rc_ != other.rc_;
    }

    query::iterator& query::iterator::operator++() {
        rc_ = cmd_->step();
        if (rc_ != status::row && rc_ != status::done)
            cmd_->throw_(rc_);
        return *this;
    }

    const query::iterator::value_type& query::iterator::operator*() const {
        return rows_;
    }

    const query::iterator::value_type* query::iterator::operator->() const {
        return &rows_;
    }


    transaction::transaction(database& db, bool auto_commit, bool immediate)
    : checking(db)
    , active_(true)
    , autocommit_(auto_commit)
    {
        status rc = db_.execute(immediate ? "BEGIN IMMEDIATE" : "BEGIN");
        if (rc != status::ok)
            throw_(rc);
    }

    transaction::transaction(transaction &&t)
    : checking(std::move(t))
    , active_(t.active_)
    , autocommit_(t.autocommit_)
    {
        t.active_ = false;
    }


    transaction::~transaction() {
        if (active_) {
            // execute() can return error. If you want to check the error,
            // call commit() or rollback() explicitly before this object is
            // destructed.
            exceptions(false);
            (void)db_.execute(autocommit_ ? "COMMIT" : "ROLLBACK");
        }
    }

    status transaction::commit() {
        active_ = false;
        return check(db_.execute("COMMIT"));
    }

    status transaction::rollback() {
        active_ = false;
        return check(db_.execute("ROLLBACK"));
    }


    savepoint::savepoint(database& db, bool autocommit)
    : checking(db)
    , active_(true)
    , autocommit_(autocommit)
    {
        status rc = execute("SAVEPOINT");
        if (rc != status::ok)
            throw_(rc);
    }

    savepoint::savepoint(savepoint &&s)
    : checking(std::move(s))
    , active_(s.active_)
    , autocommit_(s.autocommit_)
    {
        s.active_ = false;
    }

    savepoint::~savepoint() {
        if (active_) {
            // execute() can return error. If you want to check the error,
            // call commit() or rollback() explicitly before this object is
            // destructed.
            exceptions(false);
            execute(autocommit_ ? "RELEASE" : "ROLLBACK TO");
        }
    }

    status savepoint::commit() {
        active_ = false;
        return execute("RELEASE");
    }

    status savepoint::rollback() {
        active_ = false;
        return execute("ROLLBACK TO");
    }

    status savepoint::execute(char const *cmd) {
        // Each nested savepoint should have a distinct identifier.
        char buf[32];
        snprintf(buf, sizeof(buf), "%s sp_%p", cmd, (void*)this);
        return check(db_.execute(buf));
    }


#pragma mark - BLOB HANDLE:


    blob_handle::blob_handle(database& db,
                             const char *database,
                             const char* table, const char *column, int64_t rowid,
                             bool writeable)
    : checking(db)
    {
        status_ = check(sqlite3_blob_open(db.db_, database, table, column, rowid, writeable, &blob_));
        if (status_ == status::ok)
            size_ = sqlite3_blob_bytes(blob_);
    }


    int blob_handle::range_check(size_t len, uint64_t offset) const {
        if (!blob_) {
            return -1;
        } else if (offset + len <= size_ && offset < INT_MAX) {
            return int(len);
        } else if (offset <= size_) {
            return int(size_ - offset);
        } else {
            status_ = status::misuse;
            check(status_);
            return -1;
        }
    }


    ssize_t blob_handle::pread(void *dst, size_t len, uint64_t offset) const {
        int checked_len = range_check(len, offset);
        if (checked_len < 0)
            return -1;
        status_ = check(sqlite3_blob_read(blob_, dst, checked_len, int(offset)));
        return ok(status_) ? checked_len : -1;
    }


    ssize_t blob_handle::pwrite(const void *src, size_t len, uint64_t offset) {
        int checked_len = range_check(len, offset);
        if (checked_len < 0 || size_t(checked_len) < len)
            return -1;
        status_ = check(sqlite3_blob_write(blob_, src, checked_len, int(offset)));
        return ok(status_) ? checked_len : -1;
    }

} // namespace sqlite3pp
