#include "test.h"
#include <iostream>
#include "sqnice/sqnice.hh"
#include "sqnice/functions.hh"
#include "catch.hpp"

using namespace std;


#define expect_eq(arg1, arg2) CHECK((arg2) == (arg1))

struct db_tester {
    sqnice::database db;

    db_tester()
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


TEST_CASE_METHOD(db_tester, "SQNice insert_execute", "[sqnice]") {
    db.execute("INSERT INTO contacts (name, phone) VALUES ('Mike', '555-1234')");

    sqnice::query qry(db, "SELECT name, phone FROM contacts");
    auto iter = qry.begin();
    string name, phone;
    (*iter).getter() >> name >> phone;
    expect_eq("Mike", name);
    expect_eq("555-1234", phone);
}


TEST_CASE_METHOD(db_tester, "SQNice binder", "[sqnice]") {
    sqnice::command cmd(db, "INSERT INTO contacts (name, phone) VALUES (?, ?)");
    cmd.binder() << "Mike" << "555-1234";
    cmd.execute();

    sqnice::query qry(db, "SELECT name, phone FROM contacts");
    auto iter = qry.begin();
    string name, phone;
    (*iter).getter() >> name >> phone;
    expect_eq("Mike", name);
    expect_eq("555-1234", phone);
}

TEST_CASE_METHOD(db_tester, "SQNice bind 1", "[sqnice]") {
    sqnice::command cmd(db, "INSERT INTO contacts (name, phone) VALUES (?, ?)");
    cmd.bind(1, "Mike", sqnice::nocopy);
    cmd.bind(2, "555-1234", sqnice::nocopy);
    cmd.execute();

    sqnice::query qry(db, "SELECT name, phone FROM contacts");
    auto iter = qry.begin();
    string name, phone;
    (*iter).getter() >> name >> phone;
    expect_eq("Mike", name);
    expect_eq("555-1234", phone);
}

TEST_CASE_METHOD(db_tester, "SQNice bind 2", "[sqnice]") {
    sqnice::command cmd(db, "INSERT INTO contacts (name, phone) VALUES (?100, ?101)");
    cmd.bind(100, "Mike", sqnice::nocopy);
    cmd.bind(101, "555-1234", sqnice::nocopy);
    cmd.execute();

    sqnice::query qry(db, "SELECT name, phone FROM contacts");
    auto iter = qry.begin();
    string name, phone;
    (*iter).getter() >> name >> phone;
    expect_eq("Mike", name);
    expect_eq("555-1234", phone);
}

TEST_CASE_METHOD(db_tester, "SQNice bind 3", "[sqnice]") {
    sqnice::command cmd(db, "INSERT INTO contacts (name, phone) VALUES (:user, :phone)");
    cmd.bind(":user", "Mike", sqnice::nocopy);
    cmd.bind(":phone", "555-1234", sqnice::nocopy);
    cmd.execute();

    sqnice::query qry(db, "SELECT name, phone FROM contacts");
    auto iter = qry.begin();
    string name, phone;
    (*iter).getter() >> name >> phone;
    expect_eq("Mike", name);
    expect_eq("555-1234", phone);
}

TEST_CASE_METHOD(db_tester, "SQNice bind null", "[sqnice]") {
    sqnice::command cmd(db, "INSERT INTO contacts (name, phone, address) VALUES (:user, :phone, :address)");
    cmd.bind(":user", "Mike", sqnice::nocopy);
    cmd.bind(":phone", "555-1234", sqnice::nocopy);
    cmd.bind(":address", nullptr);
    cmd.execute();

    sqnice::query qry(db, "SELECT name, phone, address FROM contacts");
    auto iter = qry.begin();
    string name, phone;
    const char* address = nullptr;
    (*iter).getter() >> name >> phone >> address;
    expect_eq("Mike", name);
    expect_eq("555-1234", phone);
    expect_eq(nullptr, address);
}

TEST_CASE_METHOD(db_tester, "SQNice binder null", "[sqnice]") {
    sqnice::command cmd(db, "INSERT INTO contacts (name, phone, address) VALUES (?, ?, ?)");
    cmd.binder() << "Mike" << "555-1234" << nullptr;
    cmd.execute();

    sqnice::query qry(db, "SELECT name, phone, address FROM contacts");
    auto iter = qry.begin();
    string name, phone;
    const char* address = nullptr;
    (*iter).getter() >> name >> phone >> address;
    expect_eq("Mike", name);
    expect_eq("555-1234", phone);
    expect_eq(nullptr, address);
}

TEST_CASE_METHOD(db_tester, "SQNice query columns", "[sqnice]") {
    db.execute("INSERT INTO contacts (name, phone) VALUES ('Mike', '555-1234')");

    sqnice::query qry(db, "SELECT id, name, phone FROM contacts");
    expect_eq(3, qry.column_count());
    expect_eq(string("id"), qry.column_name(0));
    expect_eq(string("name"), qry.column_name(1));
    expect_eq(string("phone"), qry.column_name(2));
}

TEST_CASE_METHOD(db_tester, "SQNice query get", "[sqnice]") {
    db.execute("INSERT INTO contacts (name, phone) VALUES ('Mike', '555-1234')");

    sqnice::query qry(db, "SELECT id, name, phone FROM contacts");
    auto iter = qry.begin();
    expect_eq(string("Mike"), (*iter).get<char const*>(1));
    expect_eq(string("555-1234"), (*iter).get<char const*>(2));
}

TEST_CASE_METHOD(db_tester, "SQNice query getter", "[sqnice]") {
    db.execute("INSERT INTO contacts (name, phone) VALUES ('Mike', '555-1234')");

    sqnice::query qry(db, "SELECT id, name, phone FROM contacts");
    auto iter = qry.begin();
    string name, phone;
    (*iter).getter() >> sqnice::ignore >> name >> phone;
    expect_eq(string("Mike"), name);
    expect_eq(string("555-1234"), phone);
}

TEST_CASE_METHOD(db_tester, "SQNice query iterator", "[sqnice]") {
    db.execute("INSERT INTO contacts (name, phone) VALUES ('Mike', '555-1234')");

    sqnice::query qry(db, "SELECT id, name, phone FROM contacts");
    for (auto row : qry) {
        string name, phone;
        row.getter() >> sqnice::ignore >> name >> phone;
        expect_eq(string("Mike"), name);
        expect_eq(string("555-1234"), phone);
    }
}

TEST_CASE_METHOD(db_tester, "SQNice function", "[sqnice]") {
    sqnice::functions func(db);
    func.create<int ()>("test_fn", []{return 100;});

    sqnice::query qry(db, "SELECT test_fn()");
    auto iter = qry.begin();
    int count = (*iter).get<int>(0);
    expect_eq(100, count);
}

TEST_CASE_METHOD(db_tester, "SQNice function args", "[sqnice]") {
    sqnice::functions func(db);
    func.create<string (string)>("test_fn", [](const string& name){
        return "Hello " + name;
    });

    db.execute("INSERT INTO contacts (name, phone) VALUES ('Mike', '555-1234')");

    sqnice::query qry(db, "SELECT name, test_fn(name) FROM contacts");
    auto iter = qry.begin();
    string name = iter[0], hello_name = iter[1];
    expect_eq(string("Mike"), name);
    expect_eq(string("Hello Mike"), hello_name);
}

struct strlen_aggr {
    void step(const string& s) {
        total_len += s.size();
    }

