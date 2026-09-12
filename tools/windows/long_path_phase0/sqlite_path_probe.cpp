/*
 * Copyright (c) 2025 OceanBase.
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *     http://www.apache.org/licenses/LICENSE-2.0
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
// Standalone Phase 0 probe. This does not change or link the seekdb file layer.
#include <windows.h>
#include <fcntl.h>
#include <io.h>
#include <sys/stat.h>
#include <sqlite3.h>
#include "path_fixture.h"
#include <cerrno>
#include <iostream>
#include <vector>

static_assert(sizeof(wchar_t) == sizeof(char16_t), "Windows UTF-16 required");
static constexpr const char *CANDIDATE_VFS = "win32-longpath";

static void check(bool ok, const std::string &stage)
{
  if (!ok) throw std::runtime_error(stage);
}

static void win_check(bool ok, const char *stage)
{
  if (!ok) {
    const DWORD error = GetLastError();
    throw std::runtime_error(std::string(stage) + " win32=" + std::to_string(error));
  }
}

static std::string utf8(const std::wstring &value)
{
  const int count = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value.data(),
      static_cast<int>(value.size()), nullptr, 0, nullptr, nullptr);
  win_check(count > 0, "utf8 size");
  std::string result(count, '\0');
  win_check(WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value.data(),
      static_cast<int>(value.size()), &result[0], count, nullptr, nullptr) == count, "utf8 encode");
  return result;
}

static std::wstring full_path(const std::wstring &input)
{
  DWORD capacity = GetFullPathNameW(input.c_str(), 0, nullptr, nullptr);
  win_check(capacity != 0, "full path size");
  for (;;) {
    std::vector<wchar_t> buffer(capacity);
    const DWORD count = GetFullPathNameW(input.c_str(), capacity, buffer.data(), nullptr);
    win_check(count != 0, "full path");
    if (count < capacity) return std::wstring(buffer.data(), count);
    capacity = count + 1;
  }
}

static std::wstring cwd()
{
  const DWORD capacity = GetCurrentDirectoryW(0, nullptr);
  win_check(capacity != 0, "cwd size");
  std::vector<wchar_t> buffer(capacity);
  const DWORD count = GetCurrentDirectoryW(capacity, buffer.data());
  win_check(count != 0 && count < capacity, "cwd");
  return std::wstring(buffer.data(), count);
}

static std::wstring module_path(HMODULE module)
{
  for (DWORD capacity = 256; capacity <= 32768; capacity *= 2) {
    std::vector<wchar_t> buffer(capacity);
    const DWORD count = GetModuleFileNameW(module, buffer.data(), capacity);
    win_check(count != 0, "module path");
    if (count < capacity) return std::wstring(buffer.data(), count);
  }
  throw std::runtime_error("module path exceeds Windows limit");
}

static std::wstring extended(const std::wstring &path) { return L"\\\\?\\" + path; }

static void remove_file(const std::wstring &path)
{
  if (!DeleteFileW(extended(path).c_str())) {
    const DWORD error = GetLastError();
    check(error == ERROR_FILE_NOT_FOUND, "cleanup file win32=" + std::to_string(error));
  }
}

static void require_file(const std::wstring &path)
{
  const DWORD attributes = GetFileAttributesW(extended(path).c_str());
  win_check(attributes != INVALID_FILE_ATTRIBUTES, "expected file");
  check((attributes & FILE_ATTRIBUTE_DIRECTORY) == 0, "expected regular file");
}

static void require_absent(const std::wstring &path)
{
  const DWORD attributes = GetFileAttributesW(extended(path).c_str());
  const DWORD error = GetLastError();
  check(attributes == INVALID_FILE_ATTRIBUTES && error == ERROR_FILE_NOT_FOUND,
        "unexpected file in launch cwd or incomplete cleanup");
}

static void make_dirs(const std::wstring &path, size_t start, std::vector<std::wstring> &owned)
{
  size_t end = start;
  while (end < path.size()) {
    end = path.find(L'\\', end + 1);
    if (end == std::wstring::npos) end = path.size();
    const std::wstring directory = path.substr(0, end);
    // No EEXIST fallback: this probe must own every directory it removes.
    win_check(CreateDirectoryW(extended(directory).c_str(), nullptr) != 0, "create directory");
    owned.push_back(directory);
  }
}

static std::wstring quoted(const std::wstring &arg)
{
  std::wstring result = L"\"";
  size_t slashes = 0;
  for (wchar_t ch : arg) {
    if (ch == L'\\') {
      ++slashes;
    } else {
      result.append(ch == L'"' ? 2 * slashes + 1 : slashes, L'\\');
      result += ch;
      slashes = 0;
    }
  }
  result.append(2 * slashes, L'\\');
  return result + L'"';
}

static void run_lock_child(const std::wstring &path, const std::wstring &original, bool locked)
{
  check(cwd() == original, "parent cwd changed before child");
  const std::wstring exe = module_path(nullptr);
  std::wstring command = quoted(exe) + L" --lock-child " + quoted(path) + L" " +
      quoted(original) + (locked ? L" locked" : L" released");
  STARTUPINFOW startup = {};
  startup.cb = sizeof(startup);
  PROCESS_INFORMATION child = {};
  win_check(CreateProcessW(exe.c_str(), &command[0], nullptr, nullptr, FALSE,
      CREATE_NO_WINDOW, nullptr, original.c_str(), &startup, &child) != 0, "create lock child");
  CloseHandle(child.hThread);
  const DWORD wait = WaitForSingleObject(child.hProcess, 30000);
  if (wait == WAIT_TIMEOUT) {
    // Only terminate this probe's owned test child; never a database service.
    TerminateProcess(child.hProcess, 124);
    WaitForSingleObject(child.hProcess, 5000);
  }
  DWORD code = 1;
  const BOOL got_code = GetExitCodeProcess(child.hProcess, &code);
  std::cout << "lock_child_pid=" << child.dwProcessId << " locked=" << locked
            << " wait=" << wait << " exit=" << code << '\n';
  CloseHandle(child.hProcess);
  check(wait == WAIT_OBJECT_0 && got_code && code == 0, "cross-process lock check failed/timeout");
  check(cwd() == original, "parent cwd changed after child");
}

class Database {
public:
  sqlite3 *db = nullptr;
  Database() = default;
  Database(const Database &) = delete;
  Database &operator=(const Database &) = delete;
  ~Database() { if (db != nullptr) sqlite3_close_v2(db); }
  void open(const std::string &path, const char *vfs = CANDIDATE_VFS)
  {
    const int rc = sqlite3_open_v2(path.c_str(), &db,
        SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, vfs);
    if (rc != SQLITE_OK) {
      throw std::runtime_error("sqlite open rc=" + std::to_string(rc) +
          " input_utf8_bytes=" + std::to_string(path.size()) +
          (db == nullptr ? " no handle" :
           " extended_rc=" + std::to_string(sqlite3_extended_errcode(db)) +
           " sqlite_system_errno=" + std::to_string(sqlite3_system_errno(db)) +
           " " + sqlite3_errmsg(db)));
    }
    sqlite3_vfs *actual_vfs = nullptr;
    check(sqlite3_file_control(db, "main", SQLITE_FCNTL_VFS_POINTER, &actual_vfs) == SQLITE_OK &&
        actual_vfs != nullptr && std::string(actual_vfs->zName) == vfs, "connection VFS mismatch");
    check(sqlite3_busy_timeout(db, 100) == SQLITE_OK, "busy timeout configuration");
    std::cout << "connection_vfs=" << actual_vfs->zName
              << " sqlite_filename=" << sqlite3_db_filename(db, "main") << '\n';
  }
  void close()
  {
    const int rc = sqlite3_close(db);
    check(rc == SQLITE_OK, "sqlite close rc=" + std::to_string(rc));
    db = nullptr;
  }
  void exec(const char *sql)
  {
    const int rc = sqlite3_exec(db, sql, nullptr, nullptr, nullptr);
    check(rc == SQLITE_OK, "sqlite exec rc=" + std::to_string(rc) + " " + sqlite3_errmsg(db));
  }
  std::string scalar(const char *sql)
  {
    sqlite3_stmt *statement = nullptr;
    int rc = sqlite3_prepare_v2(db, sql, -1, &statement, nullptr);
    if (rc != SQLITE_OK) {
      sqlite3_finalize(statement);
      throw std::runtime_error("sqlite prepare rc=" + std::to_string(rc));
    }
    rc = sqlite3_step(statement);
    std::string result;
    if (rc == SQLITE_ROW && sqlite3_column_text(statement, 0) != nullptr) {
      result = reinterpret_cast<const char *>(sqlite3_column_text(statement, 0));
    }
    const int final_rc = sqlite3_finalize(statement);
    check(rc == SQLITE_ROW && final_rc == SQLITE_OK,
          std::string("sqlite scalar sql=") + sql + " step_rc=" + std::to_string(rc) +
          " finalize_rc=" + std::to_string(final_rc) +
          " extended_rc=" + std::to_string(sqlite3_extended_errcode(db)) +
          " sqlite_system_errno=" + std::to_string(sqlite3_system_errno(db)) +
          " " + sqlite3_errmsg(db));
    return result;
  }
};

static void roundtrip(const std::wstring &path, bool use_extended, const std::wstring &original,
                      bool legacy_writer = false)
{
  const std::string native = utf8(extended(path));
  const std::string input = use_extended ? native : utf8(path);
  Database writer;
  writer.open(input, legacy_writer ? "win32" : CANDIDATE_VFS);
  check(writer.scalar("PRAGMA journal_mode=WAL") == "wal", "WAL was not enabled");
  writer.exec("PRAGMA synchronous=NORMAL");
  check(writer.scalar("PRAGMA synchronous") == "1", "synchronous is not NORMAL");
  writer.exec("CREATE TABLE probe(id INTEGER PRIMARY KEY, value TEXT NOT NULL);"
              "BEGIN; INSERT INTO probe VALUES(7,'seek533'); COMMIT;");
  // Open the expected physical path independently. This detects a wrong VFS target.
  Database reader;
  reader.open(native);
  check(reader.scalar("SELECT value FROM probe WHERE id=7") == "seek533", "second connection mismatch");
  require_file(path);
  require_file(path + L"-wal");
  require_file(path + L"-shm");
  check(reader.scalar("PRAGMA journal_mode") == "wal", "reader mode mismatch");
  writer.exec("BEGIN IMMEDIATE");
  const int lock_rc = sqlite3_exec(reader.db, "BEGIN IMMEDIATE", nullptr, nullptr, nullptr);
  check(lock_rc == SQLITE_BUSY, "second connection did not observe writer lock rc=" +
        std::to_string(lock_rc));
  run_lock_child(path, original, true);
  writer.exec("ROLLBACK");
  run_lock_child(path, original, false);
  check(reader.scalar("SELECT value FROM probe WHERE id=8") == "child", "child commit mismatch");
  reader.close();
  check(writer.scalar("PRAGMA wal_checkpoint(TRUNCATE)") == "0", "checkpoint busy");
  writer.close();
  writer.open(input);
  check(writer.scalar("PRAGMA journal_mode") == "wal", "reopened mode mismatch");
  check(writer.scalar("SELECT count(*) FROM probe WHERE id=7 AND value='seek533'") == "1",
        "reopened data mismatch");
  writer.close();
  require_absent(path + L"-wal");
  require_absent(path + L"-shm");
  check(cwd() == original, "SQLite changed launch cwd");
}

static void identify_vendor()
{
  check(sqlite3_libversion_number() == SQLITE_VERSION_NUMBER, "SQLite header/library version mismatch");
  check(std::string(sqlite3_sourceid()) == SQLITE_SOURCE_ID, "SQLite header/library source mismatch");
  sqlite3_vfs *vfs = sqlite3_vfs_find(nullptr);
  check(vfs != nullptr, "no default VFS");
  HMODULE module = nullptr;
  win_check(GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
      GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
      reinterpret_cast<LPCWSTR>(&sqlite3_libversion), &module) != 0, "SQLite module");
  std::cout << "sqlite_version=" << sqlite3_libversion() << "\nsource_id=" << sqlite3_sourceid()
            << "\nheader_source_id=" << SQLITE_SOURCE_ID << "\nvfs=" << vfs->zName
            << "\nmxPathname=" << vfs->mxPathname << "\nsqlite_module=" << utf8(module_path(module)) << '\n';
  // Keep the default unchanged. Only individual connections select the candidate.
  sqlite3_vfs *candidate = sqlite3_vfs_find(CANDIDATE_VFS);
  check(candidate != nullptr && candidate->mxPathname >= 4 * 4096 + 16,
        "candidate VFS missing or insufficient UTF-8 capacity");
  std::cout << "selected_vfs=" << candidate->zName
            << " candidate_mxPathname=" << candidate->mxPathname << '\n';
  for (const sqlite3_vfs *entry = vfs; entry != nullptr; entry = entry->pNext) {
    std::cout << "registered_vfs=" << entry->zName
              << " mxPathname=" << entry->mxPathname << '\n';
  }
  for (int i = 0; sqlite3_compileoption_get(i) != nullptr; ++i) {
    std::cout << "sqlite_option=" << sqlite3_compileoption_get(i) << '\n';
  }
}

int wmain(int argc, wchar_t **argv)
{
  std::cout << std::unitbuf;
  std::wstring run;
  try {
    if (argc == 5 && std::wstring(argv[1]) == L"--lock-child") {
      check(cwd() == argv[3], "lock child cwd mismatch");
      Database child;
      child.open(utf8(extended(argv[2])));
      check(child.scalar("SELECT value FROM probe WHERE id=7") == "seek533", "child read mismatch");
      const int rc = sqlite3_exec(child.db, "BEGIN IMMEDIATE", nullptr, nullptr, nullptr);
      if (std::wstring(argv[4]) == L"locked") {
        check(rc == SQLITE_BUSY, "child did not observe writer lock rc=" + std::to_string(rc));
      } else {
        check(std::wstring(argv[4]) == L"released" && rc == SQLITE_OK,
              "child cannot write after lock release rc=" + std::to_string(rc));
        child.exec("INSERT INTO probe VALUES(8,'child'); COMMIT");
      }
      child.close();
      return 0;
    }
    check(argc == 3 && (std::wstring(argv[2]) == L"0" || std::wstring(argv[2]) == L"1"),
          "usage: sqlite_path_probe.exe EXISTING_SHORT_TEST_ROOT EXPECTED_POLICY_0_OR_1");
    using VersionFunction = LONG (WINAPI *)(OSVERSIONINFOW *);
    const auto version_function = reinterpret_cast<VersionFunction>(
        GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "RtlGetVersion"));
    OSVERSIONINFOW version = {};
    version.dwOSVersionInfoSize = sizeof(version);
    check(version_function != nullptr && version_function(&version) == 0,
          "cannot identify Windows version");
    check(version.dwMajorVersion == 10 && version.dwBuildNumber >= 22000, "Windows 11 required");
    std::cout << "windows_build=" << version.dwBuildNumber << "\ncompiler=" << __clang_version__ << '\n';
    DWORD policy = 0, bytes = sizeof(policy);
    const LSTATUS status = RegGetValueW(HKEY_LOCAL_MACHINE,
        L"SYSTEM\\CurrentControlSet\\Control\\FileSystem", L"LongPathsEnabled",
        RRF_RT_REG_DWORD, nullptr, &policy, &bytes);
    check(status == ERROR_SUCCESS, "cannot read LongPathsEnabled; no assumed policy");
    check(policy == static_cast<DWORD>(argv[2][0] - L'0'), "unexpected LongPathsEnabled");
    std::cout << "LongPathsEnabled=" << policy << "\nparent_pid=" << GetCurrentProcessId() << '\n';
    identify_vendor();
    const std::wstring original = cwd();
    check(original.size() < 240, "launch from a short working directory");
    std::wstring root = full_path(argv[1]);
    check(root.size() >= 3 && root.size() <= 100 && root[1] == L':' && root[2] == L'\\',
          "test root must be a short local drive absolute path");
    if (root.back() == L'\\') root.pop_back();
    // Refuse reparse points in the chosen root's ancestry, before creating anything.
    size_t end = 2;
    for (;;) {
      end = root.find(L'\\', end + 1);
      if (end == std::wstring::npos) end = root.size();
      const DWORD attrs = GetFileAttributesW((root.substr(0, end) + L"\\").c_str());
      win_check(attrs != INVALID_FILE_ATTRIBUTES, "test root attributes");
      check((attrs & FILE_ATTRIBUTE_DIRECTORY) && !(attrs & FILE_ATTRIBUTE_REPARSE_POINT),
            "test root must be an existing directory without reparse points");
      if (end == root.size()) break;
    }
    wchar_t volume[MAX_PATH] = {}, filesystem[32] = {};
    DWORD component_limit = 0;
    win_check(GetVolumePathNameW(root.c_str(), volume, MAX_PATH) != 0, "volume path");
    win_check(GetVolumeInformationW(volume, nullptr, 0, nullptr, &component_limit, nullptr,
        filesystem, 32) != 0, "volume information");
    check(_wcsicmp(filesystem, L"NTFS") == 0 && component_limit >= 100, "NTFS required");
    std::cout << "filesystem=NTFS\ncomponent_limit=" << component_limit << '\n';
    run = root + L"\\seek533-" + std::to_wstring(GetCurrentProcessId()) + L"-" + std::to_wstring(GetTickCount64());
    win_check(CreateDirectoryW(run.c_str(), nullptr) != 0, "exclusive run directory");
    std::cout << "run_directory=" << utf8(run) << '\n';
    const std::wstring compatibility_path = run + L"\\short-compat.db";
    roundtrip(compatibility_path, false, original, true);
    remove_file(compatibility_path);
    std::cout << "DEFAULT_CANDIDATE_COMPAT_PASS\n";
    unsigned case_id = 0;
    for (bool unicode : {false, true}) {
      for (bool use_extended : {false, true}) {
        // 0 is a short control; negative entries measure the complete DB/WAL/SHM envelope.
        for (int target : {0, -259, -260, -261, 280, 600, 1200, 2048, -4096}) {
          const std::wstring prefix = run + L"\\c " + std::to_wstring(++case_id) + L"%";
          const std::wstring name = L"probe-" + std::to_wstring(GetCurrentProcessId()) +
              L"-" + std::to_wstring(case_id) + L".db";
          const size_t units = target == 0 ? prefix.size() + 12 : target > 0 ?
              static_cast<size_t>(target) : static_cast<size_t>(-target) - name.size() - 1 - 4;
          const auto fixture = seekdb_phase0::directory_at_length(
              std::u16string(prefix.begin(), prefix.end()), units, unicode);
          const std::wstring directory(fixture.begin(), fixture.end());
          const std::wstring path = directory + L"\\" + name;
          const std::wstring wrong = original + L"\\" + name;
          require_absent(wrong);
          std::vector<std::wstring> owned;
          make_dirs(directory, run.size(), owned);
          std::cout << "case=" << case_id << " extended=" << use_extended << " unicode=" << unicode
                    << " base_units=" << directory.size() << " max_file_units=" << path.size() + 4
                    << " db_utf8_bytes=" << utf8(path).size() << "\npath=" << utf8(path) << '\n';
          if (use_extended) {
            check(cwd() == original, "cwd changed during directory creation");
            const std::wstring crt = extended(directory + L"\\crt-probe");
            const int fd = _wopen(crt.c_str(), _O_CREAT | _O_EXCL | _O_RDWR | _O_BINARY,
                                 _S_IREAD | _S_IWRITE);
            check(fd >= 0, "wide CRT open errno=" + std::to_string(errno));
            check(_close(fd) == 0, "wide CRT close");
            win_check(DeleteFileW(crt.c_str()) != 0, "wide CRT delete");
            WIN32_FIND_DATAW entry = {};
            HANDLE search = FindFirstFileW(extended(directory + L"\\*").c_str(), &entry);
            win_check(search != INVALID_HANDLE_VALUE, "wide directory enumeration");
            while (FindNextFileW(search, &entry)) {}
            const DWORD enumeration_error = GetLastError();
            FindClose(search);
            check(enumeration_error == ERROR_NO_MORE_FILES, "directory enumeration failed");
          }
          bool passed = false;
          try {
            roundtrip(path, use_extended, original);
            passed = true;
          } catch (const std::exception &error) {
            std::cout << "case_failure=" << error.what() << '\n';
            // Non-extended long paths are diagnostic controls, not the chosen
            // product route. WAL/SHM can fail even when the DB itself opened.
            // Every extended-path case and every short control remains fatal.
            if (use_extended || target == 0) throw;
          }
          for (const wchar_t *suffix : {L"-wal", L"-shm", L"-journal", L""}) {
            require_absent(wrong + suffix);
          }
          // Only this run's exclusively owned names are deleted, after connections close.
          for (const wchar_t *suffix : {L"-wal", L"-shm", L"-journal", L""}) remove_file(path + suffix);
          require_absent(path);
          for (auto it = owned.rbegin(); it != owned.rend(); ++it) {
            win_check(RemoveDirectoryW(extended(*it).c_str()) != 0, "cleanup directory");
          }
          std::cout << (passed ? "CASE_PASS" : "CONTROL_UNSUPPORTED") << '\n';
        }
      }
    }
    win_check(RemoveDirectoryW(run.c_str()) != 0, "cleanup run directory");
    check(std::string(sqlite3_vfs_find(nullptr)->zName) == "win32", "default VFS changed");
    check(cwd() == original, "matrix changed launch cwd");
    std::cout << "SQLITE_LONGPATH_MATRIX_PASS policy=" << policy
              << " (one policy only; vendor identity and full seekdb gate require separate review)\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "PHASE0_FAILED " << error.what() << '\n';
    if (!run.empty()) std::cerr << "preserved_run=" << utf8(run) << '\n';
    return 1;
  }
}
