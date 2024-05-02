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
#include <condition_variable>
#include <functional>
#include <mutex>
#include <stack>

ASSUME_NONNULL_BEGIN

namespace sqnice {
    class pool;

    /** A unique pointer to a read-only database borrowed from a `pool`. */
    using borrowed_database = std::unique_ptr<const database, pool&>;

    /** A unique pointer to a writeable database borrowed from a `pool`. */
    using borrowed_writeable_database = std::unique_ptr<database, pool&>;


    /** A thread-safe pool of databases, for multi-threaded use. */
    class pool : noncopyable {
    public:
        /// Constructs a pool that will manage databases on the given file.
        /// No databases are opened until one of the borrow methods is called,
        /// so any errors, like file-not-found, won't occur until then.
        ///
        /// - If you don't include the flag `readwrite`, you won't be allowed to borrow a
        ///   writeable database.
        /// - The flags `temporary` and `memory` are not allowed, since SQLite doesn't support
        ///   multiple connections to temporary databases.
        /// - The flag `delete_first` is honored when the first database is opened, ignored
        ///   after that (so you don't delete your own database!)
        explicit pool(std::string_view filename,
                      open_flags flags           = open_flags::readwrite | open_flags::create,
                      const char* _Nullable vfs  = nullptr);

        /// `pool`'s destructor waits until all borrowed databases have been returned.
        ~pool();

        /// The maximum number of databases the pool will create, including one writeable one.
        /// Defaults to 5. Minimum value is 2 (otherwise why are you using a pool at all?)
        unsigned capacity();

        /// Sets the maximum number of databases the pool will create, including one writeable one.
        /// The default is 5. Minimum value is 2 (otherwise why are you using a pool at all?)
        void set_capacity(unsigned capacity);

        /// Registers a function that will be called just after a `database` is opened,
        /// and can make connection-level changes; for example, calling `setup_connection`
        /// or registering functions.
        ///
        /// This callback should not be used for file-level initialization, like creating tables,
        /// since it will be called multiple times.
        void on_open(std::function<void(database&)>);

        /// The number of databases open, both borrowed and available.
        unsigned open_count() const;

        /// The number of databases currently borrowed. Ranges from 0 up to `capacity`.
        unsigned borrowed_count() const;

        /// Returns a `unique_ptr` to a **read-only** database a client can use.
        /// When the `borrowed_database` goes out of scope, the database is returned to the pool.
        /// @note  If all read-only databases are checked out, waits until one is returned.
        /// @throws `database_error` if opening a new database connection fails.
        borrowed_database borrow()                          {return borrow(true);}

        /// Same as `borrow`, except returns `nullptr` instead of waiting.
        /// @throws `database_error` if opening a new database connection fails.
        borrowed_database try_borrow()                      {return borrow(false);}

        /// Returns a `unique_ptr` to a **writeable** database a client can use.
        /// There is only one of these per pool, since SQLite only supports one writer at a time.
        /// When the `borrowed_writeable_database` goes out of scope, the database is returned to
        /// the pool.
        /// @note  If the writeable database is checcked out, waits until it's returned.
        /// @throws `database_error` if opening a new database connection fails.
        borrowed_writeable_database borrow_writeable()      {return borrow_writeable(true);}

        /// Same as `borrow_writeable`, except returns `nullptr` instead of waiting.
        /// @throws `database_error` if opening a new database connection fails.
        borrowed_writeable_database try_borrow_writeable()  {return borrow_writeable(false);}

        /// Blocks until all borrowed databases have been returned, then closes them.
        /// (The destructor also does this.)
        void close_all();

        /// Closes all databases the pool has opened that aren't currently in use.
        /// (The pool can still re-open more databases on demand, up to its capacity.)
        void close_unused();

#ifndef __GNUC__
    private:
        friend borrowed_database;
        friend borrowed_writeable_database;
#endif
        void operator() (database* _Nullable) noexcept;       // deleter
        void operator() (database const* _Nullable) noexcept; // deleter

    private:
        pool(pool&&) = delete;
        pool& operator=(pool&&) = delete;
        unsigned _borrowed_count() const;
        unsigned _open_count() const                    {return _ro_total + _rw_total;}
        borrowed_database borrow(bool);
        borrowed_writeable_database borrow_writeable(bool);
        std::unique_ptr<database> new_db(bool writeable);
        void _close_unused();

        using db_ptr = std::unique_ptr<const database>;

        std::string const               _dbname, _vfs;  // Path & vfs to open
        open_flags                      _flags;         // Flags to open with
        std::mutex mutable              _mutex;         // Magic thread-safety voodoo
        std::condition_variable mutable _cond;          // Magic thread-safety voodoo
        std::function<void(database&)>  _initializer;   // Init fn called on each new `database`
        unsigned                        _ro_capacity =4;// Current capacity (of read-only dbs)
        unsigned                        _ro_total = 0;  // Number of read-only DBs I created
        unsigned                        _rw_total = 0;  // Number of read-write DBs I created (0, 1)
        std::vector<db_ptr>             _readonly;      // Stack of available RO DBs
        std::unique_ptr<database>       _readwrite;     // The available RW DB
    };

}

ASSUME_NONNULL_END

#endif
