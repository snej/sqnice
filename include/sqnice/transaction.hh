// sqnice/transaction.hh
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
#ifndef SQNICE_TRANSACTION_H
#define SQNICE_TRANSACTION_H

#include "sqnice/base.hh"

ASSUME_NONNULL_BEGIN

namespace sqnice {
    class database;

    /** An RAII wrapper around SQLite's transactions and savepoints. */
    class transaction : noncopyable {
    public:
        /// Begins a transaction, which lasts until this object exits scope / is destructed.
        /// Transactions may be nested. Aborting a nested transaction only rolls back the changes
        /// made since that nested transaction began.
        ///
        /// Normally, you should call `commit` to commit the transaction, otherwise it will
        /// abort/rollback when the object exits scope. However, you can set the `autocommit`
        /// argument in the constructor, which will cause the destructor to commit the transaction.
        /// (If the destructor is called because an exception is thrown, the transaction aborts
        /// instead.)
        ///
        /// @param db  The database.
        /// @param immediate  If true, uses `BEGIN IMMEDIATE`, which immediately grabs
        ///     the database lock. If false, the first write you make in the transaction
        ///     will try to grab the lock but will fail if another transaction is active.
        /// @param autocommit  If true, the transaction will automatically commit when
        ///     the object exits scope (unless it's because an exception is being thrown.)
        /// @throws database_error on any error (whether or not database.exceptions() is true)
        explicit transaction(database& db,
                             bool immediate = true,
                             bool autocommit = false);
        transaction(transaction&&) noexcept;
        ~transaction() noexcept;

        /// Commits the transaction.
        status commit()     {return end(true);}

        /// Rolls back (aborts) the transaction.
        /// This also happens when the transaction object exits scope without being committed first
        /// (unless you specified `autocommit` in the constructor.)
        status rollback()   {return end(false);}

    private:
        status end(bool commit);

        database& db_;
        bool active_;
        bool autocommit_;
    };

}

ASSUME_NONNULL_END

#endif
