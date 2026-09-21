// Copyright (c) 2026 OceanBase. Licensed under the Apache License, Version 2.0.
#define USING_LOG_PREFIX SQL_ENG
#include "sql/engine/basic/plugin_custom_op.h"
#include "sql/engine/ob_exec_context.h"
#include "sql/engine/expr/ob_expr_lob_utils.h"
#include "sql/engine/expr/plugin_function_expr.h"
#include "sql/resolver/expr/plugin_expr_type.h"
#include "share/rc/ob_module_provider.h"
#include "rust/plugin-runtime/include/plugin_runtime.h"
#include <array>
#include <cmath>
#include <cstring>
#include <limits>
#include <vector>
namespace oceanbase { namespace sql {
using namespace common;
using share::plugin::CustomExecutorBinding;
static int input_state_status(int32_t status)
{
  switch (status) {
    case SEEKDB_RUNTIME_OK: return OB_SUCCESS;
    case SEEKDB_RUNTIME_INVALID: case SEEKDB_RUNTIME_DEPENDENCY_CYCLE: return OB_INVALID_ARGUMENT;
    case SEEKDB_RUNTIME_STATE_MISMATCH: return OB_STATE_NOT_MATCH;
    case SEEKDB_RUNTIME_NO_MEMORY: return OB_ALLOCATE_MEMORY_FAILED;
    case SEEKDB_RUNTIME_LIMIT: return OB_SIZE_OVERFLOW;
    default: return OB_ERR_UNEXPECTED;
  }
}
OB_SERIALIZE_MEMBER((PluginCustomSpec, ObOpSpec), columns_, type_ids_, nullable_, service_, owner_, incarnation_,
                    parameters_, generation_, major_, minor_, patch_, codecs_, explicit_input_,
                    input_columns_, input_type_ids_, input_nullable_, input_codecs_, input_offsets_, input_bindings_, binding_sources_,
                    binding_inputs_, binding_targets_);
static int bind_custom_codec(ObIAllocator &allocator, const ExprFixedArray &columns_,
    ObFixedArray<ObString, ObIAllocator> &codecs_, uint32_t column, const PluginExprType &type)
try {
  if (!share::g_mp || column >= columns_.count() || !type.stored_ ||
      !ob_is_valid_obj_type(type.physical_type_) || !ob_is_string_type(type.physical_type_) || type.sql_name_.length() <= 0 ||
      type.sql_name_.length() > SEEKDB_PLUGIN_MAX_IDENTIFIER_BYTES || !type.sql_name_.ptr() ||
      std::memchr(type.sql_name_.ptr(), 0, type.sql_name_.length())) return OB_INVALID_ARGUMENT;
  PluginStoredArgument stored; // One standalone type binding, not a SQL argument ordinal.
  const std::string name(type.sql_name_.ptr(), type.sql_name_.length());
  int ret = share::g_mp->resolve_plugin_sql_object(SEEKDB_PLUGIN_EXTENSION_TYPE,
      name.c_str(), nullptr, 0, &stored.binding_);
  if (ret != OB_SUCCESS) return ret;
  const auto &b = stored.binding_;
  if (!stored.valid() || type.logical_id_ != ObString::make_string(b.object_id) ||
      type.owner_ != ObString::make_string(b.owner_plugin_id) ||
      type.format_ != ObString::make_string(b.physical_format_id) || type.format_version_ != b.physical_format_version ||
      (type.catalog_epoch_ && type.catalog_epoch_ != b.catalog_epoch)) return OB_STATE_NOT_MATCH;
  if (codecs_.empty() && OB_FAIL(codecs_.prepare_allocate(columns_.count()))) return ret;
  if (codecs_.count() != columns_.count()) return OB_INVALID_DATA;
  const int64_t length = stored.get_serialize_size();
  if (length <= 0 || length > 4096) return OB_INVALID_DATA;
  char *bytes = static_cast<char *>(allocator.alloc(length));
  if (!bytes) return OB_ALLOCATE_MEMORY_FAILED;
  int64_t position = 0;
  if (OB_FAIL(stored.serialize(bytes, length, position))) return ret;
  if (position != length) return OB_ERR_UNEXPECTED;
  codecs_.at(column).assign_ptr(bytes, length);
  return OB_SUCCESS;
} catch (const std::bad_alloc &) { return OB_ALLOCATE_MEMORY_FAILED;
} catch (...) { return OB_ERR_UNEXPECTED; }
int PluginCustomSpec::bind_stored_column(ObIAllocator &allocator, uint32_t column, const PluginExprType &type)
{ return bind_custom_codec(allocator, columns_, codecs_, column, type); }
int PluginCustomSpec::bind_stored_input(ObIAllocator &allocator, uint32_t column, const PluginExprType &type)
{ return explicit_input_ ? bind_custom_codec(allocator, input_columns_, input_codecs_, column, type) : OB_INVALID_ARGUMENT; }

namespace {
// The logical builtin ID defines the wire width, not the SQL Datum width.
// User-defined IDs keep their physical representation; never suffix-match them.
enum class CustomNumber {
  BYTES = SEEKDB_PLUGIN_CUSTOM_ENCODING_BYTES, NULL_VALUE = SEEKDB_PLUGIN_CUSTOM_ENCODING_NULL,
  BOOL = SEEKDB_PLUGIN_CUSTOM_ENCODING_BOOL, I32 = SEEKDB_PLUGIN_CUSTOM_ENCODING_INT32,
  U32 = SEEKDB_PLUGIN_CUSTOM_ENCODING_UINT32, I64 = SEEKDB_PLUGIN_CUSTOM_ENCODING_INT64,
  U64 = SEEKDB_PLUGIN_CUSTOM_ENCODING_UINT64, F64 = SEEKDB_PLUGIN_CUSTOM_ENCODING_FLOAT64
};
bool custom_builtin(const std::string &id, const char *name)
{
  return id == std::string("core.type.") + name || id == std::string("org.seekdb.gis.scalar.") + name;
}
CustomNumber custom_number(ObObjType type, const std::string &id)
{
  if (custom_builtin(id, "bool")) return CustomNumber::BOOL;
  if (custom_builtin(id, "int32")) return CustomNumber::I32;
  if (custom_builtin(id, "uint32")) return CustomNumber::U32;
  if (ob_is_int_tc(type)) return CustomNumber::I64;
  if (ob_is_uint_tc(type)) return CustomNumber::U64;
  if (ob_is_float_type(type) || ob_is_double_type(type)) return CustomNumber::F64;
  return type == ObNullType ? CustomNumber::NULL_VALUE : CustomNumber::BYTES;
}
bool valid_custom_number(ObObjType type, const std::string &id)
{
  if (custom_builtin(id, "bool") || custom_builtin(id, "int32") || custom_builtin(id, "uint32"))
    return ob_is_integer_type(type);
  if (custom_builtin(id, "int64")) return ob_is_int_tc(type);
  if (custom_builtin(id, "uint64")) return ob_is_uint_tc(type);
  if (custom_builtin(id, "float64")) return ob_is_float_type(type) || ob_is_double_type(type);
  if (id == "core.type.null") return type == ObNullType;
  if (id == "core.type.bytes") return ob_is_string_type(type);
  return true;
}
int custom_integer_bits(ObObjType type)
{
  switch (type) {
    case ObTinyIntType: case ObUTinyIntType: return 8;
    case ObSmallIntType: case ObUSmallIntType: return 16;
    case ObMediumIntType: case ObUMediumIntType: return 24;
    case ObInt32Type: case ObUInt32Type: return 32;
    default: return 64;
  }
}
template <typename T> T custom_load_number(const uint8_t *bytes)
{
  T value; std::memcpy(&value, bytes, sizeof(value)); return value;
}
// Validate without a Datum before any output column is published. Only publish
// supplies an expression-backed Datum (a default Datum has no numeric storage).
// memcpy handles unaligned plugin buffers without typed pointer casts.
int decode_custom_number(CustomNumber wire, const uint8_t *bytes, uint64_t size,
                         ObObjType type, ObDatum *out = nullptr)
{
  const uint64_t width = wire == CustomNumber::BOOL ? 1 :
      wire == CustomNumber::I32 || wire == CustomNumber::U32 ? 4 : 8;
  if (size != width || !bytes) return OB_INVALID_DATA;
  if (wire == CustomNumber::F64) {
    const double value = custom_load_number<double>(bytes);
    if (((type == ObUFloatType || type == ObUDoubleType) && value < 0) ||
        (ob_is_float_type(type) && std::isfinite(value) &&
         std::abs(value) > std::numeric_limits<float>::max())) return OB_DATA_OUT_OF_RANGE;
    if (out) {
      if (ob_is_float_type(type)) out->set_float(static_cast<float>(value));
      else out->set_double(value);
    }
    return OB_SUCCESS;
  }
  const bool signed_wire = wire == CustomNumber::I32 || wire == CustomNumber::I64;
  const int64_t signed_value = wire == CustomNumber::I32 ? custom_load_number<int32_t>(bytes) :
      wire == CustomNumber::I64 ? custom_load_number<int64_t>(bytes) : 0;
  const uint64_t unsigned_value = wire == CustomNumber::BOOL ? bytes[0] :
      wire == CustomNumber::U32 ? custom_load_number<uint32_t>(bytes) :
      wire == CustomNumber::U64 ? custom_load_number<uint64_t>(bytes) : static_cast<uint64_t>(signed_value);
  if (wire == CustomNumber::BOOL && unsigned_value > 1) return OB_INVALID_DATA;
  const int bits = custom_integer_bits(type);
  if (ob_is_int_tc(type)) {
    const int64_t maximum = bits == 64 ? INT64_MAX : (int64_t(1) << (bits - 1)) - 1;
    if ((signed_wire && (signed_value < -maximum - 1 || signed_value > maximum)) ||
        (!signed_wire && unsigned_value > static_cast<uint64_t>(maximum))) return OB_DATA_OUT_OF_RANGE;
    if (out) out->set_int(signed_wire ? signed_value : static_cast<int64_t>(unsigned_value));
  } else {
    const uint64_t maximum = bits == 64 ? UINT64_MAX : (uint64_t(1) << bits) - 1;
    if ((signed_wire && signed_value < 0) || unsigned_value > maximum) return OB_DATA_OUT_OF_RANGE;
    if (out) out->set_uint(unsigned_value);
  }
  return OB_SUCCESS;
}
int encode_custom_number(CustomNumber wire, const ObDatum &datum, ObObjType type,
                         uint8_t *bytes, uint64_t &size)
{
  const auto copy = [&](const auto value) {
    size = sizeof(value); std::memcpy(bytes, &value, sizeof(value));
  };
  if (wire == CustomNumber::F64) {
    copy(ob_is_float_type(type) ? static_cast<double>(datum.get_float()) : datum.get_double());
  } else {
    const bool negative = ob_is_int_tc(type) && datum.get_int() < 0;
    const uint64_t value = ob_is_int_tc(type) ? static_cast<uint64_t>(datum.get_int()) : datum.get_uint();
    if (wire == CustomNumber::BOOL) {
      if (negative || value > 1) return OB_DATA_OUT_OF_RANGE;
      copy(static_cast<uint8_t>(value));
    } else if (wire == CustomNumber::I32) {
      if (negative ? datum.get_int() < INT32_MIN : value > INT32_MAX) return OB_DATA_OUT_OF_RANGE;
      copy(static_cast<int32_t>(negative ? datum.get_int() : static_cast<int64_t>(value)));
    } else if (wire == CustomNumber::U32) {
      if (negative || value > UINT32_MAX) return OB_DATA_OUT_OF_RANGE;
      copy(static_cast<uint32_t>(value));
    } else if (wire == CustomNumber::I64) copy(datum.get_int());
    else copy(value);
  }
  return decode_custom_number(wire, bytes, size, type);
}
struct CustomCodecSink {
  ObIAllocator &memory;
  const char *type;
  uint64_t limit;
  bool nullable;
  ObString bytes;
  bool emitted = false, null = false;
  int error = OB_SUCCESS;
  static seekdb_plugin_status_t SEEKDB_PLUGIN_CALL emit(seekdb_plugin_host_handle_t *host,
      const seekdb_plugin_execution_result_v1_t *value)
  {
    if (!host) return SEEKDB_PLUGIN_STATUS_INVALID_ARGUMENT;
    auto &sink = *reinterpret_cast<CustomCodecSink *>(host);
    if (sink.error != OB_SUCCESS) return SEEKDB_PLUGIN_STATUS_INVALID_ARGUMENT;
    try {
      if (sink.emitted || !value || value->struct_size < sizeof(*value) || !value->type_id ||
          std::strncmp(value->type_id, sink.type, SEEKDB_PLUGIN_MAX_IDENTIFIER_BYTES + 1) ||
          value->is_null > 1 || (value->is_null && (!sink.nullable || value->data || value->data_size)) ||
          (value->data_size && !value->data)) sink.error = OB_INVALID_DATA;
      if (sink.error == OB_SUCCESS) {
        for (auto word : value->reserved) if (word) sink.error = OB_INVALID_DATA;
        for (auto byte : value->reserved_bytes) if (byte) sink.error = OB_INVALID_DATA;
      }
      if (sink.error == OB_SUCCESS && value->data_size > sink.limit) sink.error = OB_SIZE_OVERFLOW;
      if (sink.error == OB_SUCCESS) {
        sink.emitted = true; sink.null = value->is_null;
        if (value->data_size) {
          char *copy = static_cast<char *>(sink.memory.alloc(value->data_size));
          if (!copy) sink.error = OB_ALLOCATE_MEMORY_FAILED;
          else { std::memcpy(copy, value->data, value->data_size); sink.bytes.assign_ptr(copy, value->data_size); }
        }
      }
    } catch (const std::bad_alloc &) { sink.error = OB_ALLOCATE_MEMORY_FAILED;
    } catch (...) { sink.error = OB_ERR_UNEXPECTED; }
    return sink.error == OB_SUCCESS ? SEEKDB_PLUGIN_STATUS_OK :
        sink.error == OB_ALLOCATE_MEMORY_FAILED ? SEEKDB_PLUGIN_STATUS_NO_MEMORY : SEEKDB_PLUGIN_STATUS_INVALID_ARGUMENT;
  }
};
int convert_custom_value(const PluginStoredArgument &codec, bool encode, const ObString &input,
    CustomCodecSink &sink)
{
  seekdb_plugin_execution_context_v1_t context = {};
  context.struct_size = sizeof(context); context.host = reinterpret_cast<seekdb_plugin_host_handle_t *>(&sink);
  context.emit_result = CustomCodecSink::emit;
  seekdb_plugin_execution_value_v1_t value = {};
  value.struct_size = sizeof(value); value.type_id = codec.binding_.object_id;
  value.data = reinterpret_cast<const uint8_t *>(input.ptr()); value.data_size = input.length();
  const int ret = encode ? share::g_mp->encode_bound_plugin_type(&codec.binding_, &context, &value) :
      share::g_mp->decode_bound_plugin_type(&codec.binding_, &context, value.data, value.data_size);
  if (sink.error != OB_SUCCESS) return sink.error; // Retain even a swallowed emit failure.
  if (ret != OB_SUCCESS) return ret;
  return sink.emitted ? OB_SUCCESS : OB_INVALID_DATA;
}
}
int PluginCustomSpec::set_binding(ObIAllocator &allocator, const CustomExecutorBinding &b,
                                 const std::string &parameters)
{
  int ret = OB_SUCCESS;
  const std::string *source[] = {&b.service_id, &b.owner_id, &b.runtime_incarnation, &parameters};
  ObString *dest[] = {&service_, &owner_, &incarnation_, &parameters_};
  for (int i = 0; ret == OB_SUCCESS && i < 4; ++i)
    ret = ob_write_string(allocator, ObString(source[i]->size(), source[i]->data()), *dest[i]);
  generation_ = b.generation; major_ = b.major; minor_ = b.minor; patch_ = b.patch;
  return ret;
}
struct CustomLayout {
  std::vector<std::string> ids;
  std::vector<CustomNumber> number_types;
  std::vector<seekdb_plugin_custom_column_v1_t> columns;
  seekdb_plugin_custom_schema_v1_t schema = {};
  std::vector<std::unique_ptr<PluginStoredArgument>> codecs;
  bool has_codecs = false;
  int initialize(const ExprFixedArray &columns, const ObFixedArray<ObString, ObIAllocator> &type_ids,
      const ObFixedArray<uint8_t, ObIAllocator> &nullable, const ObFixedArray<ObString, ObIAllocator> &codec_blobs,
      int64_t begin = 0, int64_t count = -1);
};
struct PluginCustomOp::State : CustomLayout {
  struct Cell { bool null = false; std::vector<uint8_t> data; };
  std::unique_ptr<share::plugin::ICustomExecutor> cursor;
  seekdb_runtime_input_state *input_state = nullptr;
  ~State() { seekdb_runtime_input_state_destroy(input_state); }
  std::vector<std::unique_ptr<CustomLayout>> input_layouts;
  std::vector<seekdb_plugin_custom_schema_v1_t> input_schemas;
  std::vector<uint32_t> input_begins;
  const CustomLayout &input_description(uint32_t index) const {
    if (!input_layouts.empty()) return *input_layouts[index];
    return *this;
  }
  std::vector<seekdb_plugin_execution_value_v1_t> inputs;
  std::vector<std::array<uint8_t, 8>> numbers;
  std::vector<Cell> pending;
  // Out-of-row payloads must outlive read_input(), but not the next input
  // callback. Reset the whole row arena, not per-column high-water buffers.
  ObArenaAllocator input_memory{"PluginCustRow"};
  std::vector<std::unique_ptr<ObArenaAllocator>> binding_memory;
  std::vector<uint64_t> binding_bytes;
  std::vector<ObDatum> binding_values;
  bool emitted = false;
  bool failed = false;
};
PluginCustomOp::PluginCustomOp(ObExecContext &ctx, const ObOpSpec &spec, ObOpInput *input)
    : ObOperator(ctx, spec, input) {}
PluginCustomOp::~PluginCustomOp() = default;
bool PluginCustomOp::supported_column(const ObExpr &expr)
{
  const auto type = expr.datum_meta_.type_;
  return ob_is_valid_obj_type(type) && (ob_is_integer_type(type) || ob_is_float_type(type) ||
      ob_is_double_type(type) || type == ObNullType || ob_is_string_type(type));
}
int CustomLayout::initialize(const ExprFixedArray &expressions, const ObFixedArray<ObString, ObIAllocator> &type_ids,
    const ObFixedArray<uint8_t, ObIAllocator> &nullable, const ObFixedArray<ObString, ObIAllocator> &codec_blobs,
    int64_t begin, int64_t count)
{
  if (count == -1) count = expressions.count();
  if (expressions.count() != type_ids.count() || expressions.count() != nullable.count() ||
      (!codec_blobs.empty() && expressions.count() != codec_blobs.count()) ||
      begin < 0 || count < 0 || begin > expressions.count() || count > expressions.count() - begin ||
      count > SEEKDB_PLUGIN_CUSTOM_MAX_COLUMNS)
    return OB_INVALID_ARGUMENT;
  codecs.resize(count);
  for (int64_t i = 0; i < count; ++i) {
    const int64_t slot = begin + i;
    const auto *expr = expressions.at(slot);
    const auto &id = type_ids.at(slot);
    if (!expr || !PluginCustomOp::supported_column(*expr) || nullable.at(slot) > 1 || id.length() <= 0 || id.length() > SEEKDB_PLUGIN_MAX_IDENTIFIER_BYTES ||
        !id.ptr() || std::memchr(id.ptr(), 0, id.length())) return OB_NOT_SUPPORTED;
    ids.emplace_back(id.ptr(), id.length());
    if (!valid_custom_number(expr->datum_meta_.type_, ids.back())) return OB_INVALID_DATA;
    number_types.push_back(custom_number(expr->datum_meta_.type_, ids.back()));
    if (!codec_blobs.empty() && !codec_blobs.at(slot).empty()) {
      if (!ob_is_string_type(expr->datum_meta_.type_)) return OB_INVALID_DATA;
      const auto &bytes = codec_blobs.at(slot);
      if (!bytes.ptr() || bytes.length() < 0 || bytes.length() > 4096) return OB_INVALID_DATA;
      auto codec = std::make_unique<PluginStoredArgument>();
      int64_t position = 0;
      const int status = codec->deserialize(bytes.ptr(), bytes.length(), position);
      if (status != OB_SUCCESS || position != bytes.length() || !codec->valid() || codec->index_ ||
          id != ObString::make_string(codec->binding_.object_id)) return OB_INVALID_DATA;
      codecs[i] = std::move(codec);
      has_codecs = true;
    }
    seekdb_plugin_custom_column_v1_t column = {};
    column.struct_size = sizeof(column);
    column.flags = (nullable.at(slot) ? SEEKDB_PLUGIN_CUSTOM_COLUMN_NULLABLE : 0) |
        (codecs[i] ? SEEKDB_PLUGIN_CUSTOM_COLUMN_STORED : 0);
    column.encoding = static_cast<uint32_t>(number_types.back());
    const auto &meta = expr->datum_meta_;
    column.sql_type = meta.type_; column.collation = meta.cs_type_;
    // For strings the precision slot is a union containing length semantics.
    const bool numeric = ob_is_integer_type(meta.type_) || ob_is_float_type(meta.type_) || ob_is_double_type(meta.type_);
    column.precision = numeric ? meta.precision_ : -1; column.scale = numeric ? meta.scale_ : -1;
    std::memcpy(column.type_id, id.ptr(), id.length());
    columns.push_back(column);
  }
  schema = {sizeof(schema), static_cast<uint32_t>(columns.size()), columns.data(), {0}};
  return OB_SUCCESS;
}
int PluginCustomOp::inner_open()
try {
  if (!share::g_mp || !ctx_.get_my_session() || child_cnt_ > SEEKDB_PLUGIN_CUSTOM_MAX_INPUTS ||
      (child_cnt_ && !children_) || state_ ||
      MY_SPEC.parameters_.length() < 0 || MY_SPEC.parameters_.length() > SEEKDB_PLUGIN_CUSTOM_MAX_PLAN_BYTES ||
      (!MY_SPEC.explicit_input_ && (!MY_SPEC.input_columns_.empty() || !MY_SPEC.input_type_ids_.empty() ||
          !MY_SPEC.input_nullable_.empty() || !MY_SPEC.input_codecs_.empty() || !MY_SPEC.input_offsets_.empty()))) return OB_INVALID_ARGUMENT;
  const auto &offsets = MY_SPEC.input_offsets_;
  if (offsets.empty()) {
    if (child_cnt_ != 1) return OB_INVALID_ARGUMENT;
  } else {
    if (!MY_SPEC.explicit_input_ || offsets.count() != child_cnt_ + 1 || offsets.at(0) != 0 ||
        offsets.at(child_cnt_) != MY_SPEC.input_columns_.count()) return OB_INVALID_ARGUMENT;
    for (uint32_t i = 0; i < child_cnt_; ++i)
      if (offsets.at(i + 1) < offsets.at(i) || offsets.at(i + 1) - offsets.at(i) > SEEKDB_PLUGIN_CUSTOM_MAX_COLUMNS)
        return OB_INVALID_ARGUMENT;
  }
  for (uint32_t i = 0; i < child_cnt_; ++i) {
    if (!children_[i]) return OB_INVALID_ARGUMENT;
    for (uint32_t j = 0; j < i; ++j) if (children_[i] == children_[j]) return OB_INVALID_ARGUMENT;
  }
  if (MY_SPEC.input_bindings_.count() != MY_SPEC.binding_sources_.count() ||
      MY_SPEC.input_bindings_.count() != MY_SPEC.binding_inputs_.count() ||
      MY_SPEC.input_bindings_.count() != MY_SPEC.binding_targets_.count() || MY_SPEC.input_bindings_.count() > 1024)
    return OB_INVALID_ARGUMENT;
  if (!MY_SPEC.input_bindings_.empty()) {
    if (offsets.empty() || !ctx_.get_physical_plan_ctx()) return OB_INVALID_ARGUMENT;
    const auto count = ctx_.get_physical_plan_ctx()->get_param_store().count();
    for (int64_t i = 0; i < MY_SPEC.input_bindings_.count(); ++i) {
      const auto &binding = MY_SPEC.input_bindings_.at(i);
      const auto source = MY_SPEC.binding_sources_.at(i);
      const auto input = MY_SPEC.binding_inputs_.at(i), target = MY_SPEC.binding_targets_.at(i);
      if (input >= child_cnt_ || target >= child_cnt_ || input == target ||
          source < offsets.at(input) || source >= offsets.at(input + 1) ||
          !binding.src_ || !binding.dst_ || binding.param_idx_ < 0 ||
          binding.param_idx_ >= count || binding.src_ != MY_SPEC.input_columns_.at(source)) return OB_INVALID_ARGUMENT;
      for (int64_t j = 0; j < i; ++j)
        if (MY_SPEC.input_bindings_.at(j).param_idx_ == binding.param_idx_ || MY_SPEC.input_bindings_.at(j).dst_ == binding.dst_)
          return OB_INVALID_ARGUMENT;
    }
  }
  if (MY_SPEC.explicit_input_ && (MY_SPEC.input_columns_.count() != MY_SPEC.input_type_ids_.count() ||
      MY_SPEC.input_columns_.count() != MY_SPEC.input_nullable_.count() ||
      (!MY_SPEC.input_codecs_.empty() && MY_SPEC.input_columns_.count() != MY_SPEC.input_codecs_.count()))) return OB_INVALID_ARGUMENT;
  auto state = std::make_unique<State>();
  int ret = state->initialize(MY_SPEC.columns_, MY_SPEC.type_ids_, MY_SPEC.nullable_, MY_SPEC.codecs_);
  if (ret != OB_SUCCESS) return ret;
  size_t largest_input = 0;
  for (uint32_t i = 0; i < child_cnt_; ++i) {
    const uint32_t begin = offsets.empty() ? 0 : offsets.at(i);
    state->input_begins.push_back(begin);
    if (MY_SPEC.explicit_input_) {
      auto layout = std::make_unique<CustomLayout>();
      const int64_t count = offsets.empty() ? MY_SPEC.input_columns_.count() : offsets.at(i + 1) - begin;
      ret = layout->initialize(MY_SPEC.input_columns_, MY_SPEC.input_type_ids_, MY_SPEC.input_nullable_, MY_SPEC.input_codecs_, begin, count);
      if (ret != OB_SUCCESS) return ret;
      state->input_layouts.push_back(std::move(layout));
    }
    const auto &description = state->input_description(i);
    state->input_schemas.push_back(description.schema);
    largest_input = std::max(largest_input, description.ids.size());
  }
  state->inputs.resize(largest_input); state->numbers.resize(largest_input);
  state->pending.resize(state->ids.size());
  state->binding_values.resize(MY_SPEC.input_bindings_.count());
  if (!MY_SPEC.input_bindings_.empty()) {
    state->binding_memory.resize(child_cnt_);
    state->binding_bytes.resize(child_cnt_);
    std::vector<seekdb_runtime_input_edge> edges;
    for (int64_t i = 0; i < MY_SPEC.input_bindings_.count(); ++i) {
      const auto source = MY_SPEC.binding_inputs_.at(i);
      edges.push_back({source, MY_SPEC.binding_targets_.at(i)});
      if (!state->binding_memory[source]) state->binding_memory[source] = std::make_unique<ObArenaAllocator>("PluginCustBind");
    }
    ret = input_state_status(seekdb_runtime_input_state_create(child_cnt_, edges.data(), edges.size(), &state->input_state));
    if (ret != OB_SUCCESS) return ret;
  }
  CustomExecutorBinding binding;
  const ObString *source[] = {&MY_SPEC.service_, &MY_SPEC.owner_, &MY_SPEC.incarnation_};
  std::string *dest[] = {&binding.service_id, &binding.owner_id, &binding.runtime_incarnation};
  for (int i = 0; i < 3; ++i) {
    if (!source[i]->ptr() || source[i]->length() <= 0 || source[i]->length() > SEEKDB_PLUGIN_MAX_IDENTIFIER_BYTES ||
        std::memchr(source[i]->ptr(), 0, source[i]->length())) return OB_INVALID_DATA;
    dest[i]->assign(source[i]->ptr(), source[i]->length());
  }
  binding.generation = MY_SPEC.generation_; binding.major = MY_SPEC.major_;
  binding.minor = MY_SPEC.minor_; binding.patch = MY_SPEC.patch_;
  ret = share::g_mp->open_plugin_custom_executor(binding,
      reinterpret_cast<const uint8_t *>(MY_SPEC.parameters_.ptr()), MY_SPEC.parameters_.length(), state->cursor);
  if (ret == OB_SUCCESS) state_ = std::move(state);
  return ret;
} catch (const std::bad_alloc &) { return OB_ALLOCATE_MEMORY_FAILED;
} catch (...) { return OB_ERR_UNEXPECTED; }
int PluginCustomOp::read_input(uint32_t input, seekdb_plugin_custom_row_v1_t &row)
{
  if (!state_ || input >= child_cnt_) return OB_INVALID_ARGUMENT;
  uint64_t ticket = 0;
  int ret = begin_input(SEEKDB_RUNTIME_INPUT_READ, input, ticket);
  if (ret != OB_SUCCESS) return ret;
  try { ret = read_input_values(input, row); }
  catch (const std::bad_alloc &) { ret = OB_ALLOCATE_MEMORY_FAILED; }
  catch (...) { ret = OB_ERR_UNEXPECTED; }
  return finish_input(ticket, ret == OB_SUCCESS ? SEEKDB_RUNTIME_INPUT_ROW :
      ret == OB_ITER_END ? SEEKDB_RUNTIME_INPUT_EOF : SEEKDB_RUNTIME_INPUT_ERROR, ret);
}
int PluginCustomOp::read_input_values(uint32_t input, seekdb_plugin_custom_row_v1_t &row)
{
  const auto &description = state_->input_description(input);
  const uint32_t begin = state_->input_begins[input];
  const auto &columns = MY_SPEC.explicit_input_ ? MY_SPEC.input_columns_ : MY_SPEC.columns_;
  const auto &nullable = MY_SPEC.explicit_input_ ? MY_SPEC.input_nullable_ : MY_SPEC.nullable_;
  state_->input_memory.reset();
  int ret = children_[input]->get_next_row(); // Core's row adapter also accepts vectorized children.
  if (ret != OB_SUCCESS) return ret;
  uint64_t total = 0;
  for (size_t i = 0; i < description.ids.size(); ++i) {
    const uint32_t slot = begin + i;
    auto &expr = *columns.at(slot);
    ObDatum *datum = nullptr;
    if (OB_FAIL(expr.eval(eval_ctx_, datum))) return ret;
    if (!datum) return OB_ERR_UNEXPECTED;
    auto &value = state_->inputs[i];
    value = {}; value.struct_size = sizeof(value); value.type_id = description.ids[i].c_str();
    value.is_null = datum->is_null();
    if (value.is_null && !nullable.at(slot)) return OB_INVALID_DATA;
    if (!value.is_null) {
      const auto type = expr.datum_meta_.type_;
      if (ob_is_integer_type(type) || ob_is_float_type(type) || ob_is_double_type(type)) {
        if (OB_FAIL(encode_custom_number(description.number_types[i], *datum, type,
            state_->numbers[i].data(), value.data_size))) return ret;
        value.data = state_->numbers[i].data();
      } else if (type == ObNullType) return OB_INVALID_DATA;
      else {
        // Encoded LOB content is temporary per column. Keeping all encoded
        // forms until the next row could multiply the limit when codecs shrink.
        ObArenaAllocator encoded_memory("PluginCustDec");
        const auto *codec = description.codecs[i].get();
        auto bytes = datum->get_string();
        if (is_lob_storage(type)) {
          // Delta locators carry a patch, not the materialized payload length.
          // Do not use that length to authorize an unbounded reconstruction.
          ObLobLocatorV2 locator(bytes, expr.obj_meta_.has_lob_header());
          if (expr.obj_meta_.has_lob_header() && !bytes.empty() && locator.is_delta_temp_lob())
            return OB_NOT_SUPPORTED;
          ObArenaAllocator temporary("PluginCustLob");
          ObTextStringIter iter(type, expr.datum_meta_.cs_type_, bytes, expr.obj_meta_.has_lob_header());
          int64_t length = 0;
          ObIAllocator *payload_memory = codec ? &encoded_memory : &state_->input_memory;
          if (OB_FAIL(ObTextStringHelper::build_text_iter(iter, ctx_, payload_memory, &temporary))) return ret;
          if (OB_FAIL(iter.get_byte_len(length))) return ret;
          // Validate logical payload length before fetching/allocating the
          // full out-of-row value; locator length is not payload length.
          if (length < 0 || static_cast<uint64_t>(length) > SEEKDB_PLUGIN_CUSTOM_MAX_ROW_BYTES - (codec ? 0 : total))
            return OB_SIZE_OVERFLOW;
          if (OB_FAIL(check_status())) return ret;
          if (OB_FAIL(iter.get_full_data(bytes))) return ret;
          if (bytes.length() != length) return OB_INVALID_DATA;
        }
        if (bytes.length() < 0) return OB_INVALID_DATA;
        if (codec) {
          if (bytes.length() > SEEKDB_PLUGIN_CUSTOM_MAX_ROW_BYTES) return OB_SIZE_OVERFLOW;
          if (OB_FAIL(check_status())) return ret;
          CustomCodecSink sink{state_->input_memory, description.ids[i].c_str(),
              SEEKDB_PLUGIN_CUSTOM_MAX_ROW_BYTES - total, bool(nullable.at(slot)), {}};
          if (OB_FAIL(convert_custom_value(*codec, false, bytes, sink))) return ret;
          if (OB_FAIL(check_status())) return ret;
          bytes = sink.bytes; value.is_null = sink.null;
        }
        value.data = reinterpret_cast<const uint8_t *>(bytes.ptr()); value.data_size = bytes.length();
      }
    }
    if (value.data_size > SEEKDB_PLUGIN_CUSTOM_MAX_ROW_BYTES - total) return OB_SIZE_OVERFLOW;
    total += value.data_size;
  }
  if (!MY_SPEC.input_bindings_.empty() && state_->binding_memory[input]) {
    uint64_t charged = 0;
    for (const auto bytes : state_->binding_bytes) charged += bytes;
    for (int64_t i = 0; i < MY_SPEC.input_bindings_.count(); ++i) {
      if (MY_SPEC.binding_inputs_.at(i) != input) continue;
      ObDatum *datum = nullptr;
      if (OB_FAIL(MY_SPEC.input_bindings_.at(i).src_->eval(eval_ctx_, datum))) return ret;
      if (!datum || datum->get_deep_copy_size() < 0 ||
          static_cast<uint64_t>(datum->get_deep_copy_size()) > SEEKDB_PLUGIN_CUSTOM_MAX_ROW_BYTES - charged)
        return OB_SIZE_OVERFLOW;
      const auto size = datum->get_deep_copy_size();
      charged += size;
      if (OB_FAIL(state_->binding_values[i].deep_copy(*datum, *state_->binding_memory[input]))) return ret;
      state_->binding_bytes[input] += size;
    }
  }
  row = {sizeof(row), static_cast<uint32_t>(description.ids.size()), state_->inputs.data(), {0}};
  return OB_SUCCESS;
}
int PluginCustomOp::receive(const seekdb_plugin_execution_value_v1_t *values, uint32_t count)
{
  if (state_->emitted || count != state_->pending.size()) return OB_INVALID_DATA;
  // Loader validates the ABI, sizes and pointers before entering this sink.
  // Validate SQL identity/representation before copying, and publish no Datum
  // until the whole plugin next callback has succeeded (including exit cancel).
  for (uint32_t i = 0; i < count; ++i) {
    const auto &value = values[i];
    const auto type = MY_SPEC.columns_.at(i)->datum_meta_.type_;
    if (state_->ids[i] != value.type_id || (value.is_null && !MY_SPEC.nullable_.at(i)) ||
        (!value.is_null && type == ObNullType))
      return OB_INVALID_DATA;
    if (!value.is_null && state_->number_types[i] != CustomNumber::BYTES) {
      const int ret = decode_custom_number(state_->number_types[i], value.data, value.data_size, type);
      if (ret != OB_SUCCESS) return ret;
    }
  }
  // Do not retain each column's historical maximum capacity. A sequence of
  // rows moving a wide payload across columns must not multiply the row limit
  // by the column count. Failed copies discard the entire provisional row.
  std::vector<State::Cell> pending(count);
  for (uint32_t i = 0; i < count; ++i) {
    auto &out = pending[i]; const auto &value = values[i];
    out.null = value.is_null;
    if (value.data_size) out.data.assign(value.data, value.data + value.data_size);
  }
  state_->pending.swap(pending);
  state_->emitted = true;
  return OB_SUCCESS;
}
int PluginCustomOp::publish()
{
  if (!state_->emitted) return OB_INVALID_DATA;
  // Finish all encoders before touching the SQL frame. The plugin's logical
  // output and the encoded SQL row each have their own aggregate size bound.
  ObArenaAllocator encoded_memory("PluginCustEnc");
  std::vector<ObString> output;
  std::vector<bool> nulls;
  uint64_t total = 0;
  if (state_->has_codecs) {
    output.resize(state_->pending.size()); nulls.resize(state_->pending.size());
  }
  for (size_t i = 0; state_->has_codecs && i < state_->pending.size(); ++i) {
    const auto &cell = state_->pending[i]; nulls[i] = cell.null;
    output[i] = ObString(cell.data.size(), reinterpret_cast<const char *>(cell.data.data()));
    if (state_->codecs[i] && !cell.null) {
      int ret = check_status();
      if (ret != OB_SUCCESS) return ret;
      CustomCodecSink sink{encoded_memory, "core.type.bytes", SEEKDB_PLUGIN_CUSTOM_MAX_ROW_BYTES - total,
          bool(MY_SPEC.nullable_.at(i)), {}};
      if (OB_FAIL(convert_custom_value(*state_->codecs[i], true, output[i], sink))) return ret;
      output[i] = sink.bytes; nulls[i] = sink.null;
    }
    if (output[i].length() > SEEKDB_PLUGIN_CUSTOM_MAX_ROW_BYTES - total) return OB_SIZE_OVERFLOW;
    total += output[i].length();
  }
  const int status = check_status();
  if (status != OB_SUCCESS) return status;
  clear_evaluated_flag();
  for (int64_t i = 0; i < MY_SPEC.columns_.count(); ++i) {
    auto &expr = *MY_SPEC.columns_.at(i); const auto &value = state_->pending[i];
    auto &datum = expr.locate_datum_for_write(eval_ctx_);
    if (state_->has_codecs ? nulls[i] : value.null) datum.set_null();
    else if (state_->number_types[i] != CustomNumber::BYTES) {
      const int ret = decode_custom_number(state_->number_types[i], value.data.data(), value.data.size(),
          expr.datum_meta_.type_, &datum);
      if (ret != OB_SUCCESS) return ret; // Already validated in receive().
    } else {
      // Plugins exchange content, never an internal locator/header. Rebuild
      // a temporary LOB in expression-owned memory when the SQL type needs it.
      const ObString bytes = state_->has_codecs ? output[i] :
          ObString(value.data.size(), reinterpret_cast<const char *>(value.data.data()));
      const int ret = ObTextStringHelper::string_to_templob_result(expr, eval_ctx_, datum, bytes);
      if (ret != OB_SUCCESS) return ret;
    }
    expr.set_evaluated_projected(eval_ctx_);
  }
  return OB_SUCCESS;
}
seekdb_plugin_status_t SEEKDB_PLUGIN_CALL PluginCustomOp::input(void *p, uint32_t index,
    seekdb_plugin_custom_row_v1_t *row, int32_t *error)
{
  *error = static_cast<PluginCustomOp *>(p)->read_input(index, *row);
  if (*error == OB_ITER_END) { *error = 0; return SEEKDB_PLUGIN_STATUS_END_OF_STREAM; }
  return *error ? SEEKDB_PLUGIN_STATUS_INTERNAL : SEEKDB_PLUGIN_STATUS_OK;
}
seekdb_plugin_status_t SEEKDB_PLUGIN_CALL PluginCustomOp::emit(void *p,
    const seekdb_plugin_execution_value_v1_t *values, uint32_t count, int32_t *error)
{
  *error = static_cast<PluginCustomOp *>(p)->receive(values, count);
  return *error ? SEEKDB_PLUGIN_STATUS_INTERNAL : SEEKDB_PLUGIN_STATUS_OK;
}
seekdb_plugin_status_t SEEKDB_PLUGIN_CALL PluginCustomOp::poll(void *p, int32_t *error)
{
  *error = static_cast<PluginCustomOp *>(p)->check_status();
  return *error ? SEEKDB_PLUGIN_STATUS_INTERNAL : SEEKDB_PLUGIN_STATUS_OK;
}
int PluginCustomOp::inner_get_next_row()
try {
  if (!state_) return OB_NOT_INIT;
  if (state_->failed) return OB_STATE_NOT_MATCH;
  state_->failed = true;
  clear_evaluated_flag(); state_->emitted = false;
  const seekdb_plugin_custom_context_v4_t view = {{{{sizeof(view), static_cast<uint32_t>(state_->input_schemas.size()),
      static_cast<uint32_t>(state_->ids.size()), 0, this, input, emit, poll, {0}},
      state_->input_schemas.data(), &state_->schema, {0}}, rewind, {0}}, bind_rewind, {0}};
  int ret = state_->cursor->next(view.v3.v2.v1);
  if (ret == OB_SUCCESS) ret = publish();
  if (ret == OB_SUCCESS || ret == OB_ITER_END) state_->failed = false;
  else clear_owned_parameters();
  return ret;
} catch (const std::bad_alloc &) { clear_owned_parameters(); return OB_ALLOCATE_MEMORY_FAILED;
} catch (...) { clear_owned_parameters(); return OB_ERR_UNEXPECTED; }
seekdb_plugin_status_t SEEKDB_PLUGIN_CALL PluginCustomOp::rewind(void *opaque, uint32_t index, int32_t *error)
{
  auto &self = *static_cast<PluginCustomOp *>(opaque);
  if (!self.state_ || index >= self.child_cnt_ || self.state_->emitted) {
    *error = OB_INVALID_ARGUMENT;
  } else {
    uint64_t ticket = 0;
    *error = self.begin_input(SEEKDB_RUNTIME_INPUT_RESCAN, index, ticket);
    if (*error == OB_SUCCESS) {
      try {
        *error = self.check_status();
        if (*error == OB_SUCCESS) {
          self.state_->input_memory.reset();
          *error = self.children_[index]->rescan();
        }
        if (*error == OB_SUCCESS) *error = self.check_status();
      } catch (const std::bad_alloc &) { *error = OB_ALLOCATE_MEMORY_FAILED;
      } catch (...) { *error = OB_ERR_UNEXPECTED; }
      *error = self.finish_input(ticket, *error == OB_SUCCESS ? SEEKDB_RUNTIME_INPUT_DONE : SEEKDB_RUNTIME_INPUT_ERROR, *error);
    }
  }
  return *error ? SEEKDB_PLUGIN_STATUS_INTERNAL : SEEKDB_PLUGIN_STATUS_OK;
}
int PluginCustomOp::begin_input(uint32_t operation, uint32_t input, uint64_t &ticket)
{
  ticket = 0;
  if (!state_->input_state) return OB_SUCCESS;
  seekdb_runtime_input_effect effect{};
  const int ret = input_state_status(seekdb_runtime_input_state_begin(state_->input_state, operation, input, &effect));
  invalidate_input_state(effect.rows, effect.bindings);
  if (ret == OB_SUCCESS) ticket = effect.ticket;
  return ret;
}
int PluginCustomOp::finish_input(uint64_t ticket, uint32_t outcome, int result)
{
  if (!ticket) return result;
  seekdb_runtime_input_effect effect{};
  const int ret = input_state_status(seekdb_runtime_input_state_finish(state_->input_state, ticket, outcome, &effect));
  invalidate_input_state(effect.rows, effect.bindings);
  // Preserve the original child/codec/parameter error, not a cleanup status.
  return result != OB_SUCCESS && result != OB_ITER_END ? result : ret != OB_SUCCESS ? ret : result;
}
void PluginCustomOp::invalidate_input_state(uint64_t rows, uint64_t bindings)
{
  if (!state_ || MY_SPEC.input_bindings_.empty()) return;
  auto *physical = ctx_.get_physical_plan_ctx();
  for (int64_t i = 0; i < MY_SPEC.input_bindings_.count(); ++i) {
    const auto &binding = MY_SPEC.input_bindings_.at(i);
    if (bindings & (uint64_t{1} << MY_SPEC.binding_targets_.at(i))) {
      ObDynamicParamSetter::clear_parent_evaluated_flag(eval_ctx_, *binding.dst_);
      binding.dst_->locate_expr_datum(eval_ctx_).set_null();
      if (physical && binding.param_idx_ >= 0 && binding.param_idx_ < physical->get_param_store().count())
        physical->get_param_store_for_update().at(binding.param_idx_).set_null();
    }
    if (rows & (uint64_t{1} << MY_SPEC.binding_inputs_.at(i))) state_->binding_values[i].set_null();
  }
  for (uint32_t i = 0; i < child_cnt_; ++i) if (rows & (uint64_t{1} << i)) {
    if (state_->binding_memory[i]) state_->binding_memory[i]->reset();
    state_->binding_bytes[i] = 0;
  }
}
void PluginCustomOp::clear_owned_parameters(bool reusable)
{
  if (!state_ || !state_->input_state) return;
  seekdb_runtime_input_effect effect{};
  const int ret = seekdb_runtime_input_state_reset(state_->input_state, reusable ? 1 : 0, &effect);
  OB_ASSERT(ret == SEEKDB_RUNTIME_OK);
  invalidate_input_state(effect.rows, effect.bindings);
}
seekdb_plugin_status_t SEEKDB_PLUGIN_CALL PluginCustomOp::bind_rewind(void *opaque, uint32_t input, int32_t *error)
{
  auto &self = *static_cast<PluginCustomOp *>(opaque);
  const auto &spec = static_cast<const PluginCustomSpec &>(self.spec_);
  *error = OB_SUCCESS;
  if (!self.state_ || input >= self.child_cnt_ || spec.input_bindings_.empty() || self.state_->emitted) *error = OB_INVALID_ARGUMENT;
  else {
    uint64_t ticket = 0;
    *error = self.begin_input(SEEKDB_RUNTIME_INPUT_BIND, input, ticket);
    if (*error == OB_SUCCESS) {
      try {
        *error = self.check_status();
        for (int64_t i = 0; *error == OB_SUCCESS && i < spec.input_bindings_.count(); ++i)
          if (spec.binding_targets_.at(i) == input)
            *error = spec.input_bindings_.at(i).update_dynamic_param(self.eval_ctx_, self.state_->binding_values[i]);
        if (*error == OB_SUCCESS) *error = self.children_[input]->rescan();
        if (*error == OB_SUCCESS) *error = self.check_status();
      } catch (const std::bad_alloc &) { *error = OB_ALLOCATE_MEMORY_FAILED;
      } catch (...) { *error = OB_ERR_UNEXPECTED; }
      *error = self.finish_input(ticket, *error == OB_SUCCESS ? SEEKDB_RUNTIME_INPUT_DONE : SEEKDB_RUNTIME_INPUT_ERROR, *error);
    }
  }
  return *error ? SEEKDB_PLUGIN_STATUS_INTERNAL : SEEKDB_PLUGIN_STATUS_OK;
}
int PluginCustomOp::inner_rescan()
{
  if (state_) state_->failed = true;
  int ret = ObOperator::inner_rescan();
  if (ret == OB_SUCCESS && state_) ret = state_->cursor->rescan();
  if (state_ && ret == OB_SUCCESS) state_->failed = false;
  return ret;
}
int PluginCustomOp::rescan()
{
  // Base rescan visits children before inner_rescan. A later child's failure
  // must not leave a previously buffered cursor usable after partial reset.
  if (state_) state_->failed = true;
  const int ret = ObOperator::rescan();
  clear_owned_parameters(ret == OB_SUCCESS);
  return ret;
}
int PluginCustomOp::get_next_row()
{
  // Check before the base's EOF shortcut as well as before cursor->next().
  return state_ && state_->failed ? OB_STATE_NOT_MATCH : ObOperator::get_next_row();
}
int PluginCustomOp::get_next_batch(const int64_t max_row_cnt, const ObBatchRows *&batch_rows)
{
  if (state_ && state_->failed) { batch_rows = nullptr; return OB_STATE_NOT_MATCH; }
  return ObOperator::get_next_batch(max_row_cnt, batch_rows);
}
int PluginCustomOp::inner_close()
{
  clear_owned_parameters();
  const int ret = state_ ? state_->cursor->close() : OB_SUCCESS;
  state_.reset(); return ret;
}
void PluginCustomOp::destroy()
{
  clear_owned_parameters();
  state_.reset(); // ExecContext calls destroy, not the placement object's destructor.
  ObOperator::destroy();
}
} }
