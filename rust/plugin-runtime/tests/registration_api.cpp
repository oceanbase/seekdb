// Copyright (c) 2026 OceanBase. Licensed under the Apache License, Version 2.0.
// White-box test of borrowing and registration atomicity, complementing the
// separate real-DSO loader test. This TU is not linked into the server/plugin.
#include "share/plugin/ob_plugin_loader.cpp"
#include <cstdlib>
#include <iostream>

using namespace oceanbase::share::plugin;
using namespace oceanbase::common;
#define CHECK(expr) do { if (!(expr)) { \
  std::cerr << __LINE__ << ": " << #expr << std::endl; std::abort(); \
} } while (false)

static seekdb_plugin_function_descriptor_v2_t function(const char *id, const char *name,
                                                       const char *const *signature)
{
  seekdb_plugin_function_descriptor_v2_t f = {};
  f.descriptor.struct_size = sizeof(f);
  f.descriptor.object_id = id;
  f.descriptor.sql_name = name;
  f.descriptor.minimum_arity = f.descriptor.maximum_arity = 1;
  f.descriptor.static_result_type_id = "core.type.int64";
  auto &implementation = f.descriptor.implementation;
  implementation.struct_size = sizeof(implementation);
  implementation.service_id = "test.execute";
  implementation.version_range.struct_size = sizeof(implementation.version_range);
  implementation.version_range.minimum_inclusive = {1, 0, 0};
  implementation.version_range.maximum_exclusive = {2, 0, 0};
  f.argument_type_ids = signature;
  f.argument_type_count = 1;
  return f;
}

static seekdb_runtime_registration_stats_t stats(HostContext &host)
{
  seekdb_runtime_registration_stats_t result = {};
  CHECK(seekdb_runtime_registration_stats(host.registration_.get(), &result) == SEEKDB_RUNTIME_OK);
  return result;
}

static const ObPluginExtensionSpec &extension(HostContext &host, uint32_t index)
{
  uint32_t family = 0;
  const void *payload = nullptr;
  CHECK(seekdb_runtime_registration_get(host.registration_.get(), index, &family, &payload) == SEEKDB_RUNTIME_OK);
  CHECK(family == SEEKDB_RUNTIME_EXTENSION && payload != nullptr);
  return *static_cast<const ObPluginExtensionSpec *>(payload);
}

static void conversion_output_contract()
{
  ConvertedArgument copied;
  copied.target_type_ = "core.type.bytes";
  copied.byte_limit_ = 3;
  auto *handle = reinterpret_cast<seekdb_plugin_host_handle_t *>(&copied);
  std::vector<uint8_t> bytes{1, 2, 3};
  seekdb_plugin_execution_result_v1_t value = {};
  value.struct_size = sizeof(value); value.type_id = "core.type.bytes";
  value.data = bytes.data(); value.data_size = bytes.size();
  CHECK(emit_converted_argument(handle, &value) == SEEKDB_PLUGIN_STATUS_OK);
  bytes[0] = 9;
  CHECK(copied.bytes_ == std::vector<uint8_t>({1, 2, 3}));
  CHECK(copied.value_.data == copied.bytes_.data());
  CHECK(emit_converted_argument(handle, &value) == SEEKDB_PLUGIN_STATUS_FAILED_PRECONDITION);
  CHECK(copied.error_ == SEEKDB_PLUGIN_STATUS_FAILED_PRECONDITION);
  for (int invalid = 0; invalid < 5; ++invalid) {
    ConvertedArgument sink;
    sink.target_type_ = "core.type.bytes"; sink.byte_limit_ = 3;
    auto bad = value;
    if (invalid == 0) bad.type_id = "core.type.int64";
    if (invalid == 1) bad.data_size = 4;
    if (invalid == 2) bad.data = nullptr;
    if (invalid == 3) bad.struct_size = 0;
    if (invalid == 4) bad.type_id = nullptr;
    auto *opaque = reinterpret_cast<seekdb_plugin_host_handle_t *>(&sink);
    CHECK(emit_converted_argument(opaque, &bad) == SEEKDB_PLUGIN_STATUS_INVALID_ARGUMENT);
    CHECK(emit_converted_argument(opaque, &value) == SEEKDB_PLUGIN_STATUS_INVALID_ARGUMENT);
    CHECK(sink.bytes_.empty());
  }
  for (bool is_null : {false, true}) {
    ConvertedArgument sink;
    sink.target_type_ = "core.type.bytes"; sink.byte_limit_ = 0;
    value.data = nullptr; value.data_size = 0; value.is_null = is_null;
    CHECK(emit_converted_argument(reinterpret_cast<seekdb_plugin_host_handle_t *>(&sink), &value) == SEEKDB_PLUGIN_STATUS_OK);
    CHECK(sink.value_.is_null == is_null && sink.value_.data_size == 0);
  }
}

