#include "test.h"
#include <exception>
#include <functional>
#include <iostream>
#include "sqnice/sqnice.hh"
#include "catch.hpp"

using namespace std;
using namespace std::placeholders;

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

TEST_CASE("SQNice callbacks", "[sqnice]") {
    sqnice::database db= sqnice::database::temporary();
    db.execute("CREATE TABLE contacts (name, phone)");

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