    int finish() {
        return total_len;
    }
    int total_len = 0;
};

TEST_CASE_METHOD(db_tester, "SQNice aggregate", "[sqnice]") {
    sqnice::aggregates aggr(db);
    aggr.create<strlen_aggr, string>("strlen_aggr");

    db.execute("INSERT INTO contacts (name, phone) VALUES ('Mike', '555-1234')");
    db.execute("INSERT INTO contacts (name, phone) VALUES ('Janette', '555-4321')");

    sqnice::query qry(db, "SELECT strlen_aggr(name), strlen_aggr(phone) FROM contacts");
    auto iter = qry.begin();
    expect_eq(11, (*iter).get<int>(0));
    expect_eq(16, (*iter).get<int>(1));
}

TEST_CASE_METHOD(db_tester, "SQNice invalid path", "[.sqnice]") {
    auto open_bad = []{
        sqnice::database bad_db("/test/invalid/path");
    };
    REQUIRE_THROWS(open_bad());
}

TEST_CASE_METHOD(db_tester, "SQNice reset", "[sqnice]") {
    sqnice::command cmd(db, "INSERT INTO contacts (name, phone) VALUES (:user, :phone)");
    cmd.bind(":user", "Mike", sqnice::nocopy);
    cmd.bind(":phone", "555-1234", sqnice::nocopy);
    cmd.execute();

    cmd.reset();
    cmd.bind(":user", "Janette", sqnice::nocopy);
    cmd.execute();

    sqnice::query qry(db, "SELECT COUNT(*) FROM contacts");
    auto iter = qry.begin();
    int count = (*iter).get<int>(0);
    expect_eq(2, count);

    cmd.reset();
    cmd.clear_bindings();
    cmd.bind(":user", "Dave", sqnice::nocopy);
    expect_eq(sqnice::status::constraint, basic_status(cmd.try_execute()));
}
