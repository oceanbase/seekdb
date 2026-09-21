// Copyright (c) 2026 OceanBase. Licensed under the Apache License, Version 2.0.
// Real Rust DSO/loader batch execution; SQL transport below remains a fixture.
#ifndef SEEKDB_TEST_RUST_BATCH_LOADER_FIXTURE_H_
#define SEEKDB_TEST_RUST_BATCH_LOADER_FIXTURE_H_
namespace rust_batch_loader_test {
struct Output {
  std::vector<int64_t> values;
  std::vector<bool> nulls;
  int fail_at = -1;
};
static seekdb_plugin_status_t SEEKDB_PLUGIN_CALL emit_indexed(seekdb_plugin_host_handle_t *host,
    uint32_t index, const seekdb_plugin_execution_result_v1_t *value)
{
  auto &out = *reinterpret_cast<Output *>(host);
  CHECK(index == out.values.size() && value && value->struct_size == sizeof(*value));
  CHECK(std::strcmp(value->type_id, "core.type.int64") == 0);
  int64_t number = 0;
  if (!value->is_null) { CHECK(value->data_size == 8); std::memcpy(&number, value->data, 8); }
  out.values.push_back(number); out.nulls.push_back(value->is_null);
  return int(index) == out.fail_at ? SEEKDB_PLUGIN_STATUS_NO_MEMORY : SEEKDB_PLUGIN_STATUS_OK;
}
struct Control { int calls = 0, fail_at = 0; };
static seekdb_plugin_status_t SEEKDB_PLUGIN_CALL poll(seekdb_plugin_sql_context_handle_t *handle,
    seekdb_plugin_query_status_v1_t *out)
{
  auto &control = *reinterpret_cast<Control *>(handle);
  ++control.calls; out->remaining_us = 1000;
  if (control.fail_at && control.calls >= control.fail_at) {
    out->database_error = OB_TIMEOUT; return SEEKDB_PLUGIN_STATUS_TIMEOUT;
  }
  return SEEKDB_PLUGIN_STATUS_OK;
}
static seekdb_plugin_status_t SEEKDB_PLUGIN_CALL fail_second_sql(seekdb_plugin_sql_context_handle_t *handle,
    const char *sql, uint64_t size, const seekdb_plugin_sql_value_v1_t *values, uint32_t count,
    uint64_t maximum, seekdb_plugin_sql_consume_row_v1_fn consume, void *consumer,
    seekdb_plugin_sql_result_v1_t *result)
{
  auto &fixture = *reinterpret_cast<SqlFixture *>(handle);
  fixture.fail = fixture.calls > 0;
  return rust_fixture_sql(handle, sql, size, values, count, maximum, consume, consumer, result);
}
static void run(ObPluginLoader &loader)
{
  const char *types[] = {"core.type.bytes"};
  seekdb_plugin_sql_binding_v1_t binding{};
  CHECK(loader.resolve_sql_extension(SEEKDB_PLUGIN_EXTENSION_FUNCTION, "seekdb_rust_char_count", types, 1, binding) == OB_SUCCESS);
  seekdb_plugin_execution_value_v1_t values[4]{};
  seekdb_plugin_batch_row_v1_t rows[4]{};
  const char *texts[] = {u8"A中🙂", nullptr, "", "z"};
  for (int i = 0; i < 4; ++i) {
    values[i].struct_size = sizeof(values[i]); values[i].type_id = types[0]; values[i].is_null = !texts[i];
    values[i].data = reinterpret_cast<const uint8_t *>(texts[i]); values[i].data_size = texts[i] ? std::strlen(texts[i]) : 0;
    rows[i].struct_size = sizeof(rows[i]); rows[i].arguments = &values[i]; rows[i].argument_count = 1;
  }
  Output output; Sink scalar_output; Control control;
  seekdb_plugin_sql_api_v2_t api{}; api.v1.struct_size = sizeof(api); api.v1.spi_major = 1; api.v1.spi_minor = 1; api.poll_query = poll;
  seekdb_plugin_execution_context_v2_t query{}; query.v1.struct_size = sizeof(query);
  query.v1.host = reinterpret_cast<seekdb_plugin_host_handle_t *>(&scalar_output); query.v1.emit_result = emit;
  query.sql_api = &api.v1; query.sql_context = reinterpret_cast<seekdb_plugin_sql_context_handle_t *>(&control);
  seekdb_plugin_batch_context_v1_t context{}; context.struct_size = sizeof(context); context.query_context = &query.v1;
  context.host = reinterpret_cast<seekdb_plugin_host_handle_t *>(&output); context.emit_result = emit_indexed;
  const auto run = [&]() { return loader.execute_bound_function_batch(binding, &context, rows, 4); };
  const auto clear = [&]() { output = {}; control = {}; };
  const auto check = [&]() {
    CHECK(output.values == std::vector<int64_t>({3,0,0,1}));
    CHECK(output.nulls == std::vector<bool>({false,true,false,false}));
    CHECK(scalar_output.calls == 0);
    ObPluginStatusSnapshot status;
    CHECK(loader.get_status("org.seekdb.rust-text", status) == OB_SUCCESS && status.lease_count_ == 0);
  };
  CHECK(run() == OB_SUCCESS); check();
  // Six loader polls before entry, SDK pre/post + four row polls in ONE batch
  // handler, then one loader post-call and five delivery polls. A scalar loop
  // through execute_count instead has a different callback trace.
  CHECK(control.calls == 18);
  clear(); CHECK(loader.execute_bound_function_batch(binding, &context, nullptr, 0) == OB_SUCCESS);
  CHECK(output.values.empty() && control.calls == 0);
  clear(); values[0].type_id = "org.seekdb.rust-text.utf8"; // Real implicit cast under a core.bytes signature.
  CHECK(run() == OB_SUCCESS); check(); values[0].type_id = types[0];
  clear(); auto stale = binding; ++stale.catalog_epoch;
  CHECK(loader.execute_bound_function_batch(stale, &context, rows, 4) == OB_STATE_NOT_MATCH && output.values.empty());
  clear(); ++rows[3].reserved[0]; CHECK(run() == OB_INVALID_ARGUMENT && output.values.empty()); --rows[3].reserved[0];
  CHECK(loader.execute_bound_function_batch(binding, &context, rows, SEEKDB_PLUGIN_MAX_BATCH_ROWS + 1) == OB_INVALID_ARGUMENT);
  clear(); const uint8_t invalid[] = {0xff}; const auto original = values[2];
  values[2].data = invalid; values[2].data_size = 1;
  CHECK(run() == OB_INVALID_ARGUMENT && output.values.empty()); values[2] = original;
  clear(); control.fail_at = 1; CHECK(run() == OB_TIMEOUT && output.values.empty());
  clear(); control.fail_at = 13; // Cancellation after all Rust results were staged, before publication.
  CHECK(run() == OB_TIMEOUT && output.values.empty() && control.calls == 13);
  clear(); output.fail_at = 1; CHECK(run() == OB_ALLOCATE_MEMORY_FAILED && output.values.size() == 2);
  clear(); CHECK(run() == OB_SUCCESS); check();

  // The old SQL-aware v1 service has no batch suffix. Four scalar calls retain
  // SQL context; a later SQL error must discard earlier staged scalar output.
  CHECK(loader.resolve_sql_extension(SEEKDB_PLUGIN_EXTENSION_FUNCTION, "seekdb_rust_sql_chars", types, 1, binding) == OB_SUCCESS);
  SqlFixture sql;
  seekdb_plugin_sql_api_v1_t sql_api{sizeof(sql_api),1,0,0,rust_fixture_sql,{0}};
  query.sql_api = &sql_api; query.sql_context = reinterpret_cast<seekdb_plugin_sql_context_handle_t *>(&sql);
  for (int i : {2,3}) { values[i].data = values[0].data; values[i].data_size = values[0].data_size; }
  clear(); CHECK(run() == OB_SUCCESS && sql.calls == 4);
  CHECK(output.values == std::vector<int64_t>({3,0,3,3}) && output.nulls[1]);
  clear(); sql = {}; sql_api.execute = fail_second_sql;
  CHECK(run() == OB_TIMEOUT && sql.calls == 2 && output.values.empty());
  ObPluginStatusSnapshot status;
  CHECK(loader.get_status("org.seekdb.rust-text", status) == OB_SUCCESS && status.lease_count_ == 0);
}
} // namespace rust_batch_loader_test
#endif
