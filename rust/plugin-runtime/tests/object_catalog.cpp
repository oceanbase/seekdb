// Copyright (c) 2026 OceanBase. Licensed under the Apache License, Version 2.0.
#include "plugin_runtime.h"
#include <atomic>
#include <cstddef>
#include <cstdlib>
#include <iostream>
#include <thread>
#include <vector>

#define CHECK(expr) do { if (!(expr)) { std::cerr << __LINE__ << ": " << #expr << std::endl; std::abort(); } } while (false)

struct Payload {
  std::atomic<unsigned> *released;
  unsigned value;
};
static void release(void *pointer) noexcept
{
  auto *payload = static_cast<Payload *>(pointer);
  ++*payload->released;
  delete payload;
}

int main()
{
  static_assert(sizeof(seekdb_runtime_extension_member_t) == 16);
  static_assert(offsetof(seekdb_runtime_extension_member_t, object_id) == 8);
  const uint8_t name[] = "test_extension";
  const uint8_t version[] = "1.0";
  seekdb_runtime_extension_member_t members[] = {{1, 0, 42}, {2, 0, 42}};
  CHECK(seekdb_runtime_extension_install_validate(1, 100, 10,
      name, sizeof(name) - 1, version, sizeof(version) - 1,
      nullptr, 0, members, 2) == SEEKDB_RUNTIME_OK);
  CHECK(seekdb_runtime_extension_install_validate(1, 200, 10,
      name, sizeof(name) - 1, version, sizeof(version) - 1,
      nullptr, 0, members, 2) == SEEKDB_RUNTIME_OK);
  members[1].object_class = 1;
  CHECK(seekdb_runtime_extension_install_validate(1, 100, 10,
      name, sizeof(name) - 1, version, sizeof(version) - 1,
      nullptr, 0, members, 2) == SEEKDB_RUNTIME_CONFLICT);
  std::atomic<unsigned> released{0};
  auto *base = seekdb_runtime_objects_create();
  CHECK(base != nullptr);
  const uint8_t id[] = "test.object";
  auto *payload = new Payload{&released, 42};
  CHECK(seekdb_runtime_objects_insert(base, 2, id, sizeof(id) - 1, payload, release) == SEEKDB_RUNTIME_OK);
  std::vector<std::thread> workers;
  for (unsigned thread = 0; thread < 8; ++thread) {
    workers.emplace_back([base, &released, &id] {
      for (unsigned iteration = 0; iteration < 256; ++iteration) {
        auto *copy = seekdb_runtime_objects_clone(base);
        CHECK(copy != nullptr);
        const auto *found = static_cast<const Payload *>(
            seekdb_runtime_objects_find(copy, 2, id, sizeof(id) - 1));
        CHECK(found != nullptr && found->value == 42);
        auto *retained = seekdb_runtime_objects_clone(copy);
        CHECK(retained != nullptr);
        CHECK(seekdb_runtime_objects_remove(copy, 0) == SEEKDB_RUNTIME_OK);
        seekdb_runtime_objects_destroy(copy);
        CHECK(released.load() == 0);
        CHECK(seekdb_runtime_objects_at(retained, 0) == found);
        CHECK(found->value == 42);
        seekdb_runtime_objects_destroy(retained);
      }
    });
  }
  for (auto &worker : workers) worker.join();
  CHECK(released.load() == 0);
  CHECK(seekdb_runtime_objects_count(base) == 1);
  auto *retained = seekdb_runtime_objects_clone(base);
  CHECK(retained != nullptr);
  seekdb_runtime_objects_destroy(base);
  CHECK(released.load() == 0);
  CHECK(seekdb_runtime_objects_at(retained, 0) == payload);
  seekdb_runtime_objects_destroy(retained);
  CHECK(released.load() == 1);
}