struct NullCastProbe { bool source_null = false, result_null = false, invalid_end = false; int calls = 0; };
static seekdb_plugin_status_t SEEKDB_PLUGIN_CALL null_cast(
    seekdb_plugin_instance_handle_t *instance, const seekdb_plugin_execution_context_v1_t *context,
    const seekdb_plugin_execution_value_v1_t *input, uint32_t count)
{
  auto &probe = *reinterpret_cast<NullCastProbe *>(instance);
  CHECK(count == 1 && input && bool(input[0].is_null) == probe.source_null);
  ++probe.calls;
  if (probe.invalid_end) return SEEKDB_PLUGIN_STATUS_END_OF_STREAM;
  seekdb_plugin_execution_result_v1_t result = {};
  result.struct_size = sizeof(result); result.type_id = "core.type.bytes";
  result.is_null = probe.result_null;
  const uint8_t bytes[] = {'o', 'k'};
  if (!result.is_null) { result.data = bytes; result.data_size = sizeof(bytes); }
  return context->emit_result(context->host, &result);
}

static void table_null_after_cast_contract()
{
  ObPluginServiceRegistry registry;
  auto generation = std::make_shared<ObPluginGeneration>("test.null-cast", 1);
  CHECK(generation->transition_to(ObPluginState::VALIDATED) == OB_SUCCESS);
  CHECK(generation->transition_to(ObPluginState::LOADED) == OB_SUCCESS);
  CHECK(generation->transition_to(ObPluginState::INITIALIZING) == OB_SUCCESS);
  seekdb_plugin_function_service_v1_t service = {};
  service.struct_size = sizeof(service); service.spi_major = 1; service.execute = null_cast;
  ObPluginRegistration registration;
  CHECK(registry.begin_registration(generation, registration) == OB_SUCCESS);
  CHECK(registration.add_service("test.null-cast.execute", 1, 0, &service) == OB_SUCCESS);
  CHECK(registration.commit() == OB_SUCCESS);
  NullCastProbe probe;
  seekdb_plugin_execution_context_v1_t context = {}; context.struct_size = sizeof(context);
  const uint8_t bytes[] = {'i', 'n'};
  for (bool source_null : {false, true}) for (bool result_null : {false, true}) {
    for (bool second_null : {false, true}) for (bool strict : {false, true}) {
      std::vector<ConvertedArgument> prepared(2);
      prepared[0].source_type_ = "test.type.source"; prepared[0].target_type_ = "core.type.bytes";
      prepared[0].instance_ = reinterpret_cast<seekdb_plugin_instance_handle_t *>(&probe);
      CHECK(registry.acquire("test.null-cast.execute", 1, 0, prepared[0].implementation_) == OB_SUCCESS);
      prepared[1].source_type_ = prepared[1].target_type_ = "core.type.bytes";
      seekdb_plugin_execution_value_v1_t input[2] = {};
      for (uint32_t i = 0; i < 2; ++i) {
        input[i].struct_size = sizeof(input[i]); input[i].type_id = prepared[i].source_type_.c_str();
        input[i].is_null = i == 0 ? source_null : second_null;
        if (!input[i].is_null) { input[i].data = bytes; input[i].data_size = sizeof(bytes); }
      }
      probe.source_null = source_null; probe.result_null = result_null;
      const int before = probe.calls;
      const int ret = apply_prepared_arguments(prepared, &context, input, 2,
          [&](const seekdb_plugin_execution_value_v1_t *converted, uint32_t count) {
            CHECK(count == 2 && bool(converted[0].is_null) == result_null);
            CHECK(std::strcmp(converted[0].type_id, "core.type.bytes") == 0);
            return null_propagating_table_input(strict ? SEEKDB_PLUGIN_EXTENSION_FLAG_NULL_PROPAGATING : 0,
                converted, count) ? OB_ITER_END : OB_SUCCESS;
          });
      CHECK(probe.calls == before + 1);
      CHECK(ret == ((strict && (result_null || second_null)) ? OB_ITER_END : OB_SUCCESS));
      probe.invalid_end = true;
      int consumes = 0;
      CHECK(apply_prepared_arguments(prepared, &context, input, 2,
          [&](const seekdb_plugin_execution_value_v1_t *, uint32_t) {
            ++consumes; return OB_SUCCESS;
          }) == OB_INVALID_DATA);
      CHECK(consumes == 0 && probe.calls == before + 2);
      probe.invalid_end = false;
    }
  }
  CHECK(!null_propagating_table_input(SEEKDB_PLUGIN_EXTENSION_FLAG_NULL_PROPAGATING, nullptr, 0));
  CHECK(registry.quiesce(generation) == OB_SUCCESS);
  CHECK(registry.mark_stopped(generation) == OB_SUCCESS);
}

struct ResolutionProbe { int mode = 0; int calls = 0; };
static seekdb_plugin_status_t SEEKDB_PLUGIN_CALL probe_execute(
    seekdb_plugin_instance_handle_t *, const seekdb_plugin_execution_context_v1_t *,
    const seekdb_plugin_execution_value_v1_t *, uint32_t)
{ CHECK(false); return SEEKDB_PLUGIN_STATUS_INTERNAL; }

