# SQNice

SQNice is a damn nice SQLite API for C++.

It is based on sqlite3pp by Wongoo Lee, but has been extensively modified by me, Jens Alfke (@snej).

In the nearly 20(!) years I've been using SQLite, I've learned a number of good practices, and run into a number of weird problems from suble misuse of its APIs. 

In SQNice I've tried to take advantage of my learning and bake it into the library's API, to make those good practices automatic or at least easy. I've also tried to make it idiomatic C++, and make simple things simple. Some examples:

* Strongly typed: errors are returned as `status`, an enum class, not generic `int`s.
* Most strings are passed in as `string_view` so you don't have to call `c_str()` on them.
* Uses C++ exceptions by default, but they can be turned off at the database or query level when necessary if you need to handle expected errors like constraint violations.
* Supports some cool but lesser-known features, like backups and blob streams.

* Lets you set up best practices like WAL and incremental vacuuming with one [optional] call.
* Avoids nasty problems that can result from closing a database while statements are still busy. (At one point in the past my co-workers and I had to diagnose a very nasty database corruption bug stemming from this!)
* Manages nested transactions smoothly with RAII, including safely destructing a transaction object when an exception is thrown.
* Easy access to getting and setting pragmas and limits.

* Super easy to reuse compiled statements (`sqlite3_stmt`), without running into problems with leftover bindings or forgetting to reset.
* It's very simple to bind arguments to statement parameters, and to get query column values. This is extensible, so you can make your custom types easily bindable too.
* Unlike some other C++ APIs, you can bind or return unsigned types without having use manual casts; and it catches problems like binding a `uint64_t` that's too big for SQLite to represent.
* It's very easy to run a query that returns a single value.
* Includes idiomatic APIs for defining custom SQL functions, even aggregates.
