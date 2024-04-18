// sqlite3pp/blob_stream.hh
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

#pragma once
#ifndef SQLITE3PP_BLOB_STREAM_H
#define SQLITE3PP_BLOB_STREAM_H

#include "sqlite3pp/base.hh"

ASSUME_NONNULL_BEGIN

struct sqlite3_blob;

namespace sqlite3pp {
    class database;

    /** Random access to the data in a blob. */
    class blob_stream : public checking, noncopyable {
    public:
        /// Opens a handle for reading a blob.
        /// @note  If the blob doesn't exist, the behavior depends on the database's `exceptions`
        ///     status. If they're enabled, this will throw an exception. If not, it will return
        ///     normally, but `status` will return the error, and all reads will fail.
        /// @param db  The `database` handle.
        /// @param database  The "symbolic name" of the database:
        ///     - For the main database file: "main".
        ///     - For TEMP tables: "temp".
        ///     - For attached databases: the name that appears after `AS` in the ATTACH statement.
        /// @param table  The name of the table.
        /// @param column  The name of the table column.
        /// @param rowid  The row ID containing the blob.
        /// @param writeable  True if you want to write to the data.
        blob_stream(database& db,
                    const char *database,
                    const char* table,
                    const char *column,
                    int64_t rowid,
                    bool writeable);

        ~blob_stream();

        /// The status of the last operation: opening the blob handle, or the last read.
        /// @note  If exceptions are enabled,
        status status() const               {return status_;}

        /// The size in bytes of the blob.
        /// @note  This API uses `int` internally so blobs are limited to 2^31 bytes (~2GB.)
        uint64_t size() const               {return size_;}

        /// Reads from the blob.
        /// @note  It is not an error to read past the end of the blob; the read will be truncated
        ///     and the byte count returned will be less than you asked for. But it _is_ an error
        ///     for the read to start past the end of the blob.
        /// @param dst  The destination address to copy data to.
        /// @param len  The number of bytes to read.
        /// @param offset  The offset in the blob to start reading at.
        /// @returns  The number of bytes actually read, or -1 on error; check `status()`.
        /// @throws database_error  on error if exceptions are enabled.
        [[nodiscard]] int pread(void *dst, size_t len, uint64_t offset) const;

        /// Writes to the blob.
        /// @note  Unlike reads, it _is_ an error to write past the end of a blob, since this may
        ///     cause data loss. (The blob's length cannot be changed.)
        /// @param src  The address of the data to write.
        /// @param len  The number of bytes to write.
        /// @param offset  The offset in the blob to start writing at.
        /// @returns  The number of bytes actually written, or -1 on error; check `status()`.
        /// @throws database_error  on error if exceptions are enabled.
        [[nodiscard]] int pwrite(const void *src, size_t len, uint64_t offset);

    private:
        int range_check(size_t len, uint64_t offset) const;

        sqlite3_blob* _Nullable blob_ = nullptr;
        uint64_t                size_ = 0;
        mutable enum status     status_;
    };

}

ASSUME_NONNULL_END

#endif
