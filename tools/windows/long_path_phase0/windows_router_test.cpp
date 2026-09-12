// Copyright (c) 2026 OceanBase. Licensed under the Apache License, Version 2.0.
#include "storage/ob_file_system_router.h"
#include "path_fixture.h"
#include <windows.h>
#include <filesystem>
#include <iostream>
#include <stdexcept>
void request_finish_callback() { std::abort(); }
using namespace oceanbase::common;
using namespace oceanbase::storage;
static void require(bool value, const char *message)
{ if (!value) { throw std::runtime_error(message); } }
int main(int argc, char **argv)
{
  try {
    const size_t units = argc == 2 ? std::stoul(argv[1]) : 2048;
    const auto cwd = std::filesystem::current_path();
    const auto prefix = cwd.wstring() + L"\\router-test-" + std::to_wstring(GetCurrentProcessId());
    require(!std::filesystem::exists(prefix), "empty fixture");
    const auto generated = seekdb_phase0::directory_at_length(std::u16string(prefix.begin(), prefix.end()), units, true);
    const std::wstring root(generated.begin(), generated.end());
    const int bytes = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, root.data(), static_cast<int>(root.size()), nullptr, 0, nullptr, nullptr);
    require(bytes > 0, "UTF8 size");
    std::string utf8(bytes, '\0');
    require(bytes == WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, root.data(), static_cast<int>(root.size()), utf8.data(), bytes, nullptr, nullptr), "UTF8");
    auto &router = ObFileSystemRouter::get_instance();
    require(router.init(".", "redo", utf8.c_str()) == OB_INVALID_ARGUMENT, "reject root as data");
    require(!std::filesystem::exists(prefix), "invalid input creates nothing");
    require(router.init("store", "STORE", utf8.c_str()) == OB_INVALID_ARGUMENT, "case insensitive collision");
    require(!std::filesystem::exists(prefix), "collision creates nothing");
    require(router.init("store", "redo", utf8.c_str()) == OB_SUCCESS, "initialize router");
    require(utf8 == router.get_instance_root(), "owned instance root");
    utf8.assign("overwritten caller input");
    require(std::string(router.get_instance_root()) != utf8, "root must not borrow caller input");
    const auto extended = std::filesystem::path(L"\\\\?\\" + root);
    require(std::filesystem::is_directory(extended / L"store" / L"sstable"), "sstable directory");
    require(std::filesystem::is_directory(extended / L"store" / L"slog"), "slog directory");
    require(std::filesystem::is_directory(extended / L"redo"), "redo directory");
    ObSqlString server;
    require(router.get_server_clog_dir(server) == OB_SUCCESS, "server clog output");
    require(std::string(server.ptr()) == std::string(router.get_clog_dir()) + "/sys", "complete server output");
    require(cwd == std::filesystem::current_path(), "cwd unchanged");
    std::filesystem::remove_all(std::filesystem::path(L"\\\\?\\" + prefix));
    std::cout << "ROUTER_PATH_PASS units=" << units << std::endl;
    return 0;
  } catch (const std::exception &e) {
    std::cerr << "ROUTER_PATH_FAIL " << e.what() << std::endl;
    return 1;
  }
}
