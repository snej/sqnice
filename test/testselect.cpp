#include "test.h"
#include <string>
#include <iostream>
#include "sqnice/sqnice.hh"
#include "catch.hpp"

using namespace std;

TEST_CASE("SQNice select", "[.sqnice]") {
    //FIXME: Needs a pre-populated database
    sqnice::database db("test.db", sqnice::open_flags::readonly);

    sqnice::transaction xct(db, true);

    {
        sqnice::query qry(db, "SELECT id, name, phone FROM contacts");

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

        qry.reset();

        for (sqnice::query::iterator i = qry.begin(); i != qry.end(); ++i) {
            int id = i[0];
            char const* name = i[1], *phone = i[2];
            cout << id << "\t" << name << "\t" << phone << endl;
        }
        cout << endl;

        qry.reset();

        for (sqnice::query::iterator i = qry.begin(); i != qry.end(); ++i) {
            int id = 0;
            std::string name, phone;
            (*i).getter() >> sqnice::ignore >> name >> phone;
            cout << id << "\t" << name << "\t" << phone << endl;
        }
    }
}
