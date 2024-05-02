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


#include "sqnice/transaction.hh"
#include "sqnice/database.hh"
#include "sqnice/pool.hh"
#include <exception>

#ifdef SQNICE_LOADABLE_EXTENSION
#  include <sqlite3ext.h>
SQLITE_EXTENSION_INIT1
#else
#  include <sqlite3.h>
#endif

namespace sqnice {
    using namespace std;

    transaction::transaction() = default;

    transaction::transaction(database& db, bool autocommit, bool immediate) {
        status rc = begin(db, autocommit, immediate);
        if (!ok(rc)) [[unlikely]]
            db.raise(rc);  // always throw -- constructor cannot return a status
    }

    transaction::transaction(pool& pool, bool autocommit, bool immediate) {
        status rc = begin(pool, autocommit, immediate);
        if (!ok(rc)) [[unlikely]]
            checking::raise(rc, "can't begin transaction");  // always throw -- constructor cannot return a status
    }

    transaction::transaction(transaction &&t) noexcept
    :db_(t.db_)
    ,from_pool_(t.from_pool_)
    ,autocommit_(t.autocommit_)
    {
        t.db_ = nullptr;
        t.from_pool_ = nullptr;
    }

    status transaction::begin(database& db, bool autocommit, bool immediate) {
        if (db_) [[unlikely]]
            throw std::logic_error("transaction is already active");
        status rc = db.check( db.begin_transaction(immediate) );
        if (ok(rc)) [[likely]] {
            db_ = &db;
            autocommit_ = autocommit;
        }
        return rc;
    }

    status transaction::begin(pool& pool, bool autocommit, bool immediate) {
        if (db_) [[unlikely]]
            throw std::logic_error("transaction is already active");
        auto db = pool.borrow_writeable();
        status rc = begin(*db, autocommit, immediate);
        if (ok(rc)) {
            from_pool_ = &db.get_deleter();
            db_ = db.release();
        }
        return rc;
    }

    transaction::~transaction() {
        if (db_) {
            optional<borrowed_writeable_database> bdb;
            if (from_pool_)
                bdb.emplace(db_, *from_pool_); // will be returned to pool on exit

            if (autocommit_ && !current_exception()) {
                // It is legal (though discouraged) to throw an exception from a destructor,
                // as long as the destructor was not called as part of unwinding the stack
                // while an exception is thrown.
                (void)db_->end_transaction(true);
            } else {
                // abort the transaction, being careful not to throw:
                auto x = db_->exceptions();
                db_->exceptions(false);
                (void)db_->end_transaction(false);
                db_->exceptions(x);
            }
        }
    }

    database& transaction::active_database() const {
        if (db_) [[likely]]
            return *db_;
        else
            throw std::logic_error("transaction is not active");
    }

    status transaction::end(bool commit) {
        if (auto db = db_) [[likely]] {
            optional<borrowed_writeable_database> bdb; // will be returned to pool on exit
            if (from_pool_) {
                bdb.emplace(db_, *from_pool_);
                from_pool_ = nullptr;
            }
            db_ = nullptr;
            return db->end_transaction(commit);
        } else if (commit) {
            throw std::logic_error("transaction is not active");
        } else {
            return status::ok;
        }
    }

}
