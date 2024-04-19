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
        transaction(transaction&&) noexcept;
        ~transaction() noexcept;

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
        savepoint(savepoint&&) noexcept;
        ~savepoint() noexcept;

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

}

ASSUME_NONNULL_END

#endif
