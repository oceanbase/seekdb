// Copyright (c) 2026 OceanBase. Licensed under the Apache License, Version 2.0.
#include "share/ob_telemetry.cpp"
#include "path_fixture.h"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
void request_finish_callback() { std::abort(); }
using namespace oceanbase::common;
using namespace oceanbase::share;
namespace {
namespace common = oceanbase::common;
#include "telemetry_baseline.h"
void require(bool ok, const char *message) { if (!ok) { throw std::runtime_error(message); } }
std::string utf8(const std::wstring &s)
{
  int n = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, s.data(), static_cast<int>(s.size()), nullptr, 0, nullptr, nullptr);
  require(n > 0, "encoding size");
  std::string result(n, '\0');
  require(n == WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, s.data(), static_cast<int>(s.size()), result.data(), n, nullptr, nullptr), "encoding");
  return result;
}
std::string read_file(const std::filesystem::path &file)
{
  std::ifstream input(file, std::ios::binary);
  require(input.is_open(), "read local telemetry");
  return std::string(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>());
}
void exercise(const std::wstring &directory)
{
  const auto path = utf8(directory);
  const auto file = std::filesystem::path(L"\\\\?\\" + directory + L"\\run\\telemetry.json");
  std::filesystem::create_directories(file.parent_path());
  ObArenaAllocator allocator;
  ObString json;
  require(report_telemetry("path-test", "local-only", path.c_str()) == OB_SUCCESS, "persist local telemetry");
  const auto original = read_file(file);
  require(read_telemetry_file(allocator, json, path.c_str()) == OB_SUCCESS, "read instance state");
  require(original == std::string(json.ptr(), json.length()), "state reader content");
  ObJsonObject *state = nullptr;
  ObJsonBoolean *sent = nullptr;
  require(parse_telemetry_file(allocator, json, state, sent) == OB_SUCCESS && !sent->get_boolean(),
      "valid creation time and unsent local state");
  require(report_telemetry("second-call", "local-only", path.c_str()) == OB_SUCCESS, "repeat local telemetry");
  require(read_file(file) == original, "repeat preserves creation time identity and sent state");
  char first[37] = {}, second[37] = {};
  constexpr int64_t created_at = 123456;
  require(generate_id(first, sizeof(first), created_at, path.c_str()) == OB_SUCCESS, "identity");
  require(generate_id(second, sizeof(second), created_at, path.c_str()) == OB_SUCCESS, "repeat identity");
  require(std::string(first) == second, "stable identity");
  require(generate_id(second, sizeof(second), created_at + 1, path.c_str()) == OB_SUCCESS, "new creation time");
  require(std::string(first) != second, "creation time distinguishes recreated instance");

  const auto other = directory + L"\\other";
  const auto other_path = utf8(other);
  const auto other_file = std::filesystem::path(L"\\\\?\\" + other + L"\\run\\telemetry.json");
  std::filesystem::create_directories(other_file.parent_path());
  require(report_telemetry("other-path-test", "local-only", other_path.c_str()) == OB_SUCCESS, "second root state");
  const auto other_data = read_file(other_file);
  require(read_file(file) == original, "second root preserves first state");
  require(generate_id(second, sizeof(second), created_at, other_path.c_str()) == OB_SUCCESS, "second root identity");
  require(std::string(first) != second, "distinct root identities");

  // Force atomic replacement to fail; the original state and other root survive.
  require(SetFileAttributesW(file.c_str(), FILE_ATTRIBUTE_READONLY), "protect state fixture");
  require(write_telemetry_file(json, path.c_str()) == OB_IO_ERROR, "replacement failure");
  require(read_file(file) == original && read_file(other_file) == other_data, "failure preserves state");
  require(std::distance(std::filesystem::directory_iterator(file.parent_path()),
      std::filesystem::directory_iterator()) == 1, "owned staging file cleaned");
  require(SetFileAttributesW(file.c_str(), FILE_ATTRIBUTE_NORMAL), "restore state fixture");
  require(write_telemetry_file(json, path.c_str()) == OB_SUCCESS, "recover replacement");
  require(read_file(file) == original, "recovered JSON");
  std::cout << "TELEMETRY_ISOLATION_RECOVERY_PASS" << std::endl;
  std::cout << "TELEMETRY_PATH_PASS units=" << directory.size() << std::endl;
}
}
int main()
{
  try {
    require(_putenv_s("TELEMETRY_ENABLED", "false") == 0, "disable external reporting");
    const auto cwd = std::filesystem::current_path();
    char old_root[OB_MAX_FILE_NAME_LENGTH] = {}, new_root[OB_MAX_FILE_NAME_LENGTH] = {};
    int64_t old_len = 0, new_len = 0;
    const auto cwd_utf8 = utf8(cwd.wstring());
    require(baseline_get_telemetry_base_dir(old_root, sizeof(old_root), old_len) == OB_SUCCESS, "baseline short root");
    require(get_telemetry_base_dir(cwd_utf8.c_str(), new_root, sizeof(new_root), new_len) == OB_SUCCESS, "explicit short root");
    require(std::string(old_root, old_len) == std::string(new_root, new_len), "canonical short root compatibility");
    const char *machine = "00112233-4455-6677-8899-aabbccddeeff";
    for (const char *scope : {static_cast<const char *>(nullptr), machine}) {
      char old_id[37] = {}, new_id[37] = {};
      const int64_t scope_len = scope == nullptr ? 0 : 36;
      require(baseline_generate_telemetry_uuid(machine, 36, old_root, old_len, scope, scope_len, old_id, sizeof(old_id)) == OB_SUCCESS, "baseline UUID");
      require(generate_telemetry_uuid(machine, 36, new_root, new_len, scope, scope_len, new_id, sizeof(new_id)) == OB_SUCCESS, "new UUID");
      require(std::string(old_id) == new_id, "baseline UUID compatibility");
    }
    std::cout << "TELEMETRY_BASELINE_COMPATIBILITY_PASS" << std::endl;
    const auto root = cwd.wstring() + L"\\telemetry-test-" + std::to_wstring(GetCurrentProcessId());
    require(!std::filesystem::exists(root), "empty test root");
    const auto foreign = std::filesystem::path(root) / L"foreign";
    std::filesystem::create_directories(foreign / L"run");
    const auto sentinel = foreign / L"run" / L"telemetry.json";
    { std::ofstream output(sentinel); output << "foreign telemetry sentinel"; }
    std::filesystem::current_path(foreign);
    for (size_t units : {size_t(100), size_t(280), size_t(2048)}) {
      auto path = seekdb_phase0::directory_at_length(std::u16string(root.begin(), root.end()), units, true);
      exercise(std::wstring(path.begin(), path.end()));
    }
    require(foreign == std::filesystem::current_path(), "cwd unchanged");
    require(read_file(sentinel) == "foreign telemetry sentinel", "foreign state unchanged");
    std::filesystem::current_path(cwd);
    std::filesystem::remove_all(L"\\\\?\\" + root);
    return 0;
  } catch (const std::exception &e) {
    std::cerr << "TELEMETRY_PATH_FAIL " << e.what() << std::endl;
    return 1;
  }
}
