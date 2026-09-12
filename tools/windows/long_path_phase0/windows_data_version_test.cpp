// Copyright (c) 2026 OceanBase. Licensed under the Apache License, Version 2.0.
#include "common/ob_data_version_mgr.h"
#include "lib/allocator/ob_malloc.h"
#include "path_fixture.h"
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <cstdlib>
void request_finish_callback() { std::abort(); }
using namespace oceanbase::common;
namespace {
void require(bool ok, const char *message) { if (!ok) { throw std::runtime_error(message); } }
void exercise(const std::wstring &directory)
{
  const std::wstring wide = L"\\\\?\\" + directory;
  std::filesystem::create_directories(wide);
  const int bytes = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, directory.data(),
      static_cast<int>(directory.size()), nullptr, 0, nullptr, nullptr);
  require(bytes > 0, "encoding length");
  std::string path(bytes, '\0');
  require(bytes == WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, directory.data(),
      static_cast<int>(directory.size()), path.data(), bytes, nullptr, nullptr), "encoding");
  {
    ObDataVersionMgr first;
    require(first.init(path.c_str()) == OB_SUCCESS, "init");
    require(first.load_from_file() == OB_SUCCESS && !first.get_file_exists_when_loading(), "missing file");
    require(first.validate_or_init_current_version() == OB_SUCCESS, "initialize version");
  }
  require(std::filesystem::exists(wide + L"\\seekdb.data_version.bin"), "version file location");
  require(!std::filesystem::exists(wide + L"\\seekdb.data_version.bin.tmp"), "published temporary file");
  {
    ObDataVersionMgr second;
    require(second.init(path.c_str()) == OB_SUCCESS, "reinit");
    require(second.load_from_file() == OB_SUCCESS && second.get_file_exists_when_loading(), "load persisted version");
    require(second.validate_or_init_current_version() == OB_SUCCESS, "validate persisted version");
  }
  // Exercise the existing backup boundary with a valid pre-existing file.
  {
    ObDataVersionMgr replacement;
    require(replacement.init(path.c_str()) == OB_SUCCESS, "replacement init");
    require(replacement.validate_or_init_current_version() == OB_SUCCESS, "replace version");
    require(std::filesystem::exists(wide + L"\\seekdb.data_version.bin.history"), "history location");
  }
  // A directory at the temporary filename forces a real open failure.
  std::filesystem::remove(wide + L"\\seekdb.data_version.bin");
  std::filesystem::create_directory(wide + L"\\seekdb.data_version.bin.tmp");
  {
    ObDataVersionMgr retry;
    require(retry.init(path.c_str()) == OB_SUCCESS, "retry init");
    require(retry.validate_or_init_current_version() != OB_SUCCESS, "temporary open must fail");
    require(!std::filesystem::exists(wide + L"\\seekdb.data_version.bin"), "failed write not published");
    require(std::filesystem::exists(wide + L"\\seekdb.data_version.bin.history"), "failure preserves history");
    std::filesystem::remove(wide + L"\\seekdb.data_version.bin.tmp");
    require(retry.validate_or_init_current_version() == OB_SUCCESS, "retry after open failure");
  }
  {
    ObDataVersionMgr reload;
    require(reload.init(path.c_str()) == OB_SUCCESS, "reload init");
    require(reload.load_from_file() == OB_SUCCESS && reload.get_file_exists_when_loading(), "reload after retry");
    require(reload.validate_or_init_current_version() == OB_SUCCESS, "validate after retry");
  }
  std::cout << "DATA_VERSION_FAILURE_HISTORY_PASS units=" << directory.size() << std::endl;
  std::cout << "DATA_VERSION_PATH_PASS units=" << directory.size() << std::endl;
}
}
int main()
{
  try {
    const auto root = std::filesystem::current_path().wstring() + L"\\data-version-test-" + std::to_wstring(GetCurrentProcessId());
    require(!std::filesystem::exists(root), "empty root");
    for (size_t units : {size_t(280), size_t(2048)}) {
      auto path = seekdb_phase0::directory_at_length(std::u16string(root.begin(), root.end()), units, true);
      exercise(std::wstring(path.begin(), path.end()));
    }
    std::filesystem::remove_all(L"\\\\?\\" + root);
    return 0;
  } catch (const std::exception &e) {
    std::cerr << "DATA_VERSION_PATH_FAIL " << e.what() << std::endl;
    return 1;
  }
}
