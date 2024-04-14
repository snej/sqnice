// sqlite3pp.h
//
// The MIT License
//
// Copyright (c) 2015 Wongoo Lee (iwongu at gmail dot com)
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
// THE SOFTWARE.

#ifndef SQLITE3PP_H
#define SQLITE3PP_H

#define SQLITE3PP_VERSION "1.1.0"
#define SQLITE3PP_VERSION_MAJOR 1
#define SQLITE3PP_VERSION_MINOR 1
#define SQLITE3PP_VERSION_PATCH 0

#include <functional>
#include <iterator>
#include <stdexcept>
#include <string>
#include <string_view>
#include <tuple>
#include <unordered_map>

#ifdef SQLITE3PP_LOADABLE_EXTENSION
#include <sqlite3ext.h>
SQLITE_EXTENSION_INIT1
#else
#  include <sqlite3.h>
#endif

namespace sqlite3pp
{
  class database;

  namespace ext
  {
    class function;
    class aggregate;
    database borrow(sqlite3* pdb);
  }

  template <class T>
  struct convert {
    using to_int = int;
  };

  class null_type {};
  extern null_type ignore;

  class noncopyable
  {
   protected:
    noncopyable() = default;
    ~noncopyable() = default;

    noncopyable(noncopyable&&) = default;
    noncopyable& operator=(noncopyable&&) = default;

    noncopyable(noncopyable const&) = delete;
    noncopyable& operator=(noncopyable const&) = delete;
  };

  class checking
  {
  public:
    void exceptions(bool x)     {exceptions_ = x;}
    bool exceptions()           {return exceptions_;}
  protected:
    checking(database &db)      :db_(db) { }
    int check(int rc) const     {if (rc != SQLITE_OK && exceptions_) throw_(rc); return rc;}
    [[noreturn]] void throw_(int rc) const;

    database& db_;
    bool exceptions_ = false;
  };

  class database : public checking, noncopyable
  {
    friend class statement;
    friend class database_error;
    friend class blob_handle;
    friend class ext::function;
    friend class ext::aggregate;
    friend database ext::borrow(sqlite3* pdb);

   public:
    using busy_handler = std::function<int (int)>;
    using commit_handler = std::function<int ()>;
    using rollback_handler = std::function<void ()>;
    using update_handler = std::function<void (int, char const*, char const*, long long int)>;
    using authorize_handler = std::function<int (int, char const*, char const*, char const*, char const*)>;
    using backup_handler = std::function<void (int, int, int)>;

    explicit database(char const* dbname = nullptr, int flags = SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, const char* vfs = nullptr);

    database(database&& db);
    database& operator=(database&& db);

    ~database();

    const char* filename() const;

    int connect(char const* dbname, int flags, const char* vfs = nullptr);
    int disconnect();

    int attach(char const* dbname, char const* name);
    int detach(char const* name);

    int backup(database& destdb, backup_handler h = {});
    int backup(char const* dbname, database& destdb, char const* destdbname, backup_handler h, int step_page = 5);

    long long int last_insert_rowid() const;

    int enable_foreign_keys(bool enable = true);
    int enable_triggers(bool enable = true);
    int enable_extended_result_codes(bool enable = true);

    int changes() const;
    int64_t total_changes() const;

    int error_code() const;
    int extended_error_code() const;
    char const* error_msg() const;

    int execute(char const* sql);
    int executef(char const* sql, ...);

    int set_busy_timeout(int ms);

    void set_busy_handler(busy_handler h);
    void set_commit_handler(commit_handler h);
    void set_rollback_handler(rollback_handler h);
    void set_update_handler(update_handler h);
    void set_authorize_handler(authorize_handler h);

    sqlite3* handle() const         {return db_;}
    
   private:
    database(sqlite3* pdb);

   private:
    sqlite3* db_;
    bool borrowing_;

    busy_handler bh_;
    commit_handler ch_;
    rollback_handler rh_;
    update_handler uh_;
    authorize_handler ah_;
  };

