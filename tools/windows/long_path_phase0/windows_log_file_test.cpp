// Copyright (c) 2026 OceanBase. Licensed under the Apache License, Version 2.0.
#include "lib/oblog/ob_async_log_struct.h"
#include "path_fixture.h"
#include "lib/oblog/ob_log_compressor.h"
#include "lib/allocator/ob_malloc.h"
#include "lib/compress/zstd_1_3_8/ob_zstd_compressor_1_3_8.h"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <io.h>
#include <cstdlib>
// This file-only test must never enter the SQL request completion path.
void request_finish_callback() { std::abort(); }
using namespace oceanbase::common;
namespace {
void require(bool value, const char *message)
{
  if (!value) { throw std::runtime_error(message); }
}
std::string utf8(const std::wstring &value)
{
  int n = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value.data(),
      static_cast<int>(value.size()), nullptr, 0, nullptr, nullptr);
  require(n > 0, "UTF8 size");
  std::string result(n, '\0');
  require(n == WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value.data(),
      static_cast<int>(value.size()), result.data(), n, nullptr, nullptr), "UTF8 conversion");
  return result;
}
std::string read(const std::wstring &path)
{
  std::ifstream in(std::filesystem::path(path), std::ios::binary);
  require(in.is_open(), "read open");
  return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}
