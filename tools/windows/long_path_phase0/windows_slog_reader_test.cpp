// Copyright (c) 2026 OceanBase. Licensed under the Apache License, Version 2.0.
#include "share/redolog/ob_log_file_reader.h"
#include "share/ob_io_device_helper.h"
#include "share/ob_device_manager.h"
#include "share/ob_i_local_device_space_provider.h"
#include "storage/slog/ob_storage_logger.h"
#include "lib/file/windows_file_path.h"
#include "lib/allocator/page_arena.h"
#include "path_fixture.h"
#include <filesystem>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <io.h>
#include <fcntl.h>
#include <malloc.h>

void request_finish_callback() { std::abort(); }
using namespace oceanbase::common;
using namespace oceanbase::share;
static void require(bool ok, const char *stage)
{ if (!ok) { throw std::runtime_error(stage); } }
static void check_cleanup(const std::string &instance_root)
{
  ObArenaAllocator allocator;
  WindowsFilePath directory(allocator), file(allocator);
  const std::string slog_root = instance_root + "/slog";
  require(directory.assign((slog_root + "/sys").c_str()) == OB_SUCCESS &&
      directory.create_directory(true) == OB_SUCCESS, "create cleanup directory");
  const auto mutation_dir = instance_root + "/mutation";
  require(ObIODeviceLocalFileOp::mkdir(mutation_dir.c_str(), 0700) == OB_SUCCESS &&
      ObIODeviceLocalFileOp::mkdir(mutation_dir.c_str(), 0700) == OB_SUCCESS, "wide mkdir and existing directory");
  const auto original = mutation_dir + "/original";
  const auto renamed = mutation_dir + "/renamed";
  int mutation_fd = -1;
  require(file.assign(original.c_str()) == OB_SUCCESS &&
      file.open(_O_CREAT | _O_EXCL | _O_WRONLY | _O_BINARY, 0600, mutation_fd) == OB_SUCCESS,
      "create mutation file");
  require(_close(mutation_fd) == 0, "close mutation file");
  require(ObIODeviceLocalFileOp::rename(original.c_str(), renamed.c_str()) == OB_SUCCESS,
      "wide rename");
  WIN32_FILE_ATTRIBUTE_DATA moved = {};
  require(file.get_info(moved) == OB_FILE_NOT_EXIST && file.assign(renamed.c_str()) == OB_SUCCESS &&
      file.get_info(moved) == OB_SUCCESS, "rename moves physical file");
  require(ObIODeviceLocalFileOp::rmdir(mutation_dir.c_str()) != OB_SUCCESS &&
      file.get_info(moved) == OB_SUCCESS, "nonempty rmdir preserves file");
  require(ObIODeviceLocalFileOp::rename(original.c_str(), renamed.c_str()) == OB_NO_SUCH_FILE_OR_DIRECTORY,
      "missing rename source fails");
  require(ObIODeviceLocalFileOp::unlink(renamed.c_str()) == OB_SUCCESS &&
      file.get_info(moved) == OB_FILE_NOT_EXIST, "wide unlink");
  require(ObIODeviceLocalFileOp::rmdir(mutation_dir.c_str()) == OB_SUCCESS &&
      file.assign(mutation_dir.c_str()) == OB_SUCCESS && file.get_info(moved) == OB_FILE_NOT_EXIST,
      "wide rmdir");
  for (int id = 1; id <= 3; ++id) {
    int fd = -1;
    const auto name = slog_root + "/sys/" + std::to_string(id);
    require(file.assign(name.c_str()) == OB_SUCCESS &&
        file.open(_O_CREAT | _O_EXCL | _O_WRONLY | _O_BINARY, 0600, fd) == OB_SUCCESS, "create cleanup file");
    const bool written = _write(fd, "retained", 8) == 8 && _commit(fd) == 0;
    const int closed = _close(fd);
    require(written && closed == 0, "write cleanup file");
  }
  oceanbase::blocksstable::ObLogFileSpec spec;
  spec.retry_write_policy_ = "normal";
  spec.log_create_policy_ = "normal";
  spec.log_write_policy_ = "truncate";
  oceanbase::storage::ObStorageLogger slogger;
  require(slogger.init(slog_root.c_str(), 64 * 1024 * 1024, spec) == OB_SUCCESS, "slogger init");
  auto begin = GetTickCount64();
  require(slogger.remove_useless_log_file(3) == OB_SUCCESS, "checkpoint cleanup");
  require(GetTickCount64() - begin < 5000, "bounded checkpoint cleanup");
  WIN32_FILE_ATTRIBUTE_DATA info = {};
  for (int id = 1; id <= 2; ++id) {
    require(file.assign((slog_root + "/sys/" + std::to_string(id)).c_str()) == OB_SUCCESS &&
        file.get_info(info) == OB_FILE_NOT_EXIST, "obsolete slog physically deleted");
  }
  require(file.assign((slog_root + "/sys/3").c_str()) == OB_SUCCESS &&
      file.get_info(info) == OB_SUCCESS && info.nFileSizeLow == 8, "active slog retained");
  require(SetFileAttributesW(file.wide(), FILE_ATTRIBUTE_READONLY), "set deletion denial");
  begin = GetTickCount64();
  const int denied = slogger.remove_useless_log_file(4);
  const auto elapsed = GetTickCount64() - begin;
  require(SetFileAttributesW(file.wide(), FILE_ATTRIBUTE_NORMAL), "restore file attributes");
  require(denied == OB_FILE_OR_DIRECTORY_PERMISSION_DENIED && elapsed < 5000,
      "permanent cleanup error returned without infinite retry");
  require(file.get_info(info) == OB_SUCCESS, "denied deletion preserves slog");
  require(slogger.remove_useless_log_file(4) == OB_SUCCESS && file.get_info(info) == OB_FILE_NOT_EXIST,
      "cleanup succeeds after permission restored");
  require(slogger.remove_useless_log_file(4) == OB_SUCCESS, "already absent files are harmless");
  slogger.destroy();
  std::puts("SLOG_CHECKPOINT_CLEANUP_PASS base_units=2048 deleted=2 retained=1 denied_bounded=1 recovered=1");
}
class FixtureSpaceProvider final : public ObILocalDeviceSpaceProvider
{
public:
  int get_reserved_size(int64_t &size) const override
  { size = 0; return OB_SUCCESS; }
};
int main()
{
  FixtureSpaceProvider space_provider;
  auto &device = ObSNIODeviceWrapper::get_instance();
  auto &reader = ObLogFileReader2::get_instance();
  try {
    const auto prefix = std::filesystem::current_path().wstring() + L"\\slog-reader-" +
        std::to_wstring(GetCurrentProcessId());
    require(!std::filesystem::exists(prefix), "fresh fixture");
    const auto generated = seekdb_phase0::directory_at_length(
        std::u16string(prefix.begin(), prefix.end()), 2048, true);
    const std::wstring root(generated.begin(), generated.end());
    int bytes = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, root.data(),
        static_cast<int>(root.size()), nullptr, 0, nullptr, nullptr);
    require(bytes > 0, "UTF8 size");
    std::string directory(bytes, '\0');
    require(WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, root.data(),
        static_cast<int>(root.size()), directory.data(), bytes, nullptr, nullptr) == bytes, "UTF8 conversion");
    ObArenaAllocator allocator;
    WindowsFilePath dir(allocator);
    require(dir.assign(directory.c_str()) == OB_SUCCESS &&
        dir.create_directory(true) == OB_SUCCESS, "create directory");
    for (int id = 1; id <= 2; ++id) {
      WindowsFilePath file(allocator);
      const auto name = directory + "/" + std::to_string(id);
      int fd = -1;
      require(file.assign(name.c_str()) == OB_SUCCESS &&
          file.open(_O_CREAT | _O_EXCL | _O_WRONLY | _O_BINARY, 0600, fd) == OB_SUCCESS, "create file");
      char data[4096];
      std::memset(data, id, sizeof(data));
      const bool written = _write(fd, data, sizeof(data)) == sizeof(data) && _commit(fd) == 0;
      const int closed = _close(fd);
      require(written && closed == 0, "write file");
    }
    require(ObDeviceManager::get_instance().init_devices_env() == OB_SUCCESS, "device manager init");
    const int device_ret = device.init(directory.c_str(), directory.c_str(), 2 * 1024 * 1024,
        0, 32 * 1024 * 1024, space_provider);
    if (device_ret != OB_SUCCESS) { std::fprintf(stderr, "LOCAL_DEVICE_INIT ret=%d\n", device_ret); }
    require(device_ret == OB_SUCCESS, "local device init");
    require(reader.init() == OB_SUCCESS, "reader init");
    {
      ObLogReadFdHandle first, second, reopened;
      require(reader.get_fd(directory.c_str(), 1, first) == OB_SUCCESS, "first fd");
      require(reader.get_fd(directory.c_str(), 2, second) == OB_SUCCESS, "second fd");
      require(reader.evict_fd(directory.c_str(), 1) == OB_SUCCESS, "evict first");
      require(reader.get_fd(directory.c_str(), 1, reopened) == OB_SUCCESS, "reopen first");
      alignas(4096) char data[4096];
      ObLogReadFdHandle *handles[] = {&first, &second, &reopened};
      const int markers[] = {1, 2, 1};
      for (int i = 0; i < 3; ++i) {
        int64_t size = 0;
        require(reader.pread(*handles[i], data, sizeof(data), 0, size) == OB_SUCCESS &&
            size == sizeof(data), "read data");
        for (char value : data) { require(value == markers[i], "independent file contents"); }
      }
    }
    reader.destroy();
    check_cleanup(directory);
    device.destroy();
    ObDeviceManager::get_instance().destroy();
    std::filesystem::remove_all(std::filesystem::path(L"\\\\?\\" + prefix));
    std::puts("SLOG_READER_LONG_PATH_PASS");
    return 0;
  } catch (const std::exception &error) {
    reader.destroy();
    device.destroy();
    ObDeviceManager::get_instance().destroy();
    std::fprintf(stderr, "SLOG_READER_FAIL: %s\n", error.what());
    return 1;
  }
}
