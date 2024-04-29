// sqnice/pool.cc
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


#include "sqnice/pool.hh"
#include <mutex>
#include <cassert>

#ifdef SQNICE_LOADABLE_EXTENSION
#  include <sqlite3ext.h>
SQLITE_EXTENSION_INIT1
#else
#  include <sqlite3.h>
#endif

namespace sqnice {
    using namespace std;


    pool::pool(std::string_view dbname, open_flags flags, const char* vfs)
    :_dbname(std::move(dbname))
    ,_vfs(vfs ? vfs : "")
    ,_flags(flags)
    {
        if (_dbname.empty() || !!(_flags & open_flags::memory))
            throw invalid_argument("pool does not support in-memory or temporary databases");
    }


    unsigned pool::capacity() {
        unique_lock lock(_mutex);
        return _ro_capacity + 1;    // public API includes writeable db in capacity
    }

    void pool::set_capacity(unsigned newCapacity) {
        if (newCapacity < 2)
            throw invalid_argument("capacity must be at least 2");
        unique_lock lock(_mutex);
        // internally I don't include writeable db in capacity:
        _ro_capacity = newCapacity - 1;
        // Toss out any excess RO databases:
        int keep = std::max(0, int(_ro_capacity) - int(_ro_total - _readonly.size()));
        if (keep < _readonly.size())
            _readonly.resize(keep);
    }


    void pool::close_all() {
        unique_lock lock(_mutex);
        _cond.wait(lock, [&] { return borrowed_count() == 0; });
    }


    void pool::close_unused() {
        unique_lock lock(_mutex);
        _ro_total -= _readonly.size();
        _readonly.clear();
        if (_readwrite) {
            _readwrite = nullptr;
            _rw_total = 0;
        }
    }


    unsigned pool::borrowed_count() const {
        return unsigned(_ro_total - _readonly.size()) + (_rw_total - !!_readwrite);
    }


    borrowed_database pool::borrow(bool or_wait) {
        unique_lock lock(_mutex);
        while(true) {
            database const* db = nullptr;
            if (!_readonly.empty()) {
                db = std::move(_readonly.back()).release();
                _readonly.pop_back();
            } else if (_ro_total < _ro_capacity) {
                db = new_db(false);
                ++_ro_total;
            }
            if (db)
                return borrowed_database(db, *this);
            else if (!or_wait)
                return {nullptr, *this};

            _cond.wait(lock);
        }
    }


    borrowed_writeable_database pool::borrow_writeable(bool or_wait) {
        if (!(_flags & open_flags::readwrite))
            throw logic_error("no writeable database available");
        unique_lock lock(_mutex);
        database* dbp = nullptr;
        if (_rw_total == 0) {
            dbp = new_db(true);
            if (!dbp->is_writeable()) {
                delete dbp;
                throw database_error("database file is not writeable", status::locked);
            }
            ++_rw_total;
        } else if (or_wait) {
            _cond.wait(lock, [&] {return _readwrite != nullptr;});
            dbp = _readwrite.release();
        }
        return borrowed_writeable_database(dbp, *this);
    }


    // The "deleter" function of `borrowed_ro_db`. Returns the db to the pool.
    void pool::operator()(database const* dbp) noexcept {
        if (dbp) {
            unique_lock lock(_mutex);
            assert(!dbp->is_writeable());
            assert(_readonly.size() < _ro_total);
            if (_ro_total < _ro_capacity) {
                _readonly.emplace_front(dbp);
                _cond.notify_all();
            } else {
                // Toss out a DB if capacity was lowered after it was checked out:
                delete dbp;
                --_ro_total;
            }
        }
    }


    // The "deleter" function of `borrowed_writeable_database`. Returns the db to the pool.
    void pool::operator()(database* dbp) noexcept {
        if (dbp) {
            assert(dbp->is_writeable());
            assert(dbp->transaction_depth() == 0);
            unique_lock lock(_mutex);
            assert(_rw_total == 1);
            assert(!_readwrite);
            _readwrite.reset(const_cast<database*>(dbp));
            _cond.notify_all();
        }
    }


    // Allocates a new database.
    database* pool::new_db(bool writeable) {
        auto flags = _flags;
        if (!writeable)
            flags = (flags - open_flags::readwrite) | open_flags::readonly;
        return new database(_dbname, flags, (_vfs.empty() ? nullptr : _vfs.c_str()));
    }

}
