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


    void context_result::set_int(int value) {
        sqlite3_result_int(ctx_, value);
    }

    void context_result::set_int64(int64_t value) {
        sqlite3_result_int64(ctx_, value);
    }

    void context_result::set_double(double value) {
        sqlite3_result_double(ctx_, value);
    }

    void context_result::set(std::string_view value, copy_semantic fcopy) {
        sqlite3_result_text(ctx_, value.data(), int(value.size()), as_dtor(fcopy));
    }

    void context_result::set(char const* value, copy_semantic fcopy) {
        sqlite3_result_text(ctx_, value, int(std::strlen(value)), as_dtor(fcopy));
    }

    void context_result::operator= (blob const& value) {
        sqlite3_result_blob(ctx_, value.data, int(value.size), as_dtor(value.fcopy));
    }

    void context_result::operator= (nullptr_t) {
        sqlite3_result_null(ctx_);
    }

    void context_result::operator= (arg_value const& arg) {
        sqlite3_result_value(ctx_, arg.value());
    }

    void context_result::set_error(status s, string_view msg) {
        sqlite3_result_error(ctx_, msg.data(), int(msg.size()));
        sqlite3_result_error_code(ctx_, int(s));
    }

    void context_result::set_error(string_view msg) {
        sqlite3_result_error(ctx_, msg.data(), int(msg.size()));
    }

    void* context::aggregate_data(int size) {
        return sqlite3_aggregate_context(ctx_, size);
    }

    void* context::user_data() {
        return sqlite3_user_data(ctx_);
    }

    
#pragma mark - FUNCTION ARGUMENTS:

    
    arg_value context::context_args::operator[] (unsigned arg) const {
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


#pragma mark - FUNCTIONS:


    using callFn = void (*)(sqlite3_context*, int, sqlite3_value *_Nonnull*_Nonnull);
    using finishFn = void (*)(sqlite3_context*);
    using destroyFn = void (*)(void*);


    status functions::register_function(std::shared_ptr<sqlite3> const& db,
                                        const char *name, int nArgs, void* _Nullable pApp,
                                        callFn _Nullable call,
                                        callFn _Nullable step,
                                        finishFn _Nullable finish,
                                        destroyFn destroy) {
        return check( sqlite3_create_function_v2(db.get(), name, nArgs, SQLITE_UTF8,
                                                 pApp, call, step, finish, destroy) );
    }


    status functions::create(char const* name,
                             function_handler h,
                             int nargs)
    {
        shared_ptr<sqlite3> db = check_get_db();
        auto fh = new function_handler(std::move(h));

        auto function_impl = [](sqlite3_context* ctx, int nargs, sqlite3_value** values) noexcept {
            auto f = static_cast<functions::function_handler*>(sqlite3_user_data(ctx));
            context c(ctx, nargs, values);
            (*f)(c);
        };

        auto destroy_impl = [](void* pApp) noexcept {
            delete static_cast<function_handler*>(pApp);
        };

        return register_function(db, name, nargs,
                                 (void*)fh, function_impl,
                                 nullptr, nullptr, destroy_impl);
    }

    status functions::create_aggregate(char const* name, 
                                       function_handler s, function_handler f,
                                       int nargs)
    {
        shared_ptr<sqlite3> db = check_get_db();
        auto ah = new pair(new function_handler(s), new function_handler(f));

        auto step_impl = [](sqlite3_context* ctx, int nargs, sqlite3_value** values) noexcept {
            auto p = static_cast<std::pair<functions::pfunction_base, functions::pfunction_base>*>(sqlite3_user_data(ctx));
            auto s = static_cast<functions::function_handler*>((*p).first.get());
            context c(ctx, nargs, values);
            ((functions::function_handler&)*s)(c);
        };

        auto finalize_impl = [](sqlite3_context* ctx) noexcept {
            auto p = static_cast<std::pair<functions::pfunction_base, functions::pfunction_base>*>(sqlite3_user_data(ctx));
            auto f = static_cast<functions::function_handler*>((*p).second.get());
            context c(ctx);
            ((functions::function_handler&)*f)(c);
        };

        auto destroy_impl = [](void* pApp) noexcept {
            auto ah = static_cast<std::pair<function_handler*,function_handler*>*>(pApp);
            delete ah->first;
            delete ah->second;
            delete ah;
        };
        return register_function(db, name, nargs,
                                 (void*)ah, nullptr,
                                 step_impl, finalize_impl, destroy_impl);
    }

} // namespace sqnice
