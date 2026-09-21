// Copyright (c) 2026 OceanBase. Licensed under the Apache License, Version 2.0.
#include "share/plugin/ob_plugin_loader.h"
#include "share/rc/ob_module_provider.h"
#include "lib/ob_errno.h"
#include "seekdb/plugin/sql_spi.h"
#include "seekdb/plugin/catalog_spi.h"
#include <cstdlib>
#include <cstring>
#include <iostream>

using namespace oceanbase::share::plugin;
using oceanbase::share::IPluginTableCursor;
using namespace oceanbase::common;
#define CHECK(expr) do { if (!(expr)) { \
  std::cerr << __LINE__ << ": " << #expr << std::endl; std::abort(); \
} } while (false)

#include "native_activation_fixture.h"
#include "share/plugin/extension_package.h"
#include "share/plugin/catalog_builder.h"
#include "share/plugin/extension_install.h"
using namespace native_activation_test;
#include "custom_executor_fixture.h"
#include "memory_account_fixture.h"

struct Sink { int64_t value = 0; int calls = 0; bool is_null = false; };
struct SqlFixture { int calls = 0; bool fail = false; bool exec_mode = false; bool null_parameter = false; };
// ABI transport test double only; production prepare/execute needs SQL tests.
static seekdb_plugin_status_t SEEKDB_PLUGIN_CALL fixture_sql(
    seekdb_plugin_sql_context_handle_t *context, const char *sql, uint64_t size,
    const seekdb_plugin_sql_value_v1_t *parameters, uint32_t count,
    uint64_t max_rows, seekdb_plugin_sql_consume_row_v1_fn consume, void *consumer,
    seekdb_plugin_sql_result_v1_t *result)
{
  auto &fixture = *reinterpret_cast<SqlFixture *>(context);
  ++fixture.calls;
  if (fixture.exec_mode) {
    CHECK(std::string(sql, size) == "INSERT INTO writes VALUES (?)");
    CHECK(count == 1 && max_rows == 1024 && consume != nullptr);
    if (fixture.null_parameter) {
      CHECK(parameters[0].kind == SEEKDB_PLUGIN_SQL_NULL && parameters[0].data_size == 0);
    } else {
      CHECK(parameters[0].kind == SEEKDB_PLUGIN_SQL_INT64);
      CHECK(parameters[0].data_size == sizeof(int64_t));
      int64_t input;
      std::memcpy(&input, parameters[0].data, sizeof(input));
      CHECK(input == 41);
    }
    result->affected_rows = 3;
    return SEEKDB_PLUGIN_STATUS_OK;
  }
  CHECK(std::string(sql, size) == "SELECT CAST(? AS SIGNED) + 1");
  CHECK(count == 1 && max_rows == 1 && consume != nullptr);
  CHECK(parameters[0].kind == SEEKDB_PLUGIN_SQL_INT64);
  CHECK(parameters[0].data_size == sizeof(int64_t));
  int64_t value;
  std::memcpy(&value, parameters[0].data, sizeof(value));
  CHECK(value == 41);
  if (fixture.fail) {
    result->database_error = OB_TIMEOUT;
    return SEEKDB_PLUGIN_STATUS_TIMEOUT;
  }
  value = 42;
  seekdb_plugin_sql_value_v1_t column = {
      sizeof(column), SEEKDB_PLUGIN_SQL_INT64, &value, sizeof(value), {0, 0}};
  const auto status = consume(consumer, &column, 1);
  result->returned_rows = status == SEEKDB_PLUGIN_STATUS_OK ? 1 : 0;
  return status;
}
static seekdb_plugin_status_t SEEKDB_PLUGIN_CALL emit(
    seekdb_plugin_host_handle_t *host, const seekdb_plugin_execution_result_v1_t *result)
{
  auto &sink = *reinterpret_cast<Sink *>(host);
  if (result->is_null) {
    CHECK(result->data_size == 0);
    sink.is_null = true;
    ++sink.calls;
    return SEEKDB_PLUGIN_STATUS_OK;
  }
  CHECK(result->data_size == sizeof(sink.value));
  CHECK(std::strcmp(result->type_id, "core.type.int64") == 0);
  std::memcpy(&sink.value, result->data, sizeof(sink.value));
  ++sink.calls;
  return SEEKDB_PLUGIN_STATUS_OK;
}

struct GeometrySink { int calls = 0; std::vector<uint8_t> bytes; };
static seekdb_plugin_status_t SEEKDB_PLUGIN_CALL emit_geometry(
    seekdb_plugin_host_handle_t *host, const seekdb_plugin_execution_result_v1_t *result)
{
  auto &sink = *reinterpret_cast<GeometrySink *>(host);
  CHECK(std::strcmp(result->type_id, "org.seekdb.gis.geometry") == 0);
  sink.bytes.assign(result->data, result->data + result->data_size);
  ++sink.calls;
  return SEEKDB_PLUGIN_STATUS_OK;
}

static void exercise_gis(ObPluginLoader &loader)
{
  const char *types[] = {"core.type.double", "core.type.double"};
  seekdb_plugin_sql_binding_v1_t binding = {};
  CHECK(loader.resolve_sql_extension(SEEKDB_PLUGIN_EXTENSION_FUNCTION,
      "st_point", types, 2, binding) == OB_SUCCESS);
  double coordinates[] = {3.0, 4.0};
  seekdb_plugin_execution_value_v1_t arguments[2] = {};
  for (int i = 0; i < 2; ++i) {
    arguments[i].struct_size = sizeof(arguments[i]);
    arguments[i].type_id = types[i];
    arguments[i].data = reinterpret_cast<const uint8_t *>(&coordinates[i]);
    arguments[i].data_size = sizeof(double);
  }
  GeometrySink sink;
  seekdb_plugin_execution_context_v2_t context = {};
  context.v1.host = reinterpret_cast<seekdb_plugin_host_handle_t *>(&sink);
  context.v1.emit_result = emit_geometry;
  // Both old and extended host contexts reach the unchanged C GIS callback.
  for (uint32_t size : {uint32_t(sizeof(context.v1)), uint32_t(sizeof(context))}) {
    context.v1.struct_size = size;
    CHECK(loader.execute_bound_function(binding, &context.v1, arguments, 2) == OB_SUCCESS);
    CHECK(sink.bytes.size() == 26 && sink.bytes[4] == 1 && sink.bytes[6] == 1);
    double xy[2];
    std::memcpy(xy, sink.bytes.data() + 10, sizeof(xy));
    CHECK(xy[0] == 3.0 && xy[1] == 4.0);
  }
  CHECK(sink.calls == 2);
  // Cross the C wrapper into the actual C++ geometry engine as well.
  const std::vector<uint8_t> point = sink.bytes;
  const char *geometry_types[] = {"org.seekdb.gis.geometry"};
  CHECK(loader.resolve_sql_extension(SEEKDB_PLUGIN_EXTENSION_FUNCTION,
      "st_centroid", geometry_types, 1, binding) == OB_SUCCESS);
  arguments[0].type_id = geometry_types[0];
  arguments[0].data = point.data();
  arguments[0].data_size = point.size();
  CHECK(loader.execute_bound_function(binding, &context.v1, arguments, 1) == OB_SUCCESS);
  CHECK(sink.calls == 3 && sink.bytes == point);
  seekdb_plugin_sql_binding_v1_t geometry{};
  CHECK(loader.resolve_sql_extension(SEEKDB_PLUGIN_EXTENSION_TYPE, "geometry", nullptr, 0, geometry) == OB_SUCCESS);
  CHECK(loader.check_bound_type_comparison(geometry) == OB_NOT_SUPPORTED);
  int32_t ordering = 99;
  auto legacy_value = arguments[0];
  legacy_value.type_id = geometry.object_id; // New comparison protocol uses the TYPE identity, not the legacy GIS alias.
  CHECK(loader.compare_bound_type(geometry, legacy_value, legacy_value, ordering) == OB_NOT_SUPPORTED && ordering == 0);
}

static seekdb_plugin_status_t SEEKDB_PLUGIN_CALL rust_fixture_sql(
    seekdb_plugin_sql_context_handle_t *context, const char *sql, uint64_t size,
    const seekdb_plugin_sql_value_v1_t *parameters, uint32_t count,
    uint64_t max_rows, seekdb_plugin_sql_consume_row_v1_fn consume, void *consumer,
    seekdb_plugin_sql_result_v1_t *result)
{
  auto &fixture = *reinterpret_cast<SqlFixture *>(context);
  ++fixture.calls;
  CHECK(std::string(sql, size) == "SELECT CHAR_LENGTH(CAST(? AS CHAR CHARACTER SET utf8mb4))");
  CHECK(count == 1 && max_rows == 1 && consume != nullptr);
  CHECK(parameters[0].kind == SEEKDB_PLUGIN_SQL_NULL ||
        (parameters[0].kind == SEEKDB_PLUGIN_SQL_TEXT &&
         std::string(static_cast<const char *>(parameters[0].data), parameters[0].data_size) == u8"A中🙂"));
  if (fixture.fail) { result->database_error = OB_TIMEOUT; return SEEKDB_PLUGIN_STATUS_TIMEOUT; }
  int64_t length = 3;
  seekdb_plugin_sql_value_v1_t column = {
    sizeof(column), SEEKDB_PLUGIN_SQL_INT64, &length, sizeof(length), {0, 0}};
  if (parameters[0].kind == SEEKDB_PLUGIN_SQL_NULL) {
    column.kind = SEEKDB_PLUGIN_SQL_NULL; column.data = nullptr; column.data_size = 0;
  }
  const auto status = consume(consumer, &column, 1);
  result->returned_rows = status == SEEKDB_PLUGIN_STATUS_OK ? 1 : 0;
  return status;
}

#include "rust_batch_loader_fixture.h"

static void exercise_rust(ObPluginLoader &loader)
{
  const char *types[] = {"core.type.bytes"};
  seekdb_plugin_sql_binding_v1_t binding = {};
  const char input[] = u8"A中🙂";
  seekdb_plugin_execution_value_v1_t argument = {};
  argument.struct_size = sizeof(argument); argument.type_id = types[0];
  argument.data = reinterpret_cast<const uint8_t *>(input); argument.data_size = sizeof(input) - 1;
  Sink sink;
  SqlFixture fixture;
  seekdb_plugin_sql_api_v1_t api = {sizeof(api), 1, 0, 0, rust_fixture_sql, {0}};
  seekdb_plugin_execution_context_v2_t context = {};
  context.v1.struct_size = sizeof(context.v1);
  context.v1.host = reinterpret_cast<seekdb_plugin_host_handle_t *>(&sink);
  context.v1.emit_result = emit;
  context.sql_api = &api;
  context.sql_context = reinterpret_cast<seekdb_plugin_sql_context_handle_t *>(&fixture);
  CHECK(loader.resolve_sql_extension(SEEKDB_PLUGIN_EXTENSION_FUNCTION,
      "seekdb_rust_char_count", types, 1, binding) == OB_SUCCESS);
  CHECK(loader.execute_bound_function(binding, &context.v1, &argument, 1) == OB_SUCCESS);
  CHECK(sink.calls == 1 && sink.value == 3);
  const uint8_t invalid_utf8[] = {0xff};
  argument.data = invalid_utf8; argument.data_size = sizeof(invalid_utf8);
  CHECK(loader.execute_bound_function(binding, &context.v1, &argument, 1) != OB_SUCCESS);
  CHECK(sink.calls == 1);
  argument.data = reinterpret_cast<const uint8_t *>(input); argument.data_size = sizeof(input) - 1;
  CHECK(loader.resolve_sql_extension(SEEKDB_PLUGIN_EXTENSION_FUNCTION,
      "seekdb_rust_sql_chars", types, 1, binding) == OB_SUCCESS);
  CHECK(loader.execute_bound_function(binding, &context.v1, &argument, 1) != OB_SUCCESS);
  CHECK(fixture.calls == 0);
  context.v1.struct_size = sizeof(context);
  sink = {};
  CHECK(loader.execute_bound_function(binding, &context.v1, &argument, 1) == OB_SUCCESS);
  CHECK(fixture.calls == 1 && sink.calls == 1 && sink.value == 3);
  fixture.fail = true; sink = {};
  CHECK(loader.execute_bound_function(binding, &context.v1, &argument, 1) != OB_SUCCESS);
  CHECK(fixture.calls == 2 && sink.calls == 0);
  fixture.fail = false; argument.is_null = 1; argument.data = nullptr; argument.data_size = 0;
  CHECK(loader.execute_bound_function(binding, &context.v1, &argument, 1) == OB_SUCCESS);
  CHECK(sink.calls == 1 && sink.is_null);
}

struct TypedSink {
  ObPluginLoader *loader = nullptr;
  int calls = 0;
  bool fail = false;
  bool is_null = false;
  std::string type;
  std::vector<uint8_t> bytes;
};
static seekdb_plugin_status_t SEEKDB_PLUGIN_CALL emit_typed(
    seekdb_plugin_host_handle_t *opaque, const seekdb_plugin_execution_result_v1_t *result)
{
  auto &sink = *reinterpret_cast<TypedSink *>(opaque);
  // Reenter an administrative read: callback execution must not retain the
  // loader lock. Both object and implementation leases must pin this module.
  ObPluginStatusSnapshot status;
  CHECK(sink.loader->get_status("org.seekdb.rust-text", status) == OB_SUCCESS);
  CHECK(status.lease_count_ >= 2);
  ++sink.calls;
  if (sink.fail) return SEEKDB_PLUGIN_STATUS_INVALID_ARGUMENT;
  sink.type = result->type_id;
  sink.is_null = result->is_null;
  sink.bytes.clear();
  if (!sink.is_null && result->data_size) sink.bytes.assign(result->data, result->data + result->data_size);
  return SEEKDB_PLUGIN_STATUS_OK;
}

struct TableSink {
  std::vector<std::string> tokens;
  std::vector<int64_t> ordinals;
};
static seekdb_plugin_status_t SEEKDB_PLUGIN_CALL emit_table(
    seekdb_plugin_host_handle_t *host, const seekdb_plugin_table_row_v1_t *row)
{
  auto &sink = *reinterpret_cast<TableSink *>(host);
  CHECK(row && row->struct_size >= sizeof(*row) && row->column_count == 2);
  const auto &token = row->columns[0], &ordinal = row->columns[1];
  CHECK(!token.is_null && std::strcmp(token.type_id, "org.seekdb.rust-text.utf8") == 0);
  CHECK(!ordinal.is_null && std::strcmp(ordinal.type_id, "core.type.int64") == 0);
  CHECK(ordinal.data_size == sizeof(int64_t));
  int64_t value = 0; std::memcpy(&value, ordinal.data, sizeof(value));
  sink.tokens.emplace_back(reinterpret_cast<const char *>(token.data), token.data_size);
  sink.ordinals.push_back(value);
  return SEEKDB_PLUGIN_STATUS_OK;
}

