/*
 * Copyright (c) 2026 OceanBase.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

// Test transport only: actual loader, module pin and Rust executor, not SQL CG.
#ifndef SEEKDB_TEST_CUSTOM_EXECUTOR_FIXTURE_H_
#define SEEKDB_TEST_CUSTOM_EXECUTOR_FIXTURE_H_
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <stdexcept>
#include <thread>
namespace custom_executor_test {
// A real rendezvous, not a sleep: the controller observes every callback in
// flight before quiescing the module. Timeout only prevents a hung test.
struct InputGate {
  std::mutex mutex;
  std::condition_variable changed;
  uint32_t arrived = 0;
  bool released = false;
  void enter() {
    std::unique_lock<std::mutex> lock(mutex);
    ++arrived;
    changed.notify_all();
    CHECK(changed.wait_for(lock, std::chrono::seconds(10), [&] { return released; }));
  }
  void wait_for(uint32_t count) {
    std::unique_lock<std::mutex> lock(mutex);
    CHECK(changed.wait_for(lock, std::chrono::seconds(10), [&] { return arrived == count; }));
  }
  void release() {
    std::lock_guard<std::mutex> lock(mutex);
    released = true;
    changed.notify_all();
  }
};
struct Host {
  uint32_t reads = 0, writes = 0, polls = 0;
  uint32_t rewinds = 0;
  int rewind_error = 0;
  seekdb_plugin_status_t rewind_status = SEEKDB_PLUGIN_STATUS_OK;
  bool rewind_throws = false, cancel_after_rewind = false;
  int input_error = 0, output_error = 0;
  uint32_t cancel_at = 0;
  bool malformed = false, throws = false, empty = false, wrong_type = false;
  InputGate *gate = nullptr;
  int64_t number_base = 0;
  std::string first_value = "one";
  int64_t number = 0;
  char bytes[16] = {};
  seekdb_plugin_execution_value_v1_t values[2] = {};
  std::vector<std::string> output;
  static seekdb_plugin_status_t SEEKDB_PLUGIN_CALL input(void *opaque, uint32_t index,
      seekdb_plugin_custom_row_v1_t *row, int32_t *error) {
    auto &self = *static_cast<Host *>(opaque);
    CHECK(index == 0 && row && error);
    ++self.reads;
    if (self.reads == 1 && self.gate) self.gate->enter();
    *error = self.input_error;
    if (self.throws) throw std::bad_alloc();
    if (*error) return SEEKDB_PLUGIN_STATUS_INTERNAL;
    std::memset(self.bytes, 'x', sizeof(self.bytes)); // Invalidate the previous borrow, including at EOF.
    if (self.empty || self.reads > 3) return SEEKDB_PLUGIN_STATUS_END_OF_STREAM;
    self.number = self.number_base + self.reads;
    CHECK(self.first_value.size() <= sizeof(self.bytes));
    if (self.reads == 1) std::memcpy(self.bytes, self.first_value.data(), self.first_value.size());
    self.values[0] = {sizeof(self.values[0]), "core.type.int64", reinterpret_cast<const uint8_t *>(&self.number),
                     sizeof(self.number), 0, {0}, {0}};
    if (self.wrong_type) self.values[0].type_id = "org.example.number";
    self.values[1] = {sizeof(self.values[1]), "core.type.bytes", self.reads == 2 ? nullptr : reinterpret_cast<const uint8_t *>(self.bytes),
                     self.reads == 1 ? self.first_value.size() : 0u, static_cast<uint8_t>(self.reads == 2), {0}, {0}};
    *row = {sizeof(*row), 2, self.values, {0}};
    if (self.malformed) row->reserved[0] = 1;
    return SEEKDB_PLUGIN_STATUS_OK;
  }
  static seekdb_plugin_status_t SEEKDB_PLUGIN_CALL emit(void *opaque,
      const seekdb_plugin_execution_value_v1_t *values, uint32_t count, int32_t *error) {
    auto &self = *static_cast<Host *>(opaque);
    ++self.writes;
    *error = self.output_error;
    if (*error) return SEEKDB_PLUGIN_STATUS_INTERNAL;
    CHECK(count == 2 && std::strcmp(values[0].type_id, "core.type.int64") == 0);
    CHECK(values[0].data_size == sizeof(int64_t));
    int64_t number = 0; std::memcpy(&number, values[0].data, sizeof(number));
    CHECK(number == self.number_base + static_cast<int64_t>(self.output.size() + 1));
    const auto &text = values[1];
    CHECK(std::strcmp(text.type_id, "core.type.bytes") == 0);
    self.output.push_back(text.is_null ? "NULL" : std::string(reinterpret_cast<const char *>(text.data), text.data_size));
    return SEEKDB_PLUGIN_STATUS_OK;
  }
  static seekdb_plugin_status_t SEEKDB_PLUGIN_CALL poll(void *opaque, int32_t *error) {
    auto &self = *static_cast<Host *>(opaque);
    *error = ++self.polls == self.cancel_at || (self.cancel_after_rewind && self.rewinds) ? OB_TIMEOUT : 0;
    return *error ? SEEKDB_PLUGIN_STATUS_INTERNAL : SEEKDB_PLUGIN_STATUS_OK;
  }
  seekdb_plugin_custom_context_v1_t view() {
    return {sizeof(seekdb_plugin_custom_context_v1_t), 1, 2, 0, this, input, emit, poll, {0}};
  }
  static seekdb_plugin_status_t SEEKDB_PLUGIN_CALL rewind(void *opaque, uint32_t index, int32_t *error) {
    auto &self = *static_cast<Host *>(opaque);
    CHECK(index == 0 && error);
    ++self.rewinds; self.reads = 0;
    *error = self.rewind_error;
    if (self.rewind_throws) throw std::bad_alloc();
    return self.rewind_status;
  }
};
struct Description {
  seekdb_plugin_custom_column_v1_t columns[2] = {};
  seekdb_plugin_custom_schema_v1_t schema = {};
  Description() {
    for (auto &c : columns) { c.struct_size = sizeof(c); c.precision = c.scale = -1; }
    std::strcpy(columns[0].type_id, "core.type.int64"); columns[0].sql_type = 5;
    columns[0].encoding = SEEKDB_PLUGIN_CUSTOM_ENCODING_INT64;
    std::strcpy(columns[1].type_id, "core.type.bytes"); columns[1].sql_type = 22;
    columns[1].flags = SEEKDB_PLUGIN_CUSTOM_COLUMN_NULLABLE;
    schema = {sizeof(schema), 2, columns, {0}};
  }
  seekdb_plugin_custom_context_v2_t view(Host &host) {
    seekdb_plugin_custom_context_v2_t view = {host.view(), &schema, &schema, {0}};
    view.v1.struct_size = sizeof(view); return view;
  }
};

inline void run_control(ObPluginLoader &loader, int variant)
{
  CustomExecutorBinding binding;
  CHECK(loader.bind_custom_executor("test.custom.executor", 1, 0, binding) == OB_SUCCESS);
  std::unique_ptr<ICustomExecutor> cursor;
  CHECK(loader.open_custom_executor(binding, nullptr, 0, cursor) == OB_SUCCESS);
  Host host; Description description;
  const bool bind = variant >= 34;
  if (bind) variant -= 13; // Repeat the raw fault matrix for minor=2 binding control.
  seekdb_plugin_custom_context_v4_t bound = {{description.view(host), Host::rewind, {0}}, Host::rewind, {0}};
  auto &view = bound.v3;
  // Also prove that a minor=1 executor receives the exact v3 prefix of v4.
  view.v2.v1.struct_size = bind || variant == 21 ? sizeof(bound) : sizeof(view);
  int expected = OB_INVALID_ARGUMENT;
  if (variant == 21) expected = OB_SUCCESS;
  if (variant == 24 || variant == 33) { host.rewind_error = OB_TIMEOUT; expected = OB_TIMEOUT; }
  if (variant == 33) host.rewind_status = SEEKDB_PLUGIN_STATUS_INTERNAL;
  if (variant == 25) { host.rewind_status = SEEKDB_PLUGIN_STATUS_END_OF_STREAM; expected = OB_INVALID_DATA; }
  if (variant == 26) { host.rewind_throws = true; expected = OB_ALLOCATE_MEMORY_FAILED; }
  if (variant == 28) { host.cancel_after_rewind = true; expected = OB_TIMEOUT; }
  if (variant == 29) { if (bind) bound.bind_rescan_input = nullptr; else view.rescan_input = nullptr; }
  if (variant == 30) { if (bind) bound.reserved[1] = 1; else view.reserved[1] = 1; }
  if (variant == 31) --view.v2.v1.struct_size;
  if (variant == 32) { view.v2.v1.struct_size = bind ? sizeof(view) : sizeof(view.v2); expected = OB_NOT_SUPPORTED; }
  CHECK(cursor->next(view.v2.v1) == expected);
  if (variant == 21) {
    CHECK(host.rewinds == 1 && host.writes == 1 && host.reads == 1);
    CHECK(loader.shutdown_for_process_exit(0) == OB_TIMEOUT); // Rewinding does not release the module lease.
  } else {
    CHECK(cursor->next(view.v2.v1) == OB_STATE_NOT_MATCH);
    CHECK(host.writes == (variant == 27 ? 1 : 0));
    if (variant == 22 || variant == 23 || variant == 27 || variant >= 29 && variant <= 32) CHECK(host.rewinds == 0);
    else CHECK(host.rewinds == 1);
    if (variant >= 29 && variant <= 31) CHECK(host.polls == 0 && host.reads == 0);
    CHECK(cursor->rescan() == OB_SUCCESS);
  }
  CHECK(cursor->close() == OB_SUCCESS);
}

inline void run_schema(ObPluginLoader &loader)
{
  CustomExecutorBinding binding;
  CHECK(loader.bind_custom_executor("org.seekdb.rust-candidate.spool", 1, 0, binding) == OB_SUCCESS);
  for (int fault = 0; fault < 18; ++fault) {
    std::unique_ptr<ICustomExecutor> cursor;
    CHECK(loader.open_custom_executor(binding, nullptr, 0, cursor) == OB_SUCCESS && cursor);
    Host host; Description description; auto view = description.view(host);
    auto &column = description.columns[0];
    switch (fault) {
      case 1: view.v1.struct_size = sizeof(view.v1) + 1; break;
      case 2: view.inputs = nullptr; break;
      case 3: view.output = nullptr; break;
      case 4: view.reserved[0] = 1; break;
      case 5: --description.schema.struct_size; break;
      case 6: description.schema.column_count = 1025; break;
      case 7: description.schema.columns = nullptr; break;
      case 8: description.schema.reserved[0] = 1; break;
      case 9: --column.struct_size; break;
      case 10: column.flags = 8; break;
      case 11: std::memset(column.type_id, 'x', sizeof(column.type_id)); break;
      case 12: column.reserved[0] = 1; break;
      case 13: column.encoding = 99; break;
      case 14: column.encoding = SEEKDB_PLUGIN_CUSTOM_ENCODING_BYTES; break;
      case 15: view.v1.output_column_count = 1; break;
      case 16: host.wrong_type = true; break;
      case 17: description.columns[1].flags = 0; break;
    }
    const int ret = cursor->next(view.v1);
    if (!fault) {
      CHECK(ret == OB_SUCCESS && host.reads == 4);
      CHECK(cursor->next(view.v1) == OB_SUCCESS && cursor->next(view.v1) == OB_SUCCESS);
      CHECK(cursor->next(view.v1) == OB_ITER_END);
      CHECK(host.output == std::vector<std::string>({"one", "NULL", ""}));
    } else {
      CHECK(ret == (fault < 16 ? OB_INVALID_ARGUMENT : OB_INVALID_DATA));
      CHECK(cursor->next(view.v1) == OB_STATE_NOT_MATCH && !host.writes);
      if (fault < 16) CHECK(!host.reads && !host.polls);
    }
    // Recover under a fresh valid view; metadata storage is still host-owned.
    Description valid; view = valid.view(host); host = Host{}; host.empty = true;
    CHECK(cursor->rescan() == OB_SUCCESS && cursor->next(view.v1) == OB_ITER_END);
    CHECK(host.reads == 1 && !host.writes);
    CHECK(cursor->close() == OB_SUCCESS);
  }
}

inline void run(ObPluginLoader &loader, bool native_rust, int variant = 0, bool quiesce = true)
{
  if (variant >= 21) { run_control(loader, variant); return; }
  const char *owner = native_rust ? "org.seekdb.rust-candidate" : "org.seekdb.sql_extension";
  const char *service = native_rust ? "org.seekdb.rust-candidate.spool" : "test.custom.executor";
  CustomExecutorBinding binding;
  int ret = loader.bind_custom_executor(service, 1, 0, binding);
  if (variant == 12 || variant == 13 || variant == 18) {
    CHECK(ret == OB_NOT_SUPPORTED && binding.service_id.empty() && binding.generation == 0);
    // A caller cannot bypass admission by constructing a pointer-free binding.
    ObPluginStatusSnapshot snapshot;
    CHECK(loader.get_status(owner, snapshot) == OB_SUCCESS);
    binding = {service, owner, snapshot.runtime_incarnation_, snapshot.generation_, 1, 0, 0};
    std::unique_ptr<ICustomExecutor> rejected;
    CHECK(loader.open_custom_executor(binding, nullptr, 0, rejected) == OB_NOT_SUPPORTED && !rejected);
    CHECK(loader.get_status(owner, snapshot) == OB_SUCCESS && snapshot.lease_count_ == 0);
    return;
  }
  CHECK(ret == OB_SUCCESS && binding.owner_id == owner && binding.generation != 0 && !binding.runtime_incarnation.empty());
  std::unique_ptr<ICustomExecutor> cursor;
  for (int field = 0; field < 6; ++field) {
    auto bad = binding;
    if (field == 0) ++bad.generation;
    if (field == 1) bad.owner_id = "other.owner";
    if (field == 2) bad.runtime_incarnation = "other.runtime";
    if (field == 3) ++bad.minor;
    if (field == 4) ++bad.patch;
    if (field == 5) ++bad.major;
    CHECK(loader.open_custom_executor(bad, nullptr, 0, cursor) != OB_SUCCESS && !cursor);
  }
  ret = loader.open_custom_executor(binding, nullptr, 0, cursor);
  if (variant == 8 || variant == 9) { CHECK(ret != OB_SUCCESS && !cursor); return; }
  CHECK(ret == OB_SUCCESS && cursor);
  auto *original = cursor.get();
  CHECK(loader.open_custom_executor(binding, nullptr, 0, cursor) == OB_INVALID_ARGUMENT && cursor.get() == original);
  ObPluginStatusSnapshot snapshot;
  CHECK(loader.get_status(owner, snapshot) == OB_SUCCESS && snapshot.lease_count_ == 1);
  Host host;
  if (variant == 5) host.input_error = OB_TIMEOUT;
  if (variant == 6) host.output_error = OB_ALLOCATE_MEMORY_FAILED;
  if (variant == 7) host.cancel_at = 2;
  if (variant == 14) host.throws = true;
  if (variant == 15) host.malformed = true;
  if (variant == 20) host.wrong_type = true;
  Description description; auto extended_view = description.view(host);
  seekdb_plugin_custom_context_v3_t controlled_view = {extended_view, Host::rewind, {0}};
  controlled_view.v2.v1.struct_size = sizeof(controlled_view);
  const auto legacy_view = host.view();
  const auto &view = !native_rust && variant == 0 ? controlled_view.v2.v1 : variant >= 19 ? extended_view.v1 : legacy_view;
  ret = cursor->next(view);
  if (variant == 0 || variant == 10 || variant == 11) {
    CHECK(ret == OB_SUCCESS);
    if (native_rust) CHECK(host.reads == 4); // Rust buffered all input before first output.
    CHECK(cursor->next(view) == OB_SUCCESS);
    CHECK(cursor->next(view) == OB_SUCCESS);
    CHECK(cursor->next(view) == OB_ITER_END);
    const auto reads = host.reads;
    CHECK(cursor->next(view) == OB_ITER_END && host.reads == reads);
    CHECK(host.output == std::vector<std::string>({"one", "NULL", ""}));
  } else {
    CHECK(ret != OB_SUCCESS && ret != OB_ITER_END);
    if (variant == 5 || variant == 7) CHECK(ret == OB_TIMEOUT);
    if (variant == 6 || variant == 14) CHECK(ret == OB_ALLOCATE_MEMORY_FAILED);
    if (variant == 15) CHECK(ret == OB_INVALID_DATA);
    if (variant == 19) CHECK(ret == OB_INVALID_ARGUMENT && !host.writes);
    if (variant == 20) CHECK(ret == OB_INVALID_DATA && !host.writes);
    CHECK(cursor->next(view) == OB_STATE_NOT_MATCH);
  }
  CHECK(cursor->rescan() == (variant == 10 ? OB_INVALID_DATA : OB_SUCCESS));
  host = Host{};
  if (variant == 0) {
    CHECK(cursor->next(view) == OB_SUCCESS);
    CHECK(host.output == std::vector<std::string>({"one"}));
    // Quiesce unpublishes new acquisitions but cannot stop code under a live
    // custom cursor. Its existing callback remains callable until close.
    if (quiesce) {
      CHECK(loader.shutdown_for_process_exit(0) == OB_TIMEOUT);
      std::unique_ptr<ICustomExecutor> rejected;
      CHECK(loader.open_custom_executor(binding, nullptr, 0, rejected) != OB_SUCCESS && !rejected);
    }
    CHECK(cursor->next(view) == OB_SUCCESS);
  }
  CHECK(cursor->close() == (variant == 11 ? OB_ERR_UNEXPECTED : OB_SUCCESS));
  CHECK(cursor->close() == OB_SUCCESS);
  CHECK(cursor->next(view) == OB_NOT_INIT);
  cursor.reset();
  CHECK(loader.get_status(owner, snapshot) == OB_SUCCESS && snapshot.lease_count_ == 0);
}

inline void run_concurrent(ObPluginLoader &loader)
{
  constexpr uint32_t workers = 8;
  const char *owner = "org.seekdb.rust-candidate";
  const char *service = "org.seekdb.rust-candidate.spool";
  InputGate gate;
  std::vector<std::unique_ptr<ICustomExecutor>> cursors(workers);
  std::vector<std::thread> threads;
  for (uint32_t i = 0; i < workers; ++i) {
    threads.emplace_back([&, i] {
      CustomExecutorBinding binding;
      CHECK(loader.bind_custom_executor(service, 1, 0, binding) == OB_SUCCESS);
      CHECK(loader.open_custom_executor(binding, nullptr, 0, cursors[i]) == OB_SUCCESS);
      Host host;
      host.number_base = 100 * i;
      host.first_value = "first-" + std::to_string(i);
      host.gate = &gate;
      const auto view = host.view();
      for (uint32_t row = 0; row < 3; ++row) CHECK(cursors[i]->next(view) == OB_SUCCESS);
      CHECK(cursors[i]->next(view) == OB_ITER_END);
      CHECK(host.output == std::vector<std::string>({host.first_value, "NULL", ""}));
      CHECK(host.reads == 4);
      CHECK(cursors[i]->rescan() == OB_SUCCESS);
    });
  }
  gate.wait_for(workers); // All eight Rust next callbacks are simultaneously live.
  ObPluginStatusSnapshot snapshot;
  CHECK(loader.get_status(owner, snapshot) == OB_SUCCESS && snapshot.lease_count_ == workers);
  CHECK(loader.shutdown_for_process_exit(0) == OB_TIMEOUT);
  CustomExecutorBinding rejected_binding;
  CHECK(loader.bind_custom_executor(service, 1, 0, rejected_binding) != OB_SUCCESS);
  gate.release();
  for (auto &thread : threads) thread.join();
  threads.clear();
  CHECK(loader.get_status(owner, snapshot) == OB_SUCCESS && snapshot.lease_count_ == workers);

  // The controller is a different, still-live thread from each first-wave
  // worker: use the transferred cursor here before moving it to another worker.
  // No call on an individual cursor overlaps another call on that cursor.
  for (auto &cursor : cursors) {
    Host host;
    CHECK(cursor->next(host.view()) == OB_SUCCESS);
    CHECK(cursor->rescan() == OB_SUCCESS);
  }
  for (uint32_t i = 0; i < workers; ++i) {
    threads.emplace_back([&, i] {
      Host host;
      if (i == 0) host.cancel_at = 3;
      if (i == 1) host.input_error = OB_TIMEOUT;
      if (i == 2) host.output_error = OB_ALLOCATE_MEMORY_FAILED;
      if (i < 3) {
        CHECK(cursors[i]->next(host.view()) == (i == 2 ? OB_ALLOCATE_MEMORY_FAILED : OB_TIMEOUT));
        CHECK(cursors[i]->next(host.view()) == OB_STATE_NOT_MATCH);
        host = Host{}; // Child transport resets before the plugin state.
        CHECK(cursors[i]->rescan() == OB_SUCCESS);
      }
      host.number_base = 1000 * i;
      host.first_value = "second-" + std::to_string(i);
      for (uint32_t row = 0; row < 3; ++row) CHECK(cursors[i]->next(host.view()) == OB_SUCCESS);
      CHECK(cursors[i]->next(host.view()) == OB_ITER_END);
      CHECK(host.output == std::vector<std::string>({host.first_value, "NULL", ""}));
      // Retain one final lease to prove terminal stop waits for every cursor.
      if (i != workers - 1) CHECK(cursors[i]->close() == OB_SUCCESS);
    });
  }
  for (auto &thread : threads) thread.join();
  CHECK(loader.get_status(owner, snapshot) == OB_SUCCESS && snapshot.lease_count_ == 1);
  CHECK(loader.shutdown_for_process_exit(0) == OB_TIMEOUT);
  // Destructor close, rather than another explicit close, releases the last pin.
  cursors.back().reset();
  CHECK(loader.get_status(owner, snapshot) == OB_SUCCESS && snapshot.lease_count_ == 0);
}
}
#endif