static seekdb_plugin_status_t SEEKDB_PLUGIN_CALL probe_resolve(
    seekdb_plugin_instance_handle_t *instance, const char *const *types, uint32_t count,
    seekdb_plugin_resolved_type_v1_t *out)
{
  auto &probe = *reinterpret_cast<ResolutionProbe *>(instance);
  ++probe.calls;
  CHECK(out->struct_size == sizeof(*out) && out->type_id[0] == 0);
  CHECK(count == 1 && types && types[0]);
  std::strcpy(out->type_id, types[0]);
  if (probe.mode == 1) out->type_id[0] = 0;
  if (probe.mode == 2) std::memset(out->type_id, 'x', sizeof(out->type_id));
  if (probe.mode == 3) out->struct_size = 0;
  if (probe.mode == 4) out->reserved[3] = 1;
  if (probe.mode == 5) return SEEKDB_PLUGIN_STATUS_INVALID_ARGUMENT;
  if (probe.mode == 6) throw std::bad_alloc();
  if (probe.mode == 7) out->type_id[0] = ' ';
  return SEEKDB_PLUGIN_STATUS_OK;
}

static void resolution_output_contract()
{
  ResolutionProbe probe;
  auto *instance = reinterpret_cast<seekdb_plugin_instance_handle_t *>(&probe);
  seekdb_plugin_function_service_v2_t service = {};
  service.v1 = {sizeof(service), 1, SEEKDB_PLUGIN_EXECUTION_RESULT_TYPE_MINOR, 0, probe_execute, {0}};
  service.resolve_result = probe_resolve;
  const char *arguments[] = {"org.test.type"};
  std::string result;
  CHECK(resolve_function_result_type(&service.v1, instance, arguments, 1, result) == OB_SUCCESS);
  CHECK(result == "org.test.type" && probe.calls == 1);
  for (int mode = 1; mode <= 7; ++mode) {
    probe.mode = mode;
    CHECK(resolve_function_result_type(&service.v1, instance, arguments, 1, result) ==
        (mode == 5 ? OB_INVALID_ARGUMENT : mode == 6 ? OB_ALLOCATE_MEMORY_FAILED : OB_INVALID_DATA));
    CHECK(result.empty());
  }
  int calls = probe.calls;
  for (int invalid = 0; invalid < 7; ++invalid) {
    auto bad = service;
    if (invalid == 0) bad.v1.struct_size = sizeof(bad.v1);
    if (invalid == 1) bad.v1.spi_minor = 0;
    if (invalid == 2) bad.v1.spi_major = 2;
    if (invalid == 3) bad.resolve_result = nullptr;
    if (invalid == 4) bad.resolution_reserved[0] = 1;
    if (invalid == 5) bad.v1.reserved[0] = 1;
    if (invalid == 6) bad.v1.execute = nullptr;
    CHECK(resolve_function_result_type(&bad.v1, instance, arguments, 1, result) == OB_NOT_SUPPORTED);
  }
  CHECK(resolve_function_result_type(&service.v1, instance, nullptr, 1, result) == OB_INVALID_ARGUMENT);
  CHECK(resolve_function_result_type(&service.v1, instance, arguments, 1025, result) == OB_INVALID_ARGUMENT);
  CHECK(probe.calls == calls && result.empty());
}

static void table_planning_output_contract()
{
  seekdb_plugin_table_estimate_v1_t valid = {};
  valid.struct_size = sizeof(valid);
  valid.rows = 8; valid.row_width = 40; valid.total_cost = 4;
  CHECK(valid_table_estimate(valid));
  for (int variant = 0; variant < 9; ++variant) {
    auto bad = valid;
    if (variant == 0) bad.struct_size -= 1;
    if (variant == 1) bad.reserved_word = 1;
    if (variant == 2) bad.reserved[3] = 1;
    if (variant == 3) bad.rows = -1;
    if (variant == 4) bad.row_width = -1;
    if (variant == 5) bad.total_cost = -1;
    if (variant == 6) bad.rows = std::numeric_limits<double>::quiet_NaN();
    if (variant == 7) bad.row_width = std::numeric_limits<double>::infinity();
    if (variant == 8) bad.total_cost = std::numeric_limits<double>::infinity();
    CHECK(!valid_table_estimate(bad));
  }
  valid.rows = valid.row_width = valid.total_cost = 0;
  CHECK(valid_table_estimate(valid));
}

