// sqnice/functions.hh
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
#ifndef SQNICEEXT_H
#define SQNICEEXT_H

#include "sqnice/database.hh"
#include "sqnice/query.hh"
#include <algorithm>
#include <cstddef>
#include <functional>
#include <memory>
#include <tuple>
#include <type_traits>
#include <utility>

ASSUME_NONNULL_BEGIN

struct sqlite3_context;
struct sqlite3_value;

namespace sqnice {
    class arg_value;
    class context;
    class function_result;


    /** The concept `resultable` identifies custom types that can be assigned as a function result.
         Declare a function `sqlite3cpp::result_helper(context&, T)`.
         This function should call context::result(U) for some built-in type U. */
    template <typename T>
    concept resultable = requires(context& ctx, T value) {
        {result_helper(ctx, value)} -> std::same_as<status>;
    };


    /** The array of arguments passed to a function call. */
    class function_args : noncopyable {
    public:
        function_args(int argc, database::argv_t argv)  :argc_(argc), argv_(argv) { }

        arg_value operator[] (size_t i) const;
        size_t size() const                             {return argc_;}

    private:
        int const               argc_;
        database::argv_t const  argv_;
    };


    /** Represents the result of a function; its only purpose is to be assigned to.
        The type of `context::result`. */
    class function_result : noncopyable {
    public:
        void operator= (std::signed_integral auto v) {
            static_assert(sizeof(v) <= 8);
            if constexpr (sizeof(v) <= 4)
                set_int(v);
            else
                set_int64(v);
        }

        void operator= (std::unsigned_integral auto v) {
            static_assert(sizeof(v) <= 8);
            if constexpr (sizeof(v) < 4)
                set_int(int(v));
            else if constexpr (sizeof(v) < 8)
                set_int64(int64_t(v));
            else
                set_uint64(v);
        }

        void operator= (std::floating_point auto v) noexcept     {set_double(v);}
        void operator= (nullptr_t) noexcept;
        void operator= (null_type) noexcept                      {*this = nullptr;}
        void operator= (arg_value const&) noexcept;

        void operator= (std::string_view) noexcept;
        void operator= (uncopied_string) noexcept;
        void operator= (blob v) noexcept                         {set_blob(v, true);}
        void operator= (uncopied_blob v) noexcept                {set_blob(v, false);}

        template <resultable T>
        void operator= (T const& v) noexcept                    {set_helper(*this, v);}

        using pointer_destructor = void(*)(void*);

        /// Sets the result to an opaque pointer value.
        /// @note see <https://sqlite.org/bindptr.html>
        void set_pointer(void* pointer, const char* type, pointer_destructor) noexcept;

        void set_subtype(unsigned) noexcept;

        /// Sets the result to an error.
        void operator= (database_error const& x) noexcept   {set_error(x.what(), x.error_code);}
        void set_error(std::string_view msg, status = status::error) noexcept;

    private:
        friend class context;
        explicit function_result(sqlite3_context* ctx) noexcept   :ctx_(ctx) { }
        void set_int(int) noexcept;
        void set_int64(int64_t) noexcept;
        void set_uint64(uint64_t) noexcept;
        void set_double(double) noexcept;
        void set_blob(blob value, bool copy) noexcept;

        sqlite3_context* ctx_;
    };


    /** The context of a SQLite function call. Holds the arguments and result. */
    class context : noncopyable {
    public:
        explicit context(sqlite3_context*, int nargs = 0, database::argv_t = nullptr) noexcept;

        size_t const    argc;       ///< The number of arguments
        function_args   argv;       ///< The "array" of arguments
        function_result result;     ///< Assign the result to this

        /// Gets the `idx`th arg as type `T`. Equivalent to `T t = argv[idx];`
        template <class T> T get(int idx) const;

    private:
        friend class database;

        void* _Nullable user_data() noexcept;
        void* _Nullable aggregate_data(int size) noexcept;

        template <class T>
        T* _Nonnull aggregate_state() {
            auto data = static_cast<uint8_t*>(aggregate_data(sizeof(T) + 1));
            if (!data[sizeof(T)]) { // last byte tracks whether T has been constructed
                new (data) T;
                data[sizeof(T)] = true;
            }
            return reinterpret_cast<T*>(data);
        }

        template <class... Ts>
        std::tuple<Ts...> to_tuple() {
            return to_tuple_impl(0, *this, std::tuple<Ts...>());
        }

        template<class H, class... Ts>
        static inline std::tuple<H, Ts...> to_tuple_impl(int index, const context& c, std::tuple<H, Ts...>&&) {
            auto h = std::make_tuple(c.context::get<H>(index));
            return std::tuple_cat(h, to_tuple_impl(++index, c, std::tuple<Ts...>()));
        }

        static inline std::tuple<> to_tuple_impl(int /*index*/, const context& /*c*/, std::tuple<>&&) {
            return std::tuple<>();
        }
    };


