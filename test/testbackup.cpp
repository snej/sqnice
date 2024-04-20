#include "test.h"
#include <iostream>
#include "sqnice/sqnice.hh"
#include "catch.hpp"

using namespace std;

int main_backup()
{
  try {
    sqnice::database db("test.db", sqnice::open_flags::readonly);
    sqnice::database backupdb = sqnice::database::temporary();

    db.backup(
      backupdb,
      [](int pagecount, int remaining, sqnice::status rc) {
        cout << pagecount << "/" << remaining << endl;
        if (rc == sqnice::status::ok || rc == sqnice::status::busy || rc == sqnice::status::locked) {
          // sleep(250);
        }
      });
  }
  catch (exception& ex) {
    cout << ex.what() << endl;
  }
    return 0;

}