static void exercise_series_null(ObPluginLoader &loader)
{
  const char *types[] = {"core.type.int64", "core.type.int64"};
  seekdb_plugin_sql_binding_v1_t binding = {};
  CHECK(loader.resolve_sql_extension(SEEKDB_PLUGIN_EXTENSION_TABLE_FUNCTION,
      "seekdb_generate_series", types, 2, binding) == OB_SUCCESS);
  CHECK(binding.flags & SEEKDB_PLUGIN_EXTENSION_FLAG_NULL_PROPAGATING);
  seekdb_plugin_table_estimate_v1_t estimate = {};
  CHECK(loader.estimate_bound_table_function(binding, estimate) == OB_SUCCESS);
  CHECK(estimate.struct_size == sizeof(estimate));
  CHECK(estimate.rows == 199 && estimate.row_width == 199 && estimate.total_cost == 1);
  TableSink sink;
  seekdb_plugin_table_execution_context_v1_t context = {};
  context.struct_size = sizeof(context); context.emit_row = emit_table;
  context.host = reinterpret_cast<seekdb_plugin_host_handle_t *>(&sink);
  const int64_t one = 1;
  for (int nulls = 1; nulls < 4; ++nulls) {
    seekdb_plugin_execution_value_v1_t arguments[2] = {};
    for (int i = 0; i < 2; ++i) {
      arguments[i].struct_size = sizeof(arguments[i]); arguments[i].type_id = types[i];
      arguments[i].is_null = bool(nulls & (1 << i));
      if (!arguments[i].is_null) {
        arguments[i].data = reinterpret_cast<const uint8_t *>(&one); arguments[i].data_size = sizeof(one);
      }
    }
    std::unique_ptr<IPluginTableCursor> cursor;
    CHECK(loader.open_bound_table_function(binding, &context, arguments, 2, cursor) == OB_ITER_END && !cursor);
    CHECK(sink.tokens.empty());
  }
}

static void exercise_rust_tables(ObPluginLoader &loader, ObPluginServiceRegistry &registry)
{
  const char *types[] = {"org.seekdb.rust-text.utf8"};
  seekdb_plugin_sql_binding_v1_t binding = {};
  CHECK(loader.resolve_sql_extension(SEEKDB_PLUGIN_EXTENSION_TABLE_FUNCTION,
      "seekdb_rust_words_bytes", types, 1, binding) == OB_SUCCESS);
  seekdb_plugin_sql_column_v1_t column = {};
  CHECK(loader.describe_sql_column(binding, 0, column) == OB_SUCCESS);
  CHECK(std::strcmp(column.type_id, types[0]) == 0 && !column.nullable);
  seekdb_plugin_execution_value_v1_t input = {};
  input.struct_size = sizeof(input); input.type_id = types[0];
  std::string text = u8"hello 中🙂 world";
  const auto set_text = [&]() {
    input.data = reinterpret_cast<const uint8_t *>(text.data()); input.data_size = text.size();
  };
  set_text();
  TableSink sink;
  seekdb_plugin_table_execution_context_v1_t context = {};
  context.struct_size = sizeof(context);
  context.host = reinterpret_cast<seekdb_plugin_host_handle_t *>(&sink); context.emit_row = emit_table;
  std::unique_ptr<IPluginTableCursor> cursor;
  const auto leases = [&]() {
    ObPluginStatusSnapshot status;
    CHECK(loader.get_status("org.seekdb.rust-text", status) == OB_SUCCESS);
    return status.lease_count_;
  };
  CHECK(leases() == 0);
  const auto reject_binding = [&](const seekdb_plugin_sql_binding_v1_t &bad, int error) {
    seekdb_plugin_table_estimate_v1_t estimate = {};
    estimate.struct_size = 999;
    CHECK(loader.estimate_bound_table_function(bad, estimate) == error && estimate.struct_size == 0);
    CHECK(loader.open_bound_table_function(bad, &context, &input, 1, cursor) == error && !cursor);
    column.struct_size = 999;
    CHECK(loader.describe_sql_column(bad, 0, column) == error && column.struct_size == 0);
    CHECK(leases() == 0);
  };
  seekdb_plugin_table_estimate_v1_t estimate = {};
  CHECK(loader.estimate_bound_table_function(binding, estimate) == OB_SUCCESS);
  CHECK(estimate.struct_size == sizeof(estimate));
  CHECK(estimate.rows == 8 && estimate.row_width == 40 && estimate.total_cost == 4 && leases() == 0);
  auto bad = binding; bad.catalog_epoch = 0; reject_binding(bad, OB_INVALID_ARGUMENT);
  bad = binding; ++bad.catalog_epoch; reject_binding(bad, OB_STATE_NOT_MATCH);
  bad = binding; ++bad.column_count; reject_binding(bad, OB_STATE_NOT_MATCH);
  bad = binding; bad.flags ^= SEEKDB_PLUGIN_EXTENSION_FLAG_IMMUTABLE; reject_binding(bad, OB_STATE_NOT_MATCH);
  bad = binding; bad.reserved[0] = 1; reject_binding(bad, OB_INVALID_ARGUMENT);
  bad = binding; std::memset(bad.object_id, 'x', sizeof(bad.object_id)); reject_binding(bad, OB_INVALID_ARGUMENT);
  for (int malformed = 0; malformed < 5; ++malformed) {
    auto invalid = input;
    if (malformed == 0) invalid.is_null = 2;
    if (malformed == 1) invalid.reserved_bytes[0] = 1;
    if (malformed == 2) invalid.reserved[0] = 1;
    if (malformed == 3) invalid.data_size = UINT64_C(16777217);
    if (malformed == 4) invalid.data = nullptr;
    CHECK(loader.open_bound_table_function(binding, &context, &invalid, 1, cursor) == OB_INVALID_ARGUMENT);
    CHECK(!cursor && leases() == 0);
  }
  // The real Rust bytes-only entry rejects the original custom ID. Success
  // therefore proves that the selected Rust implicit cast ran before open.
  CHECK(loader.open_bound_table_function(binding, &context, &input, 1, cursor) == OB_SUCCESS && cursor);
  CHECK(leases() == 4); // table object/code and cast object/code, held by cursor
  text.assign("changed caller buffer"); // Rust cursor owns the converted input.
  uint32_t rows = 99;
  CHECK(cursor->next(&context, 2, &rows) == OB_SUCCESS && rows == 2);
  CHECK(sink.tokens == (std::vector<std::string>{"hello", u8"中🙂"}));
  CHECK(cursor->next(&context, 2, &rows) == OB_SUCCESS && rows == 1);
  CHECK(sink.tokens.back() == "world" && sink.ordinals == (std::vector<int64_t>{1, 2, 3}));
  CHECK(cursor->next(&context, 2, &rows) == OB_ITER_END && rows == 0);
  text = "again words"; set_text(); sink = {};
  CHECK(cursor->rescan(&input, 1) == OB_SUCCESS);
  CHECK(cursor->next(&context, 2, &rows) == OB_SUCCESS && rows == 2);
  CHECK(sink.tokens == (std::vector<std::string>{"again", "words"}));
  CHECK(sink.ordinals == (std::vector<int64_t>{1, 2}));
  auto changed_shape = input; changed_shape.type_id = "core.type.bytes";
  CHECK(cursor->rescan(&changed_shape, 1) == OB_STATE_NOT_MATCH);
  CHECK(cursor->next(&context, 2, &rows) == OB_STATE_NOT_MATCH && rows == 0);
  text.assign(1, char(0xff)); set_text();
  CHECK(cursor->rescan(&input, 1) == OB_INVALID_ARGUMENT);
  CHECK(cursor->next(&context, 2, &rows) == OB_STATE_NOT_MATCH && rows == 0);
  text = "recovered"; set_text(); sink = {};
  CHECK(cursor->rescan(&input, 1) == OB_SUCCESS);
  CHECK(cursor->next(&context, 2, &rows) == OB_SUCCESS && rows == 1);
  CHECK(sink.tokens == std::vector<std::string>{"recovered"});
  // An admitted cursor retains its cast selection across unrelated publication.
  // New bindings and metadata reads must not use the old catalog snapshot.
  static const int marker_service = 0;
  auto marker = std::make_shared<ObPluginGeneration>("test.table.epoch", 1);
  CHECK(marker->transition_to(ObPluginState::VALIDATED) == OB_SUCCESS);
  CHECK(marker->transition_to(ObPluginState::LOADED) == OB_SUCCESS);
  CHECK(marker->transition_to(ObPluginState::INITIALIZING) == OB_SUCCESS);
  ObPluginRegistration registration;
  CHECK(registry.begin_registration(marker, registration) == OB_SUCCESS);
  CHECK(registration.add_service("test.table.epoch", 1, 0, &marker_service) == OB_SUCCESS);
  CHECK(registration.commit() == OB_SUCCESS);
  std::unique_ptr<IPluginTableCursor> rejected;
  CHECK(loader.open_bound_table_function(binding, &context, &input, 1, rejected) == OB_STATE_NOT_MATCH && !rejected);
  column.struct_size = 999;
  CHECK(loader.describe_sql_column(binding, 0, column) == OB_STATE_NOT_MATCH && column.struct_size == 0);
  CHECK(cursor->rescan(&input, 1) == OB_SUCCESS && leases() == 4);
  CHECK(cursor->next(&context, 2, &rows) == OB_SUCCESS && rows == 1);
  input.is_null = 1;
  CHECK(cursor->rescan(&input, 1) == OB_SUCCESS);
  CHECK(cursor->next(&context, 2, &rows) == OB_ITER_END && rows == 0);
  CHECK(cursor->close() == OB_SUCCESS && leases() == 0);
  CHECK(cursor->close() == OB_SUCCESS); cursor.reset();
  CHECK(registry.quiesce(marker) == OB_SUCCESS);
  CHECK(registry.mark_stopped(marker) == OB_SUCCESS);
  CHECK(loader.resolve_sql_extension(SEEKDB_PLUGIN_EXTENSION_TABLE_FUNCTION,
      "seekdb_rust_words_bytes", types, 1, binding) == OB_SUCCESS);
  // The real DSO receives v4 and re-reads projection per callback. Casts and
  // cursor state still use the complete declared schema, including all-zero
  // masks used by count-only SQL consumers.
  input.is_null = 0; text = "first second third"; set_text(); sink = {};
  uint8_t requested[] = {0, 1};
  seekdb_plugin_table_execution_context_v4_t projected{};
  projected.v3.v2.v1 = context;
  projected.v3.v2.v1.struct_size = sizeof(projected);
  projected.column_count = 2; projected.requested_columns = requested;
  CHECK(loader.open_bound_table_function(binding, &projected.v3.v2.v1, &input, 1, cursor) == OB_SUCCESS);
  CHECK(leases() == 4);
  CHECK(cursor->next(&projected.v3.v2.v1, 1, &rows) == OB_SUCCESS && rows == 1);
  requested[0] = 1; requested[1] = 0;
  CHECK(cursor->next(&projected.v3.v2.v1, 1, &rows) == OB_SUCCESS && rows == 1);
  requested[0] = 0;
  CHECK(cursor->next(&projected.v3.v2.v1, 1, &rows) == OB_SUCCESS && rows == 1);
  CHECK(sink.tokens == (std::vector<std::string>{"", "second", ""}));
  CHECK(sink.ordinals == (std::vector<int64_t>{1, 0, 0}));
  CHECK(cursor->next(&projected.v3.v2.v1, 1, &rows) == OB_ITER_END && rows == 0);
  sink = {};
  CHECK(cursor->rescan(&input, 1) == OB_SUCCESS);
  CHECK(cursor->next(&context, 3, &rows) == OB_SUCCESS && rows == 3);
  CHECK(sink.tokens == (std::vector<std::string>{"first", "second", "third"}));
  CHECK(sink.ordinals == (std::vector<int64_t>{1, 2, 3}));
  CHECK(cursor->rescan(&input, 1) == OB_SUCCESS);
  requested[0] = 2;
  CHECK(cursor->next(&projected.v3.v2.v1, 1, &rows) == OB_INVALID_ARGUMENT && rows == 0);
  CHECK(cursor->next(&context, 1, &rows) == OB_STATE_NOT_MATCH && rows == 0);
  cursor.reset(); CHECK(leases() == 0);
  CHECK(loader.open_bound_table_function(binding, &projected.v3.v2.v1, &input, 1, cursor) == OB_INVALID_ARGUMENT);
  CHECK(!cursor && leases() == 0);
  input.is_null = 0; text.assign(1, char(0xff)); set_text();
  CHECK(loader.open_bound_table_function(binding, &context, &input, 1, cursor) == OB_INVALID_ARGUMENT);
  CHECK(!cursor && leases() == 0);
  // Unknown NULL needs only the declared target ID, not a fictitious cast.
  input.type_id = nullptr; input.is_null = 1;
  CHECK(loader.open_bound_table_function(binding, &context, &input, 1, cursor) == OB_SUCCESS);
  CHECK(leases() == 2);
  CHECK(cursor->next(&context, 2, &rows) == OB_ITER_END && rows == 0);
  cursor.reset(); CHECK(leases() == 0);

  for (bool strict : {false, true}) {
    const char *name = strict ? "seekdb_rust_words_strict" : "seekdb_rust_words_or_null";
    for (const char *type : {static_cast<const char *>(nullptr), "core.type.bytes", types[0]}) {
      CHECK(loader.resolve_sql_extension(SEEKDB_PLUGIN_EXTENSION_TABLE_FUNCTION, name, &type, 1, binding) == OB_SUCCESS);
      input.type_id = type; input.is_null = 1;
      input.data = nullptr; input.data_size = 0; sink = {};
      const int opened = loader.open_bound_table_function(binding, &context, &input, 1, cursor);
      if (strict) {
        CHECK(opened == OB_ITER_END && !cursor && leases() == 0);
      } else {
        CHECK(opened == OB_SUCCESS && cursor);
        CHECK(cursor->next(&context, 2, &rows) == OB_SUCCESS && rows == 1);
        CHECK(sink.tokens == std::vector<std::string>{"<NULL>"});
        CHECK(cursor->next(&context, 2, &rows) == OB_ITER_END && rows == 0);
        CHECK(cursor->rescan(&input, 1) == OB_SUCCESS);
        CHECK(cursor->next(&context, 2, &rows) == OB_SUCCESS && rows == 1);
        cursor.reset(); CHECK(leases() == 0);
      }
    }
    // A strict NULL rescan suspends the old cursor, not the generation leases.
    // A subsequent non-NULL rescan restores its execution with fresh values.
    CHECK(loader.resolve_sql_extension(SEEKDB_PLUGIN_EXTENSION_TABLE_FUNCTION, name, types, 1, binding) == OB_SUCCESS);
    input.type_id = types[0]; input.is_null = 0; text = "original"; set_text();
    CHECK(loader.open_bound_table_function(binding, &context, &input, 1, cursor) == OB_SUCCESS);
    CHECK(leases() == 4);
    input.is_null = 1; sink = {};
    CHECK(cursor->rescan(&input, 1) == OB_SUCCESS);
    CHECK(cursor->next(&context, 2, &rows) == (strict ? OB_ITER_END : OB_SUCCESS));
    CHECK(rows == (strict ? 0 : 1) && leases() == 4);
    CHECK(sink.tokens == (strict ? std::vector<std::string>{} : std::vector<std::string>{"<NULL>"}));
    input.is_null = 0; text.assign(1, char(0xff)); set_text();
    CHECK(cursor->rescan(&input, 1) == OB_INVALID_ARGUMENT);
    CHECK(cursor->next(&context, 2, &rows) == OB_STATE_NOT_MATCH && rows == 0);
    text = "new value"; set_text(); sink = {};
    CHECK(cursor->rescan(&input, 1) == OB_SUCCESS);
    CHECK(cursor->next(&context, 2, &rows) == OB_SUCCESS && rows == 2);
    CHECK(sink.tokens == (std::vector<std::string>{"new", "value"}));
    CHECK(sink.ordinals == (std::vector<int64_t>{1, 2}));
    input.is_null = 1;
    CHECK(cursor->rescan(&input, 1) == OB_SUCCESS);
    CHECK(cursor->close() == OB_SUCCESS && leases() == 0);
    CHECK(cursor->close() == OB_SUCCESS); cursor.reset();
  }
}

