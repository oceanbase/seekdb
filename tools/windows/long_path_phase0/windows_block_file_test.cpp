// Copyright (c) 2026 OceanBase. Licensed under the Apache License, Version 2.0.
#include "share/ob_errno.h"
#include "share/ob_io_device_helper.h"
#include "lib/file/windows_file_path.h"
#include "lib/file/file_directory_utils.h"
#include "lib/allocator/page_arena.h"
#include "path_fixture.h"
#include "logservice/palf/log_io_utils.h"
#include "logservice/palf/palf_env_impl.h"
#include <memory>
#include "logservice/palf/log_block_pool_interface.h"
#include "logservice/ob_server_log_block_mgr.h"
#include <filesystem>
#include <iostream>
#include <thread>
#include <chrono>
#include <stdexcept>
#include <io.h>
#include <winioctl.h>
void request_finish_callback() { std::abort(); }
using namespace oceanbase::common;
using namespace oceanbase::share;
static void require(bool value, const char *message)
{ if (!value) { throw std::runtime_error(message); } }
struct FileState {
  ObSqlString path;
  int fd = -1;
  int64_t size = 0, block = 2 * 1024 * 1024, count = 0;
  int64_t *free = nullptr;
  bool *bitmap = nullptr;
  int64_t free_count = 0, push = 0, pop = 0;
  void close() {
    if (fd >= 0) { _close(fd); fd = -1; }
    ob_free(free); free = nullptr;
    ob_free(bitmap); bitmap = nullptr;
  }
  ~FileState() { close(); }
};
class ScanNames : public oceanbase::palf::ObBaseDirFunctor
{
public:
  int count = 0;
  bool found = false;
  int func(const dirent *entry) override {
    ++count;
    found = found || strcmp(entry->d_name, "palf-ccccc") == 0;
    return OB_SUCCESS;
  }
};
class RemovingPool : public oceanbase::palf::ILogBlockPool
{
public:
  int removed = 0;
  int failure = OB_SUCCESS;
  int create_block_at(const oceanbase::palf::FileDesc &, const char *, const int64_t) override {
    return OB_NOT_SUPPORTED;
  }
  int remove_block_at(const oceanbase::palf::FileDesc &fd, const char *name) override {
    if (failure != OB_SUCCESS) { return failure; }
    ObArenaAllocator allocator;
    WindowsFilePath path(allocator);
    int ret = path.assign_at(reinterpret_cast<HANDLE>(_get_osfhandle(fd)), name);
    if (ret == OB_SUCCESS) {
      ret = path.delete_file();
      if (ret != OB_SUCCESS && path.win32_error() == ERROR_ACCESS_DENIED) {
        ret = OB_FILE_OR_DIRECTORY_PERMISSION_DENIED;
      }
    }
    if (ret == OB_SUCCESS) { ++removed; }
    return ret;
  }
};
int main(int argc, char **argv)
{
  std::wstring owned_prefix;
  try {
    const size_t units = argc == 2 ? std::stoul(argv[1]) : 2048;
    const auto cwd = std::filesystem::current_path();
    const auto prefix = cwd.wstring() + L"\\block-test-" + std::to_wstring(GetCurrentProcessId());
    require(!std::filesystem::exists(prefix), "empty fixture");
    owned_prefix = prefix;
    const auto generated = seekdb_phase0::directory_at_length(std::u16string(prefix.begin(), prefix.end()), units, true);
    const std::wstring root(generated.begin(), generated.end());
    const int bytes = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, root.data(), static_cast<int>(root.size()), nullptr, 0, nullptr, nullptr);
    require(bytes > 0, "UTF8 size");
    std::string utf8(bytes, '\0');
    require(bytes == WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, root.data(), static_cast<int>(root.size()), utf8.data(), bytes, nullptr, nullptr), "UTF8 conversion");
    const std::string sstable = utf8 + "/sstable";
    ObArenaAllocator allocator;
    WindowsFilePath directory(allocator);
    require(directory.assign(sstable.c_str()) == OB_SUCCESS && directory.create_directory(true) == OB_SUCCESS, "create sstable");
    if (units == 4077) {
      // The parent and staging directory fit, but staging/log and staging/meta
      // exceed the full-path budget. Preflight must reject before mkdir.
      const std::string candidate = sstable + "/child";
      auto env = std::make_unique<oceanbase::palf::PalfEnvImpl>();
      require(env->create_directory(candidate.c_str()) == OB_SIZE_OVERFLOW,
          "PALF rejects overlong child before creating staging directory");
      WindowsFilePath staging(allocator);
      bool exists = true;
      require(staging.assign((candidate + ".tmp").c_str()) == OB_SUCCESS
          && FileDirectoryUtils::is_exists(staging.utf8(), exists) == OB_SUCCESS && !exists,
          "PALF preflight leaves no staging directory");
    }
    bool empty_directory = false;
    require(oceanbase::logservice::ObServerLogBlockMgr::check_clog_directory_is_empty(
        sstable.c_str(), empty_directory) == OB_SUCCESS && empty_directory,
        "production empty long directory");
    // Exercise the production directory-relative PALF entry points.
    struct ScopedFd {
      int value = -1;
      ~ScopedFd() { if (value >= 0) { _close(value); } }
    } palf_directory, palf_file;
    palf_directory.value = oceanbase::palf::open_directory(sstable.c_str());
    require(palf_directory.value >= 0, "PALF open directory");
    require(oceanbase::palf::fsync_with_retry(palf_directory.value) == OB_SUCCESS, "PALF directory flush");
    require(oceanbase::palf::fsync_with_retry(-1) != OB_SUCCESS, "PALF invalid flush terminates");
    const HANDLE readonly_directory = CreateFileW(directory.wide(), FILE_READ_ATTRIBUTES,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING,
        FILE_FLAG_BACKUP_SEMANTICS, nullptr);
    require(readonly_directory != INVALID_HANDLE_VALUE, "read only directory");
    ScopedFd readonly_fd;
    readonly_fd.value = _open_osfhandle(reinterpret_cast<intptr_t>(readonly_directory), _O_RDONLY);
    if (readonly_fd.value < 0) { CloseHandle(readonly_directory); }
    require(readonly_fd.value >= 0, "read only directory fd");
    require(oceanbase::palf::fsync_with_retry(readonly_fd.value) == OB_FILE_OR_DIRECTORY_PERMISSION_DENIED,
        "PALF denied flush must propagate and terminate");
    require(_close(readonly_fd.value) == 0, "close read only directory");
    readonly_fd.value = -1;
    require(oceanbase::palf::openat_with_retry(palf_directory.value, "palf-aaaaa",
        _O_CREAT | _O_EXCL | _O_RDWR, _S_IREAD | _S_IWRITE, palf_file.value) == OB_SUCCESS,
        "PALF create relative file");
    require(oceanbase::logservice::ObServerLogBlockMgr::check_clog_directory_is_empty(
        sstable.c_str(), empty_directory) == OB_SUCCESS && !empty_directory,
        "production populated long directory");
    const char palf_marker[] = "PALF-533";
    require(_write(palf_file.value, palf_marker, sizeof(palf_marker)) == sizeof(palf_marker) &&
        _commit(palf_file.value) == 0, "PALF write commit");
    require(_close(palf_file.value) == 0, "PALF close file");
    palf_file.value = -1;
    WindowsFilePath relative_source(allocator);
    require(relative_source.assign((sstable + "/palf-aaaaa").c_str()) == OB_SUCCESS,
        "relative rename source path");
    HANDLE relative_held = CreateFileW(relative_source.wide(), GENERIC_READ,
        FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    require(relative_held != INVALID_HANDLE_VALUE, "hold relative rename source");
    std::thread relative_release([relative_held]() {
      std::this_thread::sleep_for(std::chrono::milliseconds(150));
      CloseHandle(relative_held);
    });
    const int relative_result = oceanbase::palf::renameat_with_retry(palf_directory.value,
        "palf-aaaaa", palf_directory.value, "palf-bbbbb");
    relative_release.join();
    require(relative_result == OB_SUCCESS, "PALF relative rename retries sharing conflict");
    require(oceanbase::palf::renameat_with_retry(palf_directory.value, "palf-aaaaa",
        palf_directory.value, "palf-ddddd") != OB_SUCCESS, "PALF missing relative rename terminates");
    require(oceanbase::palf::check_renameat_success(palf_directory.value, "palf-aaaaa",
        palf_directory.value, "palf-bbbbb"), "PALF confirm rename");
    require(oceanbase::palf::openat_with_retry(palf_directory.value, "palf-bbbbb",
        _O_RDONLY, 0, palf_file.value) == OB_SUCCESS, "PALF reopen");
    char palf_value[sizeof(palf_marker)] = {};
    require(_read(palf_file.value, palf_value, sizeof(palf_value)) == sizeof(palf_value) &&
        memcmp(palf_value, palf_marker, sizeof(palf_marker)) == 0, "PALF read committed data");
    bool palf_exists = true;
    require(oceanbase::palf::check_file_exist(-1, "palf-bbbbb", palf_exists) != OB_SUCCESS &&
        !palf_exists, "PALF invalid directory must fail");
    int invalid_fd = -1;
    require(oceanbase::palf::openat_with_retry(palf_directory.value, "bad\xff",
        _O_RDONLY, 0, invalid_fd) != OB_SUCCESS && invalid_fd == -1, "PALF invalid UTF8 terminates");
    require(_close(palf_file.value) == 0, "PALF close read file");
    palf_file.value = -1;
    const std::string source = sstable + "/palf-bbbbb";
    const std::string destination = sstable + "/palf-ccccc";
    WindowsFilePath held_path(allocator);
    require(held_path.assign(source.c_str()) == OB_SUCCESS, "held rename path");
    HANDLE held = CreateFileW(held_path.wide(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
        nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    require(held != INVALID_HANDLE_VALUE, "hold rename source without delete sharing");
    std::thread release([held]() {
      std::this_thread::sleep_for(std::chrono::milliseconds(150));
      CloseHandle(held);
    });
    const int rename_result = oceanbase::palf::rename_with_retry(source.c_str(), destination.c_str());
    release.join();
    require(rename_result == OB_SUCCESS,
        "PALF absolute rename");
    require(oceanbase::palf::check_rename_success(source.c_str(), destination.c_str()),
        "PALF absolute rename confirmation");
    require(oceanbase::palf::check_file_exist(destination.c_str(), palf_exists) == OB_SUCCESS && palf_exists,
        "PALF absolute existence");
    require(oceanbase::palf::check_file_exist("bad\xff", palf_exists) == OB_INVALID_ARGUMENT && !palf_exists,
        "PALF invalid absolute existence");
    require(oceanbase::palf::rename_with_retry("bad\xff", destination.c_str()) == OB_INVALID_ARGUMENT,
        "PALF invalid absolute rename terminates");
    const std::string missing = sstable + "/palf-ddddd";
    require(oceanbase::palf::rename_with_retry(source.c_str(), missing.c_str()) != OB_SUCCESS,
        "PALF missing rename terminates");
    // The production pool accepts non-block cleanup before init. Exercise that
    // branch directly, including its real unlink retry and error conversion.
    require(FileDirectoryUtils::fsync_dir(sstable.c_str()) == OB_SUCCESS,
        "production directory flush succeeds on long path");
    require(FileDirectoryUtils::fsync_dir("bad\xff") != OB_SUCCESS,
        "production directory flush rejects invalid UTF8");
    oceanbase::logservice::ObServerLogBlockMgr production_pool;
    WindowsFilePath production_file(allocator);
    require(production_file.assign((sstable + "/pool-check").c_str()) == OB_SUCCESS,
        "production pool file path");
    int production_fd = -1;
    require(production_file.open(_O_CREAT | _O_EXCL | _O_RDWR,
        _S_IREAD | _S_IWRITE, production_fd) == OB_SUCCESS, "production pool fixture");
    require(_close(production_fd) == 0, "close production fixture");
    require(SetFileAttributesW(production_file.wide(), FILE_ATTRIBUTE_READONLY) != 0,
        "protect production fixture");
    const int production_denied = production_pool.remove_block_at(palf_directory.value, "pool-check");
    require(SetFileAttributesW(production_file.wide(), FILE_ATTRIBUTE_NORMAL) != 0,
        "restore production fixture");
    require(production_denied == OB_FILE_OR_DIRECTORY_PERMISSION_DENIED,
        "production pool propagates deletion denial");
    HANDLE production_held = CreateFileW(production_file.wide(), GENERIC_READ,
        FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    require(production_held != INVALID_HANDLE_VALUE, "hold production deletion");
    std::thread production_release([production_held]() {
      std::this_thread::sleep_for(std::chrono::milliseconds(150));
      CloseHandle(production_held);
    });
    const int production_removed = production_pool.remove_block_at(palf_directory.value, "pool-check");
    production_release.join();
    require(production_removed == OB_SUCCESS, "production pool retries shared deletion");
    require(oceanbase::palf::check_file_exist(production_file.utf8(), palf_exists) == OB_SUCCESS
        && !palf_exists, "production pool actually deleted file");
    if (units <= 2048) {
      const std::string pool_root = utf8 + "/pool";
      WindowsFilePath pool_path(allocator);
      for (const char *suffix : {"/sys/log_stream/log", "/sys/log_stream/meta", "/sys/tmp_dir", "/log_pool"}) {
        require(pool_path.assign((pool_root + suffix).c_str()) == OB_SUCCESS
            && pool_path.create_directory(true) == OB_SUCCESS, "production pool layout");
      }
      const auto disk_config = [](int64_t &size, int64_t &percentage, int64_t &total) -> int {
        size = total = 64 * 1024 * 1024;
        percentage = 0;
        return OB_SUCCESS;
      };
      require(production_pool.init(pool_root.c_str(), disk_config) == OB_SUCCESS,
          "production pool initializes long layout");
      require(production_pool.create_block_at(palf_directory.value, "missing-parent/block",
          oceanbase::palf::PALF_PHY_BLOCK_SIZE) != OB_SUCCESS,
          "production pool returns missing parent instead of retrying forever");
      const std::string oversized_component(256, 'x');
      require(production_pool.create_block_at(palf_directory.value, oversized_component.c_str(),
          oceanbase::palf::PALF_PHY_BLOCK_SIZE) != OB_SUCCESS,
          "production pool returns invalid component instead of retrying forever");
      require(production_pool.create_block_at(palf_directory.value, "palf-ccccc",
          oceanbase::palf::PALF_PHY_BLOCK_SIZE) != OB_SUCCESS,
          "production pool returns existing destination instead of retrying forever");
      require(oceanbase::palf::openat_with_retry(palf_directory.value, "palf-ccccc",
          _O_RDONLY, 0, palf_file.value) == OB_SUCCESS, "open preserved collision file");
      memset(palf_value, 0, sizeof(palf_value));
      require(_read(palf_file.value, palf_value, sizeof(palf_value)) == sizeof(palf_value)
          && memcmp(palf_value, palf_marker, sizeof(palf_marker)) == 0,
          "production pool preserves existing destination data");
      require(_close(palf_file.value) == 0, "close preserved collision file");
      palf_file.value = -1;
      int64_t usage = -1;
      require(production_pool.get_disk_usage(usage) == OB_SUCCESS && usage == 0,
          "production pool scans empty log and meta");
      production_pool.destroy();
      require(pool_path.assign((pool_root + "/sys/unexpected").c_str()) == OB_SUCCESS
          && pool_path.create_directory(false) == OB_SUCCESS, "unexpected runtime fixture");
      require(production_pool.init(pool_root.c_str(), disk_config) == OB_ERR_UNEXPECTED,
          "production pool rejects unexpected directory");
      require(pool_path.delete_directory() == OB_SUCCESS, "remove unexpected fixture");
      require(production_pool.init(pool_root.c_str(), disk_config) == OB_SUCCESS,
          "production pool retries initialization");
      production_pool.destroy();
    }
    require(_close(palf_directory.value) == 0, "PALF close directory");
    palf_directory.value = -1;
    ScanNames names;
    require(oceanbase::palf::scan_dir(sstable.c_str(), names) == OB_SUCCESS,
        "wide PALF scan");
    require(names.count > 0 && names.found, "PALF scan preserves entry names");
    require(oceanbase::palf::scan_dir("bad\xff", names) == OB_INVALID_ARGUMENT,
        "PALF scan rejects invalid UTF8");
    RemovingPool pool;
    WindowsFilePath protected_file(allocator);
    require(protected_file.assign(destination.c_str()) == OB_SUCCESS, "protected file path");
    require(SetFileAttributesW(protected_file.wide(), FILE_ATTRIBUTE_READONLY) != 0,
        "set test file readonly");
    const int denied_remove = oceanbase::palf::remove_directory_rec(sstable.c_str(), &pool);
    require(SetFileAttributesW(protected_file.wide(), FILE_ATTRIBUTE_NORMAL) != 0,
        "restore test file attributes");
    require(denied_remove == OB_FILE_OR_DIRECTORY_PERMISSION_DENIED && pool.removed == 0,
        "PALF deletion preserves permission failure");
    pool.failure = OB_IO_ERROR;
    require(oceanbase::palf::remove_directory_rec(sstable.c_str(), &pool) == OB_IO_ERROR,
        "PALF cleanup propagates pool failure");
    require(pool.removed == 0, "failed pool leaves files owned");
    pool.failure = OB_SUCCESS;
    require(oceanbase::palf::remove_tmp_file_or_directory_at(sstable.c_str(), &pool) == OB_SUCCESS
        && pool.removed == 0, "temporary cleanup preserves regular files");
    require(oceanbase::palf::remove_directory_rec(sstable.c_str(), &pool) == OB_SUCCESS
        && pool.removed == 1, "recursive cleanup uses pool");
    require(directory.create_directory(true) == OB_SUCCESS, "recreate sstable after cleanup");
    if (units <= 2048) {
      auto create_empty = [&](const std::string &name) {
        WindowsFilePath file(allocator);
        int fd = -1;
        require(file.assign(name.c_str()) == OB_SUCCESS
            && file.open(_O_CREAT | _O_EXCL | _O_RDWR | _O_BINARY, _S_IREAD | _S_IWRITE, fd) == OB_SUCCESS,
            "create nested cleanup fixture");
        require(_close(fd) == 0, "close nested fixture");
      };
      WindowsFilePath nested(allocator);
      require(nested.assign((sstable + "/nested/sub.tmp").c_str()) == OB_SUCCESS
          && nested.create_directory(true) == OB_SUCCESS, "create nested temporary directory");
      create_empty(sstable + "/nested/keep");
      create_empty(sstable + "/nested/remove.tmp");
      create_empty(sstable + "/nested/sub.tmp/data");
      const int before = pool.removed;
      require(oceanbase::palf::remove_tmp_file_or_directory_at(sstable.c_str(), &pool) == OB_SUCCESS
          && pool.removed == before + 2, "nested temporary cleanup removes selected files");
      bool remains = false;
      require(oceanbase::palf::check_file_exist((sstable + "/nested/keep").c_str(), remains) == OB_SUCCESS
          && remains, "nested ordinary file survives");
      require(oceanbase::palf::remove_directory_rec((sstable + "/nested").c_str(), &pool) == OB_SUCCESS
          && pool.removed == before + 3, "nested final cleanup");
    }

    std::cout << "PALF_DIRECTORY_PASS units=" << units << std::endl;
    FileState state;
    BlockFileAttr attr(state.path, "sstable", "block_file", state.fd, state.size, state.block,
      state.count, state.free, state.bitmap, state.free_count, state.push, state.pop, "PathTest");
    bool exists = false;
    require(ObIODeviceLocalFileOp::open_block_file(utf8.c_str(), sstable.c_str(), state.block,
      4 * 1024 * 1024, 0, 0, exists, attr) == OB_SUCCESS, "first open");
    require(!exists && state.size == 4 * 1024 * 1024, "new file size");
    require(_lseeki64(state.fd, 0, SEEK_SET) == 0, "seek first block");
    const char marker[] = "SEEK-533";
    require(_write(state.fd, marker, sizeof(marker)) == sizeof(marker) && _commit(state.fd) == 0, "write commit");
    state.close();
    require(ObIODeviceLocalFileOp::open_block_file(utf8.c_str(), sstable.c_str(), state.block,
      4 * 1024 * 1024, 0, 0, exists, attr) == OB_SUCCESS && exists, "reopen");
    char value[sizeof(marker)] = {};
    require(_read(state.fd, value, sizeof(value)) == sizeof(value) && memcmp(value, marker, sizeof(value)) == 0, "read committed data");
    ObIODFileStat info;
    require(ObIODeviceLocalFileOp::stat(state.path.ptr(), info) == OB_SUCCESS && info.size_ == 4 * 1024 * 1024, "stat size");
    DWORD returned = 0;
    require(DeviceIoControl(reinterpret_cast<HANDLE>(_get_osfhandle(state.fd)), FSCTL_SET_SPARSE,
      nullptr, 0, nullptr, 0, &returned, nullptr) != 0, "mark owned test file sparse");
    const int64_t large_size = (INT64_C(1) << 32) + state.block;
    LARGE_INTEGER end;
    end.QuadPart = large_size;
    const HANDLE handle = reinterpret_cast<HANDLE>(_get_osfhandle(state.fd));
    if (!SetFilePointerEx(handle, end, nullptr, FILE_BEGIN) || !SetEndOfFile(handle)) {
      throw std::runtime_error("extend sparse file Win32=" + std::to_string(GetLastError()));
    }
    require(ObIODeviceLocalFileOp::stat(state.path.ptr(), info) == OB_SUCCESS && info.size_ == large_size,
      "64 bit stat size");
    state.close();
    require(ObIODeviceLocalFileOp::open_block_file(utf8.c_str(), sstable.c_str(), state.block,
      4 * 1024 * 1024, 0, 0, exists, attr) == OB_SUCCESS && exists && state.size == large_size,
      "reopen preserves 64 bit size");
    require(_read(state.fd, value, sizeof(value)) == sizeof(value) && memcmp(value, marker, sizeof(value)) == 0,
      "sparse extension preserves committed data");
    state.close();
    require(ObIODeviceLocalFileOp::exist("\xff", exists) == OB_INVALID_ARGUMENT, "reject invalid UTF8");
    require(cwd == std::filesystem::current_path(), "cwd unchanged");
    std::filesystem::remove_all(std::filesystem::path(L"\\\\?\\" + prefix));
    std::cout << "BLOCK_FILE_PASS units=" << units << std::endl;
    return 0;
  } catch (const std::exception &e) {
    std::cerr << "BLOCK_FILE_FAIL " << e.what() << std::endl;
    if (!owned_prefix.empty()) {
      std::error_code cleanup_error;
      std::filesystem::remove_all(std::filesystem::path(L"\\\\?\\" + owned_prefix), cleanup_error);
      if (cleanup_error) { std::cerr << "FIXTURE_CLEANUP_FAIL " << cleanup_error.value() << std::endl; }
    }
    return 1;
  }
}
