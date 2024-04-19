#include "test.h"
#include <iostream>
#include "sqnice.h"

using namespace std;

int main_attach()
{
  try {
    sqnice::database db("foods.db");

    db.attach("test.db", "test");
    {
      sqnice::transaction xct(db);
      {
	sqnice::query qry(db, "SELECT epi.* FROM episodes epi, test.contacts con WHERE epi.id = con.id");

	for (sqnice::query::iterator i = qry.begin(); i != qry.end(); ++i) {
	  for (int j = 0; j < qry.column_count(); ++j) {
	    cout << (*i).get<char const*>(j) << "\t";
	  }
	  cout << endl;
	}
	cout << endl;
      }
    }
  }
  catch (exception& ex) {
    cout << ex.what() << endl;
  }
    return 0;

}