static void exercise_rust_catalog(ObPluginLoader &loader, ObPluginServiceRegistry &registry)
{
  ExtensionPackageSource source;
  source.name_ = "rust_text_ops"; source.version_ = "1.0"; source.native_module_ = "org.seekdb.rust-text";
  source.scripts_ = {{"", "1.0", "SELECT 1;"}};
  std::unique_ptr<ICatalogDeclarations> declarations;
  const auto leases = [&]() {
    ObPluginStatusSnapshot status;
    CHECK(loader.get_status(source.native_module_, status) == OB_SUCCESS);
    return status.lease_count_;
  };
  CHECK(loader.prepare_catalog_install(source, 1, 10, 20, declarations) == OB_SUCCESS && declarations);
  CHECK(declarations->sql().size() == 1 && declarations->sql()[0].find("rust_runtime_length") != std::string::npos);
  CHECK(declarations->sql()[0].find("seekdb_rust_char_count") != std::string::npos);
  CHECK(leases() == 1); // Retained AFTER prepare, through future schema commit.
  declarations.reset(); CHECK(leases() == 0);
  source.native_install_ = true; source.scripts_.clear();
  source.name_ = "rust_text_native";
  CHECK(loader.prepare_catalog_install(source, 1, 10, 20, declarations) == OB_SUCCESS && declarations);
  CHECK(declarations->sql().size() == 1 && declarations->sql()[0].find("rust_native_length") != std::string::npos);
  CHECK(leases() == 1); declarations.reset(); CHECK(leases() == 0);
  source.name_ = "other_ops";
  CHECK(loader.prepare_catalog_install(source, 1, 10, 20, declarations) == OB_SUCCESS && declarations);
  CHECK(declarations->sql().empty() && declarations->program());
  declarations.reset(); // A no-object build is rejected by the Root installer.
  CHECK(leases() == 0);
  source.name_ = "rust_text_built";
  for (int fault = 0; fault < 5; ++fault) {
    CHECK(loader.prepare_catalog_install(source, 1, 10, 20, declarations) == OB_SUCCESS && declarations);
    CHECK(declarations->sql().empty() && declarations->program() && leases() == 1);
    ExtensionInstallSpec spec{1, 10, 20, source.name_, "1.0", source.native_module_, {}, {}};
    std::string error;
    auto *program = declarations->program();
    CHECK(program->preflight(spec, error) == OB_SUCCESS);
    auto bad = spec; ++bad.owner_id_;
    CHECK(program->preflight(bad, error) == OB_STATE_NOT_MATCH);
    class Builder final : public ICatalogRoutineBuilder {
    public:
      Builder(ObPluginLoader &loader, int fault) : loader_(loader), fault_(fault) {}
      ObPluginLoader &loader_; int fault_, calls_ = 0;
      int lookup_routine(CatalogRoutineKind kind, const std::string &name, uint64_t &id, std::string &) override {
        ObPluginStatusSnapshot status;
        CHECK(loader_.get_status("org.seekdb.rust-text", status) == OB_SUCCESS && status.lease_count_ == 1);
        CHECK(name == "rust_built_length" || name == "RUST_BUILT_LENGTH");
        id = fault_ == 4 ? UINT64_MAX : kind == CatalogRoutineKind::FUNCTION && calls_ > 0 ? 101 : 0;
        return fault_ == 3 ? OB_TIMEOUT : OB_SUCCESS;
      }
      int create_routine(const std::string &sql, uint64_t &id, std::string &) override {
        ObPluginStatusSnapshot status;
        CHECK(loader_.get_status("org.seekdb.rust-text", status) == OB_SUCCESS && status.lease_count_ == 1);
        CHECK(sql.find(calls_ == 0 ? "rust_built_length" : "rust_built_nonempty") != std::string::npos);
        ++calls_;
        id = fault_ == 2 ? 0 : 100 + calls_;
        return fault_ == 1 ? OB_TIMEOUT : OB_SUCCESS;
      }
    } builder(loader, fault);
    CHECK(program->build(builder, error) == (fault == 0 ? OB_SUCCESS : fault == 1 || fault == 3 ? OB_TIMEOUT : OB_ERR_UNEXPECTED));
    CHECK(builder.calls_ == (fault == 0 ? 2 : fault >= 3 ? 0 : 1));
    CHECK(program->build(builder, error) == OB_STATE_NOT_MATCH);
    CHECK(leases() == 1); declarations.reset(); CHECK(leases() == 0);
  }
  source.native_module_ = "org.missing";
  CHECK(loader.prepare_catalog_install(source, 1, 10, 20, declarations) == OB_ENTRY_NOT_EXIST && !declarations);
  source.native_module_.clear();
  CHECK(loader.prepare_catalog_install(source, 1, 10, 20, declarations) == OB_INVALID_ARGUMENT && !declarations);
  source.native_module_ = "org.seekdb.rust-text";
  source.native_install_ = false; source.scripts_ = {{"", "1.0", "SELECT 1;"}};
  source.name_ = "other_ops";
  CHECK(loader.prepare_catalog_install(source, 1, 10, 20, declarations) == OB_SUCCESS && declarations);
  CHECK(declarations->sql().empty() && leases() == 1);
  declarations.reset();
  source.name_ = "rust_text_ops";
  source.scripts_[0].sql_.assign(4 * 1024 * 1024, 'x');
  CHECK(loader.prepare_catalog_install(source, 1, 10, 20, declarations) == OB_INVALID_ARGUMENT && !declarations);
  CHECK(leases() == 0);
  source.scripts_[0].sql_ = "SELECT 1;";
  CHECK(loader.prepare_catalog_install(source, 1, 10, 0, declarations) == OB_INVALID_ARGUMENT && !declarations);
  source.from_version_ = "0.9";
  CHECK(loader.prepare_catalog_install(source, 1, 10, 20, declarations) == OB_INVALID_ARGUMENT && !declarations);
  source.from_version_.clear(); source.native_module_.clear();
  CHECK(loader.prepare_catalog_install(source, 1, 10, 20, declarations) == OB_SUCCESS && !declarations);
  source.native_module_ = "org.missing";
  CHECK(loader.prepare_catalog_install(source, 1, 10, 20, declarations) == OB_SUCCESS && !declarations);
  // A matching service name does not confer another module's identity.
  for (bool wrong_owner : {true, false}) {
    auto generation = std::make_shared<ObPluginGeneration>(wrong_owner ? "org.other" : "org.forged", 1);
    CHECK(generation->transition_to(ObPluginState::VALIDATED) == OB_SUCCESS);
    CHECK(generation->transition_to(ObPluginState::LOADED) == OB_SUCCESS);
    CHECK(generation->transition_to(ObPluginState::INITIALIZING) == OB_SUCCESS);
    seekdb_plugin_catalog_service_v1_t service{};
    service.struct_size = wrong_owner ? sizeof(service) : 0;
    service.spi_major = 1;
    service.prepare = [](seekdb_plugin_instance_handle_t *, const seekdb_plugin_catalog_context_v1_t *) -> seekdb_plugin_status_t {
      CHECK(false); return SEEKDB_PLUGIN_STATUS_INTERNAL;
    };
    ObPluginRegistration registration;
    CHECK(registry.begin_registration(generation, registration) == OB_SUCCESS);
    CHECK(registration.add_service("org.forged.catalog.install", 1, 0, &service) == OB_SUCCESS);
    CHECK(registration.commit() == OB_SUCCESS);
    source.native_module_ = "org.forged";
    CHECK(loader.prepare_catalog_install(source, 1, 10, 20, declarations) ==
        (wrong_owner ? OB_STATE_NOT_MATCH : OB_NOT_SUPPORTED));
    CHECK(!declarations);
    CHECK(registry.quiesce(generation) == OB_SUCCESS);
    CHECK(registry.mark_stopped(generation) == OB_SUCCESS);
  }
}

// Planning-only fixture: the default-estimate path must not need a native
// instance or enter execution callbacks. Real Rust custom estimates are tested
// separately in exercise_rust_tables and the kernel optimizer fixture.
static void exercise_table_control_defaults(ObPluginLoader &loader, ObPluginServiceRegistry &registry)
{
  for (int variant = 0; variant < 5; ++variant) {
    seekdb_plugin_table_function_service_v2_t service{};
    service.v1.struct_size = sizeof(service);
    service.v1.spi_major = 1;
    service.v1.spi_minor = variant == 0 ? 0 : variant == 1 ? 1 : 2;
    if (variant == 3) service.v1.struct_size = sizeof(service.v1);
    if (variant == 4) service.reserved[0] = 1;
    service.v1.open = [](seekdb_plugin_instance_handle_t *, const seekdb_plugin_table_execution_context_v1_t *,
        const seekdb_plugin_execution_value_v1_t *, uint32_t, seekdb_plugin_table_cursor_handle_t **) -> seekdb_plugin_status_t {
      CHECK(false); return SEEKDB_PLUGIN_STATUS_INTERNAL;
    };
    service.v1.next = [](seekdb_plugin_instance_handle_t *, seekdb_plugin_table_cursor_handle_t *,
        const seekdb_plugin_table_execution_context_v1_t *, uint32_t, uint32_t *) -> seekdb_plugin_status_t {
      CHECK(false); return SEEKDB_PLUGIN_STATUS_INTERNAL;
    };
    service.v1.rescan = [](seekdb_plugin_instance_handle_t *, seekdb_plugin_table_cursor_handle_t *,
        const seekdb_plugin_execution_value_v1_t *, uint32_t) -> seekdb_plugin_status_t {
      CHECK(false); return SEEKDB_PLUGIN_STATUS_INTERNAL;
    };
    service.v1.close = [](seekdb_plugin_instance_handle_t *, seekdb_plugin_table_cursor_handle_t *) -> seekdb_plugin_status_t {
      CHECK(false); return SEEKDB_PLUGIN_STATUS_INTERNAL;
    };
    auto generation = std::make_shared<ObPluginGeneration>("test.table.control", variant + 1);
    CHECK(generation->transition_to(ObPluginState::VALIDATED) == OB_SUCCESS);
    CHECK(generation->transition_to(ObPluginState::LOADED) == OB_SUCCESS);
    CHECK(generation->transition_to(ObPluginState::INITIALIZING) == OB_SUCCESS);
    ObPluginRegistration registration;
    CHECK(registry.begin_registration(generation, registration) == OB_SUCCESS);
    CHECK(registration.add_service("test.table.control", 1, 0, &service) == OB_SUCCESS);
    ObPluginExtensionSpec spec;
    spec.kind_ = SEEKDB_PLUGIN_EXTENSION_TABLE_FUNCTION;
    spec.object_id_ = "test.table.control"; spec.sql_name_ = "fixture_control_defaults";
    spec.result_columns_.push_back({"value", "core.type.bytes", true});
    spec.implementation_.service_id_ = "test.table.control";
    auto &version = spec.implementation_.version_range_;
    version.struct_size = sizeof(version);
    version.minimum_inclusive = {1, 0, 0}; version.maximum_exclusive = {2, 0, 0};
    CHECK(registration.add_extension(spec) == OB_SUCCESS);
    CHECK(registration.commit() == OB_SUCCESS);
    seekdb_plugin_sql_binding_v1_t binding{};
    CHECK(loader.resolve_sql_extension(SEEKDB_PLUGIN_EXTENSION_TABLE_FUNCTION,
        "fixture_control_defaults", nullptr, 0, binding) == OB_SUCCESS);
    seekdb_plugin_table_estimate_v1_t estimate{};
    estimate.struct_size = 999;
    const bool supported = variant == 0 || variant == 2;
    CHECK(loader.estimate_bound_table_function(binding, estimate) ==
        (supported ? OB_SUCCESS : OB_NOT_SUPPORTED));
    if (supported) {
      CHECK(estimate.struct_size == sizeof(estimate));
      CHECK(estimate.rows == 199 && estimate.row_width == 199 && estimate.total_cost == 1);
    } else {
      CHECK(estimate.struct_size == 0);
    }
    CHECK(generation->lease_count() == 0);
    CHECK(registry.quiesce(generation) == OB_SUCCESS);
    CHECK(registry.mark_stopped(generation) == OB_SUCCESS);
  }
}

