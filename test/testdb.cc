#include "sqnice_test.hh"
#include "sqnice/functions.hh"
#include "sqnice/pool.hh"

using namespace std;
using namespace std::placeholders;

TEST_CASE_METHOD(sqnice_test, "SQNice insert", "[sqnice]") {
    db.execute("INSERT INTO contacts (name, phone) VALUES ('AAAA', '1234')");

    {
        sqnice::transaction xct;
        xct.begin(db);

        sqnice::command cmd(db, "INSERT INTO contacts (name, phone) VALUES (?, ?)");

        cmd.bind(1, "BBBB");
        cmd.bind(2, "555-1212");
        cmd.execute();

        cmd.execute("CCCC", "555-1313");


        cmd.binder() << "DD" << "555-1414";

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
    sqnice::database bad_db;
    bad_db.exceptions(false);
    auto rc = bad_db.open("/test/invalid/path");
    CHECK(rc == sqnice::status::cantopen);
    CHECK(bad_db.last_status() == sqnice::status::cantopen);
    CHECK(bad_db.error_msg() != nullptr);
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
    sqnice::database backupdb;
    backupdb.open_temporary();

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

        cmd.bind(1, "BBBB");
        cmd.bind(2, "1234");
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

TEST_CASE("SQNice pool", "[sqnice]") {
    sqnice::pool p("/tmp/sqnice_test.sqlite3",
                   sqnice::open_flags::delete_first | sqnice::open_flags::readwrite);
    {
        auto db = p.borrow_writeable();
        CHECK(p.borrowed_count() == 1);
        db->execute(R"""(
            CREATE TABLE contacts (
              id INTEGER PRIMARY KEY,
              name TEXT NOT NULL,
              phone TEXT NOT NULL,
              address TEXT,
              UNIQUE(name, phone)
            );
          )""");
        db->execute("");
        auto cmd = db->command("INSERT INTO contacts (name, phone) VALUES (?1, ?2)");
        cmd.execute("Bob", "555-1212");

        CHECK(p.try_borrow_writeable() == nullptr);
    }

    CHECK(p.borrowed_count() == 0);

    auto db1 = p.borrow();
    CHECK(p.borrowed_count() == 1);
    string name = db1->query("SELECT name FROM contacts").single_value_or<string>("");
    CHECK(name == "Bob");

    auto db2 = p.borrow();
    CHECK(p.borrowed_count() == 2);
    auto db3 = p.borrow();
    CHECK(p.borrowed_count() == 3);
    auto db4 = p.borrow();
    CHECK(p.borrowed_count() == 4);

    CHECK(p.try_borrow() == nullptr);
    db1.reset();
    CHECK(p.borrowed_count() == 3);
    auto db5 = p.borrow();
    CHECK(p.borrowed_count() == 4);

    {
        sqnice::transaction txn(p);
        CHECK(p.borrowed_count() == 5);
        CHECK(p.try_borrow_writeable() == nullptr);
    }
    CHECK(p.borrowed_count() == 4);
}
