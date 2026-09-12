// Copyright (c) 2026 OceanBase. Licensed under the Apache License, Version 2.0.
// Uses the actual sql-nio static library and C header; no substitute reactor.
#include "nio.h"
#include "path_context.h"
#include "path_fixture.h"
#include <atomic>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <stdexcept>

namespace {
using namespace seekdb_phase0;
void require(bool value, const char *stage)
{
  if (!value) throw std::runtime_error(stage);
}
void win_check(BOOL value, const char *stage)
{
  if (!value) {
    const DWORD error = GetLastError();
    throw std::runtime_error(std::string(stage) + " win32=" + std::to_string(error));
  }
}
std::string utf8(const std::wstring &value)
{
  std::string output;
  require(static_cast<bool>(path_to_utf8(value, output)), "UTF-8 encode");
  return output;
}
struct CallbacksState { std::atomic<int> connects{0}; std::atomic<int> closes{0}; std::string version{"seek533-phase0"}; };
int on_connect(void *ctx, void *, int, int local, nio_greeting_info *greeting)
{
  if (!local) return -1;
  auto *state = static_cast<CallbacksState *>(ctx);
  greeting->sessid = 1;
  std::memset(greeting->scramble, 'a', sizeof(greeting->scramble));
  if (state->version.size() > sizeof(greeting->version)) return -1;
  std::memcpy(greeting->version, state->version.data(), state->version.size());
  greeting->version_len = state->version.size();
  greeting->status_flags = 2;
  ++state->connects;
  return 0;
}
int on_readable(void *, void *, char *, int64_t, uint64_t, int,
    const nio_mysql_command_view *, uint64_t) { return -1; }
void on_disconnect(void *, void *) {}
void on_close(void *ctx, void *, int) { ++static_cast<CallbacksState *>(ctx)->closes; }
class ReactorGuard {
public:
  explicit ReactorGuard(nio_reactor *reactor) : value_(reactor) {}
  ~ReactorGuard() { if (value_) { nio_stop(value_); nio_wait_destroy(value_); } }
  void wait_destroy() { auto *value = value_; value_ = nullptr; nio_wait_destroy(value); }
  ReactorGuard(const ReactorGuard &) = delete;
  ReactorGuard &operator=(const ReactorGuard &) = delete;
private:
  nio_reactor *value_;
};
class Handle {
public:
  explicit Handle(HANDLE handle) : handle_(handle) { win_check(handle != INVALID_HANDLE_VALUE && handle != nullptr, "open file/pipe"); }
  ~Handle() { CloseHandle(handle_); }
  HANDLE get() const { return handle_; }
  Handle(const Handle &) = delete;
  Handle &operator=(const Handle &) = delete;
private:
  HANDLE handle_;
};
void rejected_inputs()
{
  int32_t error = -1;
  require(nio_start(nullptr, NIO_ABI_VERSION, nullptr, 0, 0, 0, nullptr, 0, &error, 1) == nullptr &&
      error == NIO_START_EABI, "legacy entry did not reject before other input reads");
  require(nio_start_v27(nullptr, 26, nullptr, 0, 0, 0, nullptr, 0, &error, 1, nullptr, 0) == nullptr &&
      error == NIO_START_EABI, "new entry accepted old ABI");
  std::cout << "NIO_ABI_REJECT_PASS\n";
  // A null callback ctx gives ECALLBACKS only AFTER directory validation. This
  // distinguishes a real path rejection from unrelated EINVAL, without allowing
  // a faulty validation branch to create files outside the owned test root.
  nio_callbacks callbacks{nullptr, on_connect, on_readable, on_disconnect, on_close};
  const std::vector<std::string> inputs = {"", "run", "C:run", "\\\\server\\share\\run",
      "\\\\?\\C:\\run", std::string("C:\\run\0x", 8), "C:\\\xc0\xaf", "C:\\run.", "C:\\NUL",
      "C:\\a\\..\\run", "C:\\" + std::string(256, 'a')};
  for (const auto &input : inputs) {
    error = -1;
    require(nio_start_v27("127.0.0.1:0", NIO_ABI_VERSION, &callbacks, sizeof(callbacks), 64, 1, nullptr, 0,
        &error, 1, input.data(), input.size()) == nullptr && error == NIO_START_EINVAL, "invalid run directory accepted");
  }
  require(nio_start_v27(nullptr, NIO_ABI_VERSION, nullptr, 0, 0, 0, nullptr, 0,
      &error, 1, nullptr, 16385) == nullptr && error == NIO_START_EINVAL, "oversized input accepted");
  std::cout << "NIO_PATH_REJECT_PASS count=12\n";
}
void derived_boundary_inputs(const std::wstring &root)
{
  nio_callbacks callbacks{nullptr, on_connect, on_readable, on_disconnect, on_close};
  const size_t suffix = std::wstring(L"\\sql.pipe.starting-").size() + std::to_wstring(GetCurrentProcessId()).size();
  for (const size_t total : {size_t(4096), size_t(4097)}) {
    const auto value = directory_at_length(std::u16string(root.begin(), root.end()), total - suffix, true);
    const std::string directory = utf8(std::wstring(value.begin(), value.end()));
    int32_t error = -1;
    auto *reactor = nio_start_v27("127.0.0.1:0", NIO_ABI_VERSION, &callbacks, sizeof(callbacks),
        64, 1, nullptr, 0, &error, 1, directory.data(), directory.size());
    ReactorGuard guard(reactor);
    // Valid paths reach callback validation; invalid paths must stop earlier.
    // Neither case may create files, since callback ctx is deliberately null.
    require(reactor == nullptr && error == (total == 4096 ? NIO_START_ECALLBACKS : NIO_START_EINVAL),
        "staging length boundary classification");
    std::cout << "NIO_STAGING_BOUNDARY_PASS total_units=" << total << '\n';
  }
}
std::string greeting_from_discovery(const std::wstring &run, const char *version = "seek533-phase0")
{
  std::string bare;
  {
    Handle file(CreateFileW(extended_path(run + L"\\sql.pipe").c_str(), GENERIC_READ,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr));
    char bytes[128]{};
    DWORD read = 0;
    win_check(ReadFile(file.get(), bytes, sizeof(bytes), &read, nullptr), "read discovery");
    require(read > 0 && read < sizeof(bytes), "invalid discovery length");
    bare.assign(bytes, read);
  }
  require(bare.find_first_not_of("0123456789-") == std::string::npos, "discovery format changed");
  const std::wstring endpoint = L"\\\\.\\pipe\\" + std::wstring(bare.begin(), bare.end());
  Handle pipe(CreateFileW(endpoint.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr,
      OPEN_EXISTING, FILE_FLAG_OVERLAPPED, nullptr));
  Handle event(CreateEventW(nullptr, TRUE, FALSE, nullptr));
  OVERLAPPED overlapped{};
  overlapped.hEvent = event.get();
  unsigned char packet[256]{};
  DWORD count = 0;
  const BOOL ready = ReadFile(pipe.get(), packet, sizeof(packet), &count, &overlapped);
  if (!ready) {
    const DWORD error = GetLastError();
    require(error == ERROR_IO_PENDING, "pipe greeting read failed");
    if (WaitForSingleObject(event.get(), 5000) != WAIT_OBJECT_0) {
      CancelIoEx(pipe.get(), &overlapped);
      GetOverlappedResult(pipe.get(), &overlapped, &count, TRUE);
      throw std::runtime_error("pipe greeting timed out");
    }
    win_check(GetOverlappedResult(pipe.get(), &overlapped, &count, FALSE), "pipe greeting completion");
  }
  const size_t version_size = std::strlen(version);
  require(count >= 6 + version_size && packet[3] == 0 && packet[4] == 10 &&
      std::memcmp(packet + 5, version, version_size) == 0 && packet[5 + version_size] == 0,
      "actual MySQL greeting identity missing");
  return bare;
}
void run_case(const std::wstring &base)
{
  const std::wstring run = base + L"\\run";
  CallbacksState state;
  nio_callbacks callbacks{&state, on_connect, on_readable, on_disconnect, on_close};
  for (int repeat = 0; repeat != 2; ++repeat) {
    std::string directory = utf8(run);
    int32_t error = -1;
    {
      nio_reactor *reactor = nio_start_v27("127.0.0.1:0", NIO_ABI_VERSION,
          &callbacks, sizeof(callbacks), 64, 1, nullptr, 0, &error, 1,
          directory.data(), directory.size());
      ReactorGuard guard(reactor);
      require(reactor != nullptr && error == NIO_START_OK, "v27 startup failed");
      directory.assign("caller memory overwritten");
      require(nio_get_bound_tcp_port(reactor) == 0, "TCP substituted for pipe");
      greeting_from_discovery(run);
    }
    require(state.connects == repeat + 1 && state.closes == repeat + 1, "callback teardown incomplete");
    const DWORD attributes = GetFileAttributesW(extended_path(run + L"\\sql.pipe").c_str());
    const DWORD missing_error = GetLastError();
    require(attributes == INVALID_FILE_ATTRIBUTES && missing_error == ERROR_FILE_NOT_FOUND, "discovery cleanup failed");
    for (const auto &entry : std::filesystem::directory_iterator(extended_path(run))) {
      (void)entry;
      throw std::runtime_error("staging or discovery left behind");
    }
  }
}
void sharing_cleanup(const std::wstring &base, bool retry_in_drop)
{
  const std::wstring run = base + L"\\run";
  const std::wstring discovery = extended_path(run + L"\\sql.pipe");
  CallbacksState state;
  nio_callbacks callbacks{&state, on_connect, on_readable, on_disconnect, on_close};
  const std::string directory = utf8(run);
  int32_t error = -1;
  {
    auto *reactor = nio_start_v27("127.0.0.1:0", NIO_ABI_VERSION, &callbacks, sizeof(callbacks),
        64, 1, nullptr, 0, &error, 1, directory.data(), directory.size());
    ReactorGuard guard(reactor);
    require(reactor != nullptr && error == NIO_START_OK, "cleanup scenario startup failed");
    greeting_from_discovery(run);
    {
      Handle blocker(CreateFileW(discovery.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
          nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr));
      nio_stop(reactor);
      require(GetFileAttributesW(discovery.c_str()) != INVALID_FILE_ATTRIBUTES,
          "sharing blocker did not retain discovery");
    }
    // ReactorGuard issues repeated stop and destroys after the sharing handle
    // closes. A failed removal must not be mistaken for completed cleanup.
    if (retry_in_drop) guard.wait_destroy(); // Exercise Drop without a second stop.
  }
  require(state.connects == 1 && state.closes == 1, "cleanup scenario callback teardown");
  const DWORD attrs = GetFileAttributesW(discovery.c_str());
  const DWORD missing_error = GetLastError();
  require(attrs == INVALID_FILE_ATTRIBUTES && missing_error == ERROR_FILE_NOT_FOUND,
      "discovery remained after sharing violation was released");
  std::cout << "NIO_SHARING_CLEANUP_PASS retry_in_drop=" << retry_in_drop << '\n';
}
std::wstring executable_path()
{
  std::vector<wchar_t> buffer(256);
  for (;;) {
    const DWORD length = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
    win_check(length != 0, "child executable path");
    if (length < buffer.size()) return std::wstring(buffer.data(), length);
    require(buffer.size() < 32768, "child executable exceeds limit");
    buffer.resize(buffer.size() * 2);
  }
}
HANDLE unique_event(const std::wstring &name)
{
  HANDLE event = CreateEventW(nullptr, TRUE, FALSE, name.c_str());
  const DWORD error = GetLastError();
  if (event && error == ERROR_ALREADY_EXISTS) {
    CloseHandle(event);
    throw std::runtime_error("test event already exists");
  }
  return event;
}
class InstanceChild {
public:
  InstanceChild(const std::wstring &run, const std::wstring &cwd, const wchar_t *identity)
      : prefix_(L"Local\\seek533-nio-" + std::to_wstring(GetCurrentProcessId()) + L"-" + std::to_wstring(++sequence_)),
        ready_(unique_event(prefix_ + L"-ready")), stop_(unique_event(prefix_ + L"-stop"))
  {
    const auto exe = executable_path();
    std::wstring command = quote_argument(exe);
    for (const auto &argument : {std::wstring(L"--instance-child"), run, prefix_ + L"-ready", prefix_ + L"-stop", std::wstring(identity)}) {
      command += L" " + quote_argument(argument);
    }
    require(command.size() < 32767, "instance child command exceeds limit");
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION child{};
    win_check(CreateProcessW(exe.c_str(), command.data(), nullptr, nullptr, FALSE, 0, nullptr,
        cwd.c_str(), &startup, &child), "start instance child");
    process_ = child.hProcess;
    pid_ = child.dwProcessId;
    CloseHandle(child.hThread);
    HANDLE waits[] = {ready_.get(), process_};
    if (WaitForMultipleObjects(2, waits, FALSE, 20000) != WAIT_OBJECT_0) {
      close_child();
      throw std::runtime_error("instance child failed before ready");
    }
  }
  ~InstanceChild() { close_child(); }
  void stop() { require(close_child(), "instance child did not exit cleanly"); }
  DWORD pid() const { return pid_; }
  InstanceChild(const InstanceChild &) = delete;
  InstanceChild &operator=(const InstanceChild &) = delete;
private:
  bool close_child()
  {
    if (!process_) return true;
    const bool signaled = SetEvent(stop_.get()) != FALSE;
    const bool joined = WaitForSingleObject(process_, 20000) == WAIT_OBJECT_0;
    if (!joined) {
      TerminateProcess(process_, 1); // Only this owned component child, never a service.
      WaitForSingleObject(process_, 5000);
    }
    DWORD code = 1;
    const bool queried = GetExitCodeProcess(process_, &code) != FALSE;
    CloseHandle(process_);
    process_ = nullptr;
    return signaled && joined && queried && code == 0;
  }
  inline static unsigned sequence_ = 0;
  std::wstring prefix_;
  Handle ready_;
  Handle stop_;
  HANDLE process_ = nullptr;
  DWORD pid_ = 0;
};
void instance_child(const std::wstring &run, const wchar_t *ready_name, const wchar_t *stop_name, const wchar_t *identity)
{
  Handle ready(OpenEventW(EVENT_MODIFY_STATE, FALSE, ready_name));
  Handle stop(OpenEventW(SYNCHRONIZE, FALSE, stop_name));
  CallbacksState state;
  state.version = utf8(identity);
  nio_callbacks callbacks{&state, on_connect, on_readable, on_disconnect, on_close};
  const std::string directory = utf8(run);
  int32_t error = -1;
  {
    auto *reactor = nio_start_v27("127.0.0.1:0", NIO_ABI_VERSION, &callbacks, sizeof(callbacks),
        64, 1, nullptr, 0, &error, 1, directory.data(), directory.size());
    ReactorGuard guard(reactor);
    require(reactor && error == NIO_START_OK && nio_get_bound_tcp_port(reactor) == 0, "child pipe startup");
    win_check(SetEvent(ready.get()), "child ready event");
    require(WaitForSingleObject(stop.get(), 90000) == WAIT_OBJECT_0, "child stop timeout");
  }
  require(state.connects > 0 && state.connects == state.closes, "child callbacks did not close");
}
void require_discovery_absent(const std::wstring &run)
{
  const DWORD value = GetFileAttributesW(extended_path(run + L"\\sql.pipe").c_str());
  const DWORD error = GetLastError();
  require(value == INVALID_FILE_ATTRIBUTES && error == ERROR_FILE_NOT_FOUND, "stopped instance discovery remains");
}
void two_instances(const std::wstring &root, const std::wstring &cwd)
{
  const auto make_run = [&](const std::wstring &prefix, size_t length, bool unicode) {
    const auto value = directory_at_length(std::u16string(prefix.begin(), prefix.end()), length, unicode);
    return std::wstring(value.begin(), value.end()) + L"\\run";
  };
  const auto a = make_run(root + L"\\dual-a", 600, false);
  const auto b = make_run(root + L"\\dual-b", 1200, true);
  InstanceChild child_a(a, cwd, L"seek533-A"), child_b(b, cwd, L"seek533-B");
  const auto first_a = greeting_from_discovery(a, "seek533-A");
  const auto first_b = greeting_from_discovery(b, "seek533-B");
  require(first_a != first_b, "instances share one pipe");
  // A failed third instance uses an existing regular file as a parent. The
  // valid A/B roots must remain untouched and reachable.
  const auto obstructed = root + L"\\not-a-directory";
  {
    Handle file(CreateFileW(obstructed.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW,
        FILE_ATTRIBUTE_NORMAL | FILE_FLAG_DELETE_ON_CLOSE, nullptr));
    CallbacksState failed_state;
    nio_callbacks callbacks{&failed_state, on_connect, on_readable, on_disconnect, on_close};
    const auto directory = utf8(obstructed + L"\\run");
    int32_t error = -1;
    auto *reactor = nio_start_v27("127.0.0.1:0", NIO_ABI_VERSION, &callbacks, sizeof(callbacks),
        64, 1, nullptr, 0, &error, 1, directory.data(), directory.size());
    ReactorGuard guard(reactor);
    require(reactor == nullptr && error == NIO_START_EIO && failed_state.connects == 0,
        "invalid parent startup did not preserve EIO");
  }
  greeting_from_discovery(a, "seek533-A");
  greeting_from_discovery(b, "seek533-B");
  child_a.stop();
  require_discovery_absent(a);
  greeting_from_discovery(b, "seek533-B");
  InstanceChild restarted_a(a, cwd, L"seek533-A2");
  require(greeting_from_discovery(a, "seek533-A2") != first_a, "A restart reused stale endpoint");
  child_b.stop();
  require_discovery_absent(b);
  greeting_from_discovery(a, "seek533-A2");
  InstanceChild restarted_b(b, cwd, L"seek533-B2");
  require(greeting_from_discovery(b, "seek533-B2") != first_b, "B restart reused stale endpoint");
  greeting_from_discovery(a, "seek533-A2");
  restarted_a.stop();
  require_discovery_absent(a);
  greeting_from_discovery(b, "seek533-B2");
  restarted_b.stop();
  require_discovery_absent(b);
  std::cout << "NIO_TWO_INSTANCE_PASS a=" << child_a.pid() << " b=" << child_b.pid()
      << " restarted_a=" << restarted_a.pid() << " restarted_b=" << restarted_b.pid() << '\n';
}
} // namespace

