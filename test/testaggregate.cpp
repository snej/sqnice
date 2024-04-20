#include "test.h"
#include <string>
#include <iostream>
#include "sqnice/sqnice.hh"
#include "sqnice/functions.hh"
#include "catch.hpp"

using namespace std;

namespace {
    inline std::ostream& operator<<(std::ostream& out, sqnice::status s) {return out << int(s);}

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

TEST_CASE("SQNice aggregate functions", "[.sqnice]") {
    //FIXME: Needs a pre-populated database
    sqnice::database db("foods.db", sqnice::open_flags::readonly);

    sqnice::aggregates aggr(db);
    cout << aggr.create<mysum<string>, string>("a2") << endl;
    cout << aggr.create<mysum<int>, int>("a3") << endl;
    cout << aggr.create<mycnt>("a4") << endl;
    cout << aggr.create<strcnt, string>("a5") << endl;
    cout << aggr.create<plussum, int, int>("a6") << endl;

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
