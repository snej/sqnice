#include "test.h"
#include <string>
#include <iostream>
#include "sqnice/sqnice.hh"

using namespace std;

int main_select()
{
  try {
    sqnice::database db("test.db");

    sqnice::transaction xct(db, true);

    {
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
	int id;
	char const* name, *phone;
	std::tie(id, name, phone) = (*i).get_columns<int, char const*, char const*>(0, 1, 2);
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
  }
  catch (exception& ex) {
    cout << ex.what() << endl;
  }
    return 0;
}
