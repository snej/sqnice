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
#ifndef SQNICE_POOL_H
#define SQNICE_POOL_H

#include "sqnice/database.hh"
#include <deque>
#include <mutex>

ASSUME_NONNULL_BEGIN

namespace sqnice {
    class pool;

    /** A unique pointer to a read-only database borrowed from a `pool`. */
    using borrowed_database = std::unique_ptr<const database, pool&>;

    /** A unique pointer to a writeable database borrowed from a `pool`. */
    using borrowed_writeable_database = std::unique_ptr<database, pool&>;


    /** A thread-safe pool of databases, for multi-threaded use. */
    class pool {
    public:
        /// Constructs a pool that will manage databases on the given file.
        /// No databases are opened until one of the borrow methods is called,
        /// so any errors like file-not-found won't occur until then.
        ///
        /// @note `open_flags::memory` is not allowed, nor is an empty filename,
        ///       since SQLite doesn't allow multiple connections to temporary databases.
        explicit pool(std::string_view filename,
                      open_flags flags           = open_flags::readwrite | open_flags::create,
                      const char* _Nullable vfs  = nullptr);

        ~pool()                                             {close_all();}

        /// The maximum number of databases the pool will create, including one writeable one.
        /// Defaults to 4. Minimum value is 2 (otherwise why are you using a pool at all?)
        unsigned capacity();

        /// Sets the maximum number of databases the pool will create, including one writeable one.
        /// The default is 4. Minimum value is 2 (otherwise why are you using a pool at all?)
        /// @note  If you lower the capacity to a value less than the current `borrowed_count`,
        ///        this method will block until the excess databases are returned.
        void set_capacity(unsigned capacity);

        /// The number of databases currently borrowed. Ranges from 0 up to `capacity`.
        unsigned borrowed_count() const;

        /// Returns a read-only database a client can use.
        /// When the `borrowed_db` goes out of scope, the database is returned to the pool.
        /// @note  If all RO databases are checked out, blocks until one is returned.
        /// @throws `database_error` if a database can't be opened.
        borrowed_database borrow()                          {return borrow(true);}

        /// Same as `borrow` but returns `nullptr` instead of blocking.
        /// @throws `database_error` if a database can't be opened.
        borrowed_database try_borrow()                      {return borrow(false);}

        /// Returns a writeable database a client can use.
        /// There is one of these per pool, since SQLite only supports one writer at a time.
        /// When the `borrowed_db` goes out of scope, the database is returned to the pool.
        /// @note  If the writeable database is checcked out, blocks until it's returned.
        /// @throws `database_error` if a database can't be opened.
        borrowed_writeable_database borrow_writeable()      {return borrow_writeable(false);}

        /// Same as `borrow_writeable` but returns `nullptr` instead of blocking.
        /// @throws `database_error` if a database can't be opened.
        borrowed_writeable_database try_borrow_writeable()  {return borrow_writeable(true);}

        /// Blocks until all borrowed databases have been returned, then closes them.
        /// (The destructor also does this.)
        void close_all();

        /// Closes all databases the pool has opened that aren't currently borrowed.
        void close_unused();

    private:
        friend borrowed_database;
        friend borrowed_writeable_database;

        borrowed_database borrow(bool);
        borrowed_writeable_database borrow_writeable(bool);
        database* new_db(bool writeable);
        void operator() (database* _Nullable) noexcept;       // deleter
        void operator() (database const* _Nullable) noexcept; // deleter

        using db_queue = std::deque<std::unique_ptr<const database>>;

        std::string const           _dbname, _vfs;
        open_flags const            _flags;
        std::mutex                  _mutex;
        std::condition_variable     _cond;
        unsigned                    _ro_capacity = 4;// Current capacity (of read-only)
        unsigned                    _ro_total = 0;  // Number of read-only DBs I created
        unsigned                    _rw_total = 0;  // Number of read-write DBs I created (0 or 1)
        db_queue                    _readonly;      // FIFO queue of available RO DBs
        std::unique_ptr<database>   _readwrite;     // The available RW DB
    };

}

ASSUME_NONNULL_END

#endif
