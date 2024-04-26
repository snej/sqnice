// sqnice/base.hh
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
#ifndef SQNICE_BASE_H
#define SQNICE_BASE_H

#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>

#define SQNICE_VERSION "2.0.0"
#define SQNICE_VERSION_MAJOR 2
#define SQNICE_VERSION_MINOR 0
#define SQNICE_VERSION_PATCH 0

#ifndef __has_feature
#  define __has_feature(F) 0
#endif

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

#ifdef SQNICE_TYPECHECK_FORMAT
#  define sqnice_printflike(A, B) __attribute__((__format__ (__printf__, A, B)))
#else
#  define sqnice_printflike(A, B)
#endif

ASSUME_NONNULL_BEGIN

struct sqlite3;

namespace sqnice {
    class checking;
    class database;

    /** A SQLite error code. Values are the same as `SQLITE_OK`, `SQLITE_ERROR`, ... */
    enum class status : int {
        ok          =  0,   cantopen    =  14,
        error       =  1,   constraint  =  19,
        perm        =  3,   mismatch    =  20,
        abort       =  4,   misuse      =  21,
        busy        =  5,   auth        =  23,
        locked      =  6,   range       =  25,
        readonly    =  8,   notice      =  27,
        interrupt   =  9,   warning     =  28,
        ioerr       = 10,   row         = 100,
        corrupt     = 11,   done        = 101,
    };

    /// Masks out other bits set in extended status codes
    inline status basic_status(status s)                    {return status{int(s) & 0xff};}

    /// True if a `status` is equal to `status::ok`.
    inline bool ok(status s)                                {return s == status::ok;}


    /** Exception class thrown by this API. */
    class database_error : public std::runtime_error {
    public:
        explicit database_error(char const* msg, status rc = status::error);
        explicit database_error(char const* msg, int rc)    :database_error(msg,status{rc}) { }

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
        /// For a `database`, this defaults to true.
        /// For other objects, it defaults to the value in the `database` they are created from.
        void exceptions(bool x) noexcept                {exceptions_ = x;}
        /// True if exceptions are enabled.
        bool exceptions() const noexcept                {return exceptions_;}

        /// If `rc` is not OK, and exceptions are enabled, calls `raise(rc)`. Else returns it.
        status check(status rc) const;

        /// Throws an exception, depending on the value of `rc`. Usually `database_error`,
        /// but could be `std::invalid_argument`, `std::bad_alloc` or `std::logic_error`.
        /// @warning Do not pass `status::ok`! If this might not be an error, call `check`.
        [[noreturn]] void raise(status rc) const;
        [[noreturn]] static void raise(status rc, const char* msg);

        static constexpr bool kExceptionsByDefault = true;

        static void log_warning(const char* format, ...) noexcept  sqnice_printflike(1, 2);

    protected:
        checking(std::weak_ptr<sqlite3> db, bool x)     :weak_db_(std::move(db)), exceptions_(x) { }
        explicit checking(database &db);
        explicit checking(bool x)                       :exceptions_(x) { }
        status check(int rc) const                      {return check(status{rc});}

        std::shared_ptr<sqlite3> get_db() const noexcept {return weak_db_.lock();}
        std::shared_ptr<sqlite3> check_get_db() const;

        std::weak_ptr<sqlite3> weak_db_;
        bool                   exceptions_;
    };

}

ASSUME_NONNULL_END

#endif
