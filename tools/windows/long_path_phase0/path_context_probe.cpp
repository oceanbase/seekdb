// Copyright (c) 2026 OceanBase. Licensed under the Apache License, Version 2.0.
// Component prototype, not evidence of production startup or SQL-NIO behavior.
#include "path_context.h"
#include "path_fixture.h"
#include <filesystem>
#include <cstdint>
#include <iostream>
#include <stdexcept>

namespace {
using namespace seekdb_phase0;
void require(bool value, const char *stage)
{
  if (!value) throw std::runtime_error(stage);
}
void checked(PathResult result, const char *stage)
{
  if (!result) throw std::runtime_error(std::string(stage) + " code=" +
      std::to_string(result.code) + " win32=" + std::to_string(result.win32));
}
void win_checked(BOOL value, const char *stage)
{
  if (!value) checked(failed_path_api(), stage);
}
std::string utf8(const std::wstring &value)
{
  std::string result;
  checked(path_to_utf8(value, result), "encode input");
  return result;
}
std::wstring directory(const std::wstring &prefix, size_t units, bool unicode)
{
  const auto value = directory_at_length(std::u16string(prefix.begin(), prefix.end()), units, unicode);
  return std::wstring(value.begin(), value.end());
}
class Handle {
public:
  explicit Handle(HANDLE value) : value_(value) { win_checked(value != INVALID_HANDLE_VALUE, "CreateFileW"); }
  ~Handle() { CloseHandle(value_); }
  HANDLE get() const { return value_; }
  Handle(const Handle &) = delete;
  Handle &operator=(const Handle &) = delete;
private:
  HANDLE value_;
};
void reject(const std::string &input, size_t limit, int code, const char *name)
{
  std::wstring output = L"untouched";
  const auto result = normalize_absolute(input, limit, output);
  require(result.code == code, "unexpected rejection classification");
  require(output == L"untouched", "rejected input changed output");
  std::cout << "CONTEXT_REJECT_PASS case=" << name << '\n';
}
void negative_cases(const std::wstring &root)
{
  reject("", kBaseUnits, -4002, "empty");
  reject(std::string("C:\\ab\0cd", 8), kBaseUnits, -4002, "nul");
  reject(std::string("C:\\\xc0\xaf"), kBaseUnits, -4002, "invalid_utf8");
  reject("C:relative", kBaseUnits, -4002, "drive_relative_base");
  reject("relative", kBaseUnits, -4002, "relative_base");
  reject("\\\\server\\share\\db", kBaseUnits, -4002, "unc");
  reject("\\\\.\\pipe\\db", kBaseUnits, -4002, "device");
  reject("C:\\data.", kBaseUnits, -4002, "trailing_dot");
  reject("C:\\data ", kBaseUnits, -4002, "trailing_space");
  reject("C:\\data:stream", kBaseUnits, -4002, "ads");
  reject("C:\\NUL.txt", kBaseUnits, -4002, "reserved");
  reject("C:\\COM1 .txt", kBaseUnits, -4002, "reserved_space");
  reject(utf8(L"C:\\LPT\u00b2.txt"), kBaseUnits, -4002, "reserved_superscript");
  reject("C:\\a*", kBaseUnits, -4002, "wildcard");
  reject("C:\\" + std::string(256, 'x'), kBaseUnits, -4019, "component_256");
  reject(utf8(directory(root, 2049, true)), kBaseUnits, -4019, "base_2049");
  reject(utf8(directory(root, 4097, true)), kFileUnits, -4019, "file_4097");
  std::string output = "unchanged";
  const auto result = path_to_utf8(std::wstring(1, static_cast<wchar_t>(0xd800)), output);
  require(result.code == -4002 && output == "unchanged", "unpaired UTF-16 accepted");
  std::cout << "CONTEXT_REJECT_PASS case=unpaired_utf16\n";
}
void file_lifecycle(const std::wstring &file)
{
  const auto path = extended_path(file);
  const char expected[] = {'A', '\0', 'B', '\n'};
  {
    Handle handle(CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW,
        FILE_ATTRIBUTE_NORMAL, nullptr));
    DWORD actual = 0;
    win_checked(WriteFile(handle.get(), expected, sizeof(expected), &actual, nullptr), "write");
    require(actual == sizeof(expected), "short write");
    win_checked(FlushFileBuffers(handle.get()), "flush");
  }
  WIN32_FILE_ATTRIBUTE_DATA info{};
  win_checked(GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &info), "stat");
  require(info.nFileSizeLow == sizeof(expected) && info.nFileSizeHigh == 0, "stat size");
  const std::wstring renamed = path.substr(0, path.size() - 1) + L"y";
  win_checked(MoveFileExW(path.c_str(), renamed.c_str(), 0), "rename");
  {
    Handle handle(CreateFileW(renamed.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
        OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr));
    char bytes[sizeof(expected)]{};
    DWORD actual = 0;
    win_checked(ReadFile(handle.get(), bytes, sizeof(bytes), &actual, nullptr), "read");
    require(actual == sizeof(expected) && std::equal(std::begin(bytes), std::end(bytes), expected), "readback");
  }
  win_checked(DeleteFileW(renamed.c_str()), "delete");
}
std::vector<std::wstring> child_arguments(const std::wstring &base,
    const std::wstring &data, const std::wstring &redo, const std::wstring &cwd)
{
  uint64_t hash = 14695981039346656037ULL;
  for (const auto &value : {base, data, redo, cwd}) {
    for (const wchar_t unit : value) { hash ^= static_cast<uint16_t>(unit); hash *= 1099511628211ULL; }
    hash ^= value.size(); hash *= 1099511628211ULL;
  }
  return {L"--child-check", base, data, redo, cwd, L"", L"space percent %", L"quoted \"value\"",
      L"end\\", L"slash\\\"quote", L"\u4e2d\U0001f680", L"--", L"--nodaemon",
      std::to_wstring(base.size()), std::to_wstring(hash)};
}
void child_roundtrip(const StartupPaths &paths)
{
  const auto &base = paths.instance().base();
  const auto &cwd = paths.instance().original_cwd();
  std::vector<wchar_t> buffer(256);
  for (;;) {
    const DWORD size = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
    win_checked(size != 0, "exe path");
    if (size < buffer.size()) { buffer.resize(size); break; }
    require(buffer.size() < 32768, "exe path exceeds limit");
    buffer.resize(buffer.size() * 2);
  }
  const std::wstring exe(buffer.begin(), buffer.end());
  std::wstring command = quote_argument(exe);
  for (const auto &arg : child_arguments(base, paths.data(), paths.redo(), cwd)) command += L" " + quote_argument(arg);
  require(command.size() < 32767, "child command exceeds limit");
  STARTUPINFOW startup{};
  startup.cb = sizeof(startup);
  PROCESS_INFORMATION process{};
  win_checked(CreateProcessW(exe.c_str(), command.data(), nullptr, nullptr, FALSE,
      0, nullptr, cwd.c_str(), &startup, &process), "CreateProcessW");
  CloseHandle(process.hThread);
  const DWORD wait = WaitForSingleObject(process.hProcess, 10000);
  DWORD exit = 1;
  const BOOL queried = GetExitCodeProcess(process.hProcess, &exit);
  const DWORD query_error = queried ? 0 : GetLastError();
  if (wait != WAIT_OBJECT_0) {
    TerminateProcess(process.hProcess, 1); // Only this owned test child, never a service.
    WaitForSingleObject(process.hProcess, INFINITE);
  }
  CloseHandle(process.hProcess);
  checked(queried ? PathResult{} : PathResult{-4000, query_error}, "child exit query");
  require(wait == WAIT_OBJECT_0 && exit == 0, "child argument or path roundtrip failed");
}
void startup_cases(const std::wstring &root, const std::wstring &cwd)
{
  StartupPaths defaults;
  checked(defaults.initialize(""), "default CLI base");
  require(defaults.instance().base() == cwd && defaults.data().empty() && defaults.redo().empty(), "CLI defaults changed");
  std::cout << "CONTEXT_CLI_PASS case=defaults\n";
  StartupPaths relative;
  checked(relative.initialize("relative base", "relative data", "relative redo"), "relative CLI options");
  require(relative.instance().base() == cwd + L"\\relative base" && relative.data() == cwd + L"\\relative data" &&
      relative.redo() == cwd + L"\\relative redo", "relative CLI used instance root");
  std::cout << "CONTEXT_CLI_PASS case=relative_options\n";
  StartupPaths drive_relative;
  checked(drive_relative.initialize(utf8(root), utf8(cwd.substr(0, 2) + L"drive data"),
      utf8(cwd.substr(0, 2) + L"drive redo")), "drive-relative CLI options");
  require(drive_relative.data() == cwd + L"\\drive data" && drive_relative.redo() == cwd + L"\\drive redo", "drive-relative base changed");
  std::cout << "CONTEXT_CLI_PASS case=drive_relative_options\n";
  StartupPaths rooted;
  checked(rooted.initialize(utf8(root), "\\rooted data", "\\rooted redo"), "rooted CLI options");
  require(rooted.data() == cwd.substr(0, 2) + L"\\rooted data" && rooted.redo() == cwd.substr(0, 2) + L"\\rooted redo", "rooted CLI drive changed");
  std::cout << "CONTEXT_CLI_PASS case=root_relative_options\n";
  StartupPaths rejected;
  require(rejected.initialize(utf8(root), "valid data", "invalid redo.").code == -4002, "invalid later CLI option accepted");
  require(rejected.instance().base().empty() && rejected.data().empty() && rejected.redo().empty(), "failed CLI freeze published partial state");
  checked(rejected.initialize(utf8(root), "valid data", "valid redo"), "retry after rejected CLI input");
  require(rejected.initialize(utf8(root + L"\\other")).code == -4002 && rejected.instance().base() == root,
      "CLI context could be mutated after publication");
  std::cout << "CONTEXT_CLI_PASS case=transactional_freeze\n";
}
void matrix(const std::wstring &root)
{
  negative_cases(root);
  require(std::filesystem::is_empty(root), "preflight rejection created an artifact");
  PathContext original;
  checked(original.initialize(utf8(root)), "capture original cwd");
  const std::wstring start = original.original_cwd();
  startup_cases(root, start);
  require(std::filesystem::is_empty(root), "CLI freeze created a filesystem artifact");
  const std::wstring prefix = root + L"\\space %";
  for (const bool unicode : {false, true}) {
    for (const size_t units : {prefix.size() + 16, size_t(280), size_t(600), size_t(1200), size_t(2048)}) {
      const auto base = directory(prefix, units, unicode);
      PathContext context;
      checked(context.initialize(utf8(base)), "context initialize");
      require(context.base() == base && context.original_cwd() == start, "context source");
      std::wstring result;
      checked(context.resolve_instance("run/sql.pipe", result), "instance derive");
      require(result == base + L"\\run\\sql.pipe", "instance root changed");
      checked(context.resolve_instance(utf8(result), result), "absolute passthrough");
      require(result == base + L"\\run\\sql.pipe", "absolute path joined twice");
      checked(normalize_absolute(utf8(extended_path(base)), kBaseUnits, result), "extended normalize");
      require(result == base, "extended prefix leaked");
      std::string cli_data = utf8(L"cli data %\\\u4e2d\U0001f680");
      std::string cli_redo = utf8(start.substr(0, 2) + L"cli redo %\\\u4e2d\U0001f680");
      StartupPaths startup;
      checked(startup.initialize(utf8(base), cli_data, cli_redo), "freeze startup options");
      cli_data.assign("overwritten input");
      cli_redo.assign("overwritten input");
      require(startup.data() == start + L"\\cli data %\\\u4e2d\U0001f680" &&
          startup.redo() == start + L"\\cli redo %\\\u4e2d\U0001f680", "CLI values not owned or wrong base");
      checked(startup.instance().resolve_instance("configured store", result), "configured relative store");
      require(result == base + L"\\configured store", "config did not use logical instance root");
      child_roundtrip(startup);
      require(context.base() == base, "child changed parent ownership");
      std::cout << "CONTEXT_BASE_PASS units=" << units << " unicode=" << unicode << '\n';
    }
    for (const size_t units : {prefix.size() + 18, size_t(259), size_t(260), size_t(261), size_t(4096)}) {
      const auto parent = directory(prefix, units - 2, unicode);
      const auto file = parent + L"\\x";
      std::wstring normalized;
      checked(normalize_absolute(utf8(file), kFileUnits, normalized), "file normalize");
      require(normalized.size() == units, "file units");
      std::filesystem::create_directories(extended_path(parent));
      file_lifecycle(normalized);
      std::cout << "CONTEXT_FILE_PASS units=" << units << " unicode=" << unicode << '\n';
    }
  }
  std::wstring normalized;
  checked(normalize_absolute(utf8(root + L"\\space %\\.\\child\\..\\db"), kBaseUnits, normalized), "dot segments");
  require(normalized == root + L"\\space %\\db", "lexical normalization");
  checked(normalize_absolute(utf8(root + L"\\" + std::wstring(255, L'x')), kBaseUnits, normalized), "component 255");
  PathContext end;
  checked(end.initialize(utf8(root)), "final cwd");
  require(end.original_cwd() == start, "process cwd changed");
  std::cout << "CONTEXT_CWD_UNCHANGED\n";
}
} // namespace