static void exercise_rust_sql_table(ObPluginLoader &loader)
{
  const char *type = "core.type.bytes";
  seekdb_plugin_sql_binding_v1_t binding{};
  CHECK(loader.resolve_sql_extension(SEEKDB_PLUGIN_EXTENSION_TABLE_FUNCTION,
      "seekdb_rust_sql_series", &type, 1, binding) == OB_SUCCESS);
  seekdb_plugin_table_estimate_v1_t estimate{};
  CHECK(loader.estimate_bound_table_function(binding, estimate) == OB_SUCCESS);
  CHECK(estimate.rows == 199 && estimate.row_width == 199 && estimate.total_cost == 1);
  struct Result { std::vector<int64_t> values; } sink;
  SqlFixture fixture;
  seekdb_plugin_sql_api_v1_t api{sizeof(api), 1, 0, 0, nullptr, {0}};
  api.execute = [](seekdb_plugin_sql_context_handle_t *context, const char *sql, uint64_t size,
      const seekdb_plugin_sql_value_v1_t *parameters, uint32_t count, uint64_t max_rows,
      seekdb_plugin_sql_consume_row_v1_fn consume, void *consumer,
      seekdb_plugin_sql_result_v1_t *output) -> seekdb_plugin_status_t {
    if (std::string(sql, size) == "SELECT CHAR_LENGTH(CAST(? AS CHAR CHARACTER SET utf8mb4))")
      return rust_fixture_sql(context, sql, size, parameters, count, max_rows, consume, consumer, output);
    auto &fixture = *reinterpret_cast<SqlFixture *>(context);
    ++fixture.calls;
    CHECK(std::string(sql, size) == "SELECT CAST(? AS SIGNED) + 1");
    CHECK(count == 1 && max_rows == 1 && consume);
    CHECK(parameters[0].kind == SEEKDB_PLUGIN_SQL_INT64 && parameters[0].data_size == sizeof(int64_t));
    if (fixture.fail) { output->database_error = OB_TIMEOUT; return SEEKDB_PLUGIN_STATUS_TIMEOUT; }
    int64_t value = 0; std::memcpy(&value, parameters[0].data, sizeof(value));
    CHECK(value >= 0 && value < 3); ++value;
    seekdb_plugin_sql_value_v1_t result{sizeof(result), SEEKDB_PLUGIN_SQL_INT64, &value, sizeof(value), {0}};
    const auto status = consume(consumer, &result, 1);
    output->returned_rows = status == SEEKDB_PLUGIN_STATUS_OK ? 1 : 0;
    return status;
  };
  seekdb_plugin_table_execution_context_v3_t context{};
  context.v2.v1.struct_size = sizeof(context);
  context.v2.v1.host = reinterpret_cast<seekdb_plugin_host_handle_t *>(&sink);
  context.v2.v1.emit_row = [](seekdb_plugin_host_handle_t *host, const seekdb_plugin_table_row_v1_t *row) -> seekdb_plugin_status_t {
    CHECK(row && row->column_count == 1 && !row->columns[0].is_null);
    CHECK(std::strcmp(row->columns[0].type_id, "core.type.int64") == 0 && row->columns[0].data_size == sizeof(int64_t));
    int64_t value = 0; std::memcpy(&value, row->columns[0].data, sizeof(value));
    reinterpret_cast<Result *>(host)->values.push_back(value);
    return SEEKDB_PLUGIN_STATUS_OK;
  };
  context.v2.query_context = reinterpret_cast<seekdb_plugin_sql_context_handle_t *>(&fixture);
  context.sql_api = &api;
  const char *text = u8"A中🙂";
  seekdb_plugin_execution_value_v1_t argument{};
  argument.struct_size = sizeof(argument); argument.type_id = type;
  argument.data = reinterpret_cast<const uint8_t *>(text); argument.data_size = std::strlen(text);
  std::unique_ptr<IPluginTableCursor> cursor;
  const auto open = [&](const seekdb_plugin_table_execution_context_v1_t &call) {
    return loader.open_bound_table_function(binding, &call, &argument, 1, cursor);
  };
  const auto leases = [&]() {
    ObPluginStatusSnapshot status;
    CHECK(loader.get_status("org.seekdb.rust-text", status) == OB_SUCCESS);
    return status.lease_count_;
  };
  auto legacy = context.v2.v1; legacy.struct_size = sizeof(legacy);
  CHECK(open(legacy) != OB_SUCCESS && !cursor && fixture.calls == 0 && leases() == 0);
  auto control_only = context.v2; control_only.v1.struct_size = sizeof(control_only);
  CHECK(open(control_only.v1) != OB_SUCCESS && !cursor && fixture.calls == 0);
  // Minor 3 must receive exactly v3 on both open and next, even from a v4
  // host. Poison the unknown suffix: forwarding it would fail SDK admission
  // (reserved word), or row emission (two projected columns for one column).
  seekdb_plugin_table_execution_context_v4_t newer{};
  newer.v3 = context; newer.v3.v2.v1.struct_size = sizeof(newer);
  const uint8_t requested[] = {1, 0};
  newer.column_count = 2; newer.requested_columns = requested; newer.reserved_word = 1;
  CHECK(open(newer.v3.v2.v1) == OB_SUCCESS && cursor && fixture.calls == 1 && leases() == 2);
  uint32_t rows = 99;
  CHECK(cursor->next(&newer.v3.v2.v1, 2, &rows) == OB_SUCCESS && rows == 2);
  CHECK(cursor->next(&context.v2.v1, 2, &rows) == OB_SUCCESS && rows == 1);
  CHECK(sink.values == (std::vector<int64_t>{1, 2, 3}) && fixture.calls == 4);
  CHECK(cursor->next(&context.v2.v1, 2, &rows) == OB_ITER_END && rows == 0 && fixture.calls == 4);
  CHECK(cursor->rescan(&argument, 1) != OB_SUCCESS); // No query context in raw rescan.
  CHECK(cursor->close() == OB_SUCCESS && leases() == 0); cursor.reset();
  sink.values.clear();
  CHECK(open(context.v2.v1) == OB_SUCCESS && fixture.calls == 5);
  fixture.fail = true;
  CHECK(cursor->next(&context.v2.v1, 2, &rows) == OB_TIMEOUT && rows == 0 && sink.values.empty());
  const int failed_calls = fixture.calls;
  fixture.fail = false;
  CHECK(cursor->next(&context.v2.v1, 2, &rows) == OB_STATE_NOT_MATCH && fixture.calls == failed_calls);
  cursor.reset(); CHECK(leases() == 0);
  fixture.fail = true;
  CHECK(open(context.v2.v1) == OB_TIMEOUT && !cursor && leases() == 0);
  fixture.fail = false;
  argument.is_null = 1; argument.data = nullptr; argument.data_size = 0;
  CHECK(open(context.v2.v1) == OB_SUCCESS);
  const int null_calls = fixture.calls;
  CHECK(cursor->next(&context.v2.v1, 2, &rows) == OB_ITER_END && rows == 0 && fixture.calls == null_calls);
  cursor.reset(); CHECK(leases() == 0);
}

static void exercise_rust_optimizer(ObPluginLoader &loader)
{
  seekdb_plugin_sql_binding_v1_t binding = {};
  CHECK(loader.resolve_sql_extension(SEEKDB_PLUGIN_EXTENSION_FUNCTION,
      "seekdb_rust_optimizer_calls", nullptr, 0, binding) == OB_SUCCESS);
  const auto count = [&]() {
    Sink sink;
    seekdb_plugin_execution_context_v1_t context = {};
    context.struct_size = sizeof(context);
    context.host = reinterpret_cast<seekdb_plugin_host_handle_t *>(&sink);
    context.emit_result = emit;
    CHECK(loader.execute_bound_function(binding, &context, nullptr, 0) == OB_SUCCESS);
    CHECK(sink.calls == 1 && !sink.is_null);
    return sink.value;
  };
  const auto leases = [&]() {
    ObPluginStatusSnapshot status;
    CHECK(loader.get_status("org.seekdb.rust-text", status) == OB_SUCCESS);
    return status.lease_count_;
  };
  struct Leaf {
    ObPluginLoader &loader;
    int calls = 0;
    int error = OB_SUCCESS;
    bool throws = false;
    static int invoke(void *opaque) {
      auto &self = *static_cast<Leaf *>(opaque);
      ++self.calls;
      // Reentrant status lookup proves the loader lock is not held; the object
      // and implementation are both pinned for the full continuation lifetime.
      ObPluginStatusSnapshot status;
      CHECK(self.loader.get_status("org.seekdb.rust-text", status) == OB_SUCCESS);
      CHECK(status.lease_count_ == 2);
      if (self.throws) throw 42;
      return self.error;
    }
  } leaf{loader};
  seekdb_plugin_optimizer_info_v1_t info = {};
  info.struct_size = sizeof(info); info.statement_kind = SEEKDB_PLUGIN_OPTIMIZER_SELECT;
  const int64_t initial = count();
  CHECK(leases() == 0);
  CHECK(loader.run_optimizer_hooks(info, Leaf::invoke, &leaf) == OB_SUCCESS);
  CHECK(leaf.calls == 1 && count() == initial + 1 && leases() == 0);
  leaf.error = OB_ERR_COLUMN_NOT_FOUND; // preserve exact DB error, not generic SPI status
  CHECK(loader.run_optimizer_hooks(info, Leaf::invoke, &leaf) == OB_ERR_COLUMN_NOT_FOUND);
  CHECK(leaf.calls == 2 && count() == initial + 2 && leases() == 0);
  leaf.throws = true;
  CHECK(loader.run_optimizer_hooks(info, Leaf::invoke, &leaf) == OB_ERR_UNEXPECTED);
  CHECK(leaf.calls == 3 && count() == initial + 3 && leases() == 0);
  info.reserved[0] = 1;
  CHECK(loader.run_optimizer_hooks(info, Leaf::invoke, &leaf) == OB_INVALID_ARGUMENT);
  info.reserved[0] = 0; info.statement_kind = SEEKDB_PLUGIN_OPTIMIZER_EXPLAIN + 1;
  CHECK(loader.run_optimizer_hooks(info, Leaf::invoke, &leaf) == OB_INVALID_ARGUMENT);
  info.statement_kind = SEEKDB_PLUGIN_OPTIMIZER_SELECT;
  CHECK(loader.run_optimizer_hooks(info, nullptr, &leaf) == OB_INVALID_ARGUMENT);
  CHECK(leaf.calls == 3 && count() == initial + 3 && leases() == 0);
  struct Nested {
    ObPluginLoader &loader;
    const seekdb_plugin_optimizer_info_v1_t &info;
    int calls = 0;
    static int invoke(void *opaque) {
      auto &self = *static_cast<Nested *>(opaque);
      ++self.calls;
      ObPluginStatusSnapshot status;
      CHECK(self.loader.get_status("org.seekdb.rust-text", status) == OB_SUCCESS);
      CHECK(status.lease_count_ == 2 * self.calls);
      return self.loader.run_optimizer_hooks(self.info, invoke, opaque);
    }
  } nested{loader, info};
  CHECK(loader.run_optimizer_hooks(info, Nested::invoke, &nested) == OB_SIZE_OVERFLOW);
  CHECK(nested.calls == 16 && count() == initial + 19 && leases() == 0);
  // Recursion admission and every enclosing lease unwind on the error; a new
  // top-level planning attempt remains usable on the same thread afterwards.
  leaf.error = OB_SUCCESS; leaf.throws = false;
  CHECK(loader.run_optimizer_hooks(info, Leaf::invoke, &leaf) == OB_SUCCESS);
  CHECK(leaf.calls == 4 && count() == initial + 20 && leases() == 0);
}

