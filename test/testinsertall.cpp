#include "test.h"
#include <iostream>
#include "sqnice/sqnice.hh"

using namespace std;

int main_insertall()
{
  try {
    sqnice::database db("test.db");
    {
      sqnice::transaction xct(db);
      {
	sqnice::command cmd(db,
			       "INSERT INTO contacts (name, phone) VALUES (:name, '1234');"
			       "INSERT INTO contacts (name, phone) VALUES (:name, '5678');"
			       "INSERT INTO contacts (name, phone) VALUES (:name, '9012');"
			       );
	{
	  cout << cmd.bind(":name", "user", sqnice::copy) << endl;
	  cout << cmd.execute_all() << endl;
	}
      }
      xct.commit();
    }
  }
  catch (exception& ex) {
    cout << ex.what() << endl;
  }
    return 0;

}
