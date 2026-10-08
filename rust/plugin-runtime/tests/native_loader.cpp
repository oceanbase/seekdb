// Copyright (c) 2026 OceanBase. Licensed under the Apache License, Version 2.0.
#include "plugin_runtime.h"
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <string>

#define CHECK(expr) do { if (!(expr)) { \
  std::cerr << __LINE__ << ": " << #expr << std::endl; std::abort(); \
} } while (false)

struct FixtureApi { void (*bind_counter)(int *); int (*call)(); };

int main(int argc, char **argv)
{
  CHECK(argc == 3);
  char error[256] = {};
  seekdb_runtime_native_module *module = nullptr;
  auto open = [&](const std::string &path, seekdb_runtime_native_module **output) {
    return seekdb_runtime_native_open(reinterpret_cast<const uint8_t *>(path.data()),
        static_cast<uint32_t>(path.size()), output, error, sizeof(error));
  };
  auto entry = [&](seekdb_runtime_native_module *handle) {
    seekdb_runtime_native_entry_fn result = nullptr;
    CHECK(seekdb_runtime_native_entry(handle, &result, error, sizeof(error)) == SEEKDB_RUNTIME_OK);
    CHECK(result != nullptr);
    return static_cast<const FixtureApi *>(result());
  };
  CHECK(open(std::string(argv[1]) + ".missing", &module) == SEEKDB_RUNTIME_IO_ERROR);
  CHECK(module == nullptr && error[0] != 0);
  CHECK(open(argv[2], &module) == SEEKDB_RUNTIME_OK);
  seekdb_runtime_native_entry_fn missing = nullptr;
  CHECK(seekdb_runtime_native_entry(module, &missing, error, sizeof(error)) == SEEKDB_RUNTIME_NOT_FOUND);
  CHECK(missing == nullptr && error[0] != 0);
  CHECK(seekdb_runtime_native_close(module, SEEKDB_RUNTIME_ABORT_LOAD, error, sizeof(error)) == SEEKDB_RUNTIME_OK);

  // A published handle cannot be unloaded by rollback. The real DSO remains
  // callable, and its destructor runs exactly once at explicit terminal close.
  int unloaded = 0;
  CHECK(open(argv[1], &module) == SEEKDB_RUNTIME_OK);
  const auto *api = entry(module);
  api->bind_counter(&unloaded);
  CHECK(api->call() == 42);
  CHECK(seekdb_runtime_native_publish(module) == SEEKDB_RUNTIME_OK);
  CHECK(seekdb_runtime_native_close(module, SEEKDB_RUNTIME_ABORT_LOAD, error, sizeof(error)) == SEEKDB_RUNTIME_STATE_MISMATCH);
  CHECK(unloaded == 0 && api->call() == 42);
  CHECK(seekdb_runtime_native_close(module, SEEKDB_RUNTIME_PROCESS_EXIT, error, sizeof(error)) == SEEKDB_RUNTIME_OK);
  CHECK(unloaded == 1);

  // Separate opens own separate loader references, even for an identical path.
  seekdb_runtime_native_module *second = nullptr;
  CHECK(open(argv[1], &module) == SEEKDB_RUNTIME_OK);
  CHECK(open(argv[1], &second) == SEEKDB_RUNTIME_OK);
  entry(module)->bind_counter(&unloaded);
  CHECK(seekdb_runtime_native_close(module, SEEKDB_RUNTIME_ABORT_LOAD, error, sizeof(error)) == SEEKDB_RUNTIME_OK);
  CHECK(unloaded == 1 && entry(second)->call() == 42);
  CHECK(seekdb_runtime_native_close(second, SEEKDB_RUNTIME_ABORT_LOAD, error, sizeof(error)) == SEEKDB_RUNTIME_OK);
  CHECK(unloaded == 2);
  return 0;
}
