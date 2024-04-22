#include "sqnice_test.hh"
#include "sqnice/functions.hh"

using namespace std;
using namespace std::placeholders;

TEST_CASE_METHOD(sqnice_test, "SQNice insert", "[sqnice]") {
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

TEST_CASE_METHOD(sqnice_test, "SQNice insert_execute", "[sqnice]") {
    db.execute("INSERT INTO contacts (name, phone) VALUES ('Mike', '555-1234')");

    sqnice::query qry(db, "SELECT name, phone FROM contacts");
    auto iter = qry.begin();
    string name, phone;
    (*iter).getter() >> name >> phone;
    expect_eq("Mike", name);
    expect_eq("555-1234", phone);
}

TEST_CASE_METHOD(sqnice_test, "SQNice invalid path", "[.sqnice]") {
    auto open_bad = []{
        sqnice::database bad_db("/test/invalid/path");
    };
    REQUIRE_THROWS(open_bad());
}

TEST_CASE_METHOD(sqnice_test, "SQNice close", "[sqnice]") {
    {
        sqnice::transaction xct(db);
        sqnice::command cmd(db, "INSERT INTO contacts (name, phone) VALUES ('AAAA', '1234')");

        cmd.execute();
    }
    db.close();
}

TEST_CASE_METHOD(sqnice_test, "SQNice backup", "[sqnice]") {
    sqnice::database backupdb = sqnice::database::temporary();

    db.backup(backupdb,
              [](int pagecount, int remaining, sqnice::status rc) {
        cout << pagecount << "/" << remaining << endl;
        if (rc == sqnice::status::busy || rc == sqnice::status::locked) {
            // sleep(250);
        }
    });
}

namespace {
    struct handler
    {
        handler() : cnt_(0) {}

        void handle_update(int opcode, char const* dbname, char const* tablename, long long int rowid) {
            cout << "handle_update(" << opcode << ", " << dbname << ", " << tablename << ", " << rowid << ") - " << cnt_++ << endl;
        }
        int cnt_;
    };

    sqnice::status handle_authorize(int evcode, char const* /*p1*/, char const* /*p2*/, char const* /*dbname*/, char const* /*tvname*/) {
        cout << "handle_authorize(" << evcode << ")" << endl;
        return sqnice::status::ok;
    }

    struct rollback_handler
    {
        void operator()() {
            cout << "handle_rollback" << endl;
        }
    };
}

TEST_CASE_METHOD(sqnice_test, "SQNice callbacks", "[sqnice]") {
    {
        db.set_commit_handler([]{cout << "handle_commit\n"; return 0;});
        db.set_rollback_handler(rollback_handler());
    }

    handler h;

    db.set_update_handler(std::bind(&handler::handle_update, &h, _1, _2, _3, _4));

    db.set_authorize_handler(&handle_authorize);

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
        sqnice::transaction xct(db);

        sqnice::command cmd(db, "INSERT INTO contacts (name, phone) VALUES (:name, :name)");

        cmd[":name"] = "DDDD";

        cmd.execute();
    }

}