int wmain(int argc, wchar_t **argv)
{
  try {
    if (argc == 6 && std::wstring(argv[1]) == L"--instance-child") {
      instance_child(argv[2], argv[3], argv[4], argv[5]);
      return 0;
    }
    require(argc == 3, "usage: nio_path_probe owned-empty-root policy");
    const std::wstring root = argv[1];
    require(std::filesystem::is_empty(root), "root must be empty");
    DWORD policy = 0, size = sizeof(policy);
    require(RegGetValueW(HKEY_LOCAL_MACHINE, L"SYSTEM\\CurrentControlSet\\Control\\FileSystem",
        L"LongPathsEnabled", RRF_RT_REG_DWORD, nullptr, &policy, &size) == ERROR_SUCCESS,
        "policy read failed");
    require(utf8(argv[2]) == std::to_string(policy), "policy mismatch");
    const HRSRC resource = FindResourceW(nullptr, MAKEINTRESOURCEW(1), MAKEINTRESOURCEW(24));
    win_check(resource != nullptr, "embedded manifest resource");
    const DWORD manifest_size = SizeofResource(nullptr, resource);
    const HGLOBAL loaded = LoadResource(nullptr, resource);
    win_check(loaded != nullptr, "load embedded manifest");
    const auto *manifest_data = static_cast<const char *>(LockResource(loaded));
    require(manifest_data != nullptr && manifest_size != 0, "manifest contents");
    const std::string manifest(manifest_data, manifest_size);
    require(manifest.find("http://schemas.microsoft.com/SMI/2016/WindowsSettings") != std::string::npos &&
        manifest.find(">true</longPathAware>") != std::string::npos, "embedded longPathAware missing");
    std::cout << "NIO_RUNTIME policy=" << policy << " embedded_longPathAware=true\n";
    PathContext context;
    require(static_cast<bool>(context.initialize(utf8(root))), "original cwd capture");
    rejected_inputs();
    derived_boundary_inputs(root);
    require(std::filesystem::is_empty(root), "rejected entry created artifacts");
    {
      // Caller-side instance protection for this exclusive, task-owned root.
      Handle protection(CreateFileW((root + L"\\instance.guard").c_str(), GENERIC_READ | GENERIC_WRITE,
          0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_DELETE_ON_CLOSE, nullptr));
      for (const bool unicode : {false, true}) {
        for (const size_t length : {root.size() + 20, size_t(280), size_t(600), size_t(1200), size_t(2048)}) {
          const auto value = directory_at_length(std::u16string(root.begin(), root.end()), length, unicode);
          run_case(std::wstring(value.begin(), value.end()));
          std::cout << "NIO_LOCAL_PATH_PASS base_units=" << length << " unicode=" << unicode << '\n';
        }
      }
      // Component file envelope, separate from the CLI's 2048-unit base limit.
      const size_t suffix = std::wstring(L"\\run\\sql.pipe.starting-").size() + std::to_wstring(GetCurrentProcessId()).size();
      for (const bool unicode : {false, true}) {
        const auto value = directory_at_length(std::u16string(root.begin(), root.end()), 4096 - suffix, unicode);
        run_case(std::wstring(value.begin(), value.end()));
        std::cout << "NIO_FULL_PATH_PASS staging_units=4096 unicode=" << unicode << '\n';
      }
      two_instances(root, context.original_cwd());
      for (const bool retry_in_drop : {false, true}) {
        const auto cleanup_base = directory_at_length(std::u16string(root.begin(), root.end()), 2048, true);
        sharing_cleanup(std::wstring(cleanup_base.begin(), cleanup_base.end()), retry_in_drop);
      }
    }
    for (const auto &entry : std::filesystem::directory_iterator(root)) {
      std::filesystem::remove_all(extended_path(entry.path().wstring()));
    }
    PathContext final_context;
    require(static_cast<bool>(final_context.initialize(utf8(root))) &&
        final_context.original_cwd() == context.original_cwd(), "NIO changed process cwd");
    std::cout << "NIO_COMPONENT_PASS policy=" << policy << '\n';
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "NIO_COMPONENT_FAILURE " << error.what() << '\n';
    return 1;
  }
}
