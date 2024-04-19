#include "test.h"
#include <iostream>
#include "sqnice.h"

using namespace std;

int main_backup()
{
  try {
    sqnice::database db("test.db");
    sqnice::database backupdb("backup.db");

    db.backup(
      backupdb,
      [](int pagecount, int remaining, int rc) {
        cout << pagecount << "/" << remaining << endl;
        if (rc == SQLITE_OK || rc == SQLITE_BUSY || rc == SQLITE_LOCKED) {
          // sleep(250);
        }
      });
  }
  catch (exception& ex) {
    cout << ex.what() << endl;
  }
    return 0;

}
