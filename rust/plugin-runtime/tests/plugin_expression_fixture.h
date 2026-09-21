// Copyright (c) 2026 OceanBase. Licensed under the Apache License, Version 2.0.
// Real SQL expression inference/codegen/evaluation with a controlled provider.
// This does not model a server catalog, optimizer, or plugin loader.
#ifndef SEEKDB_TEST_PLUGIN_EXPRESSION_FIXTURE_H_
#define SEEKDB_TEST_PLUGIN_EXPRESSION_FIXTURE_H_
#include "seekdb/plugin/sql_spi.h"
#include "sql/engine/expr/plugin_function_expr.h"
#include "sql/code_generator/ob_static_engine_expr_cg.h"
#include "sql/resolver/expr/ob_raw_expr.h"
#include "sql/resolver/expr/plugin_expr_type.h"

namespace plugin_expression_test {
using namespace oceanbase::common;
using namespace oceanbase::sql;
using namespace oceanbase::share;
constexpr const char *CUSTOM = "org.test.int64"; // Not a built-in integer!

class Provider final : public ObIModuleProvider
{
public:
  ObIModuleProvider *saved_ = g_mp;
  bool mixed_epoch_ = false;
  int resolves_ = 0, calls_ = 0, failure_ = 0;
  int codec_calls_ = 0, codec_failure_ = 0;
  int encode_calls_ = 0;
  int cast_resolves_ = 0, cast_calls_ = 0, cast_failure_ = 0;
  bool allow_assignment_cast_ = false;
  bool allow_numeric_casts_ = false;
  bool nonpersistent_type_ = false;
  bool allow_case_common_ = false;
  bool table_enabled_ = false;
  int table_describes_ = 0, table_opens_ = 0, table_closes_ = 0, table_failure_ = 0;
  uint64_t case_epoch_ = 11;
  const char *expected_argument_ = CUSTOM;
  const char *expected_bytes_ = nullptr;
  const char *numeric_result_id_ = nullptr;
  std::vector<std::string> seen_;
  Provider() { g_mp = this; }
  ~Provider() { g_mp = saved_; }
  int execute_plugin_function(const char *, uint32_t, uint32_t,
      const seekdb_plugin_execution_context_v1 *, const seekdb_plugin_execution_value_v1 *, uint32_t) override
  { CHECK(false); return OB_ERR_UNEXPECTED; }
  int execute_plugin_extension(seekdb_plugin_extension_kind_t, const char *,
      const seekdb_plugin_execution_context_v1 *, const seekdb_plugin_execution_value_v1 *, uint32_t) override
  { CHECK(false); return OB_ERR_UNEXPECTED; }
  int resolve_plugin_sql_object(seekdb_plugin_extension_kind_t kind, const char *name,
      const char *const *types, uint32_t count, seekdb_plugin_sql_binding_v1_t *out) override
  {
    if (kind == SEEKDB_PLUGIN_EXTENSION_TABLE_FUNCTION) {
      CHECK(table_enabled_ && name && out); *out = {};
      if (std::strcmp(name, "fixture_rows") != 0) return OB_ENTRY_NOT_EXIST;
      CHECK(count == 1); ++resolves_;
      out->struct_size = sizeof(*out); out->kind = kind;
      std::strcpy(out->object_id, "org.test.rows"); std::strcpy(out->sql_name, "fixture_rows");
      std::strcpy(out->owner_plugin_id, "org.test");
      out->owner_generation = 7; out->catalog_epoch = mixed_epoch_ ? 12 : 11;
      out->minimum_arity = out->maximum_arity = 1; out->column_count = 2;
      return OB_SUCCESS;
    }
    if (kind == SEEKDB_PLUGIN_EXTENSION_TYPE) {
      CHECK(out && count == 0 && name);
      ++resolves_;
      if (std::strcmp(name, "payload") != 0) { *out = {}; return OB_ENTRY_NOT_EXIST; }
      *out = {}; out->struct_size = sizeof(*out); out->kind = kind;
      std::strcpy(out->object_id, CUSTOM); std::strcpy(out->sql_name, name);
      std::strcpy(out->owner_plugin_id, "org.test");
      std::strcpy(out->physical_format_id, "org.test.format");
      out->physical_format_version = 1;
      out->owner_generation = 7; out->catalog_epoch = 11;
      out->flags = SEEKDB_PLUGIN_EXTENSION_FLAG_PERSISTENT;
      if (nonpersistent_type_) {
        out->flags = 0; out->physical_format_id[0] = 0; out->physical_format_version = 0;
      }
      if (mixed_epoch_) ++out->catalog_epoch;
      return OB_SUCCESS;
    }
    CHECK(kind == SEEKDB_PLUGIN_EXTENSION_FUNCTION && count == 1 && out);
    ++resolves_;
    seen_.emplace_back(types[0] ? types[0] : "NULL");
    *out = {};
    out->struct_size = sizeof(*out); out->kind = kind;
    out->minimum_arity = out->maximum_arity = 1;
    out->owner_generation = 7; out->catalog_epoch = 11;
    std::strcpy(out->sql_name, name);
    std::strcpy(out->owner_plugin_id, "org.test");
    if (std::strcmp(name, "construct") == 0) {
      CHECK(!types[0] || std::strcmp(types[0], "core.type.bytes") == 0);
      std::strcpy(out->object_id, "org.test.construct");
      std::strcpy(out->result_type_id, CUSTOM);
      if (mixed_epoch_) ++out->catalog_epoch;
    } else {
      CHECK(std::strcmp(name, "consume") == 0);
      std::strcpy(out->object_id, types[0] && std::strcmp(types[0], CUSTOM) == 0
          ? "org.test.consume.typed" : "org.test.consume.bytes");
      std::strcpy(out->result_type_id, numeric_result_id_ ? numeric_result_id_ : "core.type.int64");
    }
    return OB_SUCCESS;
  }
  int execute_bound_plugin_function(const seekdb_plugin_sql_binding_v1_t *binding,
      const seekdb_plugin_execution_context_v1 *ctx,
      const seekdb_plugin_execution_value_v1 *args, uint32_t count) override
  {
    CHECK(binding && ctx && args && count == 1 && binding->owner_generation == 7);
    ++calls_;
    seekdb_plugin_execution_result_v1_t result = {};
    result.struct_size = sizeof(result);
    result.type_id = binding->result_type_id;
    int64_t length = args[0].data_size;
    if (std::strcmp(binding->sql_name, "construct") == 0) {
      CHECK(!args[0].type_id || std::strcmp(args[0].type_id, "core.type.bytes") == 0);
      result.data = args[0].data; result.data_size = args[0].data_size;
    } else {
      CHECK(std::strcmp(binding->object_id, "org.test.consume.typed") == 0);
      CHECK(args[0].type_id && std::strcmp(args[0].type_id, expected_argument_) == 0);
      if (expected_bytes_ && !args[0].is_null) {
        CHECK(args[0].data_size == std::strlen(expected_bytes_));
        CHECK(std::memcmp(args[0].data, expected_bytes_, args[0].data_size) == 0);
      }
      result.data = reinterpret_cast<const uint8_t *>(&length); result.data_size = sizeof(length);
    }
    result.is_null = args[0].is_null;
    if (failure_ == 1) result.type_id = "core.type.float64";
    if (failure_ == 2) result.data_size = 1;
    ctx->emit_result(ctx->host, failure_ == 4 ? nullptr : &result);
    if (failure_ == 3) ctx->emit_result(ctx->host, &result);
    return OB_SUCCESS; // Deliberately ignore callback errors.
  }
  int describe_plugin_sql_column(const seekdb_plugin_sql_binding_v1_t *binding, uint32_t index,
      seekdb_plugin_sql_column_v1_t *out) override {
    CHECK(table_enabled_ && binding && out && index < 2); ++table_describes_;
    *out = {}; out->struct_size = sizeof(*out); out->nullable = 1;
    std::strcpy(out->sql_name, index ? "ordinal" : "payload");
    std::strcpy(out->type_id, index ? "core.type.int64" : CUSTOM);
    return OB_SUCCESS;
  }
  int decode_bound_plugin_type(const seekdb_plugin_sql_binding_v1_t *binding,
      const seekdb_plugin_execution_context_v1 *context, const uint8_t *bytes, uint64_t size) override
  {
    CHECK(binding && binding->kind == SEEKDB_PLUGIN_EXTENSION_TYPE && binding->owner_generation == 7);
    CHECK(context && context->struct_size == sizeof(*context));
    ++codec_calls_;
    if (codec_failure_ == 6) return OB_STATE_NOT_MATCH;
    if (size < 2 || !bytes || bytes[0] != 'E' || bytes[1] != ':') return OB_INVALID_DATA;
    std::string decoded(reinterpret_cast<const char *>(bytes + 2), size - 2);
    seekdb_plugin_execution_result_v1_t value = {};
    value.struct_size = sizeof(value); value.type_id = CUSTOM;
    value.data = reinterpret_cast<const uint8_t *>(decoded.data()); value.data_size = decoded.size();
    if (codec_failure_ == 1) value.type_id = "core.type.bytes";
    if (codec_failure_ == 4) value.data_size = UINT64_C(16777217);
    if (codec_failure_ == 7) value.reserved[0] = 1;
    if (codec_failure_ != 3) context->emit_result(context->host, codec_failure_ == 5 ? nullptr : &value);
    if (codec_failure_ == 2) context->emit_result(context->host, &value);
    decoded.assign(decoded.size(), 'x'); // The caller must already own the output.
    return OB_SUCCESS; // Deliberately ignore emit errors to exercise the host sink.
  }
  int encode_bound_plugin_type(const seekdb_plugin_sql_binding_v1_t *binding,
      const seekdb_plugin_execution_context_v1 *context, const seekdb_plugin_execution_value_v1 *value) override
  {
    CHECK(binding && value && std::strcmp(value->type_id, CUSTOM) == 0);
    CHECK(context && context->struct_size == sizeof(*context));
    ++encode_calls_;
    if (codec_failure_ == 6) return OB_STATE_NOT_MATCH;
    std::string encoded("E:");
    if (value->data_size) encoded.append(reinterpret_cast<const char *>(value->data), value->data_size);
    seekdb_plugin_execution_result_v1_t result = {};
    result.struct_size = sizeof(result); result.type_id = "core.type.bytes";
    result.data = reinterpret_cast<const uint8_t *>(encoded.data()); result.data_size = encoded.size();
    if (codec_failure_ == 1) result.type_id = CUSTOM;
    if (codec_failure_ == 4) result.data_size = UINT64_C(16777217);
    if (codec_failure_ == 7) result.reserved[0] = 1;
    if (codec_failure_ != 3) context->emit_result(context->host, codec_failure_ == 5 ? nullptr : &result);
    if (codec_failure_ == 2) context->emit_result(context->host, &result);
    encoded.assign(encoded.size(), 'x');
    return OB_SUCCESS;
  }
  class Cursor final : public IPluginTableCursor {
  public:
    Provider &provider_; std::string payload_; int64_t row_ = 0; bool closed_ = false;
    Cursor(Provider &provider, std::string payload) : provider_(provider), payload_(std::move(payload)) {}
    ~Cursor() override { close(); }
    int next(const seekdb_plugin_table_execution_context_v1_t *context, uint32_t max_rows, uint32_t *emitted) override {
      CHECK(context && emitted && max_rows == 1 && !closed_); *emitted = 0;
      if (row_ == 2) return OB_ITER_END;
      ++row_;
      seekdb_plugin_execution_result_v1_t values[2] = {};
      values[0].struct_size = values[1].struct_size = sizeof(values[0]);
      values[0].type_id = provider_.table_failure_ ? "core.type.bytes" : CUSTOM;
      values[0].data = reinterpret_cast<const uint8_t *>(payload_.data()); values[0].data_size = payload_.size();
      values[1].type_id = "core.type.int64";
      values[1].data = reinterpret_cast<const uint8_t *>(&row_); values[1].data_size = sizeof(row_);
      seekdb_plugin_table_row_v1_t row = {}; row.struct_size = sizeof(row); row.columns = values; row.column_count = 2;
      context->emit_row(context->host, &row); // Deliberately ignore failure to test the sticky host sink.
      *emitted = 1; return OB_SUCCESS;
    }
    int rescan(const seekdb_plugin_execution_value_v1_t *, uint32_t) override { CHECK(false); return OB_ERR_UNEXPECTED; }
    int close() override { if (!closed_) { closed_ = true; ++provider_.table_closes_; } return OB_SUCCESS; }
  };
  int open_bound_plugin_table_function(const seekdb_plugin_sql_binding_v1_t *binding,
      const seekdb_plugin_table_execution_context_v1_t *, const seekdb_plugin_execution_value_v1_t *args,
      uint32_t count, std::unique_ptr<IPluginTableCursor> &cursor) override {
    CHECK(table_enabled_ && binding && args && count == 1 && !cursor);
    CHECK(binding->catalog_epoch == 11 && binding->owner_generation == 7);
    if (mixed_epoch_) return OB_STATE_NOT_MATCH;
    CHECK(std::strcmp(args[0].type_id, CUSTOM) == 0);
    ++table_opens_;
    cursor = std::make_unique<Cursor>(*this, std::string(reinterpret_cast<const char *>(args[0].data), args[0].data_size));
    return OB_SUCCESS;
  }
  int resolve_plugin_common_type(const char *const *types, uint32_t count,
      std::string &common_type, uint64_t &epoch) override
  {
    CHECK(allow_case_common_ && count && types); ++resolves_;
    // Controlled stored-column fixture: one known identity plus NULL only.
    // Real multi-type selection is tested through the Rust DSO provider.
    for (uint32_t i = 0; i < count; ++i) CHECK(!types[i] || std::strcmp(types[i], CUSTOM) == 0);
    common_type = CUSTOM; epoch = case_epoch_; return OB_SUCCESS;
  }
  int resolve_plugin_cast(const char *source, const char *target, seekdb_plugin_cast_context_t context,
      seekdb_plugin_sql_cast_binding_v1_t *out, uint64_t expected_epoch = 0) override
  {
    CHECK(source && target && out); ++cast_resolves_; *out = {};
    if (expected_epoch && expected_epoch != 11) return OB_STATE_NOT_MATCH;
    if (!allow_assignment_cast_) return OB_ENTRY_NOT_EXIST;
    const bool from_bytes = std::strcmp(source, "core.type.bytes") == 0 && std::strcmp(target, CUSTOM) == 0;
    const bool to_bytes = std::strcmp(source, CUSTOM) == 0 && std::strcmp(target, "core.type.bytes") == 0;
    const bool numeric = allow_numeric_casts_ && std::strcmp(source, CUSTOM) == 0 &&
        (std::strcmp(target, "core.type.int64") == 0 || std::strcmp(target, "core.type.uint64") == 0 ||
         std::strcmp(target, "core.type.float64") == 0);
    if (!from_bytes && !to_bytes && !numeric) return OB_ENTRY_NOT_EXIST;
    out->struct_size = sizeof(*out); out->requested_context = context; out->declared_context = SEEKDB_PLUGIN_CAST_IMPLICIT;
    out->owner_generation = 7; out->catalog_epoch = 11;
    std::strcpy(out->object_id, from_bytes ? "org.test.cast.from-bytes" : "org.test.cast.to-bytes");
    std::strcpy(out->owner_plugin_id, "org.test");
    std::strcpy(out->source_type_id, source); std::strcpy(out->target_type_id, target);
    return OB_SUCCESS;
  }
  int execute_bound_plugin_cast(const seekdb_plugin_sql_cast_binding_v1_t *binding,
      const seekdb_plugin_execution_context_v1 *context, const seekdb_plugin_execution_value_v1 *value) override
  {
    CHECK(binding && context && value && context->struct_size == sizeof(seekdb_plugin_execution_context_v2_t));
    CHECK(binding->catalog_epoch == 11 && binding->owner_generation == 7);
    CHECK(value->type_id && std::strcmp(value->type_id, binding->source_type_id) == 0);
    ++cast_calls_;
    if (cast_failure_ == 6) return OB_STATE_NOT_MATCH;
    std::string converted;
    if (!value->is_null && value->data_size) converted.assign(reinterpret_cast<const char *>(value->data), value->data_size);
    if (!value->is_null && allow_numeric_casts_) {
      if (std::strcmp(binding->target_type_id, "core.type.int64") == 0) {
        const int64_t number = -19; converted.assign(reinterpret_cast<const char *>(&number), sizeof(number));
      } else if (std::strcmp(binding->target_type_id, "core.type.uint64") == 0) {
        const uint64_t number = UINT64_MAX; converted.assign(reinterpret_cast<const char *>(&number), sizeof(number));
      } else if (std::strcmp(binding->target_type_id, "core.type.float64") == 0) {
        const double number = 3.25; converted.assign(reinterpret_cast<const char *>(&number), sizeof(number));
      }
    }
    seekdb_plugin_execution_result_v1_t result = {};
    result.struct_size = sizeof(result); result.type_id = binding->target_type_id; result.is_null = value->is_null;
    result.data = reinterpret_cast<const uint8_t *>(converted.data()); result.data_size = converted.size();
    if (cast_failure_ == 1) result.type_id = "other.type";
    if (cast_failure_ == 4) result.data_size = UINT64_C(16777217);
    if (cast_failure_ == 7) result.reserved[0] = 1;
    if (cast_failure_ != 3) context->emit_result(context->host, cast_failure_ == 5 ? nullptr : &result);
    if (cast_failure_ == 2) context->emit_result(context->host, &result);
    converted.assign(converted.size(), 'x');
    return OB_SUCCESS; // Ignored emit errors must remain sticky in the host.
  }
  int mutate_plugin_type_dependency(ObISQLClient &, const seekdb_plugin_sql_binding_v1_t &,
      uint64_t, uint64_t, bool) override { CHECK(false); return OB_ERR_UNEXPECTED; }
};

inline ObConstRawExpr *literal(ObRawExprFactory &factory, const char *text)
{
  ObConstRawExpr *out = nullptr;
  CHECK(factory.create_raw_expr(T_VARCHAR, out) == OB_SUCCESS);
  ObObj value; value.set_varchar(ObString::make_string(text));
  value.set_collation_type(CS_TYPE_UTF8MB4_BIN);
  out->set_value(value);
  return out;
}

inline ObSysFunRawExpr *call(ObRawExprFactory &factory, const char *name, ObRawExpr *arg)
{
  ObSysFunRawExpr *out = nullptr;
  CHECK(factory.create_raw_expr(T_FUN_SYS_PLUGIN_FUNCTION, out) == OB_SUCCESS);
  CHECK(out->init_param_exprs(2) == OB_SUCCESS);
  CHECK(out->add_param_expr(literal(factory, name)) == OB_SUCCESS);
  CHECK(out->add_param_expr(arg) == OB_SUCCESS);
  ObExprResType physical;
  physical.set_varchar(); // The physical type does not carry CUSTOM.
  out->set_result_type(physical);
  return out;
}

inline void run()
{
  Provider provider;
  ObArenaAllocator allocator;
  ObRawExprFactory factory(allocator);
  auto *text = literal(factory, "hello");
  // Controlled metadata provider, real SQL type deduction. uint64 must not
  // become a signed SQL integer, including the existing GIS compatibility ID.
  for (const char *id : {"core.type.uint64", "org.seekdb.gis.scalar.uint64"}) {
    provider.numeric_result_id_ = id;
    auto *numeric = call(factory, "consume", text);
    PluginFunctionExpr inference(allocator); ObExprTypeCtx context; context.set_raw_expr(numeric);
    ObExprResType types[2], result;
    types[0] = numeric->get_param_expr(0)->get_result_type();
    types[0].set_param(static_cast<ObConstRawExpr *>(numeric->get_param_expr(0))->get_param());
    types[1] = text->get_result_type();
    CHECK(inference.calc_result_typeN(result, types, 2, context) == OB_SUCCESS);
    CHECK(result.get_type() == ObUInt64Type && numeric->get_plugin_type());
    CHECK(numeric->get_plugin_type()->physical_type_ == ObUInt64Type &&
        numeric->get_plugin_type()->logical_id_ == ObString::make_string(id));
  }
  provider.numeric_result_id_ = nullptr;
  auto *inner = call(factory, "construct", text);
  auto *outer = call(factory, "consume", inner);
  seekdb_plugin_sql_binding_v1_t binding = {};
  std::vector<std::string> arguments;
  CHECK(PluginFunctionExpr::resolve_raw_binding(*outer, binding, arguments) == OB_SUCCESS);
  CHECK(arguments == std::vector<std::string>{CUSTOM});
  CHECK(std::strcmp(binding.object_id, "org.test.consume.typed") == 0);
  provider.mixed_epoch_ = true;
  CHECK(PluginFunctionExpr::resolve_raw_binding(*outer, binding, arguments) == OB_STATE_NOT_MATCH);
  provider.mixed_epoch_ = false;
  CHECK(PluginFunctionExpr::resolve_raw_binding(*outer, binding, arguments, 64) == OB_INVALID_ARGUMENT);

  PluginFunctionExpr op(allocator);
  ObExprTypeCtx type_context;
  ObExprResType input[2], result_type;
  input[0] = inner->get_param_expr(0)->get_result_type();
  input[0].set_param(static_cast<ObConstRawExpr *>(inner->get_param_expr(0))->get_param());
  input[1] = text->get_result_type();
  type_context.set_raw_expr(inner);
  CHECK(op.calc_result_typeN(result_type, input, 2, type_context) == OB_SUCCESS);
  CHECK(result_type.get_type() == ObVarcharType); // Suffix must not imply int64.
  input[0] = outer->get_param_expr(0)->get_result_type();
  input[0].set_param(static_cast<ObConstRawExpr *>(outer->get_param_expr(0))->get_param());
  input[1] = inner->get_result_type();
  type_context.set_raw_expr(outer);
  CHECK(op.calc_result_typeN(result_type, input, 2, type_context) == OB_SUCCESS);
  CHECK(result_type.get_type() == ObIntType && provider.seen_.back() == CUSTOM);

  // Once typed, the hidden constant contains owned binding wire. Neither
  // re-inference nor codegen may ask a changed registry to select again.
  const int bound_resolves = provider.resolves_;
  auto *bound_metadata = outer->get_param_expr(0);
  CHECK(static_cast<ObConstRawExpr *>(bound_metadata)->get_value().is_varbinary());
  provider.mixed_epoch_ = true;
  CHECK(PluginFunctionExpr::resolve_raw_binding(*outer, binding, arguments) == OB_SUCCESS);
  CHECK(binding.catalog_epoch == 11 && binding.owner_generation == 7);
  CHECK(op.calc_result_typeN(result_type, input, 2, type_context) == OB_SUCCESS);
  CHECK(outer->get_param_expr(0) == bound_metadata && provider.resolves_ == bound_resolves);
  provider.mixed_epoch_ = false;
  auto wrong_epoch = *outer->get_plugin_type();
  CHECK(wrong_epoch.catalog_epoch_ == 11);
  wrong_epoch.catalog_epoch_ = 12;
  CHECK(outer->set_plugin_type(wrong_epoch) == OB_SUCCESS);
  CHECK(PluginFunctionExpr::resolve_raw_binding(*outer, binding, arguments) == OB_STATE_NOT_MATCH);
  CHECK(binding.struct_size == 0 && provider.resolves_ == bound_resolves);
  wrong_epoch.catalog_epoch_ = 11;
  CHECK(outer->set_plugin_type(wrong_epoch) == OB_SUCCESS);
  g_mp = nullptr;
  CHECK(PluginFunctionExpr::resolve_raw_binding(*outer, binding, arguments) == OB_SUCCESS);
  CHECK(provider.resolves_ == bound_resolves);
  g_mp = &provider;
  outer->get_param_expr(1) = text;
  CHECK(PluginFunctionExpr::resolve_raw_binding(*outer, binding, arguments) == OB_STATE_NOT_MATCH);
  CHECK(binding.struct_size == 0);
  outer->get_param_expr(1) = inner;
  const auto fixed_wire = static_cast<ObConstRawExpr *>(bound_metadata)->get_value().get_string();
  auto *bad_metadata = literal(factory, "temporary");
  outer->get_param_expr(0) = bad_metadata;
  for (int64_t size = 0; size < fixed_wire.length(); ++size) {
    ObObj truncated; truncated.set_varchar(ObString(size, fixed_wire.ptr()));
    truncated.set_collation_type(CS_TYPE_BINARY); bad_metadata->set_value(truncated);
    CHECK(PluginFunctionExpr::resolve_raw_binding(*outer, binding, arguments) != OB_SUCCESS);
    CHECK(binding.struct_size == 0);
  }
  outer->get_param_expr(0) = bound_metadata;
  CHECK(provider.resolves_ == bound_resolves);
  auto *untyped_wire_call = call(factory, "consume", inner);
  untyped_wire_call->get_param_expr(0) = bound_metadata;
  CHECK(PluginFunctionExpr::resolve_raw_binding(*untyped_wire_call, binding, arguments) == OB_INVALID_ARGUMENT);
  ObArenaAllocator raw_copy_arena;
  ObRawExprFactory raw_copy_factory(raw_copy_arena);
  ObRawExpr *raw_copy = nullptr;
  CHECK(ObPLExprCopier::copy_expr(raw_copy_factory, outer, raw_copy) == OB_SUCCESS);
  const auto &copied_wire = static_cast<const ObConstRawExpr *>(raw_copy->get_param_expr(0))->get_value();
  CHECK(copied_wire.is_varbinary() && copied_wire.get_string().ptr() != fixed_wire.ptr());
  CHECK(PluginFunctionExpr::resolve_raw_binding(*raw_copy, binding, arguments) == OB_SUCCESS);
  CHECK(binding.catalog_epoch == 11 && provider.resolves_ == bound_resolves);

  ObExpr runtime_inner, runtime_outer;
  ObExprCGCtx cg(allocator, nullptr, nullptr);
  CHECK(op.cg_expr(cg, *outer, runtime_outer) == OB_STATE_NOT_MATCH);
  outer->set_result_type(result_type);
  CHECK(op.cg_expr(cg, *inner, runtime_inner) == OB_SUCCESS);
  CHECK(op.cg_expr(cg, *outer, runtime_outer) == OB_SUCCESS);
  CHECK(provider.resolves_ == bound_resolves);
  auto *info = dynamic_cast<PluginFunctionExtraInfo *>(runtime_outer.extra_info_);
  CHECK(info && info->binding(binding) == OB_SUCCESS);
  CHECK(info->arguments().at(0) == ObString::make_string(CUSTOM));
  CHECK(std::strcmp(binding.object_id, "org.test.consume.typed") == 0);
  ObArenaAllocator copied_allocator, decoded_allocator;
  ObIExprExtraInfo *copy_base = nullptr;
  CHECK(info->deep_copy(copied_allocator, T_FUN_SYS_PLUGIN_FUNCTION, copy_base) == OB_SUCCESS);
  auto *copy = dynamic_cast<PluginFunctionExtraInfo *>(copy_base);
  CHECK(copy && copy->arguments().at(0).ptr() != info->arguments().at(0).ptr());
  CHECK(copy->binding(binding) == OB_SUCCESS && binding.owner_generation == 7);
  PluginFunctionExtraInfo rejected(copied_allocator, T_FUN_SYS_PLUGIN_FUNCTION);
  CHECK(rejected.initialize(binding, {CUSTOM}) == OB_SUCCESS);
  auto invalid = binding;
  invalid.owner_generation = 0;
  CHECK(rejected.initialize(invalid, {CUSTOM}) != OB_SUCCESS);
  CHECK(rejected.binding(invalid) != OB_SUCCESS);
  invalid = binding; invalid.catalog_epoch = 0;
  CHECK(rejected.initialize(invalid, {CUSTOM}) != OB_SUCCESS);
  invalid = binding; invalid.result_type_id[0] = 0;
  CHECK(rejected.initialize(invalid, {CUSTOM}) != OB_SUCCESS);
  CHECK(rejected.initialize(binding, {CUSTOM}) == OB_SUCCESS);
  invalid = binding; invalid.kind = SEEKDB_PLUGIN_EXTENSION_TYPE;
  CHECK(rejected.initialize(invalid, {CUSTOM}) != OB_SUCCESS);
  CHECK(rejected.binding(invalid) != OB_SUCCESS); // Failed reinit cannot retain old binding.
  CHECK(rejected.initialize(binding, {std::string("bad\0id", 6)}) != OB_SUCCESS);
  CHECK(rejected.initialize(binding, {}) != OB_SUCCESS);
  PluginFunctionExtraInfo decoded(decoded_allocator, T_FUN_SYS_PLUGIN_FUNCTION);
  std::vector<char> wire(info->get_serialize_size());
  int64_t pos = 0;
  CHECK(info->serialize(wire.data(), wire.size(), pos) == OB_SUCCESS && pos == int64_t(wire.size()));
  pos = 0;
  CHECK(decoded.deserialize(wire.data(), wire.size(), pos) == OB_SUCCESS && pos == int64_t(wire.size()));
  std::fill(wire.begin(), wire.end(), 'x');
  CHECK(decoded.binding(binding) == OB_SUCCESS && std::strcmp(binding.object_id, "org.test.consume.typed") == 0);
  CHECK(decoded.arguments().at(0) == ObString::make_string(CUSTOM));
  CHECK(std::strcmp(decoded.arguments().at(0).ptr(), CUSTOM) == 0);
  CHECK(std::strcmp(copy->arguments().at(0).ptr(), CUSTOM) == 0);
  pos = 0;
  CHECK(info->serialize(wire.data(), wire.size(), pos) == OB_SUCCESS);
  for (size_t size = 0; size < wire.size(); ++size) {
    PluginFunctionExtraInfo truncated(decoded_allocator, T_FUN_SYS_PLUGIN_FUNCTION);
    pos = 0;
    CHECK(truncated.deserialize(wire.data(), size, pos) != OB_SUCCESS);
    CHECK(truncated.binding(binding) != OB_SUCCESS);
  }

  // Small, explicit expression frames. Names/text are constant runtime nodes;
  // nested nodes use the actual PluginFunctionExpr::evaluate callback.
  ObExecContext execution(allocator);
  CHECK(execution.init_expr_op(2) == OB_SUCCESS);
  ObEvalCtx eval(execution);
  alignas(16) char frames_data[5][256] = {};
  char *frames[5];
  ObExpr name_inner, name_outer, value;
  ObExpr *nodes[] = {&name_inner, &name_outer, &value, &runtime_inner, &runtime_outer};
  for (uint32_t i = 0; i < 5; ++i) {
    frames[i] = frames_data[i]; nodes[i]->frame_idx_ = i;
    nodes[i]->datum_off_ = 0; nodes[i]->eval_info_off_ = 64;
    nodes[i]->res_buf_off_ = 128; nodes[i]->res_buf_len_ = 128;
    nodes[i]->datum_meta_.type_ = i == 4 ? ObIntType : ObVarcharType;
    new (frames[i]) ObDatum(); new (frames[i] + 64) ObEvalInfo();
  }
  eval.frames_ = frames;
  name_inner.locate_expr_datum(eval).set_string(ObString::make_string("construct"));
  name_outer.locate_expr_datum(eval).set_string(ObString::make_string("consume"));
  value.locate_expr_datum(eval).set_string(ObString::make_string("hello"));
  ObExpr *inner_args[] = {&name_inner, &value};
  ObExpr *outer_args[] = {&name_outer, &runtime_inner};
  runtime_inner.args_ = inner_args; runtime_inner.arg_cnt_ = 2; runtime_inner.expr_ctx_id_ = 0;
  runtime_outer.args_ = outer_args; runtime_outer.arg_cnt_ = 2; runtime_outer.expr_ctx_id_ = 1;
  runtime_outer.extra_info_ = &decoded; // Execute the independently decoded plan.
  const int resolves_before = provider.resolves_;
  ObDatum *output = nullptr;
  CHECK(runtime_outer.eval(eval, output) == OB_SUCCESS && output && output->get_int() == 5);
  CHECK(provider.calls_ == 2 && provider.resolves_ == resolves_before);
  // The cached plan restores its binding instead of re-resolving by bytes.
  for (int failure = 1; failure <= 4; ++failure) {
    provider.failure_ = failure;
    runtime_outer.get_eval_info(eval).evaluated_ = false;
    CHECK(runtime_outer.eval(eval, output) == OB_INVALID_DATA);
    CHECK(provider.resolves_ == resolves_before);
  }
  provider.failure_ = 0;
  // Controlled physical integer child for the narrow logical ABI aliases.
  auto *inner_info = runtime_inner.extra_info_;
  runtime_inner.extra_info_ = nullptr;
  runtime_inner.datum_meta_.type_ = ObIntType;
  const char *narrow[] = {"core.type.bool", "core.type.int32", "org.seekdb.gis.scalar.uint32"};
  for (const char *id : narrow) {
    CHECK(info->binding(binding) == OB_SUCCESS);
    CHECK(decoded.initialize(binding, {id}) == OB_SUCCESS);
    provider.expected_argument_ = id;
    runtime_inner.locate_expr_datum(eval).set_int(1);
    runtime_outer.get_eval_info(eval).evaluated_ = false;
    CHECK(runtime_outer.eval(eval, output) == OB_SUCCESS);
    CHECK(output->get_int() == (std::strcmp(id, "core.type.bool") == 0 ? 1 : 4));
  }
  runtime_inner.extra_info_ = inner_info;
  runtime_inner.datum_meta_.type_ = ObVarcharType;
  CHECK(decoded.initialize(binding, {CUSTOM}) == OB_SUCCESS);
  provider.expected_argument_ = CUSTOM;
  runtime_inner.get_eval_info(eval).evaluated_ = false;
  runtime_outer.get_eval_info(eval).evaluated_ = false;
  value.locate_expr_datum(eval).set_null();
  CHECK(runtime_outer.eval(eval, output) == OB_SUCCESS && output->is_null());
  copy->~PluginFunctionExtraInfo();
  info->~PluginFunctionExtraInfo(); runtime_outer.extra_info_ = nullptr;
  static_cast<PluginFunctionExtraInfo *>(runtime_inner.extra_info_)->~PluginFunctionExtraInfo();
  runtime_inner.extra_info_ = nullptr;
}
} // namespace plugin_expression_test
#endif
