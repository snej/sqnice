// sqlite3pp/base.hh
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
#ifndef SQLITE3PP_DBASE_H
#define SQLITE3PP_BASE_H

#define SQLITE3PP_VERSION "2.0.0"
#define SQLITE3PP_VERSION_MAJOR 2
#define SQLITE3PP_VERSION_MINOR 0
#define SQLITE3PP_VERSION_PATCH 0

#include <stdexcept>

#if __has_feature(nullability)
#  define ASSUME_NONNULL_BEGIN  _Pragma("clang assume_nonnull begin")
#  define ASSUME_NONNULL_END    _Pragma("clang assume_nonnull end")
#else
#  define ASSUME_NONNULL_BEGIN
#  define ASSUME_NONNULL_END
#  define _Nullable
#  ifndef _Nonnull
#    define _Nonnull
#  endif
#endif

ASSUME_NONNULL_BEGIN

namespace sqlite3pp {
    class database;

    /** A SQLite error code. Values are the same as `SQLITE_OK`, `SQLITE_ERROR`, ... */
    enum class status : int {
        ok          = 0,      corrupt     = 11,
        error       = 1,      cantopen    = 14,
        perm        = 3,      constraint  = 19,
        abort       = 4,      mismatch    = 20,
        busy        = 5,      misuse      = 21,
        locked      = 6,      auth        = 23,
        readonly    = 8,      range       = 25,
        interrupt   = 9,      row         = 100,
        ioerr       = 10,     done        = 101,
    };

    /// Masks out other bits set in extended status codes
    inline status basic_status(status s)                    {return status{int(s) & 0xff};}
    /// True if a `status` is successful (not an error.)
    inline bool ok(status s)                                {return s == status::ok;}


    /** Exception class thrown by this API. */
    class database_error : public std::runtime_error {
    public:
        explicit database_error(char const* msg, status rc);
        explicit database_error(database& db, status rc);
        explicit database_error(char const* msg, int rc)    :database_error(msg,status{rc}) { }
        explicit database_error(database& db, int rc)       :database_error(db,status{rc}) { }

        status const error_code;
    };


    /** Classes inheriting from this cannot be copied, but can be moved. */
    class noncopyable {
    protected:
        noncopyable() = default;
        ~noncopyable() = default;

        noncopyable(noncopyable&&) = default;
        noncopyable& operator=(noncopyable&&) = default;

        noncopyable(noncopyable const&) = delete;
        noncopyable& operator=(noncopyable const&) = delete;
    };


    /** A base class that handles exceptions by throwing or returning an error code.
     Most of the other classes derive from this. */
    class checking {
    public:
        /// Enables or disables exceptions.
        /// For a `database`, this defaults to false.
        /// For other objects, it defaults to the value in the `database` they are created from.
        void exceptions(bool x)     {exceptions_ = x;}
        /// True if exceptions are enabled.
        bool exceptions()           {return exceptions_;}
    protected:
        checking(database &db, bool x)              :db_(db), exceptions_(x) { }
        explicit checking(database &db);
        status check(status rc) const;
        status check(int rc) const                  {return check(status{rc});}
        [[noreturn]] void throw_(status rc) const;
        [[noreturn]] void throw_(int rc) const      {throw_(status{rc});}

        database&   db_;
        bool        exceptions_ = false;
    };

}

ASSUME_NONNULL_END

#endif
