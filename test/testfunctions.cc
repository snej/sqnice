#include "sqnice_test.hh"
#include "sqnice/functions.hh"

using namespace std;

namespace {
    int test0()
    {
        return 100;
    }

    void test1(sqnice::function_args argv, sqnice::function_result result)
    {
        result = 200;
    }

    void test2(sqnice::function_args argv, sqnice::function_result result)
    {
        std::string arg = argv[0];
        result = arg;
    }

    void test3(sqnice::function_args argv, sqnice::function_result result)
    {
        result = argv[0];
    }

    std::string test6(std::string const& s1, std::string const& s2, std::string const& s3)
    {
        return s1 + s2 + s3;
    }
}

TEST_CASE_METHOD(sqnice_test, "SQNice function", "[sqnice]") {
    db.create_function<int ()>("test_fn", []{return 100;},
                               sqnice::function_flags::deterministic | sqnice::function_flags::innocuous);

    sqnice::query qry(db, "SELECT test_fn()");
    auto iter = qry.begin();
    int count = (*iter).get<int>(0);
    CHECK(count == 100);
}

TEST_CASE_METHOD(sqnice_test, "SQNice function args", "[sqnice]") {
    db.create_function<string (string)>("test_fn", [](const string& name){
        return "Hello " + name;
    });

    db.execute("INSERT INTO contacts (name, phone) VALUES ('Mike', '555-1234')");

    sqnice::query qry(db, "SELECT name, test_fn(name) FROM contacts");
    auto iter = qry.begin();
    string name = iter[0], hello_name = iter[1];
    CHECK(name == "Mike");
    CHECK(hello_name == "Hello Mike");
}

TEST_CASE("SQNice functions", "[sqnice]") {
    sqnice::database db;
    db.open_temporary();

    db.create_function<int ()>("h0", &test0);
    db.create_function("h1", &test1, 0);
    db.create_function("h2", &test2, 1);
    db.create_function("h3", &test3, 1);
    db.create_function<int ()>("h4", []{return 500;});
    db.create_function<int (int)>("h5", [](int i){return i + 1000;});
    db.create_function<string (string, string, string)>("h6", &test6);

    sqnice::query qry(db, "SELECT h0(), h1(), h2('x'), h3('y'), h4(), h5(10), h6('a', 'b', 'c')");

    for (int i = 0; i < qry.column_count(); ++i) {
        cout << qry.column_name(i) << "\t";
    }
    cout << endl;

    for (sqnice::query::iterator i = qry.begin(); i != qry.end(); ++i) {
        for (int j = 0; j < qry.column_count(); ++j) {
            cout << (*i).get<char const*>(j) << "\t";
        }
        cout << endl;
    }
    cout << endl;
}


namespace {

    struct strlen_aggr {
        void step(const string& s) {
            total_len += s.size();
        }

        int finish() {
            return total_len;
        }
        int total_len = 0;
    };

    template <class T>
    struct mysum
    {
        mysum() {
            s_ = T();
        }
        void step(T s) {
            s_ += s;
        }
        T finish() {
            return s_;
        }
        T s_;
    };

    struct mycnt
    {
        void step() {
            ++n_;
        }
        int finish() {
            return n_;
        }
        int n_;
    };

    struct strcnt
    {
        void step(string const& s) {
            s_ += s;
        }
        int finish() {
            return static_cast<int>(s_.size());
        }
        string s_;
    };

    struct plussum
    {
        void step(int n1, int n2) {
            n_ += n1 + n2;
        }
        int finish() {
            return n_;
        }
        int n_;
    };
}

TEST_CASE_METHOD(sqnice_test, "SQNice aggregate", "[sqnice]") {
    db.create_aggregate<strlen_aggr, string>("strlen_aggr");

    db.execute("INSERT INTO contacts (name, phone) VALUES ('Mike', '555-1234')");
    db.execute("INSERT INTO contacts (name, phone) VALUES ('Janette', '555-4321')");

    sqnice::query qry(db, "SELECT strlen_aggr(name), strlen_aggr(phone) FROM contacts");
    auto iter = qry.begin();
    expect_eq(11, (*iter).get<int>(0));
    expect_eq(16, (*iter).get<int>(1));
}

TEST_CASE("SQNice aggregate functions", "[.sqnice]") {
    //FIXME: Needs a pre-populated database
    sqnice::database db;
    db.exceptions(false);
    auto rc = db.open("foods.db", sqnice::open_flags::readonly);
    if (rc == sqnice::status::cantopen)
        return;
    REQUIRE(rc == sqnice::status::ok);
    db.exceptions(true);

    db.create_aggregate<mysum<string>, string>("a2");
    db.create_aggregate<mysum<int>, int>("a3");
    db.create_aggregate<mycnt>("a4");
    db.create_aggregate<strcnt, string>("a5");
    db.create_aggregate<plussum, int, int>("a6");

    sqnice::query qry(db, "SELECT a2(type_id), a3(id), a4(), a5(name), sum(type_id), a6(id, type_id) FROM foods");

    for (int i = 0; i < qry.column_count(); ++i) {
        cout << qry.column_name(i) << "\t";
    }
    cout << endl;

    for (sqnice::query::iterator i = qry.begin(); i != qry.end(); ++i) {
        for (int j = 0; j < qry.column_count(); ++j) {
            cout << (*i).get<char const*>(j) << "\t";
        }
        cout << endl;
    }
    cout << endl;
}
