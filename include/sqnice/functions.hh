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
//#include <sqlite3.h>    //TODO: Remove this dependency
#include <cstddef>
#include <map>
#include <memory>
#include <tuple>
#include <type_traits>
#include <utility>

ASSUME_NONNULL_BEGIN

struct sqlite3_context;
struct sqlite3_value;

namespace sqnice {

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


    /** The context of a SQLite function call. Holds the arguments and result. */
    class context : noncopyable {
    public:
        using argv_t = sqlite3_value* _Nonnull * _Nullable;

        explicit context(sqlite3_context* ctx, int nargs = 0, argv_t values = nullptr);

        int args_count() const;
        int args_bytes(int idx) const;
        int args_type(int idx) const;

        template <class T> T get(int idx) const {
            return get(idx, T());
        }

        //TODO: Make this work the same way as statement::bind
        void result(int value);
        void result(double value);
        void result(long long int value);
        void result(std::string const& value);
        void result(char const* _Nullable value, copy_semantic = copy);
        void result(void const* value, int n, copy_semantic = copy);
        void result();
        void result(null_type);
        void result_copy(int idx);
        void result_error(char const* msg);

        void* aggregate_data(int size);

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

        void* user_data();

    private:
        //TODO: Make this work the same way as query::row::get()
        int get(int idx, int) const;
        double get(int idx, double) const;
        long long int get(int idx, long long int) const;
        char const* get(int idx, char const* _Nullable) const;
        std::string get(int idx, std::string) const;
        void const* get(int idx, void const* _Nullable) const;

        template<class H, class... Ts>
        static inline std::tuple<H, Ts...> to_tuple_impl(int index, const context& c, std::tuple<H, Ts...>&&) {
            auto h = std::make_tuple(c.context::get<H>(index));
            return std::tuple_cat(h, to_tuple_impl(++index, c, std::tuple<Ts...>()));
        }
        static inline std::tuple<> to_tuple_impl(int /*index*/, const context& /*c*/, std::tuple<>&&) {
            return std::tuple<>();
        }

    private:
        sqlite3_context* ctx_;
        int              nargs_;
        argv_t           values_;
    };

    namespace {
        template <class R, class... Ps>
        void functionx_impl(sqlite3_context* ctx, int nargs, context::argv_t values) {
            context c(ctx, nargs, values);
            auto f = static_cast<std::function<R (Ps...)>*>(c.user_data());
            c.result(apply_f(*f, c.to_tuple<Ps...>()));
        }
    }


    /** Manages user-defined functions for a database. */
    class functions : public checking, noncopyable {
    public:
        using function_handler = std::function<void (context&)>;
        using pfunction_base = std::shared_ptr<void>;

        explicit functions(database& db) : checking(db) { }

        status create(char const* name, function_handler h, int nargs);

        template <class F>
        status create(char const* name, std::function<F> h) {
            auto db = check_get_db();
            fh_[name] = std::shared_ptr<void>(new std::function<F>(h));
            return create_function_impl<F>()(this, fh_[name].get(), name);
        }

        status create_aggregate(char const* name, function_handler s, function_handler f, int nargs);

        template <class T, class... Ps>
        status create_aggregate(char const* name) {
            auto db = check_get_db();
            return register_function(db, name, sizeof...(Ps), 0, nullptr,
                                     stepx_impl<T, Ps...>, finishN_impl<T>);
        }

    private:

        template<class R, class... Ps>
        struct create_function_impl;

        template<class R, class... Ps>
        struct create_function_impl<R (Ps...)> {
            status operator()(functions* fns, void* fh, char const* name) {
                return fns->register_function(fns->check_get_db(), name, sizeof...(Ps), fh, functionx_impl<R, Ps...>);
            }
        };
        template <class T, class... Ps>
        static void stepx_impl(sqlite3_context* ctx, int nargs, context::argv_t values) {
            context c(ctx, nargs, values);
            T* t = c.aggregate_state<T>();
            apply_f([](T* tt, Ps... ps){tt->step(ps...);},
                    std::tuple_cat(std::make_tuple(t), c.to_tuple<Ps...>()));
        }

        template <class T>
        static void finishN_impl(sqlite3_context* ctx) {
            context c(ctx);
            T* t = c.aggregate_state<T>();
            c.result(t->finish());
            t->~T();
        }

        using callFn = void (*)(sqlite3_context*, int, sqlite3_value *_Nonnull*_Nonnull);
        using finishFn = void (*)(sqlite3_context*);
        using destroyFn = void (*)(void*);
        status register_function(std::shared_ptr<sqlite3> const&,
                                 const char *name, int nArg, void* _Nullable pApp,
                                 callFn _Nullable call,
                                 callFn _Nullable step = nullptr,
                                 finishFn _Nullable finish = nullptr,
                                 destroyFn _Nullable destroy = nullptr);
    private:
        std::map<std::string, std::pair<pfunction_base, pfunction_base> > ah_;
        std::map<std::string, pfunction_base> fh_;
    };

} // namespace sqnice

ASSUME_NONNULL_END

#endif