  class database_error : public std::runtime_error
  {
   public:
    explicit database_error(char const* msg, int rc);
    explicit database_error(database& db, int rc);

    int const error_code;
  };

  enum copy_semantic { copy, nocopy };

  struct blob
  {
    const void* data;
    size_t size;
    copy_semantic fcopy;
  };

  class statement : public checking, noncopyable
  {
   public:
    int prepare(char const* stmt);
    int finish();
    bool prepared() const;
    operator bool() const;

    int bind(int idx, int value);
    int bind(int idx, double value);
    int bind(int idx, long int value);
    int bind(int idx, long long int value);
    int bind(int idx, char const* value, copy_semantic fcopy = copy);
    int bind(int idx, blob value);
    int bind(int idx, void const* value, int n, copy_semantic fcopy = copy);
    int bind(int idx, std::string_view value, copy_semantic fcopy = copy);
    int bind(int idx);
    int bind(int idx, null_type);

    int bind(char const* name, int value);
    int bind(char const* name, double value);
    int bind(char const* name, long int value);
    int bind(char const* name, long long int value);
    int bind(char const* name, char const* value, copy_semantic fcopy = copy);
    int bind(char const* name, blob value);
    int bind(char const* name, void const* value, int n, copy_semantic fcopy = copy);
    int bind(char const* name, std::string_view value, copy_semantic fcopy = copy);
    int bind(char const* name);
    int bind(char const* name, null_type);

    class bindref : noncopyable { // used by operator[]
    public:
      bindref(statement &stmt, int idx) :stmt_(stmt), idx_(idx) { }

      template <class T>
      void operator= (const T &value) {
        auto rc = stmt_.bind(idx_, value);
        if (rc != SQLITE_OK) {
          throw database_error(stmt_.db_, rc);
        }
      }
    private:
      statement& stmt_;
      int const idx_;
    };

    bindref operator[] (int idx)            {return bindref(*this, idx);}
    bindref operator[] (char const *name);

    int step();
    int reset();
    int clear_bindings();

   protected:
    explicit statement(database& db, char const* stmt = nullptr);
    statement(statement&&) = default;
    ~statement();

    void share(const statement&);
    int prepare_impl(char const* stmt);
    int finish_impl(sqlite3_stmt* stmt);

   protected:
    sqlite3_stmt* stmt_;
    char const* tail_;
    bool shared_ = false;
  };

  class command : public statement
  {
   public:
    class bindstream
    {
     public:
      bindstream(command& cmd, int idx);

      template <class T>
      bindstream& operator << (T value) {
        auto rc = cmd_.bind(idx_, value);
        if (rc != SQLITE_OK) {
          cmd_.throw_(rc);
        }
        ++idx_;
        return *this;
      }
      bindstream& operator << (char const* value) {
        auto rc = cmd_.bind(idx_, value, copy);
        if (rc != SQLITE_OK) {
          cmd_.throw_(rc);
        }
        ++idx_;
        return *this;
      }
      bindstream& operator << (std::string_view value) {
        auto rc = cmd_.bind(idx_, value, copy);
        if (rc != SQLITE_OK) {
          cmd_.throw_(rc);
        }
        ++idx_;
        return *this;
      }
      bindstream& operator << (std::nullptr_t value) {
        auto rc = cmd_.bind(idx_);
        if (rc != SQLITE_OK) {
          cmd_.throw_(rc);
        }
        ++idx_;
        return *this;
      }

     private:
      command& cmd_;
      int idx_;
    };

    explicit command(database& db, char const* stmt = nullptr);

    command shared_copy() const {
      command cmd(db_);
      cmd.share(*this);
      return cmd;
    }

    bindstream binder(int idx = 1);

    int execute();
    int execute_all();
  };

  class query : public statement
  {
   public:
    class rows
    {
     public:
      class getstream
      {
       public:
        getstream(rows* rws, int idx);

        template <class T>
        getstream& operator >> (T& value) {
          value = rws_->get(idx_, T());
          ++idx_;
          return *this;
        }

       private:
        rows* rws_;
        int idx_;
      };

      explicit rows(sqlite3_stmt* stmt);

