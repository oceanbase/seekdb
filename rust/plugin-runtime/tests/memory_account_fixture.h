// Copyright (c) 2026 OceanBase. Licensed under the Apache License, Version 2.0.
#ifndef SEEKDB_TEST_MEMORY_ACCOUNT_FIXTURE_H_
#define SEEKDB_TEST_MEMORY_ACCOUNT_FIXTURE_H_
#include "plugin_runtime.h"
#include <cstddef>
#include <thread>

namespace memory_account_test {
using namespace native_activation_test;
using oceanbase::share::IPluginTableCursor;

static seekdb_plugin_status_t SEEKDB_PLUGIN_CALL emit(
    seekdb_plugin_host_handle_t *context, const seekdb_plugin_table_row_v1_t *row)
{
  CHECK(row && row->struct_size == sizeof(*row) && row->column_count == 1);
  CHECK(row->columns && row->columns[0].data_size == sizeof(int64_t));
  int64_t value = 0;
  std::memcpy(&value, row->columns[0].data, sizeof(value));
  CHECK(value == 1);
  ++*reinterpret_cast<int *>(context);
  return SEEKDB_PLUGIN_STATUS_OK;
}

inline void run(const char *directory, const char *filename)
{
  static_assert(sizeof(seekdb_runtime_memory_usage_t) == 64);
  static_assert(offsetof(seekdb_runtime_memory_usage_t, allocations) == 16);
  static_assert(offsetof(seekdb_runtime_memory_usage_t, byte_limit) == 48);
  auto *account = seekdb_runtime_memory_create(16, 1);
  CHECK(account);
  void *memory = seekdb_runtime_memory_alloc(account, 16, 32);
  CHECK(memory && reinterpret_cast<uintptr_t>(memory) % 32 == 0);
  CHECK(seekdb_runtime_memory_free(account, memory, 16, 16) == SEEKDB_RUNTIME_INVALID);
  seekdb_runtime_memory_usage_t usage{};
  CHECK(seekdb_runtime_memory_usage(account, &usage) == SEEKDB_RUNTIME_OK);
  CHECK(usage.bytes == 16 && usage.invalid_frees == 1);
  seekdb_runtime_memory_close(account);
  CHECK(!seekdb_runtime_memory_alloc(account, 1, 1));
  CHECK(seekdb_runtime_memory_free(account, memory, 16, 32) == SEEKDB_RUNTIME_OK);
  seekdb_runtime_memory_destroy(account);

  // Real C plugin -> existing host.alloc/free -> Rust ownership and limits.
  // No database or catalog SQL is supplied by this activation fixture.
  for (int scenario = 0; scenario < 3; ++scenario) {
    PluginMemoryLimits limits;
    const auto parse_limit = [](const char *text, uint32_t kind, uint64_t &value) {
      CHECK(seekdb_runtime_memory_parse_limit(reinterpret_cast<const uint8_t *>(text),
          static_cast<uint32_t>(std::strlen(text)), kind, &value) == SEEKDB_RUNTIME_OK);
    };
    parse_limit(scenario == 1 ? "1" : "unlimited", SEEKDB_RUNTIME_MEMORY_LIMIT_BYTES, limits.bytes_);
    parse_limit(scenario == 2 ? "1" : "unlimited", SEEKDB_RUNTIME_MEMORY_LIMIT_ALLOCATIONS, limits.allocations_);
    Observation observation;
    auto guard = std::make_shared<TestGuard>(observation);
    ObPluginLoader loader;
    CHECK(loader.init(directory, std::make_shared<TestVerifier>(false, false, false),
        guard, guard, observation.registry, limits) == OB_SUCCESS);
    CHECK(loader.load(filename) == OB_SUCCESS);
    const auto snapshot = [&] {
      ObPluginStatusSnapshot status;
      CHECK(loader.get_status("org.seekdb.sql_extension", status) == OB_SUCCESS);
      CHECK(status.host_memory_.byte_limit_ == limits.bytes_);
      CHECK(status.host_memory_.allocation_limit_ == limits.allocations_);
      return status;
    };
    auto before = snapshot();
    CHECK(before.host_memory_.bytes_ == 0 && before.host_memory_.allocations_ == 0);
    CHECK(before.host_memory_.peak_bytes_ == 0 && before.host_memory_.allocation_failures_ == 0);
    const char *types[] = {"core.type.int64", "core.type.int64"};
    seekdb_plugin_sql_binding_v1_t binding{};
    CHECK(loader.resolve_sql_extension(SEEKDB_PLUGIN_EXTENSION_TABLE_FUNCTION,
        "seekdb_generate_series", types, 2, binding) == OB_SUCCESS);
    int rows = 0;
    seekdb_plugin_table_execution_context_v1_t context{};
    context.struct_size = sizeof(context); context.emit_row = emit;
    context.host = reinterpret_cast<seekdb_plugin_host_handle_t *>(&rows);
    const int64_t one = 1;
    seekdb_plugin_execution_value_v1_t arguments[2]{};
    for (int i = 0; i < 2; ++i) {
      arguments[i].struct_size = sizeof(arguments[i]); arguments[i].type_id = types[i];
      arguments[i].data = reinterpret_cast<const uint8_t *>(&one); arguments[i].data_size = sizeof(one);
    }
    std::unique_ptr<IPluginTableCursor> first, second;
    const auto open = [&](std::unique_ptr<IPluginTableCursor> &cursor) {
      return loader.open_bound_table_function(binding, &context, arguments, 2, cursor);
    };
    const int ret = open(first);
    if (scenario == 1) {
      CHECK(ret == OB_ALLOCATE_MEMORY_FAILED && !first);
      const auto rejected = snapshot();
      CHECK(rejected.host_memory_.bytes_ == 0 && rejected.host_memory_.allocations_ == 0);
      CHECK(rejected.host_memory_.allocation_failures_ == 1 && rejected.lease_count_ == 0);
    } else {
      CHECK(ret == OB_SUCCESS && first);
      const auto active = snapshot();
      CHECK(active.host_memory_.bytes_ >= 2 * sizeof(int64_t));
      CHECK(active.host_memory_.allocations_ == 1 && active.lease_count_ > 0);
      const int second_ret = open(second);
      if (scenario == 2) {
        CHECK(second_ret == OB_ALLOCATE_MEMORY_FAILED && !second);
        CHECK(snapshot().host_memory_.allocation_failures_ == 1);
        CHECK(snapshot().host_memory_.allocations_ == 1);
      } else {
        CHECK(second_ret == OB_SUCCESS && second);
        CHECK(snapshot().host_memory_.bytes_ == 2 * active.host_memory_.bytes_);
        CHECK(second->close() == OB_SUCCESS); second.reset();
      }
      // Quota denial does not damage a live allocation or cancel its cursor.
      uint32_t emitted = 0;
      CHECK(first->next(&context, 1, &emitted) == OB_SUCCESS && rows == 1 && emitted == 1);
      CHECK(first->close() == OB_SUCCESS); first.reset();
      CHECK(snapshot().host_memory_.bytes_ == 0 && snapshot().lease_count_ == 0);
      CHECK(open(second) == OB_SUCCESS && second); // Released quota can be reused.
      CHECK(second->close() == OB_SUCCESS); second.reset();
      const auto after = snapshot();
      CHECK(after.host_memory_.bytes_ == 0 && after.host_memory_.allocations_ == 0);
      CHECK(after.host_memory_.peak_bytes_ > 0 && after.host_memory_.invalid_frees_ == 0);
      CHECK(after.lease_count_ == 0);
    }
    CHECK(loader.shutdown_for_process_exit(1000000) == OB_SUCCESS);
  }
}

struct RustOutput {
  int calls = 0;
  bool fail = false;
  std::string expected = "abc";
};
static seekdb_plugin_status_t SEEKDB_PLUGIN_CALL emit_owned_words(
    seekdb_plugin_host_handle_t *host, const seekdb_plugin_table_row_v1_t *row)
{
  auto &count = *reinterpret_cast<int *>(host);
  CHECK(row && row->column_count == 2 && row->columns && count < 2);
  CHECK(row->columns[0].data_size == 1 && row->columns[0].data[0] == (count == 0 ? 'a' : 'b'));
  CHECK(row->columns[1].data_size == sizeof(int64_t));
  int64_t ordinal = 0; std::memcpy(&ordinal, row->columns[1].data, sizeof(ordinal));
  CHECK(ordinal == count + 1); ++count;
  return SEEKDB_PLUGIN_STATUS_OK;
}
static seekdb_plugin_status_t SEEKDB_PLUGIN_CALL emit_rust(
    seekdb_plugin_host_handle_t *host, const seekdb_plugin_execution_result_v1_t *value)
{
  auto &output = *reinterpret_cast<RustOutput *>(host);
  CHECK(value && value->struct_size == sizeof(*value) && !value->is_null);
  CHECK(std::strcmp(value->type_id, "core.type.bytes") == 0);
  CHECK(value->data_size == output.expected.size());
  if (value->data_size) CHECK(std::memcmp(value->data, output.expected.data(), value->data_size) == 0);
  ++output.calls;
  return output.fail ? SEEKDB_PLUGIN_STATUS_NO_MEMORY : SEEKDB_PLUGIN_STATUS_OK;
}
static seekdb_plugin_status_t SEEKDB_PLUGIN_CALL emit_rust_batch(
    seekdb_plugin_host_handle_t *host, uint32_t index, const seekdb_plugin_execution_result_v1_t *value)
{
  CHECK(index == static_cast<uint32_t>(reinterpret_cast<RustOutput *>(host)->calls));
  return emit_rust(host, value);
}

inline void run_rust(const char *directory, const char *filename)
{
  for (uint64_t byte_limit : {0, 2, 3}) {
    Observation observation;
    observation.expected_services = 19; observation.expected_extensions = 23;
    auto guard = std::make_shared<TestGuard>(observation);
    ObPluginLoader loader;
    PluginMemoryLimits limits; limits.bytes_ = byte_limit; limits.allocations_ = 1;
    CHECK(loader.init(directory, std::make_shared<TestVerifier>(false, false, true),
        guard, guard, observation.registry, limits) == OB_SUCCESS);
    CHECK(loader.load(filename) == OB_SUCCESS);
    const auto usage = [&] {
      ObPluginStatusSnapshot status;
      CHECK(loader.get_status("org.seekdb.rust-text", status) == OB_SUCCESS);
      CHECK(status.lease_count_ == 0 && status.host_memory_.bytes_ == 0);
      CHECK(status.host_memory_.allocations_ == 0 && status.host_memory_.invalid_frees_ == 0);
      return status.host_memory_;
    };
    CHECK(usage().peak_bytes_ == 0);
    const char *types[] = {"core.type.bytes", "core.type.bytes", "core.type.bytes"};
    seekdb_plugin_sql_binding_v1_t binding{};
    CHECK(loader.resolve_sql_extension(SEEKDB_PLUGIN_EXTENSION_FUNCTION,
        "seekdb_rust_concat3", types, 3, binding) == OB_SUCCESS);
    const char *parts[] = {"a", "b", "c"};
    seekdb_plugin_execution_value_v1_t values[3]{};
    for (int i = 0; i < 3; ++i) {
      values[i].struct_size = sizeof(values[i]); values[i].type_id = types[i];
      values[i].data = reinterpret_cast<const uint8_t *>(parts[i]); values[i].data_size = 1;
    }
    RustOutput output;
    seekdb_plugin_execution_context_v1_t context{};
    context.struct_size = sizeof(context); context.emit_result = emit_rust;
    context.host = reinterpret_cast<seekdb_plugin_host_handle_t *>(&output);
    const auto scalar = [&] { return loader.execute_bound_function(binding, &context, values, 3); };
    CHECK(scalar() == (byte_limit == 3 ? OB_SUCCESS : OB_ALLOCATE_MEMORY_FAILED));
    CHECK(output.calls == (byte_limit == 3 ? 1 : 0));
    CHECK(usage().allocation_failures_ == (byte_limit == 3 ? 0 : 1));
    CHECK(usage().peak_bytes_ == (byte_limit == 3 ? 3 : 0));

    // Empty output uses no allocation even with a zero module budget.
    for (auto &value : values) value.data_size = 0;
    output = {}; output.expected.clear();
    CHECK(scalar() == OB_SUCCESS && output.calls == 1);
    for (auto &value : values) value.data_size = 1;
    if (byte_limit == 3) {
      output = {}; output.fail = true;
      CHECK(scalar() == OB_ALLOCATE_MEMORY_FAILED && output.calls == 1);
      CHECK(usage().allocation_failures_ == 0); // Delivery failure, not allocator denial.
      output = {};
      CHECK(scalar() == OB_SUCCESS && output.calls == 1);
      seekdb_plugin_batch_row_v1_t rows[2]{};
      for (auto &row : rows) { row.struct_size = sizeof(row); row.arguments = values; row.argument_count = 3; }
      seekdb_plugin_batch_context_v1_t batch{};
      batch.struct_size = sizeof(batch); batch.query_context = &context;
      batch.host = context.host; batch.emit_result = emit_rust_batch;
      output = {};
      CHECK(loader.execute_bound_function_batch(binding, &batch, rows, 2) == OB_SUCCESS && output.calls == 2);
      CHECK(usage().peak_bytes_ == 3 && usage().peak_allocations_ == 1); // Row buffer reused by Drop.
      auto larger = values[2]; larger.data = reinterpret_cast<const uint8_t *>("ccc"); larger.data_size = 3;
      seekdb_plugin_execution_value_v1_t second[] = {values[0], values[1], larger};
      rows[1].arguments = second;
      output = {};
      CHECK(loader.execute_bound_function_batch(binding, &batch, rows, 2) == OB_ALLOCATE_MEMORY_FAILED);
      CHECK(output.calls == 0 && usage().allocation_failures_ == 1); // First staged result is discarded.
      rows[1].arguments = values;
      CHECK(loader.execute_bound_function_batch(binding, &batch, rows, 2) == OB_SUCCESS && output.calls == 2);
      CHECK(usage().peak_bytes_ == 3);
      // Real Rust table cursor owns bytes across callbacks and serial worker
      // migration. The host cursor's code lease is separate from its byte token.
      seekdb_plugin_sql_binding_v1_t words{};
      CHECK(loader.resolve_sql_extension(SEEKDB_PLUGIN_EXTENSION_TABLE_FUNCTION,
          "seekdb_rust_words_bytes", types, 1, words) == OB_SUCCESS);
      auto text = values[0]; text.data = reinterpret_cast<const uint8_t *>("a b"); text.data_size = 3;
      int word_count = 0;
      seekdb_plugin_table_execution_context_v1_t table{};
      table.struct_size = sizeof(table); table.emit_row = emit_owned_words;
      table.host = reinterpret_cast<seekdb_plugin_host_handle_t *>(&word_count);
      std::unique_ptr<IPluginTableCursor> cursor, denied;
      CHECK(loader.open_bound_table_function(words, &table, &text, 1, cursor) == OB_SUCCESS);
      CHECK(loader.open_bound_table_function(words, &table, &text, 1, denied) == OB_ALLOCATE_MEMORY_FAILED && !denied);
      ObPluginStatusSnapshot active;
      CHECK(loader.get_status("org.seekdb.rust-text", active) == OB_SUCCESS);
      CHECK(active.host_memory_.bytes_ == 3 && active.host_memory_.allocations_ == 1 && active.lease_count_ > 0);
      output = {};
      CHECK(scalar() == OB_ALLOCATE_MEMORY_FAILED && output.calls == 0); // Same raw/owned quota.
      std::thread worker([owned = std::move(cursor), &table]() mutable {
        uint32_t emitted = 0;
        CHECK(owned->next(&table, 2, &emitted) == OB_SUCCESS && emitted == 2);
        CHECK(owned->close() == OB_SUCCESS);
        owned.reset();
      });
      worker.join();
      CHECK(word_count == 2);
      CHECK(usage().peak_bytes_ == 3 && usage().peak_allocations_ == 1);
      output = {}; CHECK(scalar() == OB_SUCCESS && output.calls == 1);
      CHECK(loader.open_bound_table_function(words, &table, &text, 1, cursor) == OB_SUCCESS);
      CHECK(cursor->close() == OB_SUCCESS); cursor.reset();
      (void)usage();
    }
    (void)usage();
    CHECK(loader.shutdown_for_process_exit(1000000) == OB_SUCCESS);
  }
}
}
#endif