static void table_context_compatibility()
{
  for (uint32_t minor : {0u, 1u, 2u, 3u, 4u}) {
    struct Probe { uint32_t size; int calls = 0, closes = 0; } probe{
        static_cast<uint32_t>(minor < 2 ? sizeof(seekdb_plugin_table_execution_context_v1_t) :
            minor == 2 ? sizeof(seekdb_plugin_table_execution_context_v2_t) :
            minor == 3 ? sizeof(seekdb_plugin_table_execution_context_v3_t) : sizeof(seekdb_plugin_table_execution_context_v4_t))};
    seekdb_plugin_table_function_service_v2_t service{};
    service.v1.struct_size = sizeof(service); service.v1.spi_major = 1; service.v1.spi_minor = minor;
    service.v1.next = [](seekdb_plugin_instance_handle_t *raw, seekdb_plugin_table_cursor_handle_t *,
        const seekdb_plugin_table_execution_context_v1_t *context, uint32_t, uint32_t *count) -> seekdb_plugin_status_t {
      auto &probe = *reinterpret_cast<Probe *>(raw);
      CHECK(context->struct_size == probe.size); ++probe.calls;
      *count = 0; return SEEKDB_PLUGIN_STATUS_END_OF_STREAM;
    };
    service.v1.close = [](seekdb_plugin_instance_handle_t *raw, seekdb_plugin_table_cursor_handle_t *) -> seekdb_plugin_status_t {
      ++reinterpret_cast<Probe *>(raw)->closes; return SEEKDB_PLUGIN_STATUS_OK;
    };
    seekdb_plugin_table_execution_context_v4_t context{}; context.v3.v2.v1.struct_size = sizeof(context);
    context.v3.v2.v1.emit_row = [](seekdb_plugin_host_handle_t *, const seekdb_plugin_table_row_v1_t *) -> seekdb_plugin_status_t {
      CHECK(false); return SEEKDB_PLUGIN_STATUS_INTERNAL;
    };
    PluginTableCursor cursor({}, {}, reinterpret_cast<seekdb_plugin_instance_handle_t *>(&probe), &service.v1,
        reinterpret_cast<seekdb_plugin_table_cursor_handle_t *>(&probe), {});
    uint32_t count = 99;
    CHECK(cursor.next(&context.v3.v2.v1, 1, &count) == OB_ITER_END && count == 0 && probe.calls == 1);
    CHECK(cursor.close() == OB_SUCCESS && probe.closes == 1);
    CHECK(cursor.close() == OB_SUCCESS && probe.closes == 1);
  }
}

static void catalog_declaration_sink_contract()
{
  CatalogDeclarations context;
  context.name = "test_ops"; context.version = "1"; context.module = "org.test";
  std::string sql = "SELECT 'a;b'; -- tail";
  CHECK(CatalogDeclarations::emit(&context, sql.data(), sql.size()) == SEEKDB_PLUGIN_STATUS_OK);
  sql.assign("caller changed storage");
  CHECK(context.statements.size() == 1 && context.statements[0] == "SELECT 'a;b'; -- tail");
  seekdb_plugin_status_t wrong_thread = SEEKDB_PLUGIN_STATUS_OK;
  std::thread worker([&]() { wrong_thread = CatalogDeclarations::emit(&context, "SELECT 2;", 9); });
  worker.join();
  CHECK(wrong_thread == SEEKDB_PLUGIN_STATUS_FAILED_PRECONDITION && context.statements.size() == 1);
  const char bad_utf8 = '\xff';
  CHECK(CatalogDeclarations::emit(&context, &bad_utf8, 1) == SEEKDB_PLUGIN_STATUS_INVALID_ARGUMENT);
  CHECK(CatalogDeclarations::emit(&context, "SELECT 2;", 9) == SEEKDB_PLUGIN_STATUS_INVALID_ARGUMENT);
  CHECK(context.statements.size() == 1 && context.error == OB_INVALID_ARGUMENT);
  for (int test = 0; test < 5; ++test) {
    CatalogDeclarations bad;
    bad.name = "test_ops"; bad.version = "1"; bad.module = "org.test";
    const char *data = "SELECT 1;"; uint64_t size = 9;
    if (test == 0) { data = "x\0y"; size = 3; }
    if (test == 1) { size = UINT64_MAX; }
    if (test == 2) { bad.bytes = 4 * 1024 * 1024; }
    if (test == 3) { bad.closed = true; }
    if (test == 4) { bad.statements.resize(4096); }
    CHECK(CatalogDeclarations::emit(&bad, data, size) == SEEKDB_PLUGIN_STATUS_INVALID_ARGUMENT);
    CHECK(bad.error == OB_INVALID_ARGUMENT);
  }
}

