// Copyright (c) 2026 OceanBase. Licensed under the Apache License, Version 2.0.
#include "observer/windows_startup_context.h"
#include "lib/alloc/ob_iallocator.h"
#include "lib/ob_errno.h"
#include "path_fixture.h"
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
using namespace oceanbase::common;
using namespace oceanbase::observer;
namespace {
void require(bool condition, const char *message) { if (!condition) { throw std::runtime_error(message); } }
class Allocator : public ObIAllocator {
public:
  int live = 0, fail_after = -1;
  void *alloc(int64_t bytes) override {
    if (fail_after == 0) { return nullptr; }
    if (fail_after > 0) { --fail_after; }
    void *p = std::malloc(bytes); if (p != nullptr) { ++live; } return p;
  }
  void *alloc(int64_t n, const ObMemAttr &) override { return alloc(n); }
  void free(void *p) override { if (p != nullptr) { --live; std::free(p); } }
};
std::string encode(const std::wstring &s) {
  const int n = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, s.c_str(), -1, nullptr, 0, nullptr, nullptr);
  require(n > 0, "encode size");
  std::string out(n, '\0');
  require(WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, s.c_str(), -1, out.data(), n, nullptr, nullptr) == n, "encode");
  out.resize(n - 1); return out;
}
std::wstring path_at(const std::wstring &root, int64_t length, bool unicode) {
  auto s = seekdb_phase0::directory_at_length(std::u16string(root.begin(), root.end()), length, unicode);
  return std::wstring(s.begin(), s.end());
}
void arguments() {
  Allocator allocator;
  wchar_t a0[] = L"seekdb", a1[] = L"", a2[] = L"\u4e2d\\\U0001f680 %";
  wchar_t *args[] = {a0, a1, a2, nullptr};
  {
    WindowsStartupContext c(allocator);
    require(c.capture(3, args) == OB_SUCCESS, "argument capture");
    require(c.argc() == 3 && c.argv()[3] == nullptr && c.wide_argv()[3] == nullptr, "argv terminator");
    require(std::string(c.argv()[1]).empty() &&
        std::string(c.argv()[2]) == "\xe4\xb8\xad\\\xf0\x9f\x9a\x80 %", "strict UTF8 or empty argument");
    a2[0] = L'x';
    require(c.wide_argv()[2][0] == L'\u4e2d' && c.argv()[2][0] == '\xe4', "argument ownership");
    require(std::filesystem::current_path().wstring() == c.cwd(), "captured cwd");
    require(std::filesystem::exists(c.executable()), "captured executable");
    require(c.capture(3, args) == OB_INIT_TWICE, "recapture allowed");
  }
  require(allocator.live == 0, "argument resource leak");
  wchar_t bad[] = {0xd800, 0};
  wchar_t *invalid[] = {a0, bad, nullptr};
  {
    WindowsStartupContext c(allocator);
    require(c.capture(2, invalid) == OB_INVALID_ARGUMENT && c.win32_error() == ERROR_NO_UNICODE_TRANSLATION, "invalid UTF16");
  }
  require(allocator.live == 0, "invalid UTF16 leak");
  int failed = 0;
  for (int i = 0; i < 32; ++i) {
    allocator.fail_after = i;
    int ret;
    { WindowsStartupContext c(allocator); ret = c.capture(3, args); }
    require(allocator.live == 0, "capture allocation failure leaked");
    if (ret == OB_SUCCESS) { break; }
    require(ret == OB_ALLOCATE_MEMORY_FAILED, "capture allocation failure classification");
    ++failed;
  }
  require(failed >= 10, "capture failure points not covered");
  allocator.fail_after = -1;
  std::cout << "STARTUP_ARGUMENTS_PASS allocation_failures=" << failed << '\n';
}
void real_child(const WindowsStartupContext &c) {
  std::wstring command = L"\"" + std::wstring(c.executable()) + L"\" --child \"\" \"\u4e2d\\\U0001f680 %\"";
  STARTUPINFOW si = {}; si.cb = sizeof(si); PROCESS_INFORMATION pi = {};
  require(CreateProcessW(c.executable(), command.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW,
      nullptr, c.cwd(), &si, &pi), "CreateProcessW argv child");
  const DWORD wait = WaitForSingleObject(pi.hProcess, 15000);
  if (wait != WAIT_OBJECT_0) { TerminateProcess(pi.hProcess, 1); WaitForSingleObject(pi.hProcess, 15000); }
  DWORD code = 1;
  const BOOL queried = GetExitCodeProcess(pi.hProcess, &code);
  CloseHandle(pi.hThread); CloseHandle(pi.hProcess);
  require(wait == WAIT_OBJECT_0 && queried && code == 0, "wide child argv");
  std::cout << "STARTUP_WIDE_CHILD_PASS\n";
}
void daemon_command() {
  Allocator allocator;
  wchar_t a0[] = L"test", a1[] = L"--daemon-child", a2[] = L"",
      a3[] = L"space \u4e2d\\", a4[] = L"before\\\"after", a5[] = L"--", a6[] = L"%\U0001f680";
  wchar_t *args[] = {a0,a1,a2,a3,a4,a5,a6,nullptr};
  {
    WindowsStartupContext c(allocator);
    require(c.capture(7,args) == OB_SUCCESS, "daemon capture");
    wchar_t *command = nullptr;
    require(c.build_daemon_command(allocator,command) == OB_SUCCESS, "daemon quote");
    STARTUPINFOW si = {}; si.cb=sizeof(si); PROCESS_INFORMATION pi={};
    const BOOL created=CreateProcessW(c.executable(),command,nullptr,nullptr,FALSE,
        CREATE_NO_WINDOW,nullptr,c.cwd(),&si,&pi);
    allocator.free(command);
    require(created, "daemon child create");
    const DWORD wait=WaitForSingleObject(pi.hProcess,15000);
    if(wait!=WAIT_OBJECT_0){TerminateProcess(pi.hProcess,1);WaitForSingleObject(pi.hProcess,15000);}
    DWORD code=1; const BOOL queried=GetExitCodeProcess(pi.hProcess,&code);
    CloseHandle(pi.hThread);CloseHandle(pi.hProcess);
    require(wait==WAIT_OBJECT_0 && queried && code==0,"daemon child arguments");
    allocator.fail_after=0;command=nullptr;
    require(c.build_daemon_command(allocator,command)==OB_ALLOCATE_MEMORY_FAILED && command==nullptr,"daemon allocation failure");
    allocator.fail_after=-1;
  }
  require(allocator.live==0,"daemon quote leak");
  {
    std::wstring huge(20000,L'\\');
    wchar_t *args2[]={a0,huge.data(),nullptr};
    WindowsStartupContext c(allocator);
    require(c.capture(2,args2)==OB_SUCCESS,"oversize capture");
    wchar_t *command=nullptr;
    require(c.build_daemon_command(allocator,command)==OB_SIZE_OVERFLOW && command==nullptr,"escaped command overflow");
  }
  require(allocator.live==0,"oversize quote leak");
  std::cout<<"STARTUP_DAEMON_COMMAND_PASS\n";
}
void paths(const std::wstring &root, const WindowsStartupContext &startup) {
  Allocator allocator;
  const auto original = std::filesystem::current_path();
  for (bool unicode : {false, true}) {
    for (int64_t length : {100,259,260,261,280,600,1200,2048}) {
      const auto base = path_at(root + (unicode ? L"\\u" : L"\\a") + std::to_wstring(length), length, unicode);
      const auto input = encode(base);
      {
        WindowsInstancePaths p(allocator);
        require(p.initialize(input.c_str(), "", "") == OB_SUCCESS, "preflight path");
        require(p.base().length() == length && p.run().length() == length + 4 &&
            p.pid().length() == length + 15 && p.log_file().length() == length + 15, "derived path lengths");
        require(!std::filesystem::exists(base), "preflight created base");
        require(p.create_directories() == OB_SUCCESS, "create preflighted directories");
        require(GetFileAttributesW(p.base().wide()) != INVALID_FILE_ATTRIBUTES, "base absent");
        if (length == 2048) {
          WindowsFilePath run(allocator);
          require(run.assign(p.run().utf8()) == OB_SUCCESS && run.create_directory(true) == OB_SUCCESS, "run create");
          require(p.install_executable(startup) == OB_SUCCESS && p.install_executable(startup) == OB_SUCCESS, "wide executable copy/replace");
          WindowsFilePath installed(allocator);
          const auto copy = encode(base + L"\\run\\seekdb.exe");
          require(installed.assign(copy.c_str()) == OB_SUCCESS, "copy path");
          HANDLE file = CreateFileW(installed.wide(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
          require(file != INVALID_HANDLE_VALUE, "copy absent");
          char magic[2] = {}; DWORD read = 0;
          const BOOL ok = ReadFile(file, magic, 2, &read, nullptr);
          CloseHandle(file);
          require(ok && read == 2 && magic[0] == 'M' && magic[1] == 'Z', "copy is not executable");
          std::cout << "STARTUP_EXECUTABLE_COPY_PASS unicode=" << unicode << '\n';
        }
        WindowsFilePath cleanup(allocator);
        require(cleanup.assign(input.c_str()) == OB_SUCCESS && cleanup.remove_tree(false) == OB_SUCCESS, "base cleanup");
      }
      require(allocator.live == 0, "path resources");
      std::cout << "STARTUP_PATH_PASS units=" << length << " unicode=" << unicode << '\n';
    }
  }
  const auto valid = encode(root + L"\\must-not-create");
  const std::string invalid_inputs[] = {
      encode(path_at(root, 2049, true)), encode(root) + "\\" + std::string(256, 'x'), "C:\\NUL.txt",
      std::string("C:\\") + "\xc0\xaf"
  };
  for (const auto &bad : invalid_inputs) {
    {
      WindowsInstancePaths p(allocator);
      const int ret = p.initialize(bad.c_str(), "", "");
      require(ret == OB_INVALID_ARGUMENT || ret == OB_SIZE_OVERFLOW, "bad base accepted");
      require(p.create_directories() == OB_NOT_INIT, "failed base created resources");
    }
    {
      WindowsInstancePaths p(allocator);
      const int ret = p.initialize(valid.c_str(), (valid + "\\" + std::string(256,'x')).c_str(), "");
      require(ret == OB_SIZE_OVERFLOW && std::string(p.option()) == "data-dir", "data preflight");
      require(p.create_directories() == OB_NOT_INIT, "bad data created base");
      require(!std::filesystem::exists(root + L"\\must-not-create"), "bad data side effect");
    }
  }
  {
    WindowsInstancePaths p(allocator);
    require(p.initialize(valid.c_str(), valid.c_str(), "C:\\different-redo") == OB_INVALID_ARGUMENT, "base/data equality");
    require(p.input_bytes() == valid.size() && std::string(p.option()) == "data-dir" &&
        p.win32_error() == 0, "equal roots diagnostic input");
    require(p.create_directories() == OB_NOT_INIT, "equal roots created resources");
  }
  {
    WindowsInstancePaths p(allocator);
    require(p.initialize(valid.c_str(), ".\\seek533-relative-data", "C:seek533-relative-redo") == OB_SUCCESS, "relative roots");
    require(encode(original.wstring() + L"\\seek533-relative-data") == p.data().utf8() &&
        encode(original.wstring() + L"\\seek533-relative-redo") == p.redo().utf8(), "relative CLI context");
    require(!std::filesystem::exists(root + L"\\must-not-create"), "relative validation created root");
  }
  int failed = 0;
  for (int i = 0; i < 100; ++i) {
    allocator.fail_after = i;
    int ret;
    {
      WindowsInstancePaths p(allocator);
      ret = p.initialize(valid.c_str(), "", "");
      if (ret != OB_SUCCESS) { require(p.create_directories() == OB_NOT_INIT, "allocation failure created root"); }
    }
    require(allocator.live == 0, "preflight failure leaked");
    if (ret == OB_SUCCESS) { break; }
    require(ret == OB_ALLOCATE_MEMORY_FAILED, "preflight failure classification");
    require(!std::filesystem::exists(root + L"\\must-not-create"), "allocation failure side effect");
    ++failed;
  }
  allocator.fail_after = -1;
  require(failed > 15, "preflight allocation points");
  require(original == std::filesystem::current_path(), "cwd changed");
  std::cout << "STARTUP_PREFLIGHT_REJECT_PASS allocation_failures=" << failed << '\n';
  WindowsFilePath cleanup(allocator);
  require(cleanup.assign(encode(root).c_str()) == OB_SUCCESS && cleanup.remove_tree(false) == OB_SUCCESS, "final fixture cleanup");
}
}
int wmain(int argc, wchar_t *argv[]) {
  std::cout << std::unitbuf;
  try {
    Allocator allocator;
    WindowsStartupContext c(allocator);
    require(c.capture(argc, argv) == OB_SUCCESS, "actual entry capture");
    if (argc == 8 && std::string(c.argv()[1]) == "--nodaemon" &&
        std::string(c.argv()[2]) == "--daemon-child") {
      require(std::string(c.argv()[3]).empty() && std::wstring(argv[4])==L"space \u4e2d\\" &&
          std::wstring(argv[5])==L"before\\\"after" && std::wstring(argv[6])==L"--" &&
          std::wstring(argv[7])==L"%\U0001f680","daemon CRT quoting");
      return 0;
    }
    if (argc == 4 && std::string(c.argv()[1]) == "--child") {
      require(std::string(c.argv()[2]).empty() && std::string(c.argv()[3]) == "\xe4\xb8\xad\\\xf0\x9f\x9a\x80 %", "child UTF8");
      return 0;
    }
    require(argc == 2, "requires dedicated test root");
    const std::wstring root(argv[1]);
    require(root.rfind(L"C:\\s\\seek533-startup-",0) == 0 && root.find(L"..") == std::wstring::npos &&
        root.find(L'\\', 5) == std::wstring::npos, "root scope");
    require(std::filesystem::is_empty(root), "root not empty");
    arguments(); real_child(c); daemon_command(); paths(root, c);
    require(!std::filesystem::exists(root), "root remains");
    std::cout << "STARTUP_COMPONENT_PASS\n";
    return 0;
  } catch(const std::exception &e) { std::cerr << e.what() << '\n'; return 1; }
}
