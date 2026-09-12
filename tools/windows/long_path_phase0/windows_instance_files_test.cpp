// Copyright (c) 2026 OceanBase. Licensed under the Apache License, Version 2.0.
#include "share/ob_errno.h"
#include "observer/ob_server.h"
#include "sql/ob_optimizer_trace_impl.h"
#include "storage/ob_file_system_router.h"
#include "lib/alloc/memory_dump.h"
#include "lib/file/ob_file.h"
#include "path_fixture.h"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>

void request_finish_callback() { std::abort(); }
using namespace oceanbase::common;
namespace fs = std::filesystem;
static void require(bool value, const char *message)
{ if (!value) { throw std::runtime_error(message); } }
static std::string read(const fs::path &path)
{
  std::ifstream file(path, std::ios::binary);
  require(file.is_open(), "open result");
  return {std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
}
static void write(const fs::path &path, const char *value)
{
  std::ofstream file(path, std::ios::binary | std::ios::trunc);
  file << value;
  require(file.good(), "write fixture");
}
int main()
{
  const auto original = fs::current_path();
  const auto prefix = original / (L"instance-files-" + std::to_wstring(GetCurrentProcessId()) + L".tmp");
  try {
    require(!fs::exists(prefix), "fixture must be new");
    const auto start = (prefix / L"instance").u16string();
    const auto generated = seekdb_phase0::directory_at_length(start, 2048, true);
    const std::wstring root(generated.begin(), generated.end());
    const fs::path extended(L"\\\\?\\" + root);
    const int bytes = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, root.c_str(),
        static_cast<int>(root.size()), nullptr, 0, nullptr, nullptr);
    require(bytes > 0, "UTF8 size");
    std::string input(bytes, '\0');
    require(bytes == WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, root.c_str(),
        static_cast<int>(root.size()), input.data(), bytes, nullptr, nullptr), "UTF8 conversion");
    const std::string instance = input;
    const auto foreign = prefix / L"cwd";
    fs::create_directories(foreign / L"log");
    fs::create_directories(foreign / L"etc");
    write(foreign / L"log" / L"memory_meta", "foreign dump sentinel");
    write(foreign / L"etc" / L"nic.rate.config", "rate=8Gbit");
    fs::current_path(foreign);

    auto &router = oceanbase::storage::ObFileSystemRouter::get_instance();
    require(router.init("store", "redo", input.c_str()) == OB_SUCCESS, "router init");
    fs::create_directories(extended / L"log");
    fs::create_directories(extended / L"etc");
    ObMemoryDump dumper;
    require(dumper.init(input.c_str()) == OB_SUCCESS && dumper.is_inited(), "dumper init");
    input.assign("caller buffer has been released");
    require(instance == router.get_instance_root(), "router owns root");

    {
      // This API has no current SQL enable caller; exercise the real file consumer.
      oceanbase::sql::LogFileAppender trace;
      require(trace.set_identifier(ObString::make_string("seek533")) == OB_SUCCESS, "trace identifier");
      require(trace.open() == OB_SUCCESS, "trace open");
      require(trace.append("trace-marker\n", 13) == OB_SUCCESS, "trace append");
      trace.close();
    }
    int traces = 0;
    for (const auto &entry : fs::directory_iterator(extended / L"log")) {
      if (entry.path().extension() == L".trac") {
        require(read(entry.path()) == "trace-marker\n", "trace content");
        ++traces;
      }
    }
    require(traces == 1, "one instance trace");

    // LOAD DATA consumes short reads and then probes EOF through this reader.
    const auto csv = extended / L"input.csv";
    write(csv, "1\n2\n");
    const std::string csv_name = instance + "/input.csv";
    {
      ObFileAppender exclusive;
      require(exclusive.create(ObString::make_string(csv_name.c_str()), false) == OB_FILE_ALREADY_EXIST,
          "exclusive create preserves file-exists error");
      require(read(csv) == "1\n2\n", "exclusive create preserves existing content");
    }
    {
      ObFileReader reader;
      require(reader.open(ObString::make_string(csv_name.c_str()), false) == OB_SUCCESS,
          "buffered reader open");
      char bytes[16] = {};
      int64_t read_size = -1;
      const int read_ret = reader.pread(bytes, sizeof(bytes), 0, read_size);
      std::cout << "BUFFERED_READ_RESULT ret=" << read_ret << " size=" << read_size
          << " errno=" << errno << " file_size=" << fs::file_size(csv) << '\n';
      require(read_ret == OB_SUCCESS
          && read_size == 4 && std::string(bytes, 4) == "1\n2\n", "buffered short read");
      require(reader.pread(bytes, sizeof(bytes), 4, read_size) == OB_SUCCESS
          && read_size == 0, "buffered EOF");
      reader.close();
    }

    ObMemoryDumpTask task = {};
    task.type_ = DUMP_CHUNK;
    task.dump_ctx_ = true;
    task.ctx_id_ = ObCtxIds::DEFAULT_CTX_ID;
    require(dumper.request_dump(task) == OB_SUCCESS, "queue real dump task");
    const auto dump = extended / L"log" / L"memory_meta";
    const auto deadline = GetTickCount64() + 30000;
    while ((!fs::exists(dump) || fs::file_size(dump) == 0) && GetTickCount64() < deadline) { Sleep(20); }
    dumper.destroy();
    require(fs::exists(dump) && !read(dump).empty(), "instance dump content");

    // No production caller currently enables NIC config reading; do not add one.
    const auto nic = extended / L"etc" / L"nic.rate.config";
    int64_t speed = -1;
    using oceanbase::observer::ObServer;
    require(ObServer::get_network_speed_from_config_file(speed, instance.c_str()) == OB_FILE_NOT_EXIST,
        "missing instance config must not read foreign config");
    write(nic, "rate=16Mbit");
    require(ObServer::get_network_speed_from_config_file(speed, instance.c_str()) == OB_SUCCESS
        && speed == 2 * 1024 * 1024, "instance NIC value");
    write(nic, "invalid");
    require(ObServer::get_network_speed_from_config_file(speed, instance.c_str()) == OB_INVALID_ARGUMENT,
        "invalid instance NIC config");
    require(read(foreign / L"log" / L"memory_meta") == "foreign dump sentinel", "foreign dump unchanged");
    require(read(foreign / L"etc" / L"nic.rate.config") == "rate=8Gbit", "foreign config unchanged");
    require(std::distance(fs::directory_iterator(foreign / L"log"), fs::directory_iterator()) == 1,
        "no trace in foreign cwd");
    fs::current_path(original);
    fs::remove_all(fs::path(L"\\\\?\\" + prefix.wstring()));
    std::cout << "INSTANCE_FILES_PASS units=2048 trace=1 exclusive=1 read_eof=1 dump=1 nic=missing,valid,invalid foreign_unchanged=1\n";
    return 0;
  } catch (const std::exception &e) {
    fs::current_path(original);
    std::cerr << "INSTANCE_FILES_FAIL " << e.what() << '\n';
    return 1;
  }
}