static void type_comparison_contract()
{
  struct Probe { int scenario = 0, calls = 0; };
  const auto compare = +[](seekdb_plugin_instance_handle_t *instance,
      const seekdb_plugin_execution_value_v1_t *, const seekdb_plugin_execution_value_v1_t *,
      seekdb_plugin_type_comparison_v1_t *out) -> seekdb_plugin_status_t {
    auto &probe = *reinterpret_cast<Probe *>(instance); ++probe.calls;
    CHECK(out && out->struct_size == sizeof(*out) && out->ordering == 0 && all_zero(out->reserved, 4));
    if (probe.scenario < 3) out->ordering = probe.scenario - 1;
    if (probe.scenario == 3) out->struct_size = 0;
    if (probe.scenario == 4) out->reserved[2] = 1;
    if (probe.scenario == 5) out->ordering = 2;
    if (probe.scenario == 6) { out->ordering = -1; return SEEKDB_PLUGIN_STATUS_TIMEOUT; }
    if (probe.scenario == 7) throw std::bad_alloc();
    if (probe.scenario == 8) throw 1;
    if (probe.scenario == 9) return SEEKDB_PLUGIN_STATUS_END_OF_STREAM;
    return SEEKDB_PLUGIN_STATUS_OK;
  };
  seekdb_plugin_type_codec_service_v1_t legacy{};
  legacy.struct_size = sizeof(legacy); legacy.spi_major = 1;
  seekdb_plugin_type_compare_v1_fn callback = compare;
  CHECK(validate_type_comparison_service(&legacy, callback) == OB_NOT_SUPPORTED && !callback);
  legacy.spi_minor = 1;
  CHECK(validate_type_comparison_service(&legacy, callback) == OB_NOT_SUPPORTED && !callback);
  seekdb_plugin_type_codec_service_v2_t service{};
  service.v1.struct_size = sizeof(service); service.v1.spi_major = 1; service.v1.spi_minor = 1;
  service.v1.decode = +[](seekdb_plugin_instance_handle_t *, const seekdb_plugin_execution_context_v1_t *,
      const uint8_t *, uint64_t) -> seekdb_plugin_status_t { CHECK(false); return SEEKDB_PLUGIN_STATUS_INTERNAL; };
  service.v1.encode = +[](seekdb_plugin_instance_handle_t *, const seekdb_plugin_execution_context_v1_t *,
      const seekdb_plugin_execution_value_v1_t *) -> seekdb_plugin_status_t { CHECK(false); return SEEKDB_PLUGIN_STATUS_INTERNAL; };
  service.compare = compare;
  CHECK(validate_type_comparison_service(&service.v1, callback) == OB_SUCCESS && callback == compare);
  for (int scenario = 0; scenario < 9; ++scenario) {
    auto bad = service;
    if (scenario == 0) --bad.v1.struct_size;
    if (scenario == 1) bad.v1.spi_major = 2;
    if (scenario == 2) bad.v1.spi_minor = 0;
    if (scenario == 3) bad.v1.reserved_word = 1;
    if (scenario == 4) bad.v1.reserved[7] = 1;
    if (scenario == 5) bad.v1.decode = nullptr;
    if (scenario == 6) bad.v1.encode = nullptr;
    if (scenario == 7) bad.compare = nullptr;
    if (scenario == 8) bad.comparison_reserved[3] = 1;
    callback = compare;
    CHECK(validate_type_comparison_service(&bad.v1, callback) == OB_NOT_SUPPORTED && !callback);
  }
  seekdb_plugin_execution_value_v1_t left{}, right{};
  for (int scenario = 0; scenario < 10; ++scenario) {
    Probe probe{scenario, 0}; int32_t ordering = 99;
    const int ret = invoke_type_comparison(compare, reinterpret_cast<seekdb_plugin_instance_handle_t *>(&probe), left, right, ordering);
    const int expected = scenario < 3 ? OB_SUCCESS : scenario == 6 ? OB_TIMEOUT : scenario == 7 ? OB_ALLOCATE_MEMORY_FAILED :
        scenario == 8 ? OB_ERR_UNEXPECTED : OB_INVALID_DATA;
    CHECK(ret == expected && probe.calls == 1 && ordering == (scenario < 3 ? scenario - 1 : 0));
  }
}

