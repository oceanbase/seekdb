// Copyright (c) 2026 OceanBase.
// SPDX-License-Identifier: Apache-2.0
#include "share/storage/ob_sqlite_connection.h"
#include "lib/resource/achunk_mgr.h"
#include <sqlite/sqlite3.h>
#include <cassert>
#include <climits>
#include <cstdio>
#include <cstring>
#include <pthread.h>

using namespace oceanbase;
using namespace oceanbase::common;
using namespace oceanbase::share;

static int64_t row_count(ObSQLiteConnection &connection)
{
  int64_t count = -1;
  assert(connection.query("SELECT count(*) FROM metadata", nullptr,
    [&](ObSQLiteRowReader &row) { count = row.get_int64(); return OB_SUCCESS; }) == OB_SUCCESS);
  return count;
}

static void *check_connection(void *)
{
  for (int attempt = 0; attempt != 3; ++attempt) {
    ObSQLiteConnection connection;
    assert(connection.init(":memory:") == OB_SUCCESS);
    assert(connection.execute("CREATE TABLE metadata(id INTEGER PRIMARY KEY, name TEXT, value BLOB)") == OB_SUCCESS);
    assert(connection.begin_transaction() == OB_SUCCESS);
    assert(connection.is_in_transaction());
    const char name[] = "seekdb 元数据";
    const unsigned char data[] = {0, 255, 17, 0, 32};
    auto bind = [&](ObSQLiteBinder &binder) {
      int ret = binder.bind_int64(INT64_MAX);
      if (ret == OB_SUCCESS) ret = binder.bind_text(name);
      if (ret == OB_SUCCESS) ret = binder.bind_blob(data, sizeof(data));
      return ret;
    };
    int64_t affected = -1;
    assert(connection.execute("INSERT INTO metadata VALUES(?,?,?)", bind, &affected) == OB_SUCCESS);
    assert(affected == 1 && row_count(connection) == 1);
    assert(connection.rollback() == OB_SUCCESS);
    assert(!connection.is_in_transaction() && row_count(connection) == 0);
    assert(connection.begin_transaction() == OB_SUCCESS);
    assert(connection.execute("INSERT INTO metadata VALUES(?,?,?)", bind) == OB_SUCCESS);
    assert(connection.commit() == OB_SUCCESS && !connection.is_in_transaction());
    int seen = 0;
    assert(connection.query("SELECT id,name,value FROM metadata", nullptr,
      [&](ObSQLiteRowReader &row) {
        assert(row.get_int64() == INT64_MAX);
        int length = 0;
        const char *text = row.get_text(&length);
        assert(length == sizeof(name) - 1 && memcmp(text, name, length) == 0);
        const void *blob = row.get_blob(&length);
        assert(length == sizeof(data) && memcmp(blob, data, length) == 0);
        ++seen;
        return OB_SUCCESS;
      }) == OB_SUCCESS);
    assert(seen == 1);
    assert(connection.execute("INSERT INTO metadata VALUES(?,?,?)", bind) != OB_SUCCESS);
    assert(connection.execute("THIS IS INVALID SQL") != OB_SUCCESS);
    assert(row_count(connection) == 1);
    assert(connection.begin_transaction() == OB_SUCCESS);
    assert(connection.execute("DELETE FROM metadata") == OB_SUCCESS);
    connection.close(); // exercises rollback of the active metadata transaction
    assert(!connection.is_valid());
    assert(connection.init(":memory:") == OB_SUCCESS);
    assert(connection.execute("SELECT * FROM metadata") != OB_SUCCESS);
    connection.close();
  }
  return nullptr;
}

int main()
{
  assert(sqlite3_libversion_number() == SQLITE_VERSION_NUMBER);
  assert(strcmp(sqlite3_sourceid(), SQLITE_SOURCE_ID) == 0);
  assert(sqlite3_threadsafe() == 1);
  lib::AChunkMgr::instance().set_limit(64 * 1024 * 1024);
  lib::AChunkMgr::instance().set_hard_limit(64 * 1024 * 1024);
  pthread_t workers[2];
  for (auto &worker : workers) assert(pthread_create(&worker, nullptr, check_connection, nullptr) == 0);
  check_connection(nullptr);
  for (auto &worker : workers) assert(pthread_join(worker, nullptr) == 0);
  puts("PASS: seekdb SQLite metadata adapter in memory (not seekdb SQL execution or persistence)");
}
