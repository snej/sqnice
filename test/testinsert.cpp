#include "test.h"
#include <iostream>
#include "sqnice/sqnice.hh"
#include "catch.hpp"

using namespace std;

TEST_CASE("SQNice insert", "[sqnice]") {
    sqnice::database db = sqnice::database::temporary();

    db.execute("CREATE TABLE contacts (name, phone)");
    db.execute("INSERT INTO contacts (name, phone) VALUES ('AAAA', '1234')");

    {
        sqnice::transaction xct(db);

        sqnice::command cmd(db, "INSERT INTO contacts (name, phone) VALUES (?, ?)");

        cmd.bind(1, "BBBB", sqnice::copy);
        cmd.bind(2, "1234", sqnice::copy);
        cmd.execute();

        cmd.reset();

        cmd.binder() << "CCCC" << "1234";

        cmd.execute();

        xct.commit();
    }

    {
        sqnice::transaction xct(db, true);

        sqnice::command cmd(db, "INSERT INTO contacts (name, phone) VALUES (:name, :name)");

        cmd[":name"] = "DDDD";

        cmd.execute();
    }
}

