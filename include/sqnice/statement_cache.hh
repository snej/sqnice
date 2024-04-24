// sqnice/statement_cache.hh
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
#ifndef SQNICE_STATEMENT_CACHE_H
#define SQNICE_STATEMENT_CACHE_H

#include "sqnice/query.hh"
#include <tuple>
#include <unordered_map>

ASSUME_NONNULL_BEGIN

namespace sqnice {

    /** A cache of pre-compiled `query` or `command` objects. This has several benefits:
        * Reusing a compiled statement is faster than compiling a new one.
        * If you work around this by keeping statement objects around, you have to remember
          to destruct them all before closing the database, else it doesn't close cleanly;
          destructing the cache destructs all of them at once.

        The `database` class already has two instances of this, storing the compiled commands
        and queries returned by the `command()` and `query()` methods, so you usually don't need
        to use this template directly. */
        
    template <class STMT>
    class statement_cache : public checking {
    public:
        explicit statement_cache(database &db) noexcept :checking(db) { }

        /// Compiles a STMT, or returns a copy of an already-compiled one with the same SQL string.
        /// @warning  If two returned STMTs with the same string are in scope at the same time,
        ///           they won't work right because they are using the same `sqlite3_stmt`.
        STMT compile(std::string const& sql) {
            const STMT* stmt;
            if (auto i = stmts_.find(sql); i != stmts_.end()) {
                stmt = &i->second;
            } else {
                auto x = stmts_.emplace(std::piecewise_construct,
                                        std::tuple<std::string>{sql},
                                        std::tuple<checking&,const char*,statement::persistence>{
                                                *this, sql.c_str(), statement::persistent});
                stmt = &x.first->second;
            }
            return *stmt;
        }

        STMT operator[] (std::string const& sql)        {return compile(std::string(sql));}
        STMT operator[] (const char *sql)               {return compile(sql);}

        /// Empties the cache, freeing all statements.
        void clear()                                    {stmts_.clear();}

    private:
        std::unordered_map<std::string,STMT>    stmts_;
    };

    /** A cache of pre-compiled `command` objects. */
    using command_cache = statement_cache<command>;

    /** A cache of pre-compiled `query` objects. */
    using query_cache = statement_cache<query>;

}

ASSUME_NONNULL_END

#endif