// A delete-pending file may reject attribute queries until its last handle closes.
// Access denied is never evidence of deletion: require FILE_NOT_FOUND, bounded
// by the same timeout used for the asynchronous compressor operation.
bool wait_for_deletion(const std::wstring &path)
{
  const ULONGLONG deadline = GetTickCount64() + 20000;
  bool denied = false;
  do {
    if (GetFileAttributesW(path.c_str()) == INVALID_FILE_ATTRIBUTES) {
      const DWORD error = GetLastError();
      if (error == ERROR_FILE_NOT_FOUND) {
        if (denied) { std::cout << "LOG_DELETE_QUERY_RECOVERED" << std::endl; }
        return true;
      }
      if (error != ERROR_ACCESS_DENIED) {
        std::cerr << "LOG_DELETE_QUERY_FAILED win32=" << error << std::endl;
        return false;
      }
      denied = true;
    }
    Sleep(100);
  } while (GetTickCount64() < deadline);
  std::cerr << "LOG_DELETE_TIMEOUT access_denied=" << denied << std::endl;
  return false;
}
void compression(const std::wstring &directory, bool conflict, bool multiblock = false, bool read_failure = false, bool write_failure = false)
{
  const std::wstring wide = L"\\\\?\\" + directory;
  std::filesystem::create_directories(wide);
  const std::wstring source = wide + L"\\observer.log.202609100001";
  const std::wstring output = source + L".zst";
  std::string payload(multiblock ? 2 * OB_SYSLOG_COMPRESS_BLOCK_SIZE + 137 : 65536, 'q');
  for (size_t i = 0; i < payload.size(); ++i) { payload[i] = static_cast<char>((i * 31 + i / 251) % 256); }
  { std::ofstream out(std::filesystem::path(source), std::ios::binary); out << payload; require(out.good(), "source fixture"); }
  HANDLE writer = INVALID_HANDLE_VALUE;
  OVERLAPPED locked_range = {};
  locked_range.Offset = OB_SYSLOG_COMPRESS_BLOCK_SIZE;
  if (conflict) {
    std::ofstream out(std::filesystem::path(output), std::ios::binary); out << "keep";
    require(out.good(), "conflict fixture");
  } else if (!write_failure) {
    writer = CreateFileW(source.c_str(), read_failure ? GENERIC_READ : GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        nullptr, OPEN_EXISTING, 0, nullptr);
    require(writer != INVALID_HANDLE_VALUE, "source handle fixture");
    if (read_failure) {
      require(LockFileEx(writer, LOCKFILE_EXCLUSIVE_LOCK | LOCKFILE_FAIL_IMMEDIATELY,
          0, 1, 0, &locked_range), "lock second source block");
    }
  }
  ObLogCompressor compressor;
  require(compressor.init(utf8(directory).c_str()) == OB_SUCCESS, "compressor init");
  require(compressor.set_max_disk_size(3LL << 30) == OB_SUCCESS, "compression threshold");
  require(compressor.set_min_uncompressed_count(0) == OB_SUCCESS, "compression retention");
  require(compressor.set_compress_func("zstd_1.3.8") == OB_SUCCESS, "compression codec");
  if (write_failure) {
    struct OutputHandle {
      HANDLE value = INVALID_HANDLE_VALUE;
      ~OutputHandle() { if (value != INVALID_HANDLE_VALUE) { CloseHandle(value); } }
    } held;
    const ULONGLONG failure_deadline = GetTickCount64() + 20000;
    LARGE_INTEGER partial_size = {};
    while (partial_size.QuadPart == 0 && GetTickCount64() < failure_deadline) {
      if (held.value == INVALID_HANDLE_VALUE) {
        held.value = CreateFileW(output.c_str(), GENERIC_READ,
            FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
            nullptr, OPEN_EXISTING, 0, nullptr);
      }
      if (held.value != INVALID_HANDLE_VALUE) {
        require(GetFileSizeEx(held.value, &partial_size), "partial output size");
      }
      Sleep(1);
    }
    require(partial_size.QuadPart > 0, "nonempty output not observed");
    OVERLAPPED blocked_output = {};
    blocked_output.Offset = partial_size.LowPart;
    blocked_output.OffsetHigh = partial_size.HighPart;
    require(LockFileEx(held.value, LOCKFILE_EXCLUSIVE_LOCK | LOCKFILE_FAIL_IMMEDIATELY,
        0, 1, 0, &blocked_output), "lock next output byte");
    FILE_STANDARD_INFO info = {};
    while (!info.DeletePending && GetTickCount64() < failure_deadline) {
      require(GetFileInformationByHandleEx(held.value, FileStandardInfo, &info, sizeof(info)),
          "partial output deletion status");
      Sleep(1);
    }
    require(info.DeletePending, "failed output was not marked for deletion");
    require(UnlockFileEx(held.value, 0, 1, 0, &blocked_output), "unlock output byte");
    require(CloseHandle(held.value), "close deleted partial output");
    held.value = INVALID_HANDLE_VALUE;
    require(!std::filesystem::exists(output), "write failure left partial output");
    std::cout << "LOG_PARTIAL_WRITE_CLEANUP_OBSERVED bytes=" << partial_size.QuadPart << std::endl;
  } else if (read_failure) {
    bool partial_seen = false;
    bool partial_removed = false;
    const ULONGLONG failure_deadline = GetTickCount64() + 20000;
    while (!partial_removed && GetTickCount64() < failure_deadline) {
      WIN32_FILE_ATTRIBUTE_DATA info = {};
      if (GetFileAttributesExW(output.c_str(), GetFileExInfoStandard, &info)) {
        partial_seen = partial_seen || info.nFileSizeHigh != 0 || info.nFileSizeLow != 0;
      } else if (GetLastError() == ERROR_FILE_NOT_FOUND) {
        partial_removed = partial_seen;
      }
      Sleep(1);
    }
    require(partial_seen && partial_removed, "partial archive creation and cleanup not observed");
    std::cout << "LOG_PARTIAL_READ_CLEANUP_OBSERVED" << std::endl;
  } else {
    Sleep(6500); // Allow the existing five-second timer to scan the refused file.
  }
  if (read_failure) {
    require(!std::filesystem::exists(output), "failed read left partial archive");
    require(UnlockFileEx(writer, 0, 1, 0, &locked_range), "unlock source block");
  }
  require(read(source) == payload, "refused compression preserved source");
  if (conflict) {
    require(read(output) == "keep", "existing output preserved");
    require(DeleteFileW(output.c_str()), "remove output conflict");
  } else {
    require(!std::filesystem::exists(output), "active source created output");
    if (writer != INVALID_HANDLE_VALUE) { CloseHandle(writer); }
  }
  const bool deleted = wait_for_deletion(source);
  compressor.stop(); compressor.wait(); compressor.destroy();
  require(deleted && !std::filesystem::exists(source), "compression recovery timed out");
  const std::string compressed = read(output);
  std::string decoded(payload.size(), '\0');
  ObMalloc allocator;
  zstd_1_3_8::ObZstdCompressor_1_3_8 decoder(allocator);
  int64_t bytes = 0;
  require(decoder.decompress(compressed.data(), compressed.size(), decoded.data(), decoded.size(), bytes) == OB_SUCCESS,
      "archive decompression");
  require(bytes == payload.size() && decoded == payload, "archive content");
  std::cout << "LOG_COMPRESSION_PASS units=" << directory.size() << " output_units=" << output.size() - 4
      << " bytes=" << payload.size() << " conflict=" << conflict << " read_failure=" << read_failure << " write_failure=" << write_failure << '\n';
}
void retention(const std::wstring &directory)
{
  const std::wstring wide = L"\\\\?\\" + directory;
  std::filesystem::create_directories(wide);
  const std::wstring archive = wide + L"\\observer.log.202609100002.zst";
  const std::wstring active = wide + L"\\observer.log";
  const std::wstring unrelated = wide + L"\\keep.txt";
  for (const auto &path : {archive, active, unrelated}) {
    std::ofstream out(std::filesystem::path(path), std::ios::binary);
    out << "retention-fixture";
    require(out.good(), "retention fixture");
  }
  ObLogCompressor compressor;
  require(compressor.init(utf8(directory).c_str()) == OB_SUCCESS, "retention init");
  // Existing two-GiB reserve: even small fixtures cross the configured limit.
  // This exercises deletion without filling the VM volume or enabling compression.
  require(compressor.set_max_disk_size(2LL << 30) == OB_SUCCESS, "retention limit");
  const bool deleted = wait_for_deletion(archive);
  compressor.stop(); compressor.wait(); compressor.destroy();
  require(deleted && !std::filesystem::exists(archive), "retention deletion timed out");
  require(read(active) == "retention-fixture", "retention preserved active name");
  require(read(unrelated) == "retention-fixture", "retention preserved unrelated file");
  std::cout << "LOG_RETENTION_PASS units=" << directory.size() << '\n';
}
void rotation(const std::wstring &directory)
{
  struct MainLoggingGuard {
    bool previous = g_ob_log_main_entered;
    MainLoggingGuard() { g_ob_log_main_entered = true; }
    ~MainLoggingGuard() { g_ob_log_main_entered = previous; }
  } main_logging_guard;
  const std::wstring wide = L"\\\\?\\" + directory;
  std::filesystem::create_directories(wide);
  const std::wstring path = wide + L"\\observer.log";
  {
    ObLogger logger;
    ObPLogWriterCfg config;
    require(logger.init(config) == OB_SUCCESS, "rotation logger init");
    logger.set_enable_async_log(false);
    logger.set_file_name(utf8(directory + L"\\observer.log").c_str(), true);
    require(logger.get_svr_log().fd_ > 2, "rotation open");
    logger.set_max_file_size(1);
    logger.log_message_fmt("[TEST]", OB_LOG_LEVEL_INFO, __FILE__, __LINE__, __FUNCTION__, 0,
        "%s", "rotation-before");
    logger.set_max_file_size(1LL << 30);
    logger.log_message_fmt("[TEST]", OB_LOG_LEVEL_INFO, __FILE__, __LINE__, __FUNCTION__, 0,
        "%s", "rotation-after");
    require(logger.get_svr_log().close_all() == OB_SUCCESS, "rotation close");
  }
  require(read(path).find("rotation-after") != std::string::npos, "post-rotation content");
  bool found = false;
  for (const auto &entry : std::filesystem::directory_iterator(wide)) {
    if (entry.path().filename().wstring().find(L"observer.log.") == 0) {
      require(read(entry.path().wstring()).find("rotation-before") != std::string::npos,
          "rotated content");
      found = true;
    }
  }
  require(found, "production rotation missing");
  std::cout << "LOG_ROTATION_PASS units=" << directory.size() << std::endl;
}
void exercise(const std::wstring &directory)
{
  const std::wstring wide = L"\\\\?\\" + directory;
  std::filesystem::create_directories(wide);
  const std::wstring path = wide + L"\\seekdb.log";
  const std::wstring rotated = path + L".old";
  std::string input = utf8(directory + L"\\seekdb.log");
  const std::string expected = input;
  ObPLogFileStruct file;
  require(file.open(input.c_str(), false) == OB_SUCCESS, "production open");
  input.assign(input.size(), 'x');
  require(expected == file.filename_, "filename ownership");
  const int fd = file.fd_;
  require(_write(fd, "first", 5) == 5, "first write");
  bool changed = true;
  require(file.needs_reopen(changed) == OB_SUCCESS && !changed, "same identity");
  require(MoveFileW(path.c_str(), rotated.c_str()), "rename active log");
  require(file.needs_reopen(changed) == OB_SUCCESS && changed, "missing identity");
  // An existing directory at the log name forces OPEN_ALWAYS to fail.
  require(CreateDirectoryW(path.c_str(), nullptr), "failure fixture");
  require(file.reopen(false) != OB_SUCCESS && file.fd_ == fd, "reopen failure preserves fd");
  require(_write(fd, "-kept", 5) == 5, "write after reopen failure");
  require(RemoveDirectoryW(path.c_str()), "remove failure fixture");
  HANDLE replacement = CreateFileW(path.c_str(), GENERIC_WRITE,
      FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, CREATE_NEW, 0, nullptr);
  require(replacement != INVALID_HANDLE_VALUE, "replacement create");
  CloseHandle(replacement);
  require(file.needs_reopen(changed) == OB_SUCCESS && changed, "replacement identity");
  require(file.reopen(false) == OB_SUCCESS && file.fd_ == fd, "stable fd on reopen");
  require(_write(fd, "new", 3) == 3, "replacement write");
  require(file.needs_reopen(changed) == OB_SUCCESS && !changed, "reopened identity");
  require(file.open(utf8(directory).c_str(), false) != OB_SUCCESS, "directory open rejected");
  require(expected == file.filename_ && file.fd_ == fd, "failed OS open preserves owner");
  require(file.open("C:\\invalid<name", false) != OB_SUCCESS, "invalid open rejected");
  require(expected == file.filename_ && file.fd_ == fd, "failed open preserves owner");
  require(_write(fd, "-tail", 5) == 5, "write after invalid open");
  require(file.close_all() == OB_SUCCESS && file.close_all() == OB_SUCCESS, "repeat close");
  require(read(rotated) == "first-kept", "archived data preserved");
  require(read(path) == "new-tail", "replacement data");
  std::cout << "LOG_FILE_CASE_PASS units=" << directory.size() << '\n';
}
}
int main()
{
  try {
    const std::wstring root = std::filesystem::current_path().wstring()
        + L"\\log-file-test-" + std::to_wstring(GetCurrentProcessId());
    require(!std::filesystem::exists(root), "test root must be absent");
    for (bool unicode : {false, true}) {
      for (size_t size : {size_t(280), size_t(2048)}) {
        auto u = seekdb_phase0::directory_at_length(
            std::u16string(root.begin(), root.end()), size, unicode);
        exercise(std::wstring(u.begin(), u.end()));
      }
    }
    for (bool conflict : {false, true}) {
      auto u = seekdb_phase0::directory_at_length(
          std::u16string(root.begin(), root.end()) + (conflict ? u"\\conflict" : u"\\active"), 2048, true);
      compression(std::wstring(u.begin(), u.end()), conflict);
    }
    const size_t archive_suffix = std::wstring(L"\\observer.log.202609100001.zst").size();
    auto boundary = seekdb_phase0::directory_at_length(
        std::u16string(root.begin(), root.end()) + u"\\multiblock", 4096 - archive_suffix, true);
    compression(std::wstring(boundary.begin(), boundary.end()), false, true);
    auto locked = seekdb_phase0::directory_at_length(
        std::u16string(root.begin(), root.end()) + u"\\read-failure", 2048, true);
    compression(std::wstring(locked.begin(), locked.end()), false, true, true);
    auto write_locked = seekdb_phase0::directory_at_length(
        std::u16string(root.begin(), root.end()) + u"\\write-failure", 2048, true);
    compression(std::wstring(write_locked.begin(), write_locked.end()), false, true, false, true);
    auto retained = seekdb_phase0::directory_at_length(
        std::u16string(root.begin(), root.end()) + u"\\retention", 2048, true);
    retention(std::wstring(retained.begin(), retained.end()));
    auto rotated = seekdb_phase0::directory_at_length(
        std::u16string(root.begin(), root.end()) + u"\\rotation", 2048, true);
    rotation(std::wstring(rotated.begin(), rotated.end()));
    std::filesystem::remove_all(L"\\\\?\\" + root);
    require(!std::filesystem::exists(root), "cleanup");
    std::cout << "LOG_FILE_LIFECYCLE_PASS cases=4\n";
    return 0;
  } catch (const std::exception &e) {
    std::cerr << "LOG_FILE_LIFECYCLE_FAIL " << e.what() << '\n';
    return 1;
  }
}