static void exercise_rust_types(ObPluginLoader &loader, ObPluginServiceRegistry &registry)
{
  const char *type_id = "org.seekdb.rust-text.utf8";
  const char *branches[] = {type_id, "core.type.bytes", nullptr, type_id};
  std::string common_type = "stale";
  uint64_t common_epoch = 99;
  {
    ObPluginLoader uninitialized;
    CHECK(uninitialized.resolve_common_type(branches, 4, common_type, common_epoch) == OB_NOT_INIT);
    CHECK(common_type.empty() && common_epoch == 0);
  }
  CHECK(loader.resolve_common_type(branches, 4, common_type, common_epoch) == OB_SUCCESS);
  CHECK(common_type == "core.type.bytes" && common_epoch == registry.registry_epoch());
  ObPluginStatusSnapshot selection_status;
  CHECK(loader.get_status("org.seekdb.rust-text", selection_status) == OB_SUCCESS);
  CHECK(selection_status.lease_count_ == 0); // Selection does not retain code.
  const auto original_common_epoch = common_epoch;
  seekdb_plugin_sql_cast_binding_v1_t common_cast = {};
  CHECK(loader.resolve_sql_cast(type_id, common_type.c_str(), SEEKDB_PLUGIN_CAST_IMPLICIT,
      common_cast, common_epoch) == OB_SUCCESS);
  CHECK(common_cast.catalog_epoch == common_epoch && common_cast.declared_context == SEEKDB_PLUGIN_CAST_IMPLICIT);
  CHECK(loader.resolve_sql_cast(type_id, common_type.c_str(), SEEKDB_PLUGIN_CAST_IMPLICIT,
      common_cast, common_epoch + 1) == OB_STATE_NOT_MATCH);
  CHECK(common_cast.struct_size == 0 && common_cast.catalog_epoch == 0);
  CHECK(loader.resolve_common_type(nullptr, 1, common_type, common_epoch) == OB_INVALID_ARGUMENT);
  CHECK(common_type.empty() && common_epoch == 0);
  const char *unknown[] = {nullptr};
  CHECK(loader.resolve_common_type(unknown, 1, common_type, common_epoch) == OB_ENTRY_NOT_EXIST);
  CHECK(common_type.empty() && common_epoch == 0);
  std::vector<ObPluginExtensionInfo> types, casts;
  uint64_t epoch = 0;
  CHECK(registry.find_extensions_by_sql_name(SEEKDB_PLUGIN_EXTENSION_TYPE, "rust_utf8", types, epoch) == OB_SUCCESS);
  CHECK(types.size() == 1 && types[0].spec_.object_id_ == type_id);
  CHECK(types[0].spec_.physical_format_version_ == 1);
  seekdb_plugin_sql_binding_v1_t type_binding = {};
  CHECK(loader.resolve_sql_extension(SEEKDB_PLUGIN_EXTENSION_TYPE, "rust_utf8", nullptr, 0, type_binding) == OB_SUCCESS);
  CHECK(type_binding.owner_generation != 0 && std::strcmp(type_binding.object_id, type_id) == 0);
  CHECK(registry.find_casts("core.type.bytes", type_id, SEEKDB_PLUGIN_CAST_IMPLICIT, casts, epoch) == OB_SUCCESS);
  CHECK(casts.empty());
  CHECK(registry.find_casts("core.type.bytes", type_id, SEEKDB_PLUGIN_CAST_EXPLICIT, casts, epoch) == OB_SUCCESS);
  CHECK(casts.size() == 1 && casts[0].spec_.cost_ == 1);
  seekdb_plugin_sql_cast_binding_v1_t explicit_cast = {}, assignment_cast = {};
  CHECK(loader.resolve_sql_cast("core.type.bytes", type_id, SEEKDB_PLUGIN_CAST_EXPLICIT, explicit_cast) == OB_SUCCESS);
  CHECK(std::strcmp(explicit_cast.object_id, casts[0].spec_.object_id_.c_str()) == 0);
  CHECK(explicit_cast.requested_context == SEEKDB_PLUGIN_CAST_EXPLICIT && explicit_cast.declared_context == SEEKDB_PLUGIN_CAST_EXPLICIT);
  auto rejected_cast = explicit_cast;
  CHECK(loader.resolve_sql_cast("core.type.bytes", type_id, SEEKDB_PLUGIN_CAST_ASSIGNMENT, rejected_cast) == OB_ENTRY_NOT_EXIST);
  CHECK(rejected_cast.struct_size == 0 && rejected_cast.owner_generation == 0);
  CHECK(loader.resolve_sql_cast(type_id, "core.type.bytes", SEEKDB_PLUGIN_CAST_ASSIGNMENT, assignment_cast) == OB_SUCCESS);
  CHECK(assignment_cast.requested_context == SEEKDB_PLUGIN_CAST_ASSIGNMENT && assignment_cast.declared_context == SEEKDB_PLUGIN_CAST_IMPLICIT);
  CHECK(assignment_cast.catalog_epoch == registry.registry_epoch());
  TypedSink sink; sink.loader = &loader;
  seekdb_plugin_execution_context_v2_t context = {};
  context.v1.struct_size = sizeof(context);
  context.v1.host = reinterpret_cast<seekdb_plugin_host_handle_t *>(&sink);
  context.v1.emit_result = emit_typed;
  // Real Rust metadata callback: one untyped identity preserves input logical
  // types; its typed sibling resolves AFTER the existing implicit UTF-8 cast.
  seekdb_plugin_sql_binding_v1_t identity = {};
  for (const char *id : {"core.type.bytes", "core.type.int64", type_id, static_cast<const char *>(nullptr)}) {
    const char *ids[] = {id};
    CHECK(loader.resolve_sql_extension(SEEKDB_PLUGIN_EXTENSION_FUNCTION, "seekdb_rust_identity", ids, 1, identity) == OB_SUCCESS);
    CHECK(std::strcmp(identity.result_type_id, id ? id : "core.type.bytes") == 0);
    CHECK(identity.owner_generation != 0 && identity.catalog_epoch == registry.registry_epoch());
    const uint8_t bytes[] = {1, 2, 3, 4, 5, 6, 7, 8};
    seekdb_plugin_execution_value_v1_t value = {};
    value.struct_size = sizeof(value); value.type_id = id;
    value.data = id ? bytes : nullptr; value.data_size = id ? sizeof(bytes) : 0; value.is_null = !id;
    CHECK(loader.execute_bound_function(identity, &context.v1, &value, 1) == OB_SUCCESS);
    CHECK(sink.type == identity.result_type_id && sink.is_null == !id);
    if (id) CHECK(sink.bytes == std::vector<uint8_t>(bytes, bytes + sizeof(bytes)));
  }
  const char *custom_ids[] = {type_id};
  CHECK(loader.resolve_sql_extension(SEEKDB_PLUGIN_EXTENSION_FUNCTION, "seekdb_rust_identity_bytes", custom_ids, 1, identity) == OB_SUCCESS);
  CHECK(std::strcmp(identity.result_type_id, "core.type.bytes") == 0);
  seekdb_plugin_execution_value_v1_t dynamic_input = {};
  dynamic_input.struct_size = sizeof(dynamic_input); dynamic_input.type_id = type_id;
  const std::string dynamic_text = u8"A中🙂";
  dynamic_input.data = reinterpret_cast<const uint8_t *>(dynamic_text.data()); dynamic_input.data_size = dynamic_text.size();
  CHECK(loader.execute_bound_function(identity, &context.v1, &dynamic_input, 1) == OB_SUCCESS);
  CHECK(sink.type == "core.type.bytes" && sink.bytes == std::vector<uint8_t>(dynamic_text.begin(), dynamic_text.end()));
  CHECK(loader.resolve_sql_extension(SEEKDB_PLUGIN_EXTENSION_FUNCTION, "seekdb_rust_identity", nullptr, 0, identity) != OB_SUCCESS);
  CHECK(identity.struct_size == 0); // Failed resolution does not leave an old binding.
  const char *arguments[] = {"core.type.bytes"};
  seekdb_plugin_sql_binding_v1_t constructor = {}, counter = {};
  CHECK(loader.resolve_sql_extension(SEEKDB_PLUGIN_EXTENSION_FUNCTION, "seekdb_rust_text", arguments, 1, constructor) == OB_SUCCESS);
  const char *typed_arguments[] = {type_id};
  CHECK(loader.resolve_sql_extension(SEEKDB_PLUGIN_EXTENSION_FUNCTION, "seekdb_rust_char_count", typed_arguments, 1, counter) == OB_SUCCESS);
  CHECK(std::strcmp(counter.object_id, "org.seekdb.rust-text.typed-char-count") == 0);
  // No custom-type overload is declared for sql_chars. Rust resolution selects
  // the bytes signature via a direct implicit cast; invocation must convert.
  seekdb_plugin_sql_binding_v1_t sql_counter = {};
  CHECK(loader.resolve_sql_extension(SEEKDB_PLUGIN_EXTENSION_FUNCTION, "seekdb_rust_sql_chars", typed_arguments, 1, sql_counter) == OB_SUCCESS);
  Sink sql_sink;
  SqlFixture sql_fixture;
  seekdb_plugin_sql_api_v1_t sql_api = {sizeof(sql_api), 1, 0, 0, rust_fixture_sql, {0}};
  seekdb_plugin_execution_context_v2_t sql_context = {};
  sql_context.v1.struct_size = sizeof(sql_context);
  sql_context.v1.host = reinterpret_cast<seekdb_plugin_host_handle_t *>(&sql_sink);
  sql_context.v1.emit_result = emit;
  sql_context.sql_api = &sql_api;
  sql_context.sql_context = reinterpret_cast<seekdb_plugin_sql_context_handle_t *>(&sql_fixture);
  const std::string query_text = u8"A中🙂";
  seekdb_plugin_execution_value_v1_t query_input = {};
  query_input.struct_size = sizeof(query_input); query_input.type_id = type_id;
  query_input.data = reinterpret_cast<const uint8_t *>(query_text.data()); query_input.data_size = query_text.size();
  CHECK(loader.execute_bound_function(sql_counter, &sql_context.v1, &query_input, 1) == OB_SUCCESS);
  CHECK(sql_fixture.calls == 1 && sql_sink.calls == 1 && sql_sink.value == 3);
  auto stale_binding = sql_counter; --stale_binding.catalog_epoch;
  CHECK(loader.execute_bound_function(stale_binding, &sql_context.v1, &query_input, 1) == OB_STATE_NOT_MATCH);
  CHECK(sql_fixture.calls == 1 && sql_sink.calls == 1);
  const uint8_t bad_utf8[] = {0xff};
  query_input.data = bad_utf8; query_input.data_size = 1;
  CHECK(loader.execute_bound_function(sql_counter, &sql_context.v1, &query_input, 1) == OB_INVALID_ARGUMENT);
  CHECK(sql_fixture.calls == 1 && sql_sink.calls == 1);
  query_input.data = nullptr; query_input.data_size = 0; query_input.is_null = 1;
  CHECK(loader.execute_bound_function(sql_counter, &sql_context.v1, &query_input, 1) == OB_SUCCESS);
  CHECK(sql_fixture.calls == 2 && sql_sink.calls == 2 && sql_sink.is_null);
  for (const std::string &text : {std::string(u8"A中🙂"), std::string(), std::string("A\0B", 3)}) {
    seekdb_plugin_execution_value_v1_t input = {};
    input.struct_size = sizeof(input); input.type_id = "core.type.bytes";
    input.data = reinterpret_cast<const uint8_t *>(text.data()); input.data_size = text.size();
    CHECK(loader.execute_bound_function(constructor, &context.v1, &input, 1) == OB_SUCCESS);
    CHECK(sink.type == type_id && !sink.is_null);
    CHECK(sink.bytes == std::vector<uint8_t>(text.begin(), text.end()));
    CHECK(loader.execute_cast(casts[0], &context.v1, &input) == OB_SUCCESS);
    CHECK(sink.type == type_id && sink.bytes == std::vector<uint8_t>(text.begin(), text.end()));
    CHECK(loader.execute_bound_cast(explicit_cast, &context.v1, &input) == OB_SUCCESS);
    CHECK(sink.type == type_id && sink.bytes == std::vector<uint8_t>(text.begin(), text.end()));
    CHECK(loader.decode_type(types[0], &context.v1, input.data, input.data_size) == OB_SUCCESS);
    CHECK(loader.decode_bound_type(type_binding, &context.v1, input.data, input.data_size) == OB_SUCCESS);
    CHECK(sink.type == type_id && sink.bytes == std::vector<uint8_t>(text.begin(), text.end()));
    const auto owned = sink.bytes;
    input.type_id = type_id; input.data = owned.data(); input.data_size = owned.size();
    CHECK(loader.execute_bound_cast(assignment_cast, &context.v1, &input) == OB_SUCCESS);
    CHECK(sink.type == "core.type.bytes" && sink.bytes == owned);
    CHECK(loader.encode_type(types[0], &context.v1, &input) == OB_SUCCESS);
    CHECK(sink.type == "core.type.bytes" && sink.bytes == owned);
    CHECK(loader.encode_bound_type(type_binding, &context.v1, &input) == OB_SUCCESS);
    CHECK(sink.type == "core.type.bytes" && sink.bytes == owned);
    Sink count_sink;
    seekdb_plugin_execution_context_v1_t count_context = {};
    count_context.struct_size = sizeof(count_context);
    count_context.host = reinterpret_cast<seekdb_plugin_host_handle_t *>(&count_sink);
    count_context.emit_result = emit;
    CHECK(loader.execute_bound_function(counter, &count_context, &input, 1) == OB_SUCCESS);
    CHECK(count_sink.calls == 1 && count_sink.value == (text.empty() ? 0 : 3));
  }
  const uint8_t invalid[] = {0xff};
  seekdb_plugin_execution_value_v1_t input = {};
  input.struct_size = sizeof(input); input.type_id = "core.type.bytes";
  input.data = invalid; input.data_size = 1;
  int calls = sink.calls;
  CHECK(loader.decode_type(types[0], &context.v1, invalid, 1) == OB_INVALID_ARGUMENT);
  CHECK(loader.execute_cast(casts[0], &context.v1, &input) == OB_INVALID_ARGUMENT);
  CHECK(loader.execute_bound_cast(explicit_cast, &context.v1, &input) == OB_INVALID_ARGUMENT);
  CHECK(loader.execute_bound_function(constructor, &context.v1, &input, 1) == OB_INVALID_ARGUMENT);
  CHECK(sink.calls == calls);
  input.type_id = type_id;
  CHECK(loader.encode_type(types[0], &context.v1, &input) == OB_INVALID_ARGUMENT);
  input.is_null = 1; input.data = nullptr; input.data_size = 0;
  CHECK(loader.encode_type(types[0], &context.v1, &input) == OB_SUCCESS);
  CHECK(sink.is_null && sink.type == "core.type.bytes");
  CHECK(loader.encode_bound_type(type_binding, &context.v1, &input) == OB_SUCCESS);
  CHECK(sink.is_null && sink.type == "core.type.bytes");
  CHECK(loader.execute_cast(casts[0], &context.v1, &input) == OB_SUCCESS);
  CHECK(sink.is_null && sink.type == type_id);
  CHECK(loader.execute_bound_cast(explicit_cast, &context.v1, &input) == OB_SUCCESS);
  CHECK(sink.is_null && sink.type == type_id);
  const auto reject_cast = [&](const seekdb_plugin_sql_cast_binding_v1_t &bad, int error) {
    const int before = sink.calls;
    CHECK(loader.execute_bound_cast(bad, &context.v1, &input) == error);
    CHECK(sink.calls == before);
  };
  auto bad_cast = explicit_cast; bad_cast.struct_size = 0;
  reject_cast(bad_cast, OB_INVALID_ARGUMENT);
  bad_cast = explicit_cast; bad_cast.requested_context = SEEKDB_PLUGIN_CAST_ASSIGNMENT;
  reject_cast(bad_cast, OB_INVALID_ARGUMENT);
  bad_cast = explicit_cast; bad_cast.declared_context = SEEKDB_PLUGIN_CAST_IMPLICIT;
  reject_cast(bad_cast, OB_INVALID_ARGUMENT);
  bad_cast = explicit_cast; bad_cast.owner_generation = 0;
  reject_cast(bad_cast, OB_INVALID_ARGUMENT);
  bad_cast = explicit_cast; ++bad_cast.owner_generation;
  reject_cast(bad_cast, OB_ENTRY_NOT_EXIST);
  bad_cast = explicit_cast; bad_cast.catalog_epoch = 0;
  reject_cast(bad_cast, OB_INVALID_ARGUMENT);
  bad_cast = explicit_cast; ++bad_cast.catalog_epoch;
  reject_cast(bad_cast, OB_STATE_NOT_MATCH);
  bad_cast = explicit_cast; bad_cast.reserved_word = 1;
  reject_cast(bad_cast, OB_INVALID_ARGUMENT);
  bad_cast = explicit_cast; bad_cast.reserved[2] = 1;
  reject_cast(bad_cast, OB_INVALID_ARGUMENT);
  bad_cast = explicit_cast; std::strcpy(bad_cast.source_type_id, "other.source");
  reject_cast(bad_cast, OB_INVALID_ARGUMENT);
  bad_cast = explicit_cast; std::strcpy(bad_cast.target_type_id, "other.target");
  reject_cast(bad_cast, OB_INVALID_ARGUMENT);
  for (int field = 0; field < 4; ++field) {
    bad_cast = explicit_cast;
    char *fields[] = {bad_cast.object_id, bad_cast.owner_plugin_id, bad_cast.source_type_id, bad_cast.target_type_id};
    std::memset(fields[field], 'a', SEEKDB_PLUGIN_MAX_IDENTIFIER_BYTES + 1);
    reject_cast(bad_cast, OB_INVALID_ARGUMENT);
  }
  CHECK(loader.execute_bound_cast(explicit_cast, nullptr, &input) == OB_INVALID_ARGUMENT);
  CHECK(loader.execute_bound_cast(explicit_cast, &context.v1, nullptr) == OB_INVALID_ARGUMENT);
  sink.fail = true;
  CHECK(loader.execute_bound_cast(explicit_cast, &context.v1, &input) == OB_INVALID_ARGUMENT);
  calls = sink.calls;
  CHECK(loader.decode_type(types[0], &context.v1, nullptr, 0) == OB_INVALID_ARGUMENT);
  CHECK(sink.calls == calls + 1);
  sink.fail = false;
  // The SQL bridge consumes only bounded, resolved identities, not registry
  // internals or historical column generations. Rejections must not emit.
  const auto reject_binding = [&](const seekdb_plugin_sql_binding_v1_t &bad, int error) {
    const int before = sink.calls;
    CHECK(loader.decode_bound_type(bad, &context.v1, nullptr, 0) == error);
    CHECK(loader.encode_bound_type(bad, &context.v1, &input) == error);
    CHECK(sink.calls == before);
  };
  auto bad_binding = type_binding; bad_binding.struct_size = 0;
  reject_binding(bad_binding, OB_INVALID_ARGUMENT);
  bad_binding = type_binding; bad_binding.kind = SEEKDB_PLUGIN_EXTENSION_FUNCTION;
  reject_binding(bad_binding, OB_INVALID_ARGUMENT);
  bad_binding = type_binding; bad_binding.owner_generation = 0;
  reject_binding(bad_binding, OB_INVALID_ARGUMENT);
  bad_binding = type_binding; ++bad_binding.owner_generation;
  reject_binding(bad_binding, OB_ENTRY_NOT_EXIST);
  bad_binding = type_binding; bad_binding.physical_format_version = 0;
  reject_binding(bad_binding, OB_INVALID_ARGUMENT);
  bad_binding = type_binding; ++bad_binding.physical_format_version;
  reject_binding(bad_binding, OB_STATE_NOT_MATCH);
  bad_binding = type_binding; std::strcpy(bad_binding.physical_format_id, "other.format");
  reject_binding(bad_binding, OB_STATE_NOT_MATCH);
  bad_binding = type_binding; bad_binding.flags ^= SEEKDB_PLUGIN_EXTENSION_FLAG_PERSISTENT;
  reject_binding(bad_binding, OB_STATE_NOT_MATCH);
  bad_binding = type_binding; bad_binding.reserved[3] = 1;
  reject_binding(bad_binding, OB_INVALID_ARGUMENT);
  bad_binding = type_binding; std::strcpy(bad_binding.object_id, "other.type");
  reject_binding(bad_binding, OB_ENTRY_NOT_EXIST);
  bad_binding = type_binding; std::strcpy(bad_binding.owner_plugin_id, "other.owner");
  reject_binding(bad_binding, OB_ENTRY_NOT_EXIST);
  for (int field = 0; field < 4; ++field) {
    bad_binding = type_binding;
    char *fields[] = {bad_binding.sql_name, bad_binding.object_id,
                      bad_binding.owner_plugin_id, bad_binding.physical_format_id};
    std::memset(fields[field], 'a', SEEKDB_PLUGIN_MAX_IDENTIFIER_BYTES + 1);
    reject_binding(bad_binding, OB_INVALID_ARGUMENT);
  }
  calls = sink.calls;
  CHECK(loader.decode_bound_type(type_binding, nullptr, nullptr, 0) == OB_INVALID_ARGUMENT);
  CHECK(loader.encode_bound_type(type_binding, &context.v1, nullptr) == OB_INVALID_ARGUMENT);
  CHECK(loader.decode_bound_type(type_binding, &context.v1, nullptr, 1) == OB_INVALID_ARGUMENT);
  CHECK(loader.decode_bound_type(type_binding, &context.v1, invalid, UINT64_C(16777217)) == OB_INVALID_ARGUMENT);
  CHECK(loader.decode_bound_type(type_binding, &context.v1, invalid, 1) == OB_INVALID_ARGUMENT);
  CHECK(sink.calls == calls);
  // An unrelated epoch is not part of the physical format identity. Exact
  // object/generation/format checks still apply; no generation wildcard.
  bad_binding = type_binding; ++bad_binding.catalog_epoch;
  CHECK(loader.decode_bound_type(bad_binding, &context.v1, nullptr, 0) == OB_SUCCESS);
  sink.fail = true;
  CHECK(loader.decode_bound_type(type_binding, &context.v1, nullptr, 0) == OB_INVALID_ARGUMENT);
  CHECK(loader.encode_bound_type(type_binding, &context.v1, &input) == OB_INVALID_ARGUMENT);
  sink.fail = false;
  auto stale = types[0]; ++stale.owner_generation_;
  CHECK(loader.decode_type(stale, &context.v1, nullptr, 0) == OB_ENTRY_NOT_EXIST);
  auto bad_format = types[0]; ++bad_format.spec_.physical_format_version_;
  CHECK(loader.decode_type(bad_format, &context.v1, nullptr, 0) == OB_INVALID_ARGUMENT);
  CHECK(loader.decode_type(casts[0], &context.v1, nullptr, 0) == OB_INVALID_ARGUMENT);
  CHECK(loader.decode_type(types[0], &context.v1, nullptr, 1) == OB_INVALID_ARGUMENT);
  CHECK(loader.decode_type(types[0], &context.v1, invalid, UINT64_C(16777217)) == OB_INVALID_ARGUMENT);
  auto changed_cast = casts[0]; changed_cast.spec_.cast_context_ = SEEKDB_PLUGIN_CAST_IMPLICIT;
  CHECK(loader.execute_cast(changed_cast, &context.v1, &input) == OB_INVALID_ARGUMENT);
  // A real snapshot change invalidates compiled selection even if the chosen
  // module itself has not changed. Rebinding is explicit, never done per row.
  static const int marker_service = 0;
  auto marker = std::make_shared<ObPluginGeneration>("test.cast.epoch", 1);
  CHECK(marker->transition_to(ObPluginState::VALIDATED) == OB_SUCCESS);
  CHECK(marker->transition_to(ObPluginState::LOADED) == OB_SUCCESS);
  CHECK(marker->transition_to(ObPluginState::INITIALIZING) == OB_SUCCESS);
  ObPluginRegistration marker_registration;
  CHECK(registry.begin_registration(marker, marker_registration) == OB_SUCCESS);
  CHECK(marker_registration.add_service("test.cast.epoch", 1, 0, &marker_service) == OB_SUCCESS);
  CHECK(marker_registration.commit() == OB_SUCCESS);
  CHECK(loader.resolve_sql_cast(type_id, "core.type.bytes", SEEKDB_PLUGIN_CAST_IMPLICIT,
      common_cast, original_common_epoch) == OB_STATE_NOT_MATCH);
  CHECK(common_cast.struct_size == 0 && common_cast.catalog_epoch == 0);
  CHECK(loader.resolve_common_type(branches, 4, common_type, common_epoch) == OB_SUCCESS);
  CHECK(common_type == "core.type.bytes" && common_epoch != original_common_epoch);
  CHECK(loader.resolve_sql_cast(type_id, common_type.c_str(), SEEKDB_PLUGIN_CAST_IMPLICIT,
      common_cast, common_epoch) == OB_SUCCESS);
  CHECK(common_cast.catalog_epoch == common_epoch);
  reject_cast(explicit_cast, OB_STATE_NOT_MATCH);
  seekdb_plugin_sql_cast_binding_v1_t rebound = {};
  CHECK(loader.resolve_sql_cast("core.type.bytes", type_id, SEEKDB_PLUGIN_CAST_EXPLICIT, rebound) == OB_SUCCESS);
  CHECK(rebound.owner_generation == explicit_cast.owner_generation && rebound.catalog_epoch != explicit_cast.catalog_epoch);
  CHECK(loader.execute_bound_cast(rebound, &context.v1, &input) == OB_SUCCESS);
  CHECK(sink.is_null && sink.type == type_id);
  CHECK(registry.quiesce(marker) == OB_SUCCESS);
  CHECK(registry.mark_stopped(marker) == OB_SUCCESS);
  // No borrowed code survives this call. Quiesce then rejects saved identities.
  CHECK(loader.shutdown_for_process_exit(1000000) == OB_SUCCESS);
  CHECK(loader.resolve_sql_extension(SEEKDB_PLUGIN_EXTENSION_FUNCTION, "seekdb_rust_identity", custom_ids, 1, identity) != OB_SUCCESS);
  CHECK(loader.decode_type(types[0], &context.v1, nullptr, 0) != OB_SUCCESS);
  CHECK(loader.decode_bound_type(type_binding, &context.v1, nullptr, 0) != OB_SUCCESS);
  CHECK(loader.encode_bound_type(type_binding, &context.v1, &input) != OB_SUCCESS);
  CHECK(loader.execute_cast(casts[0], &context.v1, &input) != OB_SUCCESS);
  CHECK(loader.execute_bound_cast(explicit_cast, &context.v1, &input) != OB_SUCCESS);
}

