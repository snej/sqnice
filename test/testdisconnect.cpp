#include "test.h"
#include <iostream>
#include "sqnice/sqnice.hh"
#include "catch.hpp"

using namespace std;

TEST_CASE("SQNice close", "[sqnice]") {
    sqnice::database db = sqnice::database::temporary();
    {
        db.execute("CREATE TABLE contacts (name, phone)");
        sqnice::transaction xct(db);
        {
            sqnice::command cmd(db, "INSERT INTO contacts (name, phone) VALUES ('AAAA', '1234')");

            cmd.execute();
        }
    }
    db.close();
}
