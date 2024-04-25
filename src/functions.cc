// sqniceext.cc
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


#include "sqnice/functions.hh"
#include <sqlite3.h>
#include <cstring>

namespace sqnice {
    using namespace std;

    static inline sqlite3_destructor_type as_dtor(copy_semantic cs) {
        return cs == copy ? SQLITE_TRANSIENT : SQLITE_STATIC;
    }

    context::context(sqlite3_context* ctx, int nargs, argv_t values)
    : argc(nargs)
    , argv(nargs, values)
    , result(ctx)
    , ctx_(ctx)
    { }


#pragma mark - CONTEXT RESULT:


    void function_result::set_int(int value) {
        sqlite3_result_int(ctx_, value);
    }

    void function_result::set_int64(int64_t value) {
        sqlite3_result_int64(ctx_, value);
    }

    void function_result::set_double(double value) {
        sqlite3_result_double(ctx_, value);
    }

    void function_result::set(std::string_view value, copy_semantic fcopy) {
        sqlite3_result_text(ctx_, value.data(), int(value.size()), as_dtor(fcopy));
    }

    void function_result::set(char const* value, copy_semantic fcopy) {
        sqlite3_result_text(ctx_, value, int(std::strlen(value)), as_dtor(fcopy));
    }

    void function_result::operator= (blob const& value) {
        sqlite3_result_blob(ctx_, value.data, int(value.size), as_dtor(value.fcopy));
    }

    void function_result::operator= (nullptr_t) {
        sqlite3_result_null(ctx_);
    }

    void function_result::operator= (arg_value const& arg) {
        sqlite3_result_value(ctx_, arg.value());
    }

    void function_result::set_error(string_view msg, status s) {
        sqlite3_result_error(ctx_, msg.data(), int(msg.size()));
        sqlite3_result_error_code(ctx_, int(s));
    }

    void* context::aggregate_data(int size) {
        return sqlite3_aggregate_context(ctx_, size);
    }

    void* context::user_data() {
        return sqlite3_user_data(ctx_);
    }

    
#pragma mark - FUNCTION ARGUMENTS:

    
    arg_value function_args::operator[] (size_t arg) const {
        if (arg >= argc_)
            throw std::invalid_argument("context arg index out of range");
        return arg_value(argv_[arg]);
    }


    data_type arg_value::type() const noexcept {
        return data_type{sqlite3_value_type(value_)};
    }

    size_t arg_value::size_bytes() const noexcept {
        return sqlite3_value_bytes(value_);
    }

    int arg_value::get_int() const noexcept {
        return sqlite3_value_int(value_);
    }

    long long int arg_value::get_int64() const noexcept {
        return sqlite3_value_int64(value_);
    }

    double arg_value::get_double() const noexcept {
        return sqlite3_value_double(value_);
    }

    template<> char const* arg_value::get() const noexcept {
        return reinterpret_cast<char const*>(sqlite3_value_text(value_));
    }

    template<> std::string_view arg_value::get() const noexcept {
        char const* cstr = get<const char*>();
        if (!cstr)
            return {};
        return {cstr, size_bytes()};
    }

    template<> void const* arg_value::get() const noexcept {
        return sqlite3_value_blob(value_);
    }

    template<> blob arg_value::get() const noexcept {
        // It's important to make the calls in this order,
        // so we get the size of the blob value, not the string value.
        auto data = sqlite3_value_blob(value_);
        auto size = sqlite3_value_bytes(value_);
        return {data, size_t(size), copy};
    }


#pragma mark - DATABASE METHOD IMPLEMENTATIONS:


    status database::create_function(string_view name,
                                     function_handler h,
                                     int nargs,
                                     function_flags flags)
    {
        auto fh = new function_handler(std::move(h));

        auto function_impl = [](sqlite3_context* ctx, int nargs, sqlite3_value** values) noexcept {
            auto f = static_cast<function_handler*>(sqlite3_user_data(ctx));
            context c(ctx, nargs, values);
            (*f)(std::move(c.argv), std::move(c.result));
        };

        auto destroy_impl = [](void* pApp) noexcept {
            delete static_cast<function_handler*>(pApp);
        };

        return register_function(name, nargs, flags, (void*)fh,
                                 function_impl, nullptr, nullptr, destroy_impl);
    }


    status database::create_aggregate(string_view name,
                                      step_handler step,
                                      finish_handler finish,
                                      int nargs,
                                      function_flags flags)
    {
        struct state {step_handler step; finish_handler finish;};
        auto ah = new state{std::move(step), std::move(finish)};

        auto step_impl = [](sqlite3_context* ctx, int nargs, sqlite3_value** values) noexcept {
            context c(ctx, nargs, values);
            auto s = static_cast<state*>(c.user_data());
            s->step(std::move(c.argv));
        };

        auto finalize_impl = [](sqlite3_context* ctx) noexcept {
            context c(ctx);
            auto s = static_cast<state*>(c.user_data());
            s->finish(std::move(c.result));
        };

        auto destroy_impl = [](void* pApp) noexcept {
            auto s = static_cast<state*>(pApp);
            delete s;
        };
        
        return register_function(name, nargs, flags, (void*)ah,
                                 nullptr, step_impl, finalize_impl, destroy_impl);
    }

} // namespace sqnice
