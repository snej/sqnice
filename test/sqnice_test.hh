#pragma once
#include "sqnice/sqnice.hh"
#include "catch.hpp"
#include <iostream>

#define expect_eq(arg1, arg2) CHECK((arg2) == (arg1))


struct sqnice_test {
    sqnice::database db;

    sqnice_test()
    :db("", sqnice::open_flags::memory)
    {
        db.execute(R"""(
            CREATE TABLE contacts (
              id INTEGER PRIMARY KEY,
              name TEXT NOT NULL,
              phone TEXT NOT NULL,
              address TEXT,
              UNIQUE(name, phone)
            );
          )""");
    }
};