static void scalar_batch_contract()
{
  auto scalar = +[](seekdb_plugin_instance_handle_t *, const seekdb_plugin_execution_context_v1_t *,
      const seekdb_plugin_execution_value_v1_t *, uint32_t) -> seekdb_plugin_status_t { return SEEKDB_PLUGIN_STATUS_OK; };
  auto execute_batch = +[](seekdb_plugin_instance_handle_t *, const seekdb_plugin_batch_context_v1_t *,
      const seekdb_plugin_batch_row_v1_t *, uint32_t) -> seekdb_plugin_status_t { return SEEKDB_PLUGIN_STATUS_OK; };
  seekdb_plugin_function_service_v1_t old{sizeof(old), 1, 0, 0, scalar, {0}};
  const seekdb_plugin_function_service_v3_t *resolved = nullptr;
  CHECK(get_batch_function_service(&old, resolved) == OB_SUCCESS && !resolved);
  old.spi_minor = SEEKDB_PLUGIN_EXECUTION_BATCH_MINOR;
  CHECK(get_batch_function_service(&old, resolved) == OB_NOT_SUPPORTED && !resolved);
  seekdb_plugin_function_service_v3_t service{};
  service.v2.v1 = old; service.v2.v1.struct_size = sizeof(service); service.execute_batch = execute_batch;
  CHECK(get_batch_function_service(&service.v2.v1, resolved) == OB_SUCCESS && resolved == &service);
  for (int fault = 0; fault < 5; ++fault) {
    auto invalid = service;
    if (fault == 0) invalid.execute_batch = nullptr;
    if (fault == 1) invalid.batch_reserved[0] = 1;
    if (fault == 2) invalid.v2.resolution_reserved[0] = 1;
    if (fault == 3) invalid.v2.v1.reserved[0] = 1;
    if (fault == 4) invalid.v2.v1.struct_size = sizeof(service) - 1;
    CHECK(get_batch_function_service(&invalid.v2.v1, resolved) == OB_NOT_SUPPORTED && !resolved);
  }
  for (int fault = 0; fault < 8; ++fault) {
    BatchResultSink sink{"core.type.bytes", std::vector<BatchResultSink::Row>(2)};
    auto *host = reinterpret_cast<seekdb_plugin_host_handle_t *>(&sink);
    uint8_t payload[] = {'a','b'};
    seekdb_plugin_execution_result_v1_t value{}; value.struct_size = sizeof(value);
    value.type_id = sink.type_; value.data = payload; value.data_size = sizeof(payload);
    if (fault == 0) value.type_id = "wrong.type";
    if (fault == 1) value.reserved[0] = 1;
    if (fault == 2) value.reserved_bytes[0] = 1;
    if (fault == 3) value.is_null = 2;
    if (fault == 4) value.data_size = MAX_CONVERTED_ARGUMENT_BYTES + 1;
    if (fault == 5) value.data = nullptr;
    if (fault == 6) sink.bytes_ = SEEKDB_PLUGIN_MAX_BATCH_BYTES;
    if (fault == 7) CHECK(reject_batch_scalar_result(host, &value) != SEEKDB_PLUGIN_STATUS_OK);
    CHECK(emit_batch_result(host, 0, &value) != SEEKDB_PLUGIN_STATUS_OK);
    value = {}; value.struct_size = sizeof(value); value.type_id = sink.type_; value.is_null = 1;
    CHECK(emit_batch_result(host, 1, &value) != SEEKDB_PLUGIN_STATUS_OK); // Sticky even on a valid later result.
  }
  BatchResultSink sink{"core.type.bytes", std::vector<BatchResultSink::Row>(1)};
  uint8_t payload[] = {'a','b'};
  seekdb_plugin_execution_result_v1_t value{}; value.struct_size = sizeof(value);
  value.type_id = sink.type_; value.data = payload; value.data_size = sizeof(payload);
  auto *host = reinterpret_cast<seekdb_plugin_host_handle_t *>(&sink);
  CHECK(emit_batch_result(host, 0, &value) == SEEKDB_PLUGIN_STATUS_OK);
  payload[0] = 'x'; CHECK(sink.rows_[0].bytes_[0] == 'a');
  CHECK(emit_batch_result(host, 0, &value) == SEEKDB_PLUGIN_STATUS_FAILED_PRECONDITION);
}

static void independent_owned_bytes()
{
  seekdb_plugin_owned_bytes_v1_t bytes{};
  {
    PluginMemoryLimits limits; limits.bytes_ = 8; limits.allocations_ = 1;
    HostContext owner{limits};
    init_host_api(owner);
    const auto &api = owner.api_;
    auto *handle = api.v2.host.host_handle;
    CHECK(api.memory_spi_major == 1 && api.memory_spi_minor == 0);
    CHECK(api.allocate_owned_bytes(handle, 8, 64, &bytes) == SEEKDB_PLUGIN_STATUS_OK);
    CHECK(bytes.struct_size == sizeof(bytes) && bytes.size == 8 && bytes.alignment == 64);
    CHECK(bytes.data && reinterpret_cast<uintptr_t>(bytes.data) % 64 == 0 && bytes.owner && bytes.release);
    bytes.data[0] = 42;
    api.v2.host.free(handle, bytes.data, bytes.size, bytes.alignment);
    seekdb_runtime_memory_usage_t usage{};
    CHECK(seekdb_runtime_memory_usage(owner.memory_.get(), &usage) == SEEKDB_RUNTIME_OK);
    CHECK(usage.bytes == 8 && usage.invalid_frees == 1);
    seekdb_plugin_owned_bytes_v1_t denied{};
    CHECK(api.allocate_owned_bytes(handle, 1, 1, &denied) == SEEKDB_PLUGIN_STATUS_NO_MEMORY);
    CHECK(!denied.data && !denied.owner && !denied.release && denied.size == 0);
    CHECK(api.allocate_owned_bytes(handle, 0, 1, &denied) == SEEKDB_PLUGIN_STATUS_INVALID_ARGUMENT);
    CHECK(!denied.owner && denied.struct_size == 0);
  } // Destroy the actual C++ host; release must not dereference that old handle.
  std::thread worker([bytes] {
    CHECK(bytes.data[0] == 42);
    bytes.release(bytes.owner);
  });
  worker.join();
}

