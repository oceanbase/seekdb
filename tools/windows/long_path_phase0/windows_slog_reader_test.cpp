// Copyright (c) 2026 OceanBase. Licensed under the Apache License, Version 2.0.
#include "share/redolog/ob_log_file_reader.h"
#include "share/ob_io_device_helper.h"
#include "share/ob_device_manager.h"
#include "share/ob_i_local_device_space_provider.h"
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