      int data_count() const;
      int column_type(int idx) const;

      bool not_null(int idx) const {
        return column_type(idx) != SQLITE_NULL;
      }

      int column_bytes(int idx) const;

      template <class T> T get(int idx) const {
        return get(idx, T());
      }

      template <class... Ts>
      std::tuple<Ts...> get_columns(typename convert<Ts>::to_int... idxs) const {
        return std::make_tuple(get(idxs, Ts())...);
      }

      getstream getter(int idx = 0);

     private:
      int get(int idx, int) const;
      double get(int idx, double) const;
      long long int get(int idx, long int) const;
      long long int get(int idx, long long int) const;
      char const* get(int idx, char const*) const;
      std::string get(int idx, std::string) const;
      std::string_view get(int idx, std::string_view) const;
      void const* get(int idx, void const*) const;
      blob get(int idx, blob) const;
      null_type get(int idx, null_type) const;

     private:
      sqlite3_stmt* stmt_;
    };

    class query_iterator    {
     public:
      typedef std::input_iterator_tag iterator_category;
      typedef rows value_type;
      typedef std::ptrdiff_t difference_type;
      typedef rows* pointer;
      typedef rows& reference;

      query_iterator();
      explicit query_iterator(query* cmd);

      bool operator==(query_iterator const&) const;
      bool operator!=(query_iterator const&) const;

      query_iterator& operator++();

      const value_type& operator*() const;
      const value_type* operator->() const;

     private:
      query* cmd_;
      int rc_;
      rows rows_;
    };

    explicit query(database& db, char const* stmt = nullptr);

    void share(const query &q) {
      statement::share(q);
    }

    query shared_copy() const {
      query q(db_);
      q.share(*this);
      return q;
    }

    int column_count() const;

    char const* column_name(int idx) const;
    char const* column_decltype(int idx) const;

    using iterator = query_iterator;

    iterator begin();
    iterator end();
  };

  /** A cache of pre-compiled `query` or `command` objects. */
  template <class STMT>
  class statement_cache {
  public:
    explicit statement_cache(database &db) :db_(db) { }

    STMT compile(const std::string &sql) {
      const STMT* stmt;
      if (auto i = stmts_.find(sql); i != stmts_.end()) {
        stmt = &i->second;
      } else {
        auto x = stmts_.emplace(std::piecewise_construct,
                                std::tuple<std::string>{sql},
                                std::tuple<database&,const char*>{db_, sql.c_str()});
        stmt = &x.first->second;
      }
      return stmt->shared_copy();
    }

    STMT operator[] (const std::string &sql)    {return compile(sql);}
    STMT operator[] (const char *sql)           {return compile(sql);}

    void clear()                                {stmts_.clear();}

  private:
    database& db_;
    std::unordered_map<std::string,STMT> stmts_;
  };

  using command_cache = statement_cache<command>;
  using query_cache = statement_cache<query>;

  class transaction : public checking, noncopyable
  {
   public:
    explicit transaction(database& db, bool fcommit = false, bool freserve = false);
    transaction(transaction&&);
    ~transaction();

    int commit();
    int rollback();

   private:
    bool active_;
    bool fcommit_;
  };

  class savepoint : public checking, noncopyable
  {
  public:
    explicit savepoint(database& db, bool fcommit = false);
    savepoint(savepoint&&);
    ~savepoint();

    int commit();
    int rollback();

  private:
    int execute(char const *cmd);
    
    bool active_;
    bool fcommit_;
  };

  /** Random access to the data in a blob. */
  class blob_handle : public noncopyable {
  public:
    blob_handle(database& db,
                const char *database,
                const char* table, const char *column, int64_t rowid,
                bool writeable);
    ~blob_handle()                      {if (blob_) sqlite3_blob_close(blob_);}
    uint64_t size() const               {return size_;}
    ssize_t read(void *dst, size_t len, uint64_t offset);

  private:
    sqlite3_blob*   blob_ = nullptr;
    uint64_t        size_;
  };

} // namespace sqlite3pp

#endif
