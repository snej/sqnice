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
    ,_flags(normalize(flags))
    {
        if (!!(_flags & open_flags::temporary))
            throw invalid_argument("pool does not support in-memory or temporary databases");
    }


    pool::~pool()  {
        close_all();
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
        while (_readonly.size() > keep)
            _readonly.pop_back();
    }


    void pool::on_open(std::function<void(database&)> init) {
        unique_lock lock(_mutex);
        _initializer = std::move(init);
    }


    unsigned pool::open_count() const {
        unique_lock lock(_mutex);
        return _ro_total + _rw_total;
    }


    unsigned pool::borrowed_count() const {
        unique_lock lock(_mutex);
        return _borrowed_count();
    }


    unsigned pool::_borrowed_count() const {
        return unsigned(_ro_total - _readonly.size()) + (_rw_total - !!_readwrite);
    }


    void pool::close_all() {
        unique_lock lock(_mutex);
        _close_unused();
        _cond.wait(lock, [&] { return _borrowed_count() == 0; });
        _close_unused();
        assert(_open_count() == 0);
    }


    void pool::close_unused() {
        unique_lock lock(_mutex);
        _close_unused();
    }


    void pool::_close_unused() {
        _ro_total -= _readonly.size();
        _readonly.clear();
        if (_readwrite) {
            _readwrite = nullptr;
            _rw_total = 0;
        }
    }


    // Allocates a new database.
    unique_ptr<database> pool::new_db(bool writeable) {
        using enum open_flags;
        auto flags = _flags;
        if (!writeable)
            flags = flags - readwrite - create;
        auto db = make_unique<database>(_dbname, flags, (_vfs.empty() ? nullptr : _vfs.c_str()));
        _flags = _flags - delete_first; // definitely don't want to do that twice!
        if (_initializer)
            _initializer(*db);
        return db;
    }


    borrowed_database pool::borrow(bool or_wait) {
        unique_lock lock(_mutex);
        while(true) {
            unique_ptr<const database> dbp;
            if (!_readonly.empty()) {
                dbp = std::move(_readonly.back());
                _readonly.pop_back();
            } else if (_ro_total < _ro_capacity) {
                dbp = new_db(false);
                ++_ro_total;
            }
            if (dbp) {
                dbp->set_borrowed(true);
                return borrowed_database(dbp.release(), *this);
            } else if (!or_wait) {
                return {nullptr, *this};
            }
            // Nothing available, so wait
            _cond.wait(lock);
        }
    }


    borrowed_writeable_database pool::borrow_writeable(bool or_wait) {
        if (!(_flags & (open_flags::readwrite | open_flags::delete_first)))
            throw logic_error("no writeable database available");
        unique_lock lock(_mutex);
        unique_ptr<database> dbp;
        if (_rw_total == 0) {
            // First-time creation of the writeable db:
            dbp = new_db(true);
            if (!dbp->is_writeable()) {
                throw database_error("database file is not writeable", status::locked);
            }
            ++_rw_total;
        } else if (_readwrite || or_wait) {
            // Get the db, waiting if necessary:
            _cond.wait(lock, [&] {return _readwrite != nullptr;});
            dbp = std::move(_readwrite);
        } else {
            // db isn't available and `or_wait` is false, so return null:
            return borrowed_writeable_database{nullptr, *this};
        }
        dbp->set_borrowed(true);
        return borrowed_writeable_database(dbp.release(), *this);
    }


    // The "deleter" function of `borrowed_ro_db`. Returns the db to the pool.
    void pool::operator()(database const* dbp) noexcept {
        if (dbp) {
            const_cast<database*>(dbp)->set_borrowed(false);
            unique_lock lock(_mutex);
            assert(!dbp->is_writeable());
            assert(_readonly.size() < _ro_total);
            if (_ro_total <= _ro_capacity) {
                _readonly.emplace_back(dbp);
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
            dbp->set_borrowed(false);
            assert(dbp->is_writeable());
            assert(dbp->transaction_depth() == 0);
            unique_lock lock(_mutex);
            assert(_rw_total == 1);
            assert(!_readwrite);
            _readwrite.reset(const_cast<database*>(dbp));
            _cond.notify_all();
        }
    }

}