static void exercise_scaffold(ObPluginLoader &loader)
{
  const char *types[] = {"core.type.bytes"};
  seekdb_plugin_sql_binding_v1_t binding = {};
  CHECK(loader.resolve_sql_extension(SEEKDB_PLUGIN_EXTENSION_FUNCTION,
      "generated_chars", types, 1, binding) == OB_SUCCESS);
  CHECK(std::strcmp(binding.owner_plugin_id, "org.seekdb.generated") == 0);
  Sink sink;
  seekdb_plugin_execution_context_v1_t context = {};
  context.struct_size = sizeof(context); context.emit_result = emit;
  context.host = reinterpret_cast<seekdb_plugin_host_handle_t *>(&sink);
  seekdb_plugin_execution_value_v1_t input = {};
  input.struct_size = sizeof(input); input.type_id = types[0];
  for (const char *text : {u8"A中🙂", ""}) {
    input.data = reinterpret_cast<const uint8_t *>(text); input.data_size = std::strlen(text);
    CHECK(loader.execute_bound_function(binding, &context, &input, 1) == OB_SUCCESS);
    CHECK(sink.value == (input.data_size ? 3 : 0));
  }
  input.is_null = 1; input.data = nullptr; input.data_size = 0;
  CHECK(loader.execute_bound_function(binding, &context, &input, 1) == OB_SUCCESS);
  CHECK(sink.is_null && sink.calls == 3);
  input.is_null = 0;
  const uint8_t bad = 0xff; input.data = &bad; input.data_size = 1;
  CHECK(loader.execute_bound_function(binding, &context, &input, 1) == OB_INVALID_ARGUMENT);
  CHECK(sink.calls == 3);
  ObPluginStatusSnapshot status;
  CHECK(loader.get_status("org.seekdb.generated", status) == OB_SUCCESS && status.lease_count_ == 0);
  CHECK(loader.shutdown_for_process_exit(1000000) == OB_SUCCESS);
  CHECK(loader.execute_bound_function(binding, &context, &input, 1) != OB_SUCCESS);
}

static void exercise_rust_type_comparison(ObPluginLoader &loader)
{
  seekdb_plugin_sql_binding_v1_t binding{};
  CHECK(loader.resolve_sql_extension(SEEKDB_PLUGIN_EXTENSION_TYPE, "rust_utf8", nullptr, 0, binding) == OB_SUCCESS);
  seekdb_plugin_sql_binding_v1_t by_id{};
  CHECK(loader.resolve_type_by_id(binding.object_id, by_id, binding.catalog_epoch) == OB_SUCCESS);
  CHECK(by_id.kind == binding.kind && by_id.owner_generation == binding.owner_generation &&
      by_id.catalog_epoch == binding.catalog_epoch && by_id.flags == binding.flags &&
      by_id.physical_format_version == binding.physical_format_version &&
      std::strcmp(by_id.sql_name, binding.sql_name) == 0 &&
      std::strcmp(by_id.object_id, binding.object_id) == 0 &&
      std::strcmp(by_id.owner_plugin_id, binding.owner_plugin_id) == 0 &&
      std::strcmp(by_id.physical_format_id, binding.physical_format_id) == 0);
  CHECK(loader.check_bound_type_comparison(by_id) == OB_SUCCESS);
  for (int scenario = 0; scenario < 6; ++scenario) {
    by_id = binding;
    const char *id = binding.object_id;
    uint64_t epoch = binding.catalog_epoch;
    if (scenario == 0) id = nullptr;
    if (scenario == 1) id = "INVALID";
    if (scenario == 2) id = "rust_utf8"; // SQL name is not a logical identity.
    if (scenario == 3) id = "missing.type";
    if (scenario == 4) ++epoch;
    ObPluginLoader uninitialized;
    const int status = scenario == 5 ? uninitialized.resolve_type_by_id(id, by_id, epoch)
        : loader.resolve_type_by_id(id, by_id, epoch);
    CHECK(status == (scenario < 2 ? OB_INVALID_ARGUMENT : scenario < 4 ? OB_ENTRY_NOT_EXIST
        : scenario == 4 ? OB_STATE_NOT_MATCH : OB_NOT_INIT));
    const seekdb_plugin_sql_binding_v1_t empty{};
    CHECK(std::memcmp(&by_id, &empty, sizeof(empty)) == 0);
  }
  CHECK(loader.check_bound_type_comparison(binding) == OB_SUCCESS);
  const auto value = [&](const std::string &bytes) {
    seekdb_plugin_execution_value_v1_t result{};
    result.struct_size = sizeof(result); result.type_id = binding.object_id;
    result.data = reinterpret_cast<const uint8_t *>(bytes.data()); result.data_size = bytes.size();
    return result;
  };
  // Length-first Rust ordering intentionally disagrees with byte ordering.
  // Exercise both signs, equality, Unicode, empty data and embedded NUL.
  const std::string cases[] = {"", "a", "z", u8"中", "aa", std::string("a\0b", 3)};
  for (size_t i = 0; i < 6; ++i) for (size_t j = 0; j < 6; ++j) {
    int32_t ordering = 99;
    CHECK(loader.compare_bound_type(binding, value(cases[i]), value(cases[j]), ordering) == OB_SUCCESS);
    CHECK(ordering == (i < j ? -1 : i == j ? 0 : 1));
  }
  const std::string good = "a", invalid(1, char(0xff));
  int32_t ordering = 99;
  CHECK(loader.compare_bound_type(binding, value(invalid), value(good), ordering) == OB_INVALID_ARGUMENT && ordering == 0);
  for (int scenario = 0; scenario < 10; ++scenario) {
    auto left = value(good), right = value(good);
    auto bad_binding = binding;
    if (scenario == 0) left.is_null = 1;
    if (scenario == 1) left.struct_size = 0;
    if (scenario == 2) left.reserved[0] = 1;
    if (scenario == 3) left.reserved_bytes[6] = 1;
    if (scenario == 4) left.type_id = "core.type.bytes";
    if (scenario == 5) left.data_size = UINT64_C(16777217); // Reject before reading the one-byte span.
    if (scenario == 6) left.data = nullptr;
    if (scenario == 7) bad_binding.owner_generation = 0;
    if (scenario == 8) ++bad_binding.catalog_epoch;
    if (scenario == 9) ++bad_binding.physical_format_version;
    ordering = 99;
    CHECK(loader.compare_bound_type(bad_binding, left, right, ordering) != OB_SUCCESS && ordering == 0);
  }
  ObPluginStatusSnapshot status;
  CHECK(loader.get_status("org.seekdb.rust-text", status) == OB_SUCCESS && status.lease_count_ == 0);
}

