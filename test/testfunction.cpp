#include "test.h"
#include <string>
#include <iostream>
#include "sqnice.h"
#include "sqniceext.h"

using namespace std;

int test0()
{
  return 100;
}

void test1(sqnice::ext::context& ctx)
{
  ctx.result(200);
}

void test2(sqnice::ext::context& ctx)
{
  std::string args = ctx.get<std::string>(0);
  ctx.result(args);
}

void test3(sqnice::ext::context& ctx)
{
  ctx.result_copy(0);
}

std::string test5(std::string const& value)
{
  return value;
}

std::string test6(std::string const& s1, std::string const& s2, std::string const& s3)
{
  return s1 + s2 + s3;
}

int main_function()
{
  try {
    sqnice::database db("test.db");

    sqnice::ext::function func(db);
    cout << func.create<int ()>("h0", &test0) << endl;
    cout << func.create("h1", &test1) << endl;
    cout << func.create("h2", &test2, 1) << endl;
    cout << func.create("h3", &test3, 1) << endl;
    cout << func.create<int ()>("h4", []{return 500;}) << endl;
    cout << func.create<int (int)>("h5", [](int i){return i + 1000;}) << endl;
    cout << func.create<string (string, string, string)>("h6", &test6) << endl;

    sqnice::query qry(db, "SELECT h0(), h1(), h2('x'), h3('y'), h4(), h5(10), h6('a', 'b', 'c')");

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
  }
  catch (exception& ex) {
    cout << ex.what() << endl;
  }
    return 0;
}
