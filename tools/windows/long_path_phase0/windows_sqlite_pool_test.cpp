// Copyright (c) 2026 OceanBase. Licensed under the Apache License, Version 2.0.
#include "share/storage/ob_sqlite_connection_pool.h"
#include "path_fixture.h"
#include <sqlite/sqlite3.h>
#include <windows.h>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <cstdlib>
#include <atomic>
#include <thread>
#include <vector>
#include <chrono>
void request_finish_callback() { std::abort(); }
using namespace oceanbase::common;
using namespace oceanbase::share;
namespace {
void require(bool ok, const char *what) { if (!ok) { throw std::runtime_error(what); } }
std::string utf8(const std::wstring &path)
{
  const int n = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, path.data(),
      static_cast<int>(path.size()), nullptr, 0, nullptr, nullptr);
  require(n > 0, "path encoding size");
  std::string out(n, '\0');
  require(n == WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, path.data(),
      static_cast<int>(path.size()), out.data(), n, nullptr, nullptr), "path encoding");
  return out;
}
void exercise(const std::wstring &directory)
{
  const std::wstring wide = L"\\\\?\\" + directory;
  std::filesystem::create_directories(wide);
  const std::wstring file = wide + L"\\meta.db";
  ObSQLiteConnectionPool pool;
  std::string input = utf8(directory + L"\\meta.db");
  require(pool.init(input.c_str()) == OB_SUCCESS, "pool init");
  input.assign(input.size(), 'x'); // The pool must own the complete original name.
  {
    ObSQLiteConnectionGuard first(&pool);
    require(first.is_valid(), "first connection");
    sqlite3_vfs *actual = nullptr;
    require(SQLITE_OK == sqlite3_file_control(first->get_db(), "main", SQLITE_FCNTL_VFS_POINTER, &actual)
        && actual == sqlite3_vfs_find("win32-longpath"), "selected VFS");
    require(first->execute("CREATE TABLE t(v INTEGER)") == OB_SUCCESS, "create");
    require(first->begin_transaction() == OB_SUCCESS, "begin");
    require(first->execute("INSERT INTO t VALUES(533)") == OB_SUCCESS, "insert");
    require(first->commit() == OB_SUCCESS, "commit");
    require(std::filesystem::exists(file + L"-wal") && std::filesystem::exists(file + L"-shm"), "WAL side files");
    ObSQLiteConnectionGuard second(&pool);
    require(second.is_valid(), "second connection");
    bool found = false;
    require(second->query("SELECT v FROM t", nullptr, [&](ObSQLiteRowReader &row) {
      found = row.get_int64() == 533; return OB_SUCCESS;
    }) == OB_SUCCESS && found, "second connection read");
  }
  {
    ObSQLiteConnectionGuard reopened(&pool);
    require(reopened.is_valid(), "reopen");
    bool found = false;
    require(reopened->query("SELECT v FROM t", nullptr, [&](ObSQLiteRowReader &row) {
      found = row.get_int64() == 533; return OB_SUCCESS;
    }) == OB_SUCCESS && found, "persisted data");
  }
  pool.destroy();
  require(std::filesystem::exists(file), "database location");
  std::cout << "SQLITE_POOL_PASS units=" << directory.size() << std::endl;
}
// Fixed concurrency workload through the production pool: each guard creates
// and closes a connection, including WAL configuration on acquisition.
void concurrent_pool(const std::wstring &directory)
{
  std::filesystem::create_directories(L"\\\\?\\" + directory);
  ObSQLiteConnectionPool pool;
  const std::string path = utf8(directory + L"\\meta.db");
  require(pool.init(path.c_str()) == OB_SUCCESS, "concurrent pool init");
  {
    ObSQLiteConnectionGuard setup(&pool);
    require(setup.is_valid(), "concurrent setup connection");
    require(setup->execute("CREATE TABLE t(id INTEGER PRIMARY KEY, v INTEGER)") == OB_SUCCESS,
        "concurrent table");
  }
  DWORD handles_before = 0;
  require(GetProcessHandleCount(GetCurrentProcess(), &handles_before) != 0, "initial handle count");
  const auto started = std::chrono::steady_clock::now();
  std::atomic<int> ready{0};
  std::atomic<int> failures{0};
  std::vector<std::thread> threads;
  for (int worker = 0; worker < 8; ++worker) {
    threads.emplace_back([&, worker]() {
      ++ready;
      while (ready.load() != 8) { std::this_thread::yield(); }
      for (int iteration = 0; iteration < 16; ++iteration) {
        ObSQLiteConnectionGuard connection(&pool);
        const std::string sql = "INSERT INTO t VALUES(" + std::to_string(worker * 16 + iteration) + ",533)";
        if (!connection.is_valid() || connection->execute(sql.c_str()) != OB_SUCCESS) { ++failures; }
      }
    });
  }
  for (auto &thread : threads) { thread.join(); }
  require(failures.load() == 0, "concurrent connection or write failure");
  {
    ObSQLiteConnectionGuard reopened(&pool);
    require(reopened.is_valid(), "concurrent reopen");
    int rows = 0;
    bool matching = true;
    require(reopened->query("SELECT id,v FROM t ORDER BY id", nullptr, [&](ObSQLiteRowReader &row) {
      matching = matching && row.get_int64() == rows;
      matching = matching && row.get_int64() == 533;
      ++rows;
      return OB_SUCCESS;
    }) == OB_SUCCESS && matching && rows == 128, "concurrent persisted contents");
  }
  pool.destroy();
  DWORD handles_after = 0;
  require(GetProcessHandleCount(GetCurrentProcess(), &handles_after) != 0, "final handle count");
  const auto elapsed_us = std::chrono::duration_cast<std::chrono::microseconds>(
      std::chrono::steady_clock::now() - started).count();
  std::cout << "SQLITE_POOL_CONCURRENT_PASS workers=8 iterations=16 rows=128 units="
      << directory.size() << std::endl;
  // Record the fixed workload, including thread creation and reopen/readback.
  // One-time runtime initialization may allocate handles: retain every round's
  // before/after counts rather than hide a delta behind an arbitrary tolerance.
  std::cout << "SQLITE_POOL_RESOURCES units=" << directory.size()
      << " handles_before=" << handles_before << " handles_after=" << handles_after
      << " elapsed_us=" << elapsed_us << std::endl;
}
void rejected_paths(const std::wstring &root)
{
  ObSQLiteConnection connection;
  require(connection.init("\xff") == OB_INVALID_ARGUMENT && !connection.is_valid(), "invalid UTF8 rejected");
  const auto u = seekdb_phase0::directory_at_length(
      std::u16string(root.begin(), root.end()) + u"\\too-long", 4085, true);
  const std::wstring directory(u.begin(), u.end());
  const std::string oversized = utf8(directory + L"\\meta.db");
  require(connection.init(oversized.c_str()) == OB_SIZE_OVERFLOW && !connection.is_valid(),
      "side file length rejected");
  require(!std::filesystem::exists(L"\\\\?\\" + directory), "rejection created directories");
  const std::string missing = utf8(root + L"\\missing\\meta.db");
  require(connection.init(missing.c_str()) != OB_SUCCESS && !connection.is_valid(), "missing parent rejected");
  const std::string valid = utf8(root + L"\\retry.db");
  require(connection.init(valid.c_str()) == OB_SUCCESS, "connection reusable after open failure");
  connection.close();
  std::cout << "SQLITE_POOL_REJECT_PASS cases=3 retry=1" << std::endl;
}
}
int main()
{
  try {
    const std::wstring root = std::filesystem::current_path().wstring()
        + L"\\sqlite-pool-test-" + std::to_wstring(GetCurrentProcessId());
    require(!std::filesystem::exists(root), "empty test root");
    for (size_t units : {size_t(251), size_t(252), size_t(253), size_t(280), size_t(600), size_t(1200), size_t(2048), size_t(4084)}) {
      auto dir = seekdb_phase0::directory_at_length(std::u16string(root.begin(), root.end()), units, true);
      exercise(std::wstring(dir.begin(), dir.end()));
    }
    for (size_t units : {size_t(160), size_t(2048)}) {
      for (int round = 0; round < 3; ++round) {
        const auto prefix = root + L"\\concurrent-" + std::to_wstring(units) + L"-" + std::to_wstring(round);
        const auto dir = seekdb_phase0::directory_at_length(std::u16string(prefix.begin(), prefix.end()), units, true);
        concurrent_pool(std::wstring(dir.begin(), dir.end()));
      }
    }
    rejected_paths(root);
    std::filesystem::remove_all(L"\\\\?\\" + root);
    require(!std::filesystem::exists(root), "cleanup");
    return 0;
  } catch (const std::exception &e) {
    std::cerr << "SQLITE_POOL_FAIL " << e.what() << std::endl;
    return 1;
  }
}