int main(int argc, char **argv)
{
  if (argc == 4 && std::strcmp(argv[3], "rust-memory") == 0) {
    memory_account_test::run_rust(argv[1], argv[2]);
    return 0;
  }
  if (argc == 4 && std::strcmp(argv[3], "memory") == 0) {
    memory_account_test::run(argv[1], argv[2]);
    return 0;
  }
  if (argc == 4 && std::strncmp(argv[3], "custom-", 7) == 0) {
    const int variant = std::atoi(argv[3] + 7);
    CHECK(variant >= 0 && variant < 47);
    Observation observation;
    observation.expected_services = 1; observation.expected_extensions = 0;
    auto guard = std::make_shared<TestGuard>(observation);
    ObPluginLoader loader;
    CHECK(loader.init(argv[1], std::make_shared<TestVerifier>(false, false, false), guard, guard, observation.registry) == OB_SUCCESS);
    CHECK(loader.load(argv[2]) == OB_SUCCESS);
    custom_executor_test::run(loader, false, variant);
    CHECK(loader.shutdown_for_process_exit(1000000) == OB_SUCCESS);
    CHECK(observation.registry->service_count() == 0 && observation.registry->extension_count() == 0);
    return 0;
  }
  if (argc == 4 && std::strncmp(argv[3], "candidate-", 10) == 0) {
    const bool native_rust = std::strcmp(argv[3], "candidate-native") == 0 ||
                             std::strcmp(argv[3], "candidate-native-reject") == 0;
    const int variant = native_rust ? 9 : std::atoi(argv[3] + 10);
    CHECK(variant >= 0 && variant <= 19);
    Observation observation;
    observation.expected_services = observation.expected_extensions = 1;
    if (native_rust) { observation.expected_services = 5; observation.expected_extensions = 7; }
    auto guard = std::make_shared<TestGuard>(observation);
    ObPluginLoader loader;
    CHECK(loader.init(argv[1], std::make_shared<TestVerifier>(false, false, false, false, native_rust), guard, guard,
                      observation.registry) == OB_SUCCESS);
    const int loaded = loader.load(argv[2]);
    if (std::strcmp(argv[3], "candidate-native-reject") == 0) {
      CHECK(loaded == OB_NOT_SUPPORTED && observation.aborted && !observation.committed);
      CHECK(loader.last_error().find("server-dev") != std::string::npos);
      CHECK(observation.registry->extension_count() == 0 && observation.registry->service_count() == 0);
      return 0;
    }
    if (loaded != OB_SUCCESS) std::cerr << loader.last_error() << std::endl;
    CHECK(loaded == OB_SUCCESS && observation.committed && observation.completed);
    struct State {
      int selected = -1, nexts = 0, validations = 0;
      int core_status = OB_SUCCESS;
      uint32_t count = 2;
      int build_error = OB_SUCCESS;
      bool fail_build = false;
      bool custom_build = false;
      bool relation_paths = false;
      bool local_serial = true;
      int graph_reads = 0;
      int query_reads = 0;
      int semantics_reads = 0;
      int sort_reads = 0, sort_key_reads = 0;
      int sort_fault = 0;
      bool non_sort = false;
      static seekdb_plugin_status_t sort_info(void *opaque, uint32_t, seekdb_plugin_sort_info_v1_t *out) {
        auto &self = *static_cast<State *>(opaque);
        CHECK(out && out->struct_size == sizeof(*out)); ++self.sort_reads;
        *out = {}; out->struct_size = sizeof(*out);
        out->topn_expression = out->topk_limit_expression = out->topk_offset_expression = out->hash_expression = UINT32_MAX;
        if (!self.non_sort) { out->flags = SEEKDB_PLUGIN_SORT_PRESENT; out->key_count = 4; out->topn_expression = 50; }
        if (self.sort_fault == 1) out->flags |= 32;
        if (self.sort_fault == 2) { self.build_error = OB_TIMEOUT; return SEEKDB_PLUGIN_STATUS_INTERNAL; }
        return SEEKDB_PLUGIN_STATUS_OK;
      }
      static seekdb_plugin_status_t sort_key(void *opaque, uint32_t, uint32_t ordinal, uint32_t *expr, uint32_t *flags) {
        auto &self = *static_cast<State *>(opaque);
        CHECK(ordinal < 4 && expr && flags); ++self.sort_key_reads;
        *expr = 50; *flags = ordinal;
        return SEEKDB_PLUGIN_STATUS_OK;
      }
      static seekdb_plugin_status_t binding_count(void *, uint32_t, uint32_t, uint32_t *)
      { CHECK(false); return SEEKDB_PLUGIN_STATUS_INTERNAL; }
      static seekdb_plugin_status_t binding(void *, uint32_t, uint32_t, uint32_t, uint32_t *, uint32_t *)
      { CHECK(false); return SEEKDB_PLUGIN_STATUS_INTERNAL; }
      static seekdb_plugin_status_t plan_semantics(void *opaque, uint32_t, seekdb_plugin_plan_semantics_v1_t *out) {
        CHECK(out && out->struct_size == sizeof(*out));
        ++static_cast<State *>(opaque)->semantics_reads;
        *out = {sizeof(*out), SEEKDB_PLUGIN_RELATION_OTHER,
            static_cast<State *>(opaque)->local_serial ? SEEKDB_PLUGIN_PLAN_LOCAL_SERIAL : 0u, 0, {0}};
        return SEEKDB_PLUGIN_STATUS_OK;
      }
      static seekdb_plugin_status_t expression_semantics(void *, uint32_t, seekdb_plugin_expr_semantics_v1_t *)
      { CHECK(false); return SEEKDB_PLUGIN_STATUS_INTERNAL; }
      static seekdb_plugin_status_t scope(void *, uint32_t, uint32_t, uint32_t *)
      { CHECK(false); return SEEKDB_PLUGIN_STATUS_INTERNAL; }
      static seekdb_plugin_status_t column_count(void *, uint32_t *)
      { CHECK(false); return SEEKDB_PLUGIN_STATUS_INTERNAL; }
      static seekdb_plugin_status_t column(void *, uint32_t, uint32_t *)
      { CHECK(false); return SEEKDB_PLUGIN_STATUS_INTERNAL; }
      static seekdb_plugin_status_t query(void *opaque, seekdb_plugin_query_info_v1_t *out) {
        CHECK(out && out->struct_size == sizeof(*out));
        ++static_cast<State *>(opaque)->query_reads;
        *out = {sizeof(*out), 1, SEEKDB_PLUGIN_QUERY_SELECT_LIST, 2, {0}};
        return SEEKDB_PLUGIN_STATUS_OK;
      }
      static seekdb_plugin_status_t target(void *opaque, uint32_t index, uint32_t *out) {
        CHECK(index < 2 && out); ++static_cast<State *>(opaque)->query_reads;
        *out = 50; return SEEKDB_PLUGIN_STATUS_OK;
      }
      static seekdb_plugin_status_t root(void *opaque, uint32_t index, uint32_t *out) {
        auto &self = *static_cast<State *>(opaque);
        CHECK(index < self.count && out); ++self.graph_reads; *out = index + 10;
        return SEEKDB_PLUGIN_STATUS_OK;
      }
      static seekdb_plugin_status_t plan(void *opaque, uint32_t id, seekdb_plugin_plan_info_v1_t *out) {
        auto &self = *static_cast<State *>(opaque);
        CHECK(id >= 10 && id < self.count + 10 && out && out->struct_size == sizeof(*out));
        ++self.graph_reads; *out = {}; out->struct_size = sizeof(*out);
        out->operator_type = 1; out->join_type = UINT32_MAX;
        out->child_count = id >= 12 ? 1 : 0; out->cost = 2; out->rows = 8; out->width = 16;
        out->expression_counts[SEEKDB_PLUGIN_PLAN_ORDERING - 1] = 1;
        return SEEKDB_PLUGIN_STATUS_OK;
      }
      static seekdb_plugin_status_t child(void *opaque, uint32_t id, uint32_t index, uint32_t *out) {
        CHECK(id == 12 && index == 0 && out); ++static_cast<State *>(opaque)->graph_reads;
        *out = 10; return SEEKDB_PLUGIN_STATUS_OK;
      }
      static seekdb_plugin_status_t expression(void *opaque, uint32_t id, uint32_t role, uint32_t index, uint32_t *out, uint32_t *ordering) {
        CHECK(id == 10 && role == SEEKDB_PLUGIN_PLAN_ORDERING && index == 0 && out && ordering);
        ++static_cast<State *>(opaque)->graph_reads; *out = 50; *ordering = 0;
        return SEEKDB_PLUGIN_STATUS_OK;
      }
      static seekdb_plugin_status_t describe_expression(void *opaque, uint32_t id, seekdb_plugin_expr_info_v1_t *out) {
        CHECK(id == 50 && out && out->struct_size == sizeof(*out));
        ++static_cast<State *>(opaque)->graph_reads; *out = {}; out->struct_size = sizeof(*out);
        out->sql_type = 5; out->precision = 19; out->scale = 0; out->collation = 63;
        return SEEKDB_PLUGIN_STATUS_OK;
      }
      static seekdb_plugin_status_t argument(void *, uint32_t, uint32_t, uint32_t *)
      { CHECK(false); return SEEKDB_PLUGIN_STATUS_INTERNAL; }
      static uint32_t current_count(void *opaque) { return static_cast<State *>(opaque)->count; }
      static int32_t get_error(void *opaque) { return static_cast<State *>(opaque)->build_error; }
      static seekdb_plugin_status_t build(void *opaque, const seekdb_plugin_path_request_v1_t *request, uint32_t *out) {
        auto &self = *static_cast<State *>(opaque);
        CHECK(request && request->kind == (self.custom_build ? SEEKDB_PLUGIN_PATH_CUSTOM : SEEKDB_PLUGIN_PATH_MATERIALIZE) && request->input_index < self.count && out);
        if (self.custom_build) {
          CHECK(request->struct_size == sizeof(seekdb_plugin_custom_path_request_v1_t));
          const auto &custom = *reinterpret_cast<const seekdb_plugin_custom_path_request_v1_t *>(request);
          CHECK(std::strcmp(custom.service_id, "org.seekdb.rust-candidate.spool") == 0 && custom.flags == 3 && custom.operator_cost > 0);
        }
        *out = UINT32_MAX;
        if (self.fail_build) { self.build_error = OB_TIMEOUT; return SEEKDB_PLUGIN_STATUS_INTERNAL; }
        *out = self.count++;
        return SEEKDB_PLUGIN_STATUS_OK;
      }
      static seekdb_plugin_status_t get(void *opaque, uint32_t index, seekdb_plugin_candidate_info_v1_t *out) {
        CHECK(index < static_cast<State *>(opaque)->count && out && out->struct_size == sizeof(*out));
        *out = {sizeof(*out), 1, index == 0 ? 2.0 : 40.0, 8.0, 16.0, {0}};
        return SEEKDB_PLUGIN_STATUS_OK;
      }
      static seekdb_plugin_status_t select(void *opaque, uint32_t index) {
        CHECK(index < static_cast<State *>(opaque)->count);
        if (static_cast<State *>(opaque)->relation_paths) {
          static_cast<State *>(opaque)->build_error = OB_INVALID_ARGUMENT;
          return SEEKDB_PLUGIN_STATUS_INVALID_ARGUMENT;
        }
        static_cast<State *>(opaque)->selected = index;
        return SEEKDB_PLUGIN_STATUS_OK;
      }
      static int next(void *opaque) {
        auto &self = *static_cast<State *>(opaque);
        ++self.nexts;
        if (!self.relation_paths) self.selected = 0;
        return self.core_status;
      }
      static int validate(void *opaque) {
        auto &self = *static_cast<State *>(opaque);
        ++self.validations;
        if (self.relation_paths) return self.build_error;
        return self.selected >= 0 ? OB_SUCCESS : OB_STATE_NOT_MATCH;
      }
    } state;
    state.core_status = variant == 7 ? OB_TIMEOUT : OB_SUCCESS;
    state.fail_build = variant == 12 || variant == 18;
    state.relation_paths = variant >= 13;
    state.custom_build = native_rust;
    seekdb_plugin_candidate_context_v1_t view = {sizeof(view), 2, &state, State::get,
        State::select, nullptr, nullptr, {0}};
    seekdb_plugin_candidate_context_v2_t extended{view, State::current_count, State::build, State::get_error, {0}};
    extended.v1.struct_size = sizeof(extended);
    seekdb_plugin_candidate_context_v3_t graph{extended, State::root, State::plan, State::child,
        State::expression, State::describe_expression, State::argument, {0}};
    graph.v2.v1.struct_size = sizeof(graph);
    seekdb_plugin_candidate_context_v4_t query{graph, State::query, State::target, {0}};
    query.v3.v2.v1.struct_size = sizeof(query);
    seekdb_plugin_candidate_context_v5_t semantics{query, State::plan_semantics,
        State::expression_semantics, State::scope, State::column_count, State::column, {0}};
    semantics.v4.v3.v2.v1.struct_size = sizeof(semantics);
    seekdb_plugin_candidate_context_v6_t bindings{semantics, State::binding_count, State::binding, {0}};
    bindings.v5.v4.v3.v2.v1.struct_size = sizeof(bindings);
    seekdb_plugin_candidate_context_v7_t sorts{bindings, State::sort_info, State::sort_key, {0}};
    sorts.v6.v5.v4.v3.v2.v1.struct_size = sizeof(sorts);
    seekdb_plugin_candidate_context_v8_t values{sorts,
      [](void *, uint32_t, uint32_t, seekdb_plugin_value_info_v1_t *) -> seekdb_plugin_status_t {
        CHECK(false); return SEEKDB_PLUGIN_STATUS_INTERNAL; // Top-N declines before value inspection.
      }, {0}};
    values.v7.v6.v5.v4.v3.v2.v1.struct_size = sizeof(values);
    if (native_rust) {
      for (auto phase : {SEEKDB_PLUGIN_PHASE_GROUP, SEEKDB_PLUGIN_PHASE_WINDOW,
          SEEKDB_PLUGIN_PHASE_DISTINCT, SEEKDB_PLUGIN_PHASE_ORDERED}) {
        bool available = false;
        CHECK(loader.candidate_hooks_available(phase, available) == OB_SUCCESS && available);
        State upper; upper.relation_paths = true; upper.custom_build = true;
        auto upper_view = values; auto &upper_base = upper_view.v7.v6.v5.v4.v3.v2.v1;
        upper_base.host_context = &upper;
        CHECK(loader.run_candidate_hooks(upper_base, State::next, &upper,
            State::validate, phase) == OB_SUCCESS);
        CHECK(upper.count == 3 && upper.selected == -1 && upper.nexts == 1 && upper.validations == 1);
        CHECK(upper.sort_reads == 1 && upper.sort_key_reads == 4);
        upper = State{}; upper.relation_paths = true; upper.custom_build = true; upper.fail_build = true;
        CHECK(loader.run_candidate_hooks(upper_base, State::next, &upper,
            State::validate, phase) == OB_TIMEOUT);
        CHECK(upper.count == 2 && upper.nexts == 0 && upper.validations == 0);
        upper = State{}; upper.relation_paths = true; upper.local_serial = false;
        CHECK(loader.run_candidate_hooks(upper_base, State::next, &upper,
            State::validate, phase) == OB_SUCCESS);
        CHECK(upper.count == 2 && upper.nexts == 1 && upper.validations == 1 && upper.semantics_reads == 2);
        CHECK(upper.sort_reads == 0);
        upper = State{}; upper.relation_paths = true; upper.custom_build = true; upper.non_sort = true;
        CHECK(loader.run_candidate_hooks(upper_base, State::next, &upper, State::validate, phase) == OB_SUCCESS);
        CHECK(upper.count == 3 && upper.sort_reads == 1 && upper.sort_key_reads == 0);
        for (int fault = 1; fault <= 2; ++fault) {
          upper = State{}; upper.relation_paths = true; upper.sort_fault = fault;
          CHECK(loader.run_candidate_hooks(upper_base, State::next, &upper, State::validate, phase) ==
              (fault == 1 ? OB_INVALID_ARGUMENT : OB_TIMEOUT));
          CHECK(upper.count == 2 && upper.nexts == 0 && upper.validations == 0);
        }
        CHECK(loader.run_candidate_hooks(bindings.v5.v4.v3.v2.v1, State::next, &state,
            State::validate, phase) == OB_NOT_SUPPORTED);
        CHECK(loader.run_candidate_hooks(sorts.v6.v5.v4.v3.v2.v1, State::next, &state,
            State::validate, phase) == OB_NOT_SUPPORTED);
        for (int fault = 0; fault < 3; ++fault) {
          auto broken = values;
          if (fault == 0) broken.value_info = nullptr;
          if (fault == 1) broken.reserved[0] = 1;
          if (fault == 2) --broken.v7.v6.v5.v4.v3.v2.v1.struct_size;
          CHECK(loader.run_candidate_hooks(broken.v7.v6.v5.v4.v3.v2.v1, State::next, &state,
              State::validate, phase) == OB_INVALID_ARGUMENT);
        }
        for (int fault = 0; fault < 4; ++fault) {
          auto broken = sorts;
          if (fault == 0) broken.sort_info = nullptr;
          if (fault == 1) broken.sort_key = nullptr;
          if (fault == 2) broken.reserved[0] = 1;
          if (fault == 3) --broken.v6.v5.v4.v3.v2.v1.struct_size;
          CHECK(loader.run_candidate_hooks(broken.v6.v5.v4.v3.v2.v1, State::next, &state,
              State::validate, phase) == OB_INVALID_ARGUMENT);
        }
      }
      bool available = true;
      CHECK(loader.candidate_hooks_available(static_cast<seekdb_plugin_candidate_phase_t>(77), available) ==
          OB_INVALID_ARGUMENT && !available);
      CHECK(loader.run_candidate_hooks(bindings.v5.v4.v3.v2.v1, State::next, &state,
          State::validate, static_cast<seekdb_plugin_candidate_phase_t>(77)) == OB_INVALID_ARGUMENT);
      CHECK(state.count == 2 && state.nexts == 0 && state.validations == 0);
    }
    {
      // The two points are independent, including when the registered service
      // is inadmissible for its own point. An absent point only calls its leaf.
      State absent;
      absent.relation_paths = !state.relation_paths;
      auto other = bindings;
      other.v5.v4.v3.v2.v1.host_context = &absent;
      CHECK(loader.run_candidate_hooks(other.v5.v4.v3.v2.v1, State::next, &absent,
          State::validate, absent.relation_paths ? SEEKDB_PLUGIN_PHASE_RELATION : SEEKDB_PLUGIN_PHASE_SELECT) == OB_SUCCESS);
      CHECK(absent.nexts == 1 && absent.validations == 1 && absent.count == 2);
      CHECK(absent.selected == (absent.relation_paths ? -1 : 0));
      CHECK(absent.semantics_reads == (native_rust ? 2 : 0)); // Rust skips non-join plans.
    }
    if (native_rust) {
      CHECK(loader.run_candidate_hooks(semantics.v4.v3.v2.v1, State::next, &state, State::validate, SEEKDB_PLUGIN_PHASE_RELATION) == OB_NOT_SUPPORTED);
      for (int fault = 0; fault < 4; ++fault) {
        auto broken = bindings;
        if (fault == 0) broken.binding_count = nullptr;
        if (fault == 1) broken.binding = nullptr;
        if (fault == 2) broken.reserved[0] = 1;
        if (fault == 3) broken.v5.v4.v3.v2.v1.struct_size -= 1;
        CHECK(loader.run_candidate_hooks(broken.v5.v4.v3.v2.v1, State::next, &state, State::validate, SEEKDB_PLUGIN_PHASE_RELATION) == OB_INVALID_ARGUMENT);
      }
      CHECK(loader.run_candidate_hooks(query.v3.v2.v1, State::next, &state, State::validate, SEEKDB_PLUGIN_PHASE_RELATION) == OB_NOT_SUPPORTED);
      for (int fault = 0; fault < 7; ++fault) {
        auto broken = semantics;
        if (fault == 0) broken.plan_semantics = nullptr;
        if (fault == 1) broken.expression_semantics = nullptr;
        if (fault == 2) broken.scope = nullptr;
        if (fault == 3) broken.column_count = nullptr;
        if (fault == 4) broken.column = nullptr;
        if (fault == 5) broken.reserved[0] = 1;
        if (fault == 6) broken.v4.v3.v2.v1.struct_size -= 1;
        CHECK(loader.run_candidate_hooks(broken.v4.v3.v2.v1, State::next, &state, State::validate, SEEKDB_PLUGIN_PHASE_RELATION) == OB_INVALID_ARGUMENT);
      }
      CHECK(state.semantics_reads == 0);
      CHECK(loader.run_candidate_hooks(view, State::next, &state, State::validate) == OB_NOT_SUPPORTED);
      CHECK(loader.run_candidate_hooks(extended.v1, State::next, &state, State::validate) == OB_NOT_SUPPORTED);
      CHECK(loader.run_candidate_hooks(graph.v2.v1, State::next, &state, State::validate) == OB_NOT_SUPPORTED);
      for (int fault = 0; fault < 8; ++fault) {
        auto broken = graph;
        if (fault == 0) broken.v2.v1.struct_size -= 1;
        if (fault == 1) broken.root = nullptr;
        if (fault == 2) broken.plan = nullptr;
        if (fault == 3) broken.child = nullptr;
        if (fault == 4) broken.expression = nullptr;
        if (fault == 5) broken.describe_expression = nullptr;
        if (fault == 6) broken.argument = nullptr;
        if (fault == 7) broken.reserved[2] = 1;
        CHECK(loader.run_candidate_hooks(broken.v2.v1, State::next, &state, State::validate) == OB_INVALID_ARGUMENT);
      }
      CHECK(state.graph_reads == 0 && state.nexts == 0 && state.selected == -1 && state.count == 2);
      for (int fault = 0; fault < 5; ++fault) {
        auto broken = query;
        if (fault == 0) broken.v3.v2.v1.struct_size -= 1;
        if (fault == 1) broken.query = nullptr;
        if (fault == 2) broken.target = nullptr;
        if (fault == 3) broken.reserved[2] = 1;
        if (fault == 4) broken.v3.argument = nullptr;
        CHECK(loader.run_candidate_hooks(broken.v3.v2.v1, State::next, &state, State::validate) == OB_INVALID_ARGUMENT);
      }
      CHECK(state.query_reads == 0 && state.graph_reads == 0 && state.nexts == 0 && state.count == 2);
    }
    // Older services also receive their exact old prefix when the host offers
    // v8; no suffix may accidentally leak into their declared ABI.
    const int status = loader.run_candidate_hooks(native_rust || variant == 0 || variant == 1 || variant == 9 ?
        values.v7.v6.v5.v4.v3.v2.v1 : variant >= 9 ? extended.v1 : view,
        State::next, &state, State::validate, state.relation_paths ? SEEKDB_PLUGIN_PHASE_RELATION : SEEKDB_PLUGIN_PHASE_SELECT);
    if (native_rust) CHECK(state.graph_reads == 8 && state.query_reads == 5);
    if (variant == 13) {
      CHECK(status == OB_SUCCESS && state.count == 3 && state.selected == -1 && state.nexts == 1 && state.validations == 1);
    } else if (variant >= 14 && variant <= 16) {
      CHECK(status == OB_NOT_SUPPORTED && state.count == 2 && state.selected == -1 && state.nexts == 0 && state.validations == 0);
    } else if (variant >= 17) {
      CHECK(status == (variant == 18 ? OB_TIMEOUT : variant == 19 ? OB_INVALID_ARGUMENT : OB_STATE_NOT_MATCH));
      CHECK(state.count == (variant == 18 ? 2 : 3) && state.selected == -1 && state.nexts == 0 && state.validations == 0);
    } else if (variant <= 1) {
      CHECK(status == OB_SUCCESS && state.selected == 1 && state.nexts == variant && state.validations == 1);
    } else if (variant == 2) {
      CHECK(status == OB_STATE_NOT_MATCH && state.selected == -1 && state.validations == 1);
    } else if (variant == 7) {
      CHECK(status == OB_TIMEOUT && state.nexts == 1 && state.validations == 0);
    } else if (variant == 8) {
      CHECK(status == OB_NOT_SUPPORTED && state.nexts == 0 && state.selected == -1 && state.validations == 0);
    } else if (variant == 9) {
      CHECK(status == OB_SUCCESS && state.count == 3 && state.selected == 2 && state.nexts == 1 && state.validations == 1);
    } else if (variant == 12) {
      CHECK(status == OB_TIMEOUT && state.count == 2 && state.selected == -1 && state.nexts == 0);
    } else {
      CHECK(status != OB_SUCCESS && state.nexts == (variant == 6 ? 1 : 0) && state.validations == 0);
    }
    if (native_rust) {
      custom_executor_test::run(loader, true, 0, false);
      custom_executor_test::run_schema(loader);
      custom_executor_test::run_concurrent(loader);
    }
    ObPluginStatusSnapshot snapshot;
    CHECK(loader.get_status(native_rust ? "org.seekdb.rust-candidate" : "org.seekdb.sql_extension", snapshot) == OB_SUCCESS && snapshot.lease_count_ == 0);
    CHECK(loader.shutdown_for_process_exit(1000000) == OB_SUCCESS);
    CHECK(observation.registry->extension_count() == 0 && observation.registry->service_count() == 0);
    return 0;
  }
  if (argc == 4 && (std::strcmp(argv[3], "serverdev") == 0 ||
                    std::strcmp(argv[3], "serverdev-reject") == 0)) {
    Observation observation;
    observation.expected_services = observation.expected_extensions = 0;
    auto guard = std::make_shared<TestGuard>(observation);
    ObPluginLoader loader;
    CHECK(loader.init(argv[1], std::make_shared<TestVerifier>(false, false, false), guard, guard,
                      observation.registry) == OB_SUCCESS);
    const int status = loader.load(argv[2]);
    if (std::strcmp(argv[3], "serverdev") == 0) {
      if (status != OB_SUCCESS) std::cerr << loader.last_error() << std::endl;
      CHECK(status == OB_SUCCESS && observation.committed && observation.completed && !observation.aborted);
    } else {
      CHECK(status == OB_NOT_SUPPORTED);
      CHECK(loader.last_error().find("server-dev") != std::string::npos);
      CHECK(observation.aborted && !observation.committed && !observation.completed);
    }
    CHECK(observation.registry->extension_count() == 0 && observation.registry->service_count() == 0);
    return 0;
  }
  CHECK(argc == 3 || (argc == 4 && (std::strcmp(argv[3], "services") == 0 ||
                                  std::strcmp(argv[3], "gis") == 0 || std::strcmp(argv[3], "rust") == 0 ||
                                  std::strcmp(argv[3], "scaffold") == 0)));
  const bool services = argc == 4 && std::strcmp(argv[3], "services") == 0;
  const bool gis = argc == 4 && std::strcmp(argv[3], "gis") == 0;
  const bool rust = argc == 4 && std::strcmp(argv[3], "rust") == 0;
  const bool scaffold = argc == 4 && std::strcmp(argv[3], "scaffold") == 0;
  for (bool reject : {false, true}) {
    Observation observation;
    observation.reject = reject;
    observation.gis = gis;
    if (rust) { observation.expected_services = 19; observation.expected_extensions = 23; }
    if (scaffold) { observation.expected_services = 1; observation.expected_extensions = 1; }
    if (services) {
      observation.expected_services = 2;
      observation.expected_extensions = 0;
    }
    auto guard = std::make_shared<TestGuard>(observation);
    ObPluginLoader loader;
    CHECK(loader.init(argv[1], std::make_shared<TestVerifier>(services, gis, rust, scaffold), guard, guard,
                      observation.registry) == OB_SUCCESS);
    const int status = loader.load(argv[2]);
    if (!reject && status != OB_SUCCESS) {
      std::cerr << "load failed: " << status << ": " << loader.last_error() << std::endl;
    }
    if (reject) {
      CHECK(status == OB_STATE_NOT_MATCH);
      CHECK(observation.aborted && !observation.committed);
      CHECK(observation.registry->extension_count() == 0);
      CHECK(observation.registry->service_count() == 0);
    } else {
      CHECK(status == OB_SUCCESS);
      CHECK(observation.committed && observation.completed && !observation.aborted);
      if (scaffold) {
        exercise_scaffold(loader);
      } else if (rust) {
        rust_batch_loader_test::run(loader);
        exercise_rust_type_comparison(loader);
        exercise_rust(loader);
        exercise_rust_tables(loader, *observation.registry);
        exercise_table_control_defaults(loader, *observation.registry);
        exercise_rust_sql_table(loader);
        exercise_rust_optimizer(loader);
        exercise_rust_catalog(loader, *observation.registry);
        exercise_rust_types(loader, *observation.registry);
      } else if (gis) {
        exercise_gis(loader);
      } else if (services) {
        for (const char *name : {"org.seekdb.reference.registration-conflict.shared",
                                 "org.seekdb.reference.registration-conflict.after-abort"}) {
          ObPluginLease lease;
          CHECK(observation.registry->acquire(name, 1, 0, lease) == OB_SUCCESS);
          CHECK(lease.is_valid());
        }
      } else {
      exercise_series_null(loader);
      const char *types[] = {"core.type.int64"};
      seekdb_plugin_sql_binding_v1_t binding = {};
      CHECK(loader.resolve_sql_extension(SEEKDB_PLUGIN_EXTENSION_FUNCTION,
          "seekdb_add_one", types, 1, binding) == OB_SUCCESS);
      int64_t input = 41;
      seekdb_plugin_execution_value_v1_t argument = {};
      argument.struct_size = sizeof(argument);
      argument.type_id = types[0];
      argument.data = reinterpret_cast<const uint8_t *>(&input);
      argument.data_size = sizeof(input);
      Sink sink;
      seekdb_plugin_execution_context_v1_t context = {};
      context.struct_size = sizeof(context);
      context.host = reinterpret_cast<seekdb_plugin_host_handle_t *>(&sink);
      context.emit_result = emit;
      CHECK(loader.execute_bound_function(binding, &context, &argument, 1) == OB_SUCCESS);
      CHECK(sink.calls == 1 && sink.value == 42);
      CHECK(loader.resolve_sql_extension(SEEKDB_PLUGIN_EXTENSION_FUNCTION,
          "seekdb_sql_add_one", types, 1, binding) == OB_SUCCESS);
      sink = {};
      CHECK(loader.execute_bound_function(binding, &context, &argument, 1) != OB_SUCCESS);
      CHECK(sink.calls == 0); // a v1-only caller must not silently use native arithmetic
      SqlFixture fixture;
      seekdb_plugin_sql_api_v1_t api = {
          sizeof(api), SEEKDB_PLUGIN_SQL_SPI_MAJOR, 0, 0, fixture_sql, {0}};
      seekdb_plugin_execution_context_v2_t v2 = {};
      v2.v1 = context;
      v2.v1.struct_size = sizeof(v2);
      v2.sql_api = &api;
      v2.sql_context = reinterpret_cast<seekdb_plugin_sql_context_handle_t *>(&fixture);
      CHECK(loader.execute_bound_function(binding, &v2.v1, &argument, 1) == OB_SUCCESS);
      CHECK(fixture.calls == 1 && sink.calls == 1 && sink.value == 42);
      fixture.fail = true;
      sink = {};
      CHECK(loader.execute_bound_function(binding, &v2.v1, &argument, 1) != OB_SUCCESS);
      CHECK(fixture.calls == 2 && sink.calls == 0);
      const char *exec_types[] = {"core.type.bytes", "core.type.int64"};
      CHECK(loader.resolve_sql_extension(SEEKDB_PLUGIN_EXTENSION_FUNCTION,
          "seekdb_sql_exec", exec_types, 2, binding) == OB_SUCCESS);
      const char query[] = "INSERT INTO writes VALUES (?)";
      seekdb_plugin_execution_value_v1_t exec_arguments[2] = {};
      exec_arguments[0].struct_size = sizeof(exec_arguments[0]);
      exec_arguments[0].type_id = exec_types[0];
      exec_arguments[0].data = reinterpret_cast<const uint8_t *>(query);
      exec_arguments[0].data_size = sizeof(query) - 1;
      exec_arguments[1] = argument;
      fixture.exec_mode = true;
      CHECK(loader.execute_bound_function(binding, &v2.v1, exec_arguments, 2) == OB_SUCCESS);
      CHECK(sink.calls == 1 && sink.value == 3);
      sink = {};
      fixture.null_parameter = true;
      exec_arguments[1].is_null = 1;
      exec_arguments[1].data = nullptr;
      exec_arguments[1].data_size = 0;
      CHECK(loader.execute_bound_function(binding, &v2.v1, exec_arguments, 2) == OB_SUCCESS);
      CHECK(sink.calls == 1 && sink.value == 3);
      }
    }
    // The successful Rust path already performs terminal shutdown and checks
    // that saved type/cast identities cannot call the now-unmapped module.
    if ((!rust && !scaffold) || reject) CHECK(loader.shutdown_for_process_exit(1000000) == OB_SUCCESS);
  }
}
