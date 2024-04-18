// sqlite3pp/database.cc
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


#include "sqlite3pp/transaction.hh"
#include "sqlite3pp/database.hh"

#ifdef SQLITE3PP_LOADABLE_EXTENSION
#  include <sqlite3ext.h>
SQLITE_EXTENSION_INIT1
#else
#  include <sqlite3.h>
#endif

namespace sqlite3pp {

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

}
