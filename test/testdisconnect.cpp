#include "test.h"
#include <iostream>
#include "sqnice/sqnice.hh"

using namespace std;

int main_disconnect()
{
  try {
    sqnice::database db("test.db");
    {
      sqnice::transaction xct(db);
      {
	sqnice::command cmd(db, "INSERT INTO contacts (name, phone) VALUES ('AAAA', '1234')");

	cout << cmd.execute() << endl;
      }
    }
    cout << db.disconnect() << endl;

  }
  catch (exception& ex) {
    cout << ex.what() << endl;
  }
    return 0;

}
