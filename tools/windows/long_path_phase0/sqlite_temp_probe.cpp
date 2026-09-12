// Copyright (c) 2026 OceanBase. Licensed under the Apache License, Version 2.0.
// Observe the selected product VFS without changing its paths or temp policy.
#include <windows.h>
#include <sqlite3.h>
#include "path_context.h"
#include "path_fixture.h"
#include <cstdio>
#include <cstring>
#include <stdexcept>
#include <string>
#include <vector>
#include <cwchar>

using namespace seekdb_phase0;
static void require(bool ok, const std::string &what)
{
  if (!ok) throw std::runtime_error(what);
}
static std::string utf8(const std::wstring &s)
{
  std::string out;
  require(bool(path_to_utf8(s, out)), "UTF-8 conversion");
  return out;
}
static std::wstring current_dir()
{
  DWORD n = GetCurrentDirectoryW(0, nullptr);
  require(n != 0, "cwd size");
  std::vector<wchar_t> b(n);
  DWORD got = GetCurrentDirectoryW(n, b.data());
  require(got > 0 && got < n, "cwd read");
  return {b.data(), got};
}
static void mkdirs(const std::wstring &p)
{
  for (size_t i = 3; i <= p.size(); ++i) {
    if (i == p.size() || p[i] == L'\\') {
      if (!CreateDirectoryW(extended_path(p.substr(0, i)).c_str(), nullptr)) {
        require(GetLastError() == ERROR_ALREADY_EXISTS, "create test directory");
      }
    }
  }
}
static void remove_tree(const std::wstring &p)
{
  WIN32_FIND_DATAW data{};
  HANDLE h = FindFirstFileW((extended_path(p) + L"\\*").c_str(), &data);
  require(h != INVALID_HANDLE_VALUE, "enumerate test root");
  do {
    if (wcscmp(data.cFileName, L".") == 0 || wcscmp(data.cFileName, L"..") == 0) continue;
    const auto child = p + L"\\" + data.cFileName;
    if (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) remove_tree(child);
    else require(DeleteFileW(extended_path(child).c_str()) != 0, "delete test file");
  } while (FindNextFileW(h, &data));
  DWORD error = GetLastError();
  FindClose(h);
  require(error == ERROR_NO_MORE_FILES, "enumeration error");
  require(RemoveDirectoryW(extended_path(p).c_str()) != 0, "remove test directory");
}
static std::wstring ordinary(std::wstring p)
{
  if (p.compare(0, 4, L"\\\\?\\") == 0) p.erase(0, 4);
  while (p.size() > 3 && (p.back() == L'\\' || p.back() == L'/')) p.pop_back();
  return p;
}

// Fixed observer storage avoids throwing through SQLite's C callbacks.
struct TempRecord {
  HANDLE handle;
  wchar_t path[8192];
  DWORD attributes;
  sqlite3_int64 bytes;
  bool closed;
};
static SRWLOCK record_lock = SRWLOCK_INIT;
static TempRecord records[128]{};
static size_t record_count = 0;
static bool observer_error = false;
using CreateFileFn = HANDLE (WINAPI *)(LPCWSTR, DWORD, DWORD, LPSECURITY_ATTRIBUTES, DWORD, DWORD, HANDLE);
using CloseHandleFn = BOOL (WINAPI *)(HANDLE);
static CreateFileFn real_create = nullptr;
static CloseHandleFn real_close = nullptr;