int wmain(int argc, wchar_t **argv)
{
  try {
    if (argc >= 6 && std::wstring(argv[1]) == L"--child-check") {
      const auto expected = child_arguments(argv[2], argv[3], argv[4], argv[5]);
      require(argc == static_cast<int>(expected.size() + 1), "child argc");
      for (size_t i = 0; i < expected.size(); ++i) require(argv[i + 1] == expected[i], "child argv");
      StartupPaths paths;
      checked(paths.initialize(utf8(argv[2]), utf8(argv[3]), utf8(argv[4])), "child context");
      require(paths.instance().base() == argv[2] && paths.data() == argv[3] && paths.redo() == argv[4],
          "parent child frozen paths mismatch");
      require(paths.instance().original_cwd() == argv[5], "child changed original cwd");
      return 0;
    }
    require(argc == 3, "usage: path_context_probe <owned-empty-root> <policy>");
    require(std::wstring(argv[2]) == L"0" || std::wstring(argv[2]) == L"1", "expected policy argument");
    DWORD policy = 0, policy_size = sizeof(policy);
    const LSTATUS status = RegGetValueW(HKEY_LOCAL_MACHINE,
        L"SYSTEM\\CurrentControlSet\\Control\\FileSystem", L"LongPathsEnabled",
        RRF_RT_REG_DWORD, nullptr, &policy, &policy_size);
    require(status == ERROR_SUCCESS && policy == static_cast<DWORD>(argv[2][0] - L'0'), "actual policy mismatch");
    const HRSRC resource = FindResourceW(nullptr, MAKEINTRESOURCEW(1), MAKEINTRESOURCEW(24));
    win_checked(resource != nullptr, "embedded manifest resource");
    const DWORD manifest_size = SizeofResource(nullptr, resource);
    const HGLOBAL loaded = LoadResource(nullptr, resource);
    win_checked(loaded != nullptr, "load embedded manifest");
    const auto *manifest_data = static_cast<const char *>(LockResource(loaded));
    require(manifest_data != nullptr && manifest_size != 0, "manifest contents");
    const std::string manifest(manifest_data, manifest_size);
    require(manifest.find("http://schemas.microsoft.com/SMI/2016/WindowsSettings") != std::string::npos &&
        manifest.find(">true</longPathAware>") != std::string::npos, "embedded longPathAware missing");
    std::cout << "CONTEXT_RUNTIME policy=" << policy << " embedded_longPathAware=true\n";
    const std::wstring root = argv[1];
    require(std::filesystem::is_empty(root), "test root is not empty");
    matrix(root);
    // The wrapper creates this unique, empty root; only descendants made here exist.
    for (const auto &entry : std::filesystem::directory_iterator(root)) {
      std::filesystem::remove_all(extended_path(entry.path().wstring()));
    }
    require(std::filesystem::is_empty(root), "owned cleanup incomplete");
    std::cout << "CONTEXT_MATRIX_PASS policy=" << utf8(argv[2]) << '\n';
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "CONTEXT_FAILURE " << error.what() << '\n';
    return 1;
  }
}
