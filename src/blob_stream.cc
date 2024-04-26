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


#include "sqnice/blob_stream.hh"
#include "sqnice/database.hh"

#ifdef SQNICE_LOADABLE_EXTENSION
#  include <sqlite3ext.h>
SQLITE_EXTENSION_INIT1
#else
#  include <sqlite3.h>
#endif

namespace sqnice {
    using namespace std;

    blob_stream::blob_stream(database& db,
                             const char* table, const char *column, int64_t rowid,
                             bool writeable)
    :blob_stream(db, "main", table, column, rowid, writeable)
    { }


    blob_stream::blob_stream(database& db,
                             const char *database_name,
                             const char* table, const char *column, int64_t rowid,
                             bool writeable)
    : checking(db)
    {
        status_ = status{sqlite3_blob_open(db.check_handle(), database_name, table, column, 
                                           rowid, writeable, &blob_)};
        check(status_);
        if (status_ == status::ok)
            size_ = sqlite3_blob_bytes(blob_);
    }


    blob_stream::~blob_stream() noexcept {
        if (blob_)
            sqlite3_blob_close(blob_);
    }


    int blob_stream::range_check(size_t len, uint64_t offset) const {
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


    int blob_stream::pread(void *dst, size_t len, uint64_t offset) const {
        int checked_len = range_check(len, offset);
        if (checked_len < 0)
            return -1;
        status_ = status{sqlite3_blob_read(blob_, dst, checked_len, int(offset))};
        check(status_);
        return ok(status_) ? checked_len : -1;
    }


    int blob_stream::pwrite(const void *src, size_t len, uint64_t offset) {
        int checked_len = range_check(len, offset);
        if (checked_len < 0 || size_t(checked_len) < len)
            return -1;
        status_ = status{sqlite3_blob_write(blob_, src, checked_len, int(offset))};
        check(status_);
        return ok(status_) ? checked_len : -1;
    }

}