static HANDLE WINAPI observe_create(LPCWSTR path, DWORD access, DWORD sharing,
    LPSECURITY_ATTRIBUTES security, DWORD creation, DWORD attributes, HANDLE templ)
{
  HANDLE h = real_create(path, access, sharing, security, creation, attributes, templ);
  const DWORD error = GetLastError();
  if ((attributes & FILE_FLAG_DELETE_ON_CLOSE) && h != INVALID_HANDLE_VALUE) {
    AcquireSRWLockExclusive(&record_lock);
    size_t len = path ? wcslen(path) : 0;
    if (record_count >= 128 || len == 0 || len >= 8192) {
      observer_error = true;
    } else {
      TempRecord &r = records[record_count++];
      r.handle = h; r.attributes = attributes; r.bytes = -1; r.closed = false;
      memcpy(r.path, path, (len + 1) * sizeof(wchar_t));
    }
    ReleaseSRWLockExclusive(&record_lock);
  }
  SetLastError(error);
  return h;
}
static BOOL WINAPI observe_close(HANDLE h)
{
  const DWORD incoming = GetLastError();
  LARGE_INTEGER size{};
  const bool sized = GetFileSizeEx(h, &size) != 0;
  SetLastError(incoming);
  const BOOL ok = real_close(h);
  const DWORD error = GetLastError();
  AcquireSRWLockExclusive(&record_lock);
  for (size_t i = 0; i < record_count; ++i) {
    if (records[i].handle == h && !records[i].closed) {
      records[i].closed = ok != 0;
      records[i].bytes = sized ? size.QuadPart : -1;
    }
  }
  ReleaseSRWLockExclusive(&record_lock);
  SetLastError(error);
  return ok;
}
class ObserveCalls {
public:
  sqlite3_vfs *vfs;
  ObserveCalls() : vfs(sqlite3_vfs_find("win32-longpath"))
  {
    require(vfs && vfs->iVersion >= 3 && vfs->xGetSystemCall && vfs->xSetSystemCall,
            "product VFS system call observation unavailable");
    real_create = reinterpret_cast<CreateFileFn>(vfs->xGetSystemCall(vfs, "CreateFileW"));
    real_close = reinterpret_cast<CloseHandleFn>(vfs->xGetSystemCall(vfs, "CloseHandle"));
    require(real_create && real_close, "missing original W calls");
    require(vfs->xSetSystemCall(vfs, "CreateFileW",
      reinterpret_cast<sqlite3_syscall_ptr>(observe_create)) == SQLITE_OK, "observe CreateFileW");
    if (vfs->xSetSystemCall(vfs, "CloseHandle",
        reinterpret_cast<sqlite3_syscall_ptr>(observe_close)) != SQLITE_OK) {
      vfs->xSetSystemCall(vfs, "CreateFileW", reinterpret_cast<sqlite3_syscall_ptr>(real_create));
      throw std::runtime_error("observe CloseHandle");
    }
  }
  ~ObserveCalls()
  {
    restore();
  }
  bool restore()
  {
    const int close_rc = vfs->xSetSystemCall(vfs, "CloseHandle", reinterpret_cast<sqlite3_syscall_ptr>(real_close));
    const int create_rc = vfs->xSetSystemCall(vfs, "CreateFileW", reinterpret_cast<sqlite3_syscall_ptr>(real_create));
    return close_rc == SQLITE_OK && create_rc == SQLITE_OK &&
      vfs->xGetSystemCall(vfs, "CreateFileW") == reinterpret_cast<sqlite3_syscall_ptr>(real_create) &&
      vfs->xGetSystemCall(vfs, "CloseHandle") == reinterpret_cast<sqlite3_syscall_ptr>(real_close);
  }
};
class Db {
public:
  sqlite3 *db = nullptr;
  explicit Db(const std::wstring &path)
  {
    int rc = sqlite3_open_v2(utf8(extended_path(path)).c_str(), &db,
        SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, "win32-longpath");
    if (rc != SQLITE_OK) {
      std::string msg = "open rc=" + std::to_string(rc);
      if (db) { msg += " " + std::string(sqlite3_errmsg(db)); sqlite3_close(db); db = nullptr; }
      throw std::runtime_error(msg);
    }
  }
  ~Db() { if (db) sqlite3_close_v2(db); }
  void exec(const char *sql)
  {
    const int rc = sqlite3_exec(db, sql, nullptr, nullptr, nullptr);
    require(rc == SQLITE_OK, "SQL rc=" + std::to_string(rc) + " ext=" +
      std::to_string(sqlite3_extended_errcode(db)) + " system=" +
      std::to_string(sqlite3_system_errno(db)) + " " + sqlite3_errmsg(db));
  }
  std::string scalar(const char *sql)
  {
    sqlite3_stmt *st = nullptr;
    int rc = sqlite3_prepare_v2(db, sql, -1, &st, nullptr);
    require(rc == SQLITE_OK, "scalar prepare");
    rc = sqlite3_step(st);
    const auto *value = rc == SQLITE_ROW ? sqlite3_column_text(st, 0) : nullptr;
    std::string text = value ? reinterpret_cast<const char *>(value) : "";
    int final = sqlite3_finalize(st);
    require(rc == SQLITE_ROW && final == SQLITE_OK, "scalar step");
    return text;
  }
  void close() { require(sqlite3_close(db) == SQLITE_OK, "close db"); db = nullptr; }
};
static size_t verify_records(size_t begin, const std::wstring &temp, const char *query)
{
  require(!observer_error, "observer overflow");
  require(record_count > begin, std::string("no actual disk temp file for ") + query);
  bool wrote = false;
  for (size_t i = begin; i < record_count; ++i) {
    const auto &r = records[i];
    const auto path = ordinary(r.path);
    const auto parent = path.substr(0, path.find_last_of(L"\\/"));
    require(_wcsicmp(parent.c_str(), temp.c_str()) == 0, "temp root differs from OS default");
    require(r.closed, "temporary file handle not closed");
    const DWORD attr = GetFileAttributesW(r.path);
    const DWORD error = GetLastError();
    require(attr == INVALID_FILE_ATTRIBUTES && error == ERROR_FILE_NOT_FOUND,
            "temporary file not deleted after close");
    wrote |= r.bytes > 0;
    std::printf("SQLITE_DISK_TEMP query=%s bytes=%lld closed=1 deleted=1 units=%zu path=%s\n",
      query, static_cast<long long>(r.bytes), path.size(), utf8(path).c_str());
  }
  require(wrote, "temporary handles created but no disk bytes observed");
  return record_count - begin;
}
static void run_query(Db &db, const char *sql, const char *kind, const std::wstring &temp)
{
  const std::string explain = std::string("EXPLAIN QUERY PLAN ") + sql;
  sqlite3_stmt *st = nullptr;
  require(sqlite3_prepare_v2(db.db, explain.c_str(), -1, &st, nullptr) == SQLITE_OK, "explain prepare");
  bool uses_sort = false;
  int rc;
  while ((rc = sqlite3_step(st)) == SQLITE_ROW) {
    const auto *text = reinterpret_cast<const char *>(sqlite3_column_text(st, 3));
    if (text) { std::printf("SQLITE_PLAN query=%s detail=%s\n", kind, text);
      uses_sort |= std::string(text).find("USE TEMP B-TREE") != std::string::npos; }
  }
  int final = sqlite3_finalize(st);
  require(rc == SQLITE_DONE && final == SQLITE_OK && uses_sort, "missing temp sort plan");
  const size_t begin = record_count;
  require(sqlite3_prepare_v2(db.db, sql, -1, &st, nullptr) == SQLITE_OK, "query prepare");
  int rows = 0;
  bool valid = true;
  while ((rc = sqlite3_step(st)) == SQLITE_ROW) {
    if (std::string(kind) == "order") valid &= sqlite3_column_int(st, 0) == 98304 - rows;
    else valid &= sqlite3_column_int(st, 1) == 1;
    ++rows;
  }
  int sorts = sqlite3_stmt_status(st, SQLITE_STMTSTATUS_SORT, 0);
  final = sqlite3_finalize(st);
  require(rc == SQLITE_DONE && final == SQLITE_OK && valid && rows == 98304 && sorts > 0,
      std::string("sort result ") + kind + " rc=" + std::to_string(rc) + " rows=" + std::to_string(rows));
  size_t files = verify_records(begin, temp, kind);
  std::printf("SQLITE_SORT_PASS query=%s rows=%d sorts=%d disk_files=%zu\n", kind, rows, sorts, files);
}
int wmain(int argc, wchar_t **argv)
{
  try {
    require(argc == 3, "usage: sqlite_temp_probe <empty-test-root> <policy>");
    const std::wstring root = argv[1], original = current_dir();
    WIN32_FIND_DATAW entry{};
    HANDLE scan = FindFirstFileW((extended_path(root) + L"\\*").c_str(), &entry);
    require(scan != INVALID_HANDLE_VALUE, "test root missing");
    bool empty = true;
    do { empty &= wcscmp(entry.cFileName,L".")==0 || wcscmp(entry.cFileName,L"..")==0; }
    while (FindNextFileW(scan, &entry));
    FindClose(scan);
    require(empty, "test root is not empty");
    require(std::string(sqlite3_sourceid()) == SQLITE_SOURCE_ID &&
        std::string(sqlite3_libversion()) == "3.51.2", "SQLite source/header identity");
    require(sqlite3_compileoption_used("TEMP_STORE=1") != 0, "product TEMP_STORE drift");
    require(sqlite3_temp_directory == nullptr, "global SQLite temp directory override");
    wchar_t temp_buf[32768];
    DWORD n = GetTempPathW(32768, temp_buf);
    require(n > 0 && n < 32768, "OS temp path");
    const auto temp = ordinary(temp_buf);
    std::printf("SQLITE_TEMP_RUNTIME version=%s source_id=%s policy=%s cwd=%s os_temp=%s\n",
      sqlite3_libversion(), sqlite3_sourceid(), utf8(argv[2]).c_str(),
      utf8(original).c_str(), utf8(temp).c_str());
    ObserveCalls observe;
    for (bool unicode : {false, true}) {
      std::wstring base = root + (unicode ? L"\\unicode" : L"\\short");
      if (unicode) {
        auto u = directory_at_length(std::u16string(base.begin(), base.end()), 2048, true);
        base.assign(u.begin(), u.end());
      }
      mkdirs(base + L"\\store\\sstable");
      const auto path = base + L"\\store\\sstable\\meta.db";
      {
        Db db(path);
        sqlite3_vfs *actual = nullptr;
        require(sqlite3_file_control(db.db, "main", SQLITE_FCNTL_VFS_POINTER, &actual) == SQLITE_OK &&
          actual == observe.vfs, "selected VFS changed");
        require(db.scalar("PRAGMA temp_store") == "0", "temp_store was changed");
        require(db.scalar("PRAGMA journal_mode=WAL") == "wal", "WAL");
        db.exec("PRAGMA synchronous=NORMAL; PRAGMA busy_timeout=5000; PRAGMA cache_size=-65536;");
        db.exec("CREATE TABLE sample(id INTEGER PRIMARY KEY, payload TEXT);"
          "WITH RECURSIVE n(x) AS (VALUES(1) UNION ALL SELECT x+1 FROM n WHERE x<98304)"
          " INSERT INTO sample SELECT x,printf('%08d',98304-x)||hex(zeroblob(512)) FROM n;");
        db.exec("PRAGMA wal_checkpoint(TRUNCATE)");
        run_query(db, "SELECT id,payload FROM sample ORDER BY payload", "order", temp);
        run_query(db, "SELECT payload,count(*) FROM sample GROUP BY payload", "group", temp);
        require(db.scalar("PRAGMA temp_store") == "0", "temp_store drift");
        require(std::string(sqlite3_vfs_find(nullptr)->zName) == "win32", "default VFS changed");
        db.close();
      }
      {
        Db reopened(path);
        require(reopened.scalar("SELECT count(*) FROM sample") == "98304", "reopen data");
        reopened.close();
      }
      require(current_dir() == original, "cwd changed");
      std::printf("SQLITE_TEMP_CASE_PASS unicode=%d base_units=%zu db_units=%zu\n",
        unicode, base.size(), path.size());
      remove_tree(root);
      require(CreateDirectoryW(extended_path(root).c_str(), nullptr) != 0, "recreate empty test root");
    }
    require(!observer_error && sqlite3_temp_directory == nullptr, "observer/temp settings changed");
    require(current_dir() == original, "final cwd changed");
    require(RemoveDirectoryW(extended_path(root).c_str()) != 0, "final root cleanup");
    require(observe.restore(), "restore original VFS system calls");
    std::puts("SQLITE_TEMP_COMPONENT_PASS");
    return 0;
  } catch (const std::exception &e) {
    std::fprintf(stderr, "SQLITE_TEMP_FAIL %s\n", e.what());
    return 1;
  }
}
