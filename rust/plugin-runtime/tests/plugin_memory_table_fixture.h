// Copyright (c) 2026 OceanBase. Licensed under the Apache License, Version 2.0.
#ifndef SEEKDB_TEST_PLUGIN_MEMORY_TABLE_FIXTURE_H_
#define SEEKDB_TEST_PLUGIN_MEMORY_TABLE_FIXTURE_H_
#include "observer/virtual_table/plugin_memory_table.h"
#include "share/inner_table/ob_inner_table_schema.h"
#include "native_activation_fixture.h"
#include <filesystem>

namespace plugin_memory_table_test {
using namespace oceanbase::common;
using namespace oceanbase::share::plugin;

// Real SQL row adapter, with a controlled snapshot source. Production uses the
// Observer bridge; the live-allocation test below supplies the actual loader.
class Table final : public oceanbase::observer::PluginMemoryTable
{
public:
  std::vector<ObPluginStatusSnapshot> source;
  ObPluginLoader *loader = nullptr;
  int calls = 0;
  int fail = OB_SUCCESS;
  int throw_kind = 0;
  void configure(ObArenaAllocator &arena, oceanbase::sql::ObSQLSessionInfo &session,
                 const std::vector<uint64_t> &columns = {})
  {
    set_allocator(&arena);
    set_session(&session);
    ObSEArray<uint64_t, 16> ids;
    if (columns.empty()) {
      for (uint64_t i = 0; i < 14; ++i) CHECK(ids.push_back(OB_APP_MIN_COLUMN_ID + i) == OB_SUCCESS);
    } else {
      for (const auto id : columns) CHECK(ids.push_back(id) == OB_SUCCESS);
    }
    CHECK(set_output_column_ids(ids) == OB_SUCCESS);
    void *storage = arena.alloc(sizeof(ObObj) * ids.count());
    CHECK(storage);
    cur_row_.cells_ = new (storage) ObObj[ids.count()];
    cur_row_.count_ = ids.count();
  }
protected:
  int read_snapshot(std::vector<ObPluginStatusSnapshot> &rows) override
  {
    ++calls;
    if (loader) return loader->list_status(rows);
    rows = source; // includes a partial vector when injecting failure
    if (throw_kind == 1) throw std::bad_alloc();
    if (throw_kind == 2) throw 42;
    return fail;
  }
};

inline void run(const char *rust_dso)
{
  using namespace oceanbase::share;
  schema::ObTableSchema schema;
  CHECK(ObInnerTableSchema::all_virtual_plugin_memory_schema(schema) == OB_SUCCESS);
  CHECK(schema.get_table_id() == OB_ALL_VIRTUAL_PLUGIN_MEMORY_TID);
  CHECK(schema.get_column_count() == 14);
  for (const char *name : {"generation", "used_bytes", "peak_bytes", "live_allocations",
                         "peak_allocations", "allocation_failures", "invalid_frees", "byte_limit", "allocation_limit"}) {
    const auto *column = schema.get_column_schema(ObString::make_string(name));
    CHECK(column && column->get_data_type() == ObUInt64Type);
  }
  CHECK(!schema.get_column_schema(ObString::make_string("canonical_path")));
  CHECK(!schema.get_column_schema(ObString::make_string("last_error")));

  ObArenaAllocator arena;
  auto session = std::make_unique<oceanbase::sql::ObSQLSessionInfo>();
  ObNewRow *row = nullptr;
  Table table;
  CHECK(table.inner_get_next_row(row) == OB_NOT_INIT && !row);
  table.configure(arena, *session);
  ObPluginStatusSnapshot one;
  one.plugin_id_ = "org.seekdb.fixture";
  one.generation_ = 7;
  one.runtime_incarnation_ = "incarnation-one";
  one.state_ = ObPluginState::ACTIVE;
  one.lease_count_ = 9;
  one.host_memory_ = {31, 63, 1, 2, 4, 5, UINT64_MAX, 4096};
  table.source = {one, one};
  table.source[1].generation_ = 8;
  CHECK(table.inner_get_next_row(row) == OB_ERR_NO_PRIVILEGE && !row && table.calls == 0);
  session->set_user_priv_set(OB_PRIV_PROCESS);
  CHECK(table.inner_get_next_row(row) == OB_SUCCESS && row && table.calls == 1);
  CHECK(row->count_ == 14);
  CHECK(row->cells_[0].get_string() == ObString::make_string("org.seekdb.fixture"));
  CHECK(row->cells_[1].get_uint64() == 7);
  CHECK(row->cells_[2].get_string() == ObString::make_string("incarnation-one"));
  CHECK(row->cells_[3].get_string() == ObString::make_string("ACTIVE"));
  CHECK(row->cells_[4].get_int() == 9);
  const uint64_t expected[] = {31, 63, 1, 2, 4, 5, UINT64_MAX, 4096};
  for (size_t i = 0; i < 8; ++i) CHECK(row->cells_[5 + i].get_uint64() == expected[i]);
  const auto sampled = row->cells_[13].get_timestamp();
  CHECK(sampled > 0);
  table.source.clear(); // scan owns strings/values; do not re-read between rows
  session->set_user_priv_set(0);
  CHECK(table.inner_get_next_row(row) == OB_ERR_NO_PRIVILEGE && !row && table.calls == 1);
  session->set_user_priv_set(OB_PRIV_PROCESS);
  CHECK(table.inner_get_next_row(row) == OB_SUCCESS && row->cells_[1].get_uint64() == 8);
  CHECK(row->cells_[0].get_string() == ObString::make_string("org.seekdb.fixture"));
  CHECK(row->cells_[13].get_timestamp() == sampled && table.calls == 1);
  CHECK(table.inner_get_next_row(row) == OB_ITER_END && !row);
  CHECK(table.inner_get_next_row(row) == OB_ITER_END && !row && table.calls == 1);
  table.reset();
  table.configure(arena, *session);
  CHECK(table.inner_get_next_row(row) == OB_ITER_END && table.calls == 2);

  table.reset(); table.configure(arena, *session);
  table.source = {one}; table.fail = OB_ALLOCATE_MEMORY_FAILED;
  CHECK(table.inner_get_next_row(row) == OB_ALLOCATE_MEMORY_FAILED && !row && table.calls == 3);
  table.fail = OB_SUCCESS;
  CHECK(table.inner_get_next_row(row) == OB_ALLOCATE_MEMORY_FAILED && !row && table.calls == 3);
  table.reset();
  // Projection order/duplicate columns work, unknown columns fail without a row.
  table.configure(arena, *session, {OB_APP_MIN_COLUMN_ID + 11, OB_APP_MIN_COLUMN_ID, OB_APP_MIN_COLUMN_ID + 11});
  CHECK(table.inner_get_next_row(row) == OB_SUCCESS && row->count_ == 3);
  CHECK(row->cells_[0].get_uint64() == UINT64_MAX && row->cells_[2].get_uint64() == UINT64_MAX);
  table.reset(); table.configure(arena, *session, {9999});
  CHECK(table.inner_get_next_row(row) == OB_ERR_UNEXPECTED && !row);
  table.reset();
  {
    Table reopened;
    reopened.configure(arena, *session); reopened.source = {one};
    CHECK(reopened.inner_get_next_row(row) == OB_SUCCESS && reopened.calls == 1);
    CHECK(reopened.inner_close() == OB_SUCCESS);
    reopened.source.clear();
    CHECK(reopened.inner_open() == OB_SUCCESS);
    CHECK(reopened.inner_get_next_row(row) == OB_ITER_END && !row && reopened.calls == 2);
    reopened.reset();
  }
  for (const int kind : {1, 2}) {
    Table failed;
    failed.configure(arena, *session); failed.source = {one}; failed.throw_kind = kind;
    const int expected_error = kind == 1 ? OB_ALLOCATE_MEMORY_FAILED : OB_ERR_UNEXPECTED;
    CHECK(failed.inner_get_next_row(row) == expected_error && !row && failed.calls == 1);
    failed.throw_kind = 0;
    CHECK(failed.inner_get_next_row(row) == expected_error && !row && failed.calls == 1);
    CHECK(failed.inner_close() == OB_SUCCESS && failed.inner_open() == OB_SUCCESS);
    CHECK(failed.inner_get_next_row(row) == OB_SUCCESS && failed.calls == 2);
    failed.reset();
  }

  // Real Rust owned-byte allocation -> loader snapshot -> SQL row conversion.
  // Only catalog activation authority is a fixture; no server/SQL transport.
  native_activation_test::Observation observation;
  observation.expected_services = 19; observation.expected_extensions = 23;
  auto guard = std::make_shared<native_activation_test::TestGuard>(observation);
  auto loader = std::make_unique<ObPluginLoader>();
  const std::filesystem::path artifact(rust_dso);
  PluginMemoryLimits limits; limits.bytes_ = 3; limits.allocations_ = 1;
  CHECK(loader->init(artifact.parent_path().string(),
      std::make_shared<native_activation_test::TestVerifier>(false, false, true),
      guard, guard, observation.registry, limits) == OB_SUCCESS);
  CHECK(loader->load(artifact.filename().string()) == OB_SUCCESS);
  const char *types[] = {"core.type.bytes"};
  seekdb_plugin_sql_binding_v1_t words{};
  CHECK(loader->resolve_sql_extension(SEEKDB_PLUGIN_EXTENSION_TABLE_FUNCTION,
      "seekdb_rust_words_bytes", types, 1, words) == OB_SUCCESS);
  seekdb_plugin_execution_value_v1_t input{};
  input.struct_size = sizeof(input); input.type_id = types[0];
  input.data = reinterpret_cast<const uint8_t *>("a b"); input.data_size = 3;
  seekdb_plugin_table_execution_context_v1_t context{};
  context.struct_size = sizeof(context);
  context.emit_row = [](seekdb_plugin_host_handle_t *, const seekdb_plugin_table_row_v1_t *) -> seekdb_plugin_status_t {
    CHECK(false); return SEEKDB_PLUGIN_STATUS_OK;
  };
  context.host = reinterpret_cast<seekdb_plugin_host_handle_t *>(&table);
  std::unique_ptr<oceanbase::share::IPluginTableCursor> cursor, denied;
  CHECK(loader->open_bound_table_function(words, &context, &input, 1, cursor) == OB_SUCCESS);
  CHECK(loader->open_bound_table_function(words, &context, &input, 1, denied) == OB_ALLOCATE_MEMORY_FAILED);
  CHECK(!denied);
  table.loader = loader.get(); table.configure(arena, *session);
  CHECK(table.inner_get_next_row(row) == OB_SUCCESS);
  CHECK(row->cells_[0].get_string() == ObString::make_string("org.seekdb.rust-text"));
  CHECK(row->cells_[4].get_int() > 0 && row->cells_[5].get_uint64() == 3);
  CHECK(row->cells_[7].get_uint64() == 1 && row->cells_[9].get_uint64() == 1);
  CHECK(row->cells_[11].get_uint64() == 3 && row->cells_[12].get_uint64() == 1);
  CHECK(cursor->close() == OB_SUCCESS); cursor.reset();
  CHECK(row->cells_[5].get_uint64() == 3); // existing scan is stable after free
  table.reset(); table.configure(arena, *session);
  CHECK(table.inner_get_next_row(row) == OB_SUCCESS);
  CHECK(row->cells_[4].get_int() == 0 && row->cells_[5].get_uint64() == 0);
  CHECK(row->cells_[6].get_uint64() == 3 && row->cells_[7].get_uint64() == 0);
  CHECK(loader->shutdown_for_process_exit(1000000) == OB_SUCCESS);
  loader.reset(); table.loader = nullptr;
  CHECK(row->cells_[0].get_string() == ObString::make_string("org.seekdb.rust-text"));
  CHECK(table.inner_get_next_row(row) == OB_ITER_END && !row);
  table.reset();
}
}
#endif
