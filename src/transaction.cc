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
#include <exception>

#ifdef SQNICE_LOADABLE_EXTENSION
#  include <sqlite3ext.h>
SQLITE_EXTENSION_INIT1
#else
#  include <sqlite3.h>
#endif

namespace sqnice {
    using namespace std;

    transaction::transaction(database& db, bool auto_commit, bool immediate)
    : db_(db)
    , active_(true)
    , autocommit_(auto_commit)
    {
        status rc = db_.beginTransaction(immediate);
        if (!ok(rc))
            db_.raise(rc);  // always throw -- constructor cannot return a status
    }

    transaction::transaction(transaction &&t) noexcept
    : db_(t.db_)
    , active_(t.active_)
    , autocommit_(t.autocommit_)
    {
        t.active_ = false;
    }


    transaction::~transaction() noexcept {
        if (active_) {
            if (autocommit_ && !current_exception()) {
                // It is legal (though discouraged) to throw an exception from a destructor,
                // as long as the destructor was not called as part of unwinding the stack
                // while an exception is thrown.
                (void)db_.endTransaction(true);
            } else {
                auto x = db_.exceptions();
                db_.exceptions(false);
                (void)db_.endTransaction(false);
                db_.exceptions(x);
            }
        }
    }

    status transaction::end(bool commit) {
        active_ = false;
        return db_.endTransaction(commit);
    }

}
