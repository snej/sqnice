#include "test.h"
#include <iostream>
#include "sqnice/sqnice.hh"

using namespace std;

int main_insert()
{
  try {
    sqnice::database db("test.db");

    {
      db.execute("INSERT INTO contacts (name, phone) VALUES ('AAAA', '1234')");
    }

    {
      sqnice::transaction xct(db);

      sqnice::command cmd(db, "INSERT INTO contacts (name, phone) VALUES (?, ?)");

      cout << cmd.bind(1, "BBBB", sqnice::copy) << endl;
      cout << cmd.bind(2, "1234", sqnice::copy) << endl;
      cout << cmd.execute() << endl;

      cout << cmd.reset() << endl;

      cmd.binder() << "CCCC" << "1234";

      cout << cmd.execute() << endl;

      xct.commit();
    }

    {
      sqnice::transaction xct(db, true);

      sqnice::command cmd(db, "INSERT INTO contacts (name, phone) VALUES (:name, :name)");

      cout << cmd.bind(":name", "DDDD", sqnice::copy) << endl;

      cout << cmd.execute() << endl;
    }
  }
  catch (exception& ex) {
    cout << ex.what() << endl;
  }
    return 0;

}
