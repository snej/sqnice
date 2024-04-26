#include "sqnice_test.hh"

using namespace std;

TEST_CASE_METHOD(sqnice_test, "SQNice reset", "[sqnice]") {
    sqnice::command cmd(db, "INSERT INTO contacts (name, phone) VALUES (:user, :phone)");
    cmd.bind(":user", sqnice::uncopied("Mike"));
    cmd.bind(":phone", sqnice::uncopied("555-1234"));
    cmd.execute();

    cmd.reset();
    cmd.bind(":user", sqnice::uncopied("Janette"));
    cmd.execute();

    sqnice::query qry(db, "SELECT COUNT(*) FROM contacts");
    auto iter = qry.begin();
    int count = (*iter).get<int>(0);
    expect_eq(2, count);

    cmd.reset();
    cmd.clear_bindings();
    cmd.bind(":user", sqnice::uncopied("Dave"));
    expect_eq(sqnice::status::constraint, basic_status(cmd.try_execute()));
}

TEST_CASE_METHOD(sqnice_test, "SQNice binder", "[sqnice]") {
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

TEST_CASE_METHOD(sqnice_test, "SQNice bind 1", "[sqnice]") {
    sqnice::command cmd(db, "INSERT INTO contacts (name, phone) VALUES (?, ?)");
    cmd.bind(1, sqnice::uncopied("Mike"));
    cmd.bind(2, sqnice::uncopied("555-1234"));
    cmd.execute();

    sqnice::query qry(db, "SELECT name, phone FROM contacts");
    auto iter = qry.begin();
    string name, phone;
    (*iter).getter() >> name >> phone;
    expect_eq("Mike", name);
    expect_eq("555-1234", phone);
}

TEST_CASE_METHOD(sqnice_test, "SQNice bind 2", "[sqnice]") {
    sqnice::command cmd(db, "INSERT INTO contacts (name, phone) VALUES (?100, ?101)");
    cmd.bind(100, sqnice::uncopied("Mike"));
    cmd.bind(101, sqnice::uncopied("555-1234"));
    cmd.execute();

    sqnice::query qry(db, "SELECT name, phone FROM contacts");
    auto iter = qry.begin();
    string name, phone;
    (*iter).getter() >> name >> phone;
    expect_eq("Mike", name);
    expect_eq("555-1234", phone);
}

TEST_CASE_METHOD(sqnice_test, "SQNice bind 3", "[sqnice]") {
    sqnice::command cmd(db, "INSERT INTO contacts (name, phone) VALUES (:user, :phone)");
    cmd.bind(":user", sqnice::uncopied("Mike"));
    cmd.bind(":phone", sqnice::uncopied("555-1234"));
    cmd.execute();

    sqnice::query qry(db, "SELECT name, phone FROM contacts");
    auto iter = qry.begin();
    string name, phone;
    (*iter).getter() >> name >> phone;
    expect_eq("Mike", name);
    expect_eq("555-1234", phone);
}

TEST_CASE_METHOD(sqnice_test, "SQNice bind null", "[sqnice]") {
    sqnice::command cmd(db, "INSERT INTO contacts (name, phone, address) VALUES (:user, :phone, :address)");
    cmd.bind(":user", sqnice::uncopied("Mike"));
    cmd.bind(":phone", sqnice::uncopied("555-1234"));
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

TEST_CASE_METHOD(sqnice_test, "SQNice binder null", "[sqnice]") {
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

TEST_CASE_METHOD(sqnice_test, "SQNice query columns", "[sqnice]") {
    db.execute("INSERT INTO contacts (name, phone) VALUES ('Mike', '555-1234')");

    sqnice::query qry(db, "SELECT id, name, phone FROM contacts");
    expect_eq(3, qry.column_count());
    expect_eq(string("id"), qry.column_name(0));
    expect_eq(string("name"), qry.column_name(1));
    expect_eq(string("phone"), qry.column_name(2));
}

TEST_CASE_METHOD(sqnice_test, "SQNice query get", "[sqnice]") {
    db.execute("INSERT INTO contacts (name, phone) VALUES ('Mike', '555-1234')");

    sqnice::query qry(db, "SELECT id, name, phone FROM contacts");
    auto iter = qry.begin();
    expect_eq(string("Mike"), (*iter).get<char const*>(1));
    expect_eq(string("555-1234"), (*iter).get<char const*>(2));
}

TEST_CASE_METHOD(sqnice_test, "SQNice query getter", "[sqnice]") {
    db.execute("INSERT INTO contacts (name, phone) VALUES ('Mike', '555-1234')");

    sqnice::query qry(db, "SELECT id, name, phone FROM contacts");
    auto iter = qry.begin();
    string name, phone;
    (*iter).getter() >> sqnice::ignore >> name >> phone;
    expect_eq(string("Mike"), name);
    expect_eq(string("555-1234"), phone);
}

TEST_CASE_METHOD(sqnice_test, "SQNice query iterator", "[sqnice]") {
    db.execute("INSERT INTO contacts (name, phone) VALUES ('Mike', '555-1234')");

    sqnice::query qry(db, "SELECT id, name, phone FROM contacts");
    for (auto row : qry) {
        string name, phone;
        row.getter() >> sqnice::ignore >> name >> phone;
        expect_eq(string("Mike"), name);
        expect_eq(string("555-1234"), phone);
    }
}

TEST_CASE_METHOD(sqnice_test, "SQNice select", "[.sqnice]") {
    //FIXME: Needs a pre-populated database
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