int main()
{
  scalar_batch_contract();
  type_comparison_contract();
  const auto prepare = +[](seekdb_plugin_instance_handle_t *, const seekdb_plugin_catalog_context_v1_t *) -> seekdb_plugin_status_t {
    return SEEKDB_PLUGIN_STATUS_OK;
  };
  const auto build = +[](seekdb_plugin_instance_handle_t *, const seekdb_plugin_catalog_build_context_v1_t *) -> seekdb_plugin_status_t {
    return SEEKDB_PLUGIN_STATUS_OK;
  };
  seekdb_plugin_catalog_service_v1_t legacy{sizeof(legacy), 1, 0, 0, prepare, {0}};
  CatalogBuildCallback callback = build;
  CHECK(validate_catalog_service(&legacy, callback) == OB_SUCCESS && callback == nullptr);
  legacy.spi_minor = 1; // Short v1 allocation must never be read as a v2 suffix.
  CHECK(validate_catalog_service(&legacy, callback) == OB_NOT_SUPPORTED && callback == nullptr);
  seekdb_plugin_catalog_service_v2_t extended{{sizeof(extended), 1, 1, 0, prepare, {0}}, build, {0}};
  CHECK(validate_catalog_service(&extended.v1, callback) == OB_SUCCESS && callback == build);
  for (int fault = 0; fault < 6; ++fault) {
    auto bad = extended;
    if (fault == 0) bad.v1.spi_minor = 2;
    if (fault == 1) bad.v1.spi_major = 2;
    if (fault == 2) bad.v1.prepare = nullptr;
    if (fault == 3) bad.build = nullptr;
    if (fault == 4) bad.v1.reserved[0] = 1;
    if (fault == 5) bad.reserved[0] = 1;
    CHECK(validate_catalog_service(&bad.v1, callback) == OB_NOT_SUPPORTED && callback == nullptr);
  }
  independent_owned_bytes();
  catalog_declaration_sink_contract();
  table_planning_output_contract();
  table_context_compatibility();
  resolution_output_contract();
  conversion_output_contract();
  table_null_after_cast_contract();
  HostContext host{PluginMemoryLimits{}};
  init_host_api(host);
  const auto &api = host.api_.v2;
  auto *handle = api.host.host_handle;
  seekdb_plugin_registration_txn_t *txn = nullptr;
  CHECK(api.host.struct_size == sizeof(host.api_));
  CHECK(api.host.begin_registration(handle, &txn) == SEEKDB_PLUGIN_STATUS_FAILED_PRECONDITION);
  CHECK(seekdb_runtime_registration_open(host.registration_.get()) == SEEKDB_RUNTIME_OK);
  CHECK(api.host.begin_registration(handle, &txn) == SEEKDB_PLUGIN_STATUS_OK);
  char id[] = "test.function";
  char name[] = "test_function";
  char type[] = "core.type.int64";
  const char *signature[] = {type};
  auto f = function(id, name, signature);
  CHECK(api.register_extension(handle, txn, 999, &f, sizeof(f)) != SEEKDB_PLUGIN_STATUS_OK);
  CHECK(api.register_extension(handle, txn, SEEKDB_PLUGIN_EXTENSION_FUNCTION, &f,
                               sizeof(f) - 1) != SEEKDB_PLUGIN_STATUS_OK);
  f.signature_reserved[0] = 1;
  CHECK(api.register_extension(handle, txn, SEEKDB_PLUGIN_EXTENSION_FUNCTION, &f,
                               sizeof(f)) != SEEKDB_PLUGIN_STATUS_OK);
  f.signature_reserved[0] = 0;
  CHECK(api.register_extension(handle, txn, SEEKDB_PLUGIN_EXTENSION_FUNCTION, &f,
                               sizeof(f)) == SEEKDB_PLUGIN_STATUS_OK);
  CHECK(api.register_extension(handle, txn, SEEKDB_PLUGIN_EXTENSION_FUNCTION, &f,
                               sizeof(f)) == SEEKDB_PLUGIN_STATUS_ALREADY_EXISTS);
  // All descriptor-owned memory can change immediately after register returns.
  id[0] = name[0] = type[0] = 'x';
  CHECK(api.host.commit_registration(handle, txn) == SEEKDB_PLUGIN_STATUS_OK);
  CHECK(stats(host).committed_extensions == 1);
  CHECK(extension(host, 0).object_id_ == "test.function");
  CHECK(extension(host, 0).sql_name_ == "test_function");
  CHECK(extension(host, 0).argument_type_ids_[0] == "core.type.int64");

  // Two open transactions can stage a name before either commits. The losing
  // commit must preserve its transaction and publish neither its services nor
  // its objects. Abort then reclaims the entire losing transaction.
  const char *types[] = {"core.type.int64"};
  auto conflict = function("test.conflict", "test_conflict", types);
  seekdb_plugin_registration_txn_t *left = nullptr, *right = nullptr;
  CHECK(api.host.begin_registration(handle, &left) == SEEKDB_PLUGIN_STATUS_OK);
  CHECK(api.host.begin_registration(handle, &right) == SEEKDB_PLUGIN_STATUS_OK);
  CHECK(api.register_extension(handle, left, SEEKDB_PLUGIN_EXTENSION_FUNCTION, &conflict,
                               sizeof(conflict)) == SEEKDB_PLUGIN_STATUS_OK);
  CHECK(api.register_extension(handle, right, SEEKDB_PLUGIN_EXTENSION_FUNCTION, &conflict,
                               sizeof(conflict)) == SEEKDB_PLUGIN_STATUS_OK);
  uint32_t service_table = sizeof(uint32_t);
  seekdb_plugin_service_provide_descriptor_t service = {};
  service.struct_size = sizeof(service);
  service.service_id = "test.execute";
  service.version = {1, 0, 0};
  service.service = &service_table;
  CHECK(api.host.register_service(handle, right, &service) == SEEKDB_PLUGIN_STATUS_OK);
  CHECK(api.host.commit_registration(handle, left) == SEEKDB_PLUGIN_STATUS_OK);
  CHECK(api.host.commit_registration(handle, right) == SEEKDB_PLUGIN_STATUS_ALREADY_EXISTS);
  CHECK(stats(host).committed_services == 0);
  CHECK(stats(host).committed_extensions == 2);
  CHECK(stats(host).open_transactions == 1);
  api.host.abort_registration(handle, right);
  CHECK(stats(host).total_services == stats(host).committed_services && stats(host).open_transactions == 0);

  CHECK(api.host.begin_registration(handle, &txn) == SEEKDB_PLUGIN_STATUS_OK);
  auto aborted = function("test.aborted", "test_aborted", types);
  CHECK(api.register_extension(handle, txn, SEEKDB_PLUGIN_EXTENSION_FUNCTION, &aborted,
                               sizeof(aborted)) == SEEKDB_PLUGIN_STATUS_OK);
  CHECK(api.host.register_service(handle, txn, &service) == SEEKDB_PLUGIN_STATUS_OK);
  api.host.abort_registration(handle, txn);
  CHECK(stats(host).committed_services == 0 && stats(host).committed_extensions == 2);

  CHECK(api.host.begin_registration(handle, &txn) == SEEKDB_PLUGIN_STATUS_OK);
  CHECK(api.register_extension(handle, txn, SEEKDB_PLUGIN_EXTENSION_FUNCTION, &aborted,
                               sizeof(aborted)) == SEEKDB_PLUGIN_STATUS_OK);
  CHECK(api.host.register_service(handle, txn, &service) == SEEKDB_PLUGIN_STATUS_OK);
  CHECK(api.host.commit_registration(handle, txn) == SEEKDB_PLUGIN_STATUS_OK);
  CHECK(stats(host).committed_services == 1 && stats(host).committed_extensions == 3);

  CHECK(api.host.begin_registration(handle, &txn) == SEEKDB_PLUGIN_STATUS_OK);
  CHECK(seekdb_runtime_registration_seal(host.registration_.get()) == SEEKDB_RUNTIME_STATE_MISMATCH);
  CHECK(api.register_extension(handle, txn, SEEKDB_PLUGIN_EXTENSION_FUNCTION, &aborted,
                               sizeof(aborted)) == SEEKDB_PLUGIN_STATUS_FAILED_PRECONDITION);
  CHECK(api.host.commit_registration(handle, txn) == SEEKDB_PLUGIN_STATUS_FAILED_PRECONDITION);
  cleanup_host_resources(host);
  CHECK(stats(host).committed_extensions == 0 && stats(host).extension_bytes == 0);

  // The quota is per host, not per transaction; abort returns that budget.
  HostContext quota{PluginMemoryLimits{}};
  init_host_api(quota);
  const auto &quota_api = quota.api_.v2;
  auto *quota_handle = quota_api.host.host_handle;
  CHECK(seekdb_runtime_registration_open(quota.registration_.get()) == SEEKDB_RUNTIME_OK);
  CHECK(quota_api.host.begin_registration(quota_handle, &left) == SEEKDB_PLUGIN_STATUS_OK);
  CHECK(quota_api.host.begin_registration(quota_handle, &right) == SEEKDB_PLUGIN_STATUS_OK);
  for (uint32_t i = 0; i < SEEKDB_PLUGIN_MAX_EXTENSIONS; ++i) {
    const std::string id = "test.quota." + std::to_string(i);
    auto entry = function(id.c_str(), "quota_function", types);
    CHECK(quota_api.register_extension(quota_handle, (i % 2 == 0 ? left : right),
        SEEKDB_PLUGIN_EXTENSION_FUNCTION, &entry, sizeof(entry)) == SEEKDB_PLUGIN_STATUS_OK);
  }
  CHECK(quota_api.register_extension(quota_handle, left, SEEKDB_PLUGIN_EXTENSION_FUNCTION, &aborted,
                               sizeof(aborted)) != SEEKDB_PLUGIN_STATUS_OK);
  quota_api.host.abort_registration(quota_handle, right);
  CHECK(quota_api.register_extension(quota_handle, left, SEEKDB_PLUGIN_EXTENSION_FUNCTION, &aborted,
                               sizeof(aborted)) == SEEKDB_PLUGIN_STATUS_OK);
  quota_api.host.abort_registration(quota_handle, left);
  CHECK(stats(quota).open_transactions == 0);
  cleanup_host_resources(quota);
}