    /** Represents a single function argument; the type of `context.argv[i]`. */
    class arg_value : sqnice::noncopyable {
    public:
        explicit arg_value(sqlite3_value* v) noexcept   :value_(v) { }
        sqlite3_value* value() const noexcept           {return value_;}

        /// The data type of the value.
        data_type type() const noexcept;
        unsigned subtype() const noexcept;
        bool not_null() const noexcept                  {return type() != data_type::null;}
        bool is_blob() const noexcept                   {return type() == data_type::blob;}

        /// The length in bytes of a text or blob value.
        size_t size_bytes() const noexcept;

        /// Gets the value as type `T`.
        template <typename T> T get() const noexcept;

        /// Implicit conversion to type `T`, for assignment or passing as a parameter.
        template <typename T> operator T() const noexcept  {return get<T>();}

        // The following are just the specializations of get<T>() ...

        template <std::signed_integral T>
        T get() const noexcept {
            if constexpr (sizeof(T) <= sizeof(int))
                return static_cast<T>(get_int());
            else
                return get_int64();
        }

        template <std::unsigned_integral T> requires (!std::same_as<bool,T>)
        T get() const noexcept {
            // pin negative values to 0 instead of returning bogus huge numbers
            if constexpr (sizeof(T) < sizeof(int))
                return static_cast<T>(std::max(0, get_int()));
            else
                return static_cast<T>(std::max(int64_t(0), get_int64()));
        }

        template<std::floating_point T>
        T get() const noexcept                          {return static_cast<T>(get_double());}

        template<std::same_as<bool> T> T get() const noexcept           {return get_int() != 0;}
        template<std::same_as<const char*> T> T get() const noexcept;
        template<std::same_as<std::string_view> T> T get() const noexcept;
        template<std::same_as<std::string> T> T get() const noexcept {
            return std::string(get<std::string_view>());
        }
        template<std::same_as<const void*> T> T get() const noexcept;
        template<std::same_as<blob> T> T get() const noexcept;
        template<std::same_as<null_type> T> T get() const noexcept      {return ignore;}

        template <columnable T> T get() const noexcept  {return column_helper<T>::get(*this);}

    private:
        arg_value(arg_value&&) = delete;
        arg_value& operator=(arg_value&&) = delete;

        int get_int() const noexcept;
        int64_t get_int64() const noexcept;
        double get_double() const noexcept;

        sqlite3_value* value_;
    };


    template <class T> T context::get(int idx) const    {return argv[idx];}


    // implementations of `database` function-related template methods.

    namespace {
        template<size_t N>
        struct Apply {
            template<typename F, typename T, typename... A>
            static inline auto apply(F&& f, T&& t, A&&... a)
            -> decltype(Apply<N-1>::apply(std::forward<F>(f),
                                          std::forward<T>(t),
                                          std::get<N-1>(std::forward<T>(t)),
                                          std::forward<A>(a)...)) {
                return Apply<N-1>::apply(std::forward<F>(f),
                                         std::forward<T>(t),
                                         std::get<N-1>(std::forward<T>(t)),
                                         std::forward<A>(a)...);
            }
        };

        template<>
        struct Apply<0> {
            template<typename F, typename T, typename... A>
            static inline auto apply(F&& f, T&&, A&&... a)
            -> decltype(std::forward<F>(f)(std::forward<A>(a)...)) {
                return std::forward<F>(f)(std::forward<A>(a)...);
            }
        };

        template<typename F, typename T>
        inline auto apply_f(F&& f, T&& t)
        -> decltype(Apply<std::tuple_size<typename std::decay<T>::type>::value>::apply(std::forward<F>(f), std::forward<T>(t))) {
            return Apply<std::tuple_size<typename std::decay<T>::type>::value>::apply(
                                                                                      std::forward<F>(f), std::forward<T>(t));
        }
    }


    template <class R, class... Ps>
    void database::functionx_impl(sqlite3_context* ctx, int nargs, argv_t values) {
        context c(ctx, nargs, values);
        auto f = static_cast<std::function<R (Ps...)>*>(c.user_data());
        c.result = apply_f(*f, c.to_tuple<Ps...>());
    }

    template <class T, class... Ps>
    void database::stepx_impl(sqlite3_context* ctx, int nargs, argv_t values) {
        context c(ctx, nargs, values);
        T* t = c.aggregate_state<T>();
        apply_f([](T* tt, Ps... ps){tt->step(ps...);},
                std::tuple_cat(std::make_tuple(t), c.to_tuple<Ps...>()));
    }

    template <class T>
    void database::finishN_impl(sqlite3_context* ctx) {
        context c(ctx);
        T* t = c.aggregate_state<T>();
        c.result = t->finish();
        t->~T();
    }

} // namespace sqnice

ASSUME_NONNULL_END

#endif
