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

#define USING_LOG_PREFIX SQL_ENG

#include "sql/engine/expr/plugin_function_expr.h"

#include <cstring>
#include <limits>
#include <string>
#include <vector>

#include "share/rc/ob_module_provider.h"
#include "sql/engine/expr/ob_expr_lob_utils.h"
#include "sql/engine/ob_exec_context.h"
#include "sql/engine/expr/plugin_sql_context.h"
#include "sql/engine/expr/ob_expr_extra_info_factory.h"
#include "sql/resolver/expr/plugin_expr_type.h"
#include "sql/resolver/dml/ob_select_stmt.h"

namespace oceanbase
{
namespace sql
{

using namespace common;

namespace
{

const char *core_type_identifier(const ObObjType type)
{
  if (ob_is_geometry(type)) return "core.type.geometry";
  if (ob_is_integer_type(type)) return "core.type.int64";
  if (ob_is_double_type(type) || ob_is_float_type(type)) {
    return "core.type.float64";
  }
  if (ob_is_string_type(type)) return "core.type.bytes";
  if (ob_is_null(type)) return nullptr;
  return "core.type.bytes";
}

// Built-in wire representations and the existing GIS compatibility aliases.
// A user type named e.g. org.example.int64 is still an opaque user type.
bool is_builtin_type(const char *type_id, const char *suffix)
{
  if (nullptr == type_id || nullptr == suffix) return false;
  constexpr const char *core = "core.type";
  constexpr const char *gis = "org.seekdb.gis.scalar";
  return (std::strncmp(type_id, core, std::strlen(core)) == 0 &&
          std::strcmp(type_id + std::strlen(core), suffix) == 0) ||
         (std::strncmp(type_id, gis, std::strlen(gis)) == 0 &&
          std::strcmp(type_id + std::strlen(gis), suffix) == 0) ||
         (std::strcmp(suffix, ".geometry") == 0 &&
          std::strcmp(type_id, "org.seekdb.gis.geometry") == 0);
}

struct ResultSink
{
  const ObExpr *expression_;
  ObEvalCtx *context_;
  ObDatum *result_;
  bool emitted_;
  const char *expected_type_ = nullptr;
  seekdb_plugin_status_t status_ = SEEKDB_PLUGIN_STATUS_OK;
};

seekdb_plugin_status_t write_sql_result(
    seekdb_plugin_host_handle_t *host,
    const seekdb_plugin_execution_result_v1_t *result)
{
  if (nullptr == host || nullptr == result ||
      result->struct_size < sizeof(*result)) {
    return SEEKDB_PLUGIN_STATUS_INVALID_ARGUMENT;
  }
  ResultSink &sink = *reinterpret_cast<ResultSink *>(host);
  if (sink.emitted_ || nullptr == sink.expression_ ||
      nullptr == sink.context_ || nullptr == sink.result_) {
    return SEEKDB_PLUGIN_STATUS_FAILED_PRECONDITION;
  }
  sink.emitted_ = true;
  if (result->is_null != 0) {
    sink.result_->set_null();
    return SEEKDB_PLUGIN_STATUS_OK;
  }
  if (nullptr == result->type_id ||
      (result->data_size != 0 && nullptr == result->data)) {
    return SEEKDB_PLUGIN_STATUS_INVALID_ARGUMENT;
  }
  if (sink.expected_type_ && sink.expected_type_[0] &&
      std::strcmp(sink.expected_type_, result->type_id) != 0) {
    return SEEKDB_PLUGIN_STATUS_INVALID_ARGUMENT;
  }

  if (is_builtin_type(result->type_id, ".float64")) {
    if (result->data_size != sizeof(double)) {
      return SEEKDB_PLUGIN_STATUS_INVALID_ARGUMENT;
    }
    double value = 0;
    std::memcpy(&value, result->data, sizeof(value));
    sink.result_->set_double(value);
  } else if (is_builtin_type(result->type_id, ".bool")) {
    if (result->data_size != sizeof(uint8_t)) {
      return SEEKDB_PLUGIN_STATUS_INVALID_ARGUMENT;
    }
    sink.result_->set_int(result->data[0] != 0 ? 1 : 0);
  } else if (is_builtin_type(result->type_id, ".int32")) {
    if (result->data_size != sizeof(int32_t)) {
      return SEEKDB_PLUGIN_STATUS_INVALID_ARGUMENT;
    }
    int32_t value = 0;
    std::memcpy(&value, result->data, sizeof(value));
    sink.result_->set_int(static_cast<int64_t>(value));
  } else if (is_builtin_type(result->type_id, ".uint32")) {
    if (result->data_size != sizeof(uint32_t)) {
      return SEEKDB_PLUGIN_STATUS_INVALID_ARGUMENT;
    }
    uint32_t value = 0;
    std::memcpy(&value, result->data, sizeof(value));
    sink.result_->set_int(static_cast<int64_t>(value));
  } else if (is_builtin_type(result->type_id, ".int64") ||
             is_builtin_type(result->type_id, ".uint64")) {
    if (result->data_size != sizeof(int64_t)) {
      return SEEKDB_PLUGIN_STATUS_INVALID_ARGUMENT;
    }
    int64_t value = 0;
    std::memcpy(&value, result->data, sizeof(value));
    sink.result_->set_int(value);
  } else {
    if (result->data_size >
        static_cast<uint64_t>(std::numeric_limits<int32_t>::max())) {
      return SEEKDB_PLUGIN_STATUS_INVALID_ARGUMENT;
    }
    char *buffer = sink.expression_->get_str_res_mem(
        *sink.context_, static_cast<int64_t>(result->data_size));
    if (result->data_size != 0 && nullptr == buffer) {
      return SEEKDB_PLUGIN_STATUS_NO_MEMORY;
    }
    if (result->data_size != 0) {
      std::memcpy(buffer, result->data,
                  static_cast<size_t>(result->data_size));
    }
    sink.result_->set_string(
        ObString(static_cast<int32_t>(result->data_size), buffer));
  }
  return SEEKDB_PLUGIN_STATUS_OK;
}

seekdb_plugin_status_t SEEKDB_PLUGIN_CALL emit_sql_result(
    seekdb_plugin_host_handle_t *host,
    const seekdb_plugin_execution_result_v1_t *result)
{
  if (!host) return SEEKDB_PLUGIN_STATUS_INVALID_ARGUMENT;
  auto &sink = *reinterpret_cast<ResultSink *>(host);
  if (sink.status_ != SEEKDB_PLUGIN_STATUS_OK) return sink.status_;
  // A callback failure remains an execution failure even if the plugin ignores
  // it and returns OK, or attempts a second emission to repair the first one.
  sink.status_ = write_sql_result(host, result);
  return sink.status_;
}

struct DecodeSink
{
  ObIAllocator &allocator_;
  const char *type_;
  uint64_t &total_bytes_;
  ObString bytes_;
  bool emitted_ = false, null_ = false;
  seekdb_plugin_status_t status_ = SEEKDB_PLUGIN_STATUS_OK;
};

seekdb_plugin_status_t SEEKDB_PLUGIN_CALL emit_decoded_argument(
    seekdb_plugin_host_handle_t *opaque, const seekdb_plugin_execution_result_v1_t *value)
{
  if (!opaque) return SEEKDB_PLUGIN_STATUS_INVALID_ARGUMENT;
  auto &sink = *reinterpret_cast<DecodeSink *>(opaque);
  if (sink.status_ != SEEKDB_PLUGIN_STATUS_OK) return sink.status_;
  if (sink.emitted_ || !value || value->struct_size < sizeof(*value) || !value->type_id ||
      strnlen(value->type_id, SEEKDB_PLUGIN_MAX_IDENTIFIER_BYTES + 1) > SEEKDB_PLUGIN_MAX_IDENTIFIER_BYTES ||
      std::strcmp(value->type_id, sink.type_) != 0 || value->is_null > 1 ||
      (value->is_null && value->data_size) || (value->data_size && !value->data) ||
      value->data_size > UINT64_C(16777216) - sink.total_bytes_) {
    return sink.status_ = SEEKDB_PLUGIN_STATUS_INVALID_ARGUMENT;
  }
  for (const auto byte : value->reserved_bytes) if (byte) return sink.status_ = SEEKDB_PLUGIN_STATUS_INVALID_ARGUMENT;
  for (const auto word : value->reserved) if (word) return sink.status_ = SEEKDB_PLUGIN_STATUS_INVALID_ARGUMENT;
  sink.emitted_ = true; sink.null_ = value->is_null;
  if (value->data_size) {
    auto *copy = static_cast<char *>(sink.allocator_.alloc(value->data_size));
    if (!copy) return sink.status_ = SEEKDB_PLUGIN_STATUS_NO_MEMORY;
    std::memcpy(copy, value->data, value->data_size);
    sink.bytes_.assign_ptr(copy, static_cast<int32_t>(value->data_size));
  }
  sink.total_bytes_ += value->data_size;
  return SEEKDB_PLUGIN_STATUS_OK;
}

struct ArgumentStorage
{
  int64_t integer_ = 0;
  double floating_ = 0;
  int32_t int32_ = 0;
  uint32_t uint32_ = 0;
  uint8_t boolean_ = 0;
};

struct TableResultSink
{
  ObEvalCtx *context_;
  const ObIArray<ObExpr *> *columns_;
  uint32_t emitted_;
  const PluginTableFunctionExtraInfo *binding_;
  uint32_t maximum_;
  bool batch_;
  seekdb_plugin_status_t status_ = SEEKDB_PLUGIN_STATUS_OK;
};

seekdb_plugin_status_t write_sql_row(
    seekdb_plugin_host_handle_t *host,
    const seekdb_plugin_table_row_v1_t *row)
{
  if (nullptr == host || nullptr == row || row->struct_size < sizeof(*row) ||
      nullptr == row->columns) {
    return SEEKDB_PLUGIN_STATUS_INVALID_ARGUMENT;
  }
  TableResultSink &sink = *reinterpret_cast<TableResultSink *>(host);
  if (sink.emitted_ >= sink.maximum_ || nullptr == sink.context_ || nullptr == sink.columns_ ||
      !sink.binding_ || sink.binding_->columns_.count() != row->column_count ||
      static_cast<int64_t>(row->column_count) != sink.columns_->count()) {
    return SEEKDB_PLUGIN_STATUS_FAILED_PRECONDITION;
  }
  if (row->reserved_word) return SEEKDB_PLUGIN_STATUS_INVALID_ARGUMENT;
  for (auto word : row->reserved) if (word) return SEEKDB_PLUGIN_STATUS_INVALID_ARGUMENT;
  ObEvalCtx::BatchInfoScopeGuard batch(*sink.context_);
  if (sink.batch_) batch.set_batch_idx(sink.emitted_);
  for (uint32_t i = 0; i < row->column_count; ++i) {
    ObExpr *column = sink.columns_->at(i);
    const auto &value = row->columns[i];
    if (value.struct_size < sizeof(value) || value.is_null > 1 ||
        (value.is_null && (!sink.binding_->columns_.at(i).nullable || value.data_size)) ||
        (!value.is_null && (!value.type_id || std::strcmp(value.type_id, sink.binding_->columns_.at(i).type_id) != 0)) ||
        (value.data_size && !value.data))
      return SEEKDB_PLUGIN_STATUS_INVALID_ARGUMENT;
    for (auto byte : value.reserved_bytes) if (byte) return SEEKDB_PLUGIN_STATUS_INVALID_ARGUMENT;
    for (auto word : value.reserved) if (word) return SEEKDB_PLUGIN_STATUS_INVALID_ARGUMENT;
    // Preserve descriptor ordinals even when SQL only reads a subset of columns.
    if (nullptr == column) continue;
    ObDatum &datum = column->locate_datum_for_write(*sink.context_);
    ResultSink column_sink{column, sink.context_, &datum, false, sink.binding_->columns_.at(i).type_id};
    const seekdb_plugin_status_t status = emit_sql_result(
        reinterpret_cast<seekdb_plugin_host_handle_t *>(&column_sink),
        &row->columns[i]);
    if (SEEKDB_PLUGIN_STATUS_OK != status) return status;
    column->set_evaluated_projected(*sink.context_);
  }
  ++sink.emitted_;
  return SEEKDB_PLUGIN_STATUS_OK;
}

seekdb_plugin_status_t SEEKDB_PLUGIN_CALL emit_sql_row(
    seekdb_plugin_host_handle_t *host, const seekdb_plugin_table_row_v1_t *row)
{
  if (!host) return SEEKDB_PLUGIN_STATUS_INVALID_ARGUMENT;
  auto &sink = *reinterpret_cast<TableResultSink *>(host);
  if (sink.status_ == SEEKDB_PLUGIN_STATUS_OK) sink.status_ = write_sql_row(host, row);
  return sink.status_;
}

void assign_sql_result_type(ObExprResType &type, const char *type_id)
{
  if (is_builtin_type(type_id, ".geometry")) {
    type.set_geometry();
    type.set_length(
        ObAccuracy::DDL_DEFAULT_ACCURACY[ObGeometryType].get_length());
  } else if (is_builtin_type(type_id, ".float64")) {
    type.set_double();
  } else if (is_builtin_type(type_id, ".uint64")) {
    // Preserve unsigned comparison/range semantics, not just the 64 payload
    // bits. This applies to scalar, table, cast and type-carrier deduction.
    type.set_uint64();
  } else if (is_builtin_type(type_id, ".int32") ||
             is_builtin_type(type_id, ".uint32") ||
             is_builtin_type(type_id, ".int64") ||
             is_builtin_type(type_id, ".bool")) {
    type.set_int();
  } else {
    type.set_varchar();
    type.set_length(OB_MAX_VARCHAR_LENGTH);
    type.set_collation_type(CS_TYPE_UTF8MB4_BIN);
    type.set_collation_level(CS_LEVEL_IMPLICIT);
  }
}

} // namespace

bool PluginStoredArgument::valid() const
{
  const auto &b = binding_;
  if (index_ >= SEEKDB_PLUGIN_MAX_ARGUMENTS || b.struct_size < sizeof(b) ||
      b.kind != SEEKDB_PLUGIN_EXTENSION_TYPE || !b.owner_generation || !b.catalog_epoch ||
      !b.physical_format_version || !(b.flags & SEEKDB_PLUGIN_EXTENSION_FLAG_PERSISTENT)) return false;
  for (const char *id : {b.sql_name, b.object_id, b.owner_plugin_id, b.physical_format_id}) {
    if (!id[0] || !std::memchr(id, 0, SEEKDB_PLUGIN_MAX_IDENTIFIER_BYTES + 1)) return false;
  }
  for (const auto word : b.reserved) if (word) return false;
  return true;
}

OB_DEF_SERIALIZE(PluginStoredArgument)
{
  int ret = valid() ? OB_SUCCESS : OB_INVALID_DATA;
  OB_UNIS_ENCODE(index_);
  for (const char *id : {binding_.sql_name, binding_.object_id, binding_.owner_plugin_id, binding_.physical_format_id}) {
    if (OB_SUCC(ret)) { const ObString field = ObString::make_string(id); OB_UNIS_ENCODE(field); }
  }
  LST_DO_CODE(OB_UNIS_ENCODE, binding_.owner_generation, binding_.catalog_epoch, binding_.flags, binding_.physical_format_version);
  return ret;
}

OB_DEF_DESERIALIZE(PluginStoredArgument)
{
  int ret = OB_SUCCESS;
  binding_ = {}; binding_.struct_size = sizeof(binding_); binding_.kind = SEEKDB_PLUGIN_EXTENSION_TYPE;
  OB_UNIS_DECODE(index_);
  for (char *id : {binding_.sql_name, binding_.object_id, binding_.owner_plugin_id, binding_.physical_format_id}) {
    ObString field;
    OB_UNIS_DECODE(field);
    if (OB_SUCC(ret)) {
      if (field.length() <= 0 || field.length() > SEEKDB_PLUGIN_MAX_IDENTIFIER_BYTES ||
          !field.ptr() || std::memchr(field.ptr(), 0, field.length())) ret = OB_INVALID_DATA;
      else std::memcpy(id, field.ptr(), field.length());
    }
  }
  LST_DO_CODE(OB_UNIS_DECODE, binding_.owner_generation, binding_.catalog_epoch, binding_.flags, binding_.physical_format_version);
  if (OB_SUCC(ret) && !valid()) ret = OB_INVALID_DATA;
  if (OB_FAIL(ret)) binding_ = {};
  return ret;
}

OB_DEF_SERIALIZE_SIZE(PluginStoredArgument)
{
  int64_t len = 0;
  if (!valid()) return len;
  OB_UNIS_ADD_LEN(index_);
  for (const char *id : {binding_.sql_name, binding_.object_id, binding_.owner_plugin_id, binding_.physical_format_id}) {
    const ObString field = ObString::make_string(id); OB_UNIS_ADD_LEN(field);
  }
  LST_DO_CODE(OB_UNIS_ADD_LEN, binding_.owner_generation, binding_.catalog_epoch, binding_.flags, binding_.physical_format_version);
  return len;
}

int PluginFunctionExtraInfo::initialize(const seekdb_plugin_sql_binding_v1_t &source,
                                       const std::vector<std::string> &arguments,
                                       const std::vector<PluginStoredArgument> &stored)
{
  generation_ = 0;
  arguments_.reset();
  stored_.reset();
  sql_name_.reset(); object_id_.reset(); owner_.reset(); result_type_.reset();
  if (source.struct_size < sizeof(source) || source.kind != SEEKDB_PLUGIN_EXTENSION_FUNCTION ||
      arguments.size() > SEEKDB_PLUGIN_MAX_ARGUMENTS || stored.size() > arguments.size()) return OB_INVALID_ARGUMENT;
  const char *strings[] = {source.sql_name, source.object_id, source.owner_plugin_id, source.result_type_id};
  ObString *destinations[] = {&sql_name_, &object_id_, &owner_, &result_type_};
  int ret = OB_SUCCESS;
  for (uint32_t i = 0; OB_SUCC(ret) && i < 4; ++i) {
    const char *end = static_cast<const char *>(std::memchr(strings[i], 0, SEEKDB_PLUGIN_MAX_IDENTIFIER_BYTES + 1));
    if (!end) ret = OB_INVALID_ARGUMENT;
    else ret = ob_write_string(allocator_, ObString(static_cast<int32_t>(end - strings[i]), strings[i]), *destinations[i]);
  }
  if (OB_SUCC(ret)) ret = arguments_.prepare_allocate(arguments.size());
  for (int64_t i = 0; OB_SUCC(ret) && i < arguments_.count(); ++i) {
    if (arguments[i].size() > SEEKDB_PLUGIN_MAX_IDENTIFIER_BYTES || arguments[i].find('\0') != std::string::npos) ret = OB_INVALID_ARGUMENT;
    else ret = ob_write_string(allocator_, ObString(static_cast<int32_t>(arguments[i].size()), arguments[i].data()), arguments_.at(i), true);
  }
  if (OB_SUCC(ret)) ret = stored_.prepare_allocate(stored.size());
  for (int64_t i = 0; OB_SUCC(ret) && i < stored_.count(); ++i) stored_.at(i) = stored[i];
  if (OB_SUCC(ret)) {
    generation_ = source.owner_generation; epoch_ = source.catalog_epoch; flags_ = source.flags;
    minimum_arity_ = source.minimum_arity; maximum_arity_ = source.maximum_arity;
    seekdb_plugin_sql_binding_v1_t checked = {};
    ret = binding(checked);
  }
  if (OB_FAIL(ret)) generation_ = 0;
  return ret;
}

int PluginFunctionExtraInfo::binding(seekdb_plugin_sql_binding_v1_t &out) const
{
  out = {};
  if (sql_name_.empty() || object_id_.empty() || owner_.empty() || result_type_.empty() ||
      generation_ == 0 || epoch_ == 0 ||
      minimum_arity_ > maximum_arity_ || maximum_arity_ > SEEKDB_PLUGIN_MAX_ARGUMENTS ||
      arguments_.count() < minimum_arity_ || arguments_.count() > maximum_arity_) return OB_INVALID_DATA;
  const ObString *strings[] = {&sql_name_, &object_id_, &owner_, &result_type_};
  out = {}; out.struct_size = sizeof(out); out.kind = SEEKDB_PLUGIN_EXTENSION_FUNCTION;
  char *destinations[] = {out.sql_name, out.object_id, out.owner_plugin_id, out.result_type_id};
  for (uint32_t i = 0; i < 4; ++i) {
    if (strings[i]->length() < 0 || strings[i]->length() > SEEKDB_PLUGIN_MAX_IDENTIFIER_BYTES ||
        (strings[i]->length() && (!strings[i]->ptr() ||
         std::memchr(strings[i]->ptr(), 0, strings[i]->length())))) return OB_INVALID_DATA;
    if (!strings[i]->empty()) std::memcpy(destinations[i], strings[i]->ptr(), strings[i]->length());
  }
  out.owner_generation = generation_; out.catalog_epoch = epoch_; out.flags = flags_;
  out.minimum_arity = minimum_arity_; out.maximum_arity = maximum_arity_;
  for (int64_t i = 0; i < stored_.count(); ++i) {
    const auto &item = stored_.at(i);
    if (!item.valid() || item.index_ >= arguments_.count() ||
        (i && stored_.at(i - 1).index_ >= item.index_) ||
        arguments_.at(item.index_) != ObString::make_string(item.binding_.object_id) ||
        item.binding_.catalog_epoch != epoch_) return OB_INVALID_DATA;
  }
  return OB_SUCCESS;
}

OB_DEF_SERIALIZE(PluginFunctionExtraInfo)
{
  seekdb_plugin_sql_binding_v1_t checked = {};
  int ret = binding(checked);
  uint32_t count = static_cast<uint32_t>(arguments_.count());
  LST_DO_CODE(OB_UNIS_ENCODE, sql_name_, object_id_, owner_, result_type_, generation_, epoch_, flags_, minimum_arity_, maximum_arity_, count);
  for (uint32_t i = 0; OB_SUCC(ret) && i < count; ++i) { OB_UNIS_ENCODE(arguments_.at(i)); }
  uint32_t stored_count = stored_.count();
  OB_UNIS_ENCODE(stored_count);
  for (uint32_t i = 0; OB_SUCC(ret) && i < stored_count; ++i) { OB_UNIS_ENCODE(stored_.at(i)); }
  return ret;
}

OB_DEF_DESERIALIZE(PluginFunctionExtraInfo)
{
  int ret = OB_SUCCESS;
  uint32_t count = 0;
  arguments_.reset(); stored_.reset(); generation_ = 0;
  LST_DO_CODE(OB_UNIS_DECODE, sql_name_, object_id_, owner_, result_type_, generation_, epoch_, flags_, minimum_arity_, maximum_arity_, count);
  if (OB_SUCC(ret) && count > SEEKDB_PLUGIN_MAX_ARGUMENTS) ret = OB_INVALID_DATA;
  if (OB_SUCC(ret)) ret = arguments_.prepare_allocate(count);
  for (uint32_t i = 0; OB_SUCC(ret) && i < count; ++i) { OB_UNIS_DECODE(arguments_.at(i)); }
  uint32_t stored_count = 0;
  OB_UNIS_DECODE(stored_count);
  if (OB_SUCC(ret) && stored_count > count) ret = OB_INVALID_DATA;
  if (OB_SUCC(ret)) ret = stored_.prepare_allocate(stored_count);
  for (uint32_t i = 0; OB_SUCC(ret) && i < stored_count; ++i) { OB_UNIS_DECODE(stored_.at(i)); }
  // ObString decoding borrows the input. Copy before that wire buffer expires.
  ObString *fields[] = {&sql_name_, &object_id_, &owner_, &result_type_};
  for (int64_t i = 0; OB_SUCC(ret) && i < 4 + arguments_.count(); ++i) {
    ObString &field = i < 4 ? *fields[i] : arguments_.at(i - 4);
    if (field.length() < 0 || field.length() > SEEKDB_PLUGIN_MAX_IDENTIFIER_BYTES ||
        (field.length() && (!field.ptr() || std::memchr(field.ptr(), 0, field.length())))) ret = OB_INVALID_DATA;
    else {
      ObString owned;
      if (OB_SUCC(ret = ob_write_string(allocator_, field, owned, true))) field = owned;
    }
  }
  seekdb_plugin_sql_binding_v1_t checked = {};
  if (OB_SUCC(ret)) ret = binding(checked);
  if (OB_FAIL(ret)) {
    generation_ = 0; sql_name_.reset(); object_id_.reset(); owner_.reset(); result_type_.reset(); arguments_.reset(); stored_.reset();
  }
  return ret;
}

OB_DEF_SERIALIZE_SIZE(PluginFunctionExtraInfo)
{
  int64_t len = 0;
  uint32_t count = static_cast<uint32_t>(arguments_.count());
  LST_DO_CODE(OB_UNIS_ADD_LEN, sql_name_, object_id_, owner_, result_type_, generation_, epoch_, flags_, minimum_arity_, maximum_arity_, count);
  for (uint32_t i = 0; i < count; ++i) { OB_UNIS_ADD_LEN(arguments_.at(i)); }
  uint32_t stored_count = stored_.count();
  OB_UNIS_ADD_LEN(stored_count);
  for (uint32_t i = 0; i < stored_count; ++i) { OB_UNIS_ADD_LEN(stored_.at(i)); }
  return len;
}

int PluginFunctionExtraInfo::deep_copy(ObIAllocator &allocator, ObExprOperatorType type, ObIExprExtraInfo *&out) const
{
  out = nullptr;
  if (type != T_FUN_SYS_PLUGIN_FUNCTION) return OB_INVALID_ARGUMENT;
  seekdb_plugin_sql_binding_v1_t source = {};
  int ret = binding(source);
  if (OB_FAIL(ret)) return ret;
  try {
    std::vector<std::string> arguments;
    for (int64_t i = 0; i < arguments_.count(); ++i) {
      arguments.emplace_back(arguments_.at(i).ptr() ? arguments_.at(i).ptr() : "", arguments_.at(i).length());
    }
    ObIExprExtraInfo *base = nullptr;
    if (OB_FAIL(ObExprExtraInfoFactory::alloc(allocator, type, base))) return ret;
    auto *copy = static_cast<PluginFunctionExtraInfo *>(base);
    std::vector<PluginStoredArgument> stored;
    for (int64_t i = 0; i < stored_.count(); ++i) stored.push_back(stored_.at(i));
    if (OB_FAIL(copy->initialize(source, arguments, stored))) copy->~PluginFunctionExtraInfo();
    else out = copy;
  } catch (const std::bad_alloc &) { ret = OB_ALLOCATE_MEMORY_FAILED;
  } catch (...) { ret = OB_ERR_UNEXPECTED; }
  return ret;
}

namespace {
constexpr int64_t MAX_FUNCTION_BINDING_BYTES = 2 * 1024 * 1024;

// Decode and validate already-selected metadata without asking the registry to
// choose again. Child rewrites must preserve logical identities and codec use.
int read_function_binding(const ObRawExpr &expression, const ObString &wire,
    seekdb_plugin_sql_binding_v1_t &binding, std::vector<std::string> &arguments,
    uint32_t depth, std::vector<PluginStoredArgument> *stored)
{
  if (!wire.ptr() || wire.length() <= 0 || wire.length() > MAX_FUNCTION_BINDING_BYTES) return OB_INVALID_DATA;
  ObArenaAllocator temporary;
  PluginFunctionExtraInfo info(temporary, T_FUN_SYS_PLUGIN_FUNCTION);
  int64_t position = 0;
  int ret = info.deserialize(wire.ptr(), wire.length(), position);
  seekdb_plugin_sql_binding_v1_t selected = {};
  if (OB_FAIL(ret)) return ret;
  if (position != wire.length() || info.arguments().count() != expression.get_param_count() - 1) return OB_INVALID_DATA;
  if (OB_FAIL(info.binding(selected))) return ret;
  const auto *result = expression.get_plugin_type();
  if (!result || result->stored_ || result->logical_id_ != ObString::make_string(selected.result_type_id) ||
      result->catalog_epoch_ != selected.catalog_epoch) return OB_STATE_NOT_MATCH;
  arguments.clear();
  if (stored) stored->clear();
  int64_t codec_index = 0;
  for (int64_t i = 1; i < expression.get_param_count(); ++i) {
    const auto *child = expression.get_param_expr(i);
    if (!child) return OB_INVALID_DATA;
    const auto *logical = child->get_plugin_type();
    if (logical && logical->physical_type_ != child->get_data_type()) return OB_STATE_NOT_MATCH;
    if (logical && logical->catalog_epoch_ && logical->catalog_epoch_ != selected.catalog_epoch) return OB_STATE_NOT_MATCH;
    std::string actual;
    uint64_t child_epoch = selected.catalog_epoch;
    if (child->get_expr_type() == T_FUN_SYS_PLUGIN_FUNCTION) {
      if (child->get_param_count() < 1 || !child->get_param_expr(0) ||
          !child->get_param_expr(0)->is_const_raw_expr() ||
          !static_cast<const ObConstRawExpr *>(child->get_param_expr(0))->get_value().is_varbinary()) return OB_STATE_NOT_MATCH;
      seekdb_plugin_sql_binding_v1_t nested = {};
      std::vector<std::string> unused;
      if (OB_FAIL(PluginFunctionExpr::resolve_raw_binding(*child, nested, unused, depth + 1))) return ret;
      actual = nested.result_type_id; child_epoch = nested.catalog_epoch;
    } else {
      if (logical) actual.assign(logical->logical_id_.ptr(), logical->logical_id_.length());
      else if (const char *id = core_type_identifier(child->get_data_type())) actual = id;
      if (child->get_expr_type() == T_FUN_SYS_PLUGIN_CAST) {
        PluginCastExtraInfo cast(temporary, T_FUN_SYS_PLUGIN_CAST);
        if (OB_FAIL(PluginCastExpr::read_binding(*child, cast, depth + 1))) return ret;
        if (actual != cast.binding_.target_type_id) return OB_STATE_NOT_MATCH;
        child_epoch = cast.binding_.catalog_epoch;
      } else if (child->get_expr_type() == T_FUN_SYS_PLUGIN_TYPE_VALUE) {
        PluginTypeValueExtraInfo typed(temporary, T_FUN_SYS_PLUGIN_TYPE_VALUE);
        if (OB_FAIL(PluginTypeValueExpr::read_binding(*child, typed, depth + 1))) return ret;
        if (actual != typed.logical_id_) return OB_STATE_NOT_MATCH;
        child_epoch = typed.catalog_epoch_;
      }
    }
    if (child_epoch != selected.catalog_epoch ||
        ObString(actual.size(), actual.data()) != info.arguments().at(i - 1)) return OB_STATE_NOT_MATCH;
    const PluginStoredArgument *codec = codec_index < info.stored().count() &&
        info.stored().at(codec_index).index_ == i - 1 ? &info.stored().at(codec_index) : nullptr;
    if (static_cast<bool>(codec) != (logical && logical->stored_)) return OB_STATE_NOT_MATCH;
    if (codec) {
      const auto &b = codec->binding_;
      if (logical->logical_id_ != ObString::make_string(b.object_id) ||
          logical->sql_name_ != ObString::make_string(b.sql_name) ||
          logical->owner_ != ObString::make_string(b.owner_plugin_id) ||
          logical->format_ != ObString::make_string(b.physical_format_id) ||
          logical->format_version_ != b.physical_format_version) return OB_STATE_NOT_MATCH;
      if (stored) stored->push_back(*codec);
      ++codec_index;
    }
    arguments.push_back(std::move(actual));
  }
  if (codec_index != info.stored().count()) return OB_STATE_NOT_MATCH;
  binding = selected;
  return OB_SUCCESS;
}
} // namespace

int PluginFunctionExpr::resolve_raw_binding(const ObRawExpr &expression,
    seekdb_plugin_sql_binding_v1_t &binding, std::vector<std::string> &arguments, uint32_t depth,
    std::vector<PluginStoredArgument> *stored)
{
  binding = {};
  if (depth >= 64 || expression.get_expr_type() != T_FUN_SYS_PLUGIN_FUNCTION ||
      expression.get_param_count() < 1 || expression.get_param_count() > SEEKDB_PLUGIN_MAX_ARGUMENTS + 1) return OB_INVALID_ARGUMENT;
  const ObRawExpr *name_expr = expression.get_param_expr(0);
  if (!name_expr || !name_expr->is_const_raw_expr()) return OB_INVALID_ARGUMENT;
  const auto &value = static_cast<const ObConstRawExpr *>(name_expr)->get_value();
  if (!ob_is_string_type(value.get_type()) || value.is_null()) return OB_INVALID_ARGUMENT;
  if (value.is_varbinary()) {
    // A user-supplied binary first argument is not a compiler-created binding.
    // Only a previously typed expression (and its owned copies) may read wire.
    if (!expression.get_plugin_type()) return OB_INVALID_ARGUMENT;
    try { return read_function_binding(expression, value.get_string(), binding, arguments, depth, stored);
    } catch (const std::bad_alloc &) { return OB_ALLOCATE_MEMORY_FAILED;
    } catch (...) { return OB_ERR_UNEXPECTED; }
  }
  if (!share::g_mp) return OB_NOT_INIT;
  const ObString name = value.get_string();
  if (name.empty() || name.length() > SEEKDB_PLUGIN_MAX_IDENTIFIER_BYTES ||
      !name.ptr() || std::memchr(name.ptr(), 0, name.length())) return OB_INVALID_ARGUMENT;
  try {
    arguments.clear();
    if (stored) stored->clear();
    std::vector<uint64_t> nested_epochs;
    for (int64_t i = 1; i < expression.get_param_count(); ++i) {
      const ObRawExpr *argument = expression.get_param_expr(i);
      if (!argument) return OB_INVALID_ARGUMENT;
      if (argument->get_expr_type() == T_FUN_SYS_PLUGIN_FUNCTION) {
        seekdb_plugin_sql_binding_v1_t nested = {};
        std::vector<std::string> unused;
        const int ret = resolve_raw_binding(*argument, nested, unused, depth + 1);
        if (ret != OB_SUCCESS) return ret;
        arguments.emplace_back(nested.result_type_id);
        nested_epochs.push_back(nested.catalog_epoch);
      } else {
        const auto *logical_type = argument->get_plugin_type();
        if (logical_type) {
          if (logical_type->physical_type_ != argument->get_result_type().get_type()) return OB_STATE_NOT_MATCH;
          if (logical_type->catalog_epoch_) nested_epochs.push_back(logical_type->catalog_epoch_);
          if (argument->get_expr_type() == T_FUN_SYS_PLUGIN_CAST) {
            // Reading the fixed-size wire binding does not allocate or mutate
            // the const source expression's factory.
            ObArenaAllocator temporary;
            PluginCastExtraInfo cast(temporary, T_FUN_SYS_PLUGIN_CAST);
            int ret = PluginCastExpr::read_binding(*argument, cast);
            if (OB_FAIL(ret)) return ret;
            if (logical_type->logical_id_ != ObString::make_string(cast.binding_.target_type_id)) return OB_STATE_NOT_MATCH;
            nested_epochs.push_back(cast.binding_.catalog_epoch);
          }
          if (argument->get_expr_type() == T_FUN_SYS_PLUGIN_TYPE_VALUE) {
            ObArenaAllocator temporary;
            PluginTypeValueExtraInfo typed(temporary, T_FUN_SYS_PLUGIN_TYPE_VALUE);
            int ret = PluginTypeValueExpr::read_binding(*argument, typed);
            if (OB_FAIL(ret)) return ret;
            if (logical_type->logical_id_ != ObString::make_string(typed.logical_id_)) return OB_STATE_NOT_MATCH;
            nested_epochs.push_back(typed.catalog_epoch_);
          }
          if (logical_type->stored_) {
            PluginStoredArgument item;
            item.index_ = i - 1;
            const std::string name(logical_type->sql_name_.ptr(), logical_type->sql_name_.length());
            int ret = share::g_mp->resolve_plugin_sql_object(SEEKDB_PLUGIN_EXTENSION_TYPE,
                name.c_str(), nullptr, 0, &item.binding_);
            if (OB_FAIL(ret)) return ret;
            if (!item.valid() || logical_type->logical_id_ != ObString::make_string(item.binding_.object_id) ||
                logical_type->owner_ != ObString::make_string(item.binding_.owner_plugin_id) ||
                logical_type->format_ != ObString::make_string(item.binding_.physical_format_id) ||
                logical_type->format_version_ != item.binding_.physical_format_version) return OB_STATE_NOT_MATCH;
            nested_epochs.push_back(item.binding_.catalog_epoch);
            if (stored) stored->push_back(item);
          }
          arguments.emplace_back(logical_type->logical_id_.ptr(), logical_type->logical_id_.length());
        } else {
          const char *id = core_type_identifier(argument->get_result_type().get_type());
          arguments.emplace_back(id ? id : "");
        }
      }
    }
    std::vector<const char *> pointers;
    for (const auto &id : arguments) pointers.push_back(id.empty() ? nullptr : id.c_str());
    const std::string owned_name(name.ptr(), name.length());
    int ret = share::g_mp->resolve_plugin_sql_object(SEEKDB_PLUGIN_EXTENSION_FUNCTION,
        owned_name.c_str(), pointers.data(), pointers.size(), &binding);
    for (uint64_t epoch : nested_epochs) if (OB_SUCC(ret) && epoch != binding.catalog_epoch) ret = OB_STATE_NOT_MATCH;
    if (OB_FAIL(ret)) binding = {};
    return ret;
  } catch (const std::bad_alloc &) { return OB_ALLOCATE_MEMORY_FAILED;
  } catch (...) { return OB_ERR_UNEXPECTED; }
}

PluginFunctionExpr::PluginFunctionExpr(ObIAllocator &allocator)
    : ObFuncExprOperator(allocator, T_FUN_SYS_PLUGIN_FUNCTION,
                         SQL_DISPATCH_NAME, PARAM_NUM_UNKNOWN,
                         NOT_VALID_FOR_GENERATED_COL, NOT_ROW_DIMENSION, true)
{}

int PluginFunctionExpr::calc_result_typeN(
    ObExprResType &type,
    ObExprResType *arguments,
    const int64_t argument_count,
    ObExprTypeCtx &type_context) const
try {
  if (nullptr == arguments || argument_count < 1 || argument_count > SEEKDB_PLUGIN_MAX_ARGUMENTS + 1 ||
      (type_context.get_raw_expr() ? type_context.get_raw_expr()->get_param_count() != argument_count
                                   : !arguments[0].is_literal())) {
    return OB_INVALID_ARGUMENT;
  }
  std::vector<const char *> argument_types;
  argument_types.reserve(static_cast<size_t>(argument_count - 1));
  for (int64_t i = 1; i < argument_count; ++i) {
    argument_types.push_back(core_type_identifier(arguments[i].get_type()));
  }

  seekdb_plugin_sql_binding_v1_t binding = {};
  int ret = OB_SUCCESS;
  if (type_context.get_raw_expr()) {
    std::vector<std::string> logical_arguments;
    std::vector<PluginStoredArgument> stored;
    auto *raw = type_context.get_raw_expr();
    ret = resolve_raw_binding(*raw, binding, logical_arguments, 0, &stored);
    if (OB_SUCC(ret) && !static_cast<const ObConstRawExpr *>(raw->get_param_expr(0))->get_value().is_varbinary()) {
      auto *factory = raw->get_expr_factory();
      if (!factory) return OB_INVALID_ARGUMENT;
      // Stored values must be decoded at the argument's evaluation boundary,
      // not after all child batches have run. Explicit nodes cache the decoded
      // datum and let strict parents mask decoder-produced NULLs before later
      // arguments. Stage replacements until metadata construction succeeds.
      std::vector<std::pair<int64_t, ObRawExpr *>> decoded;
      for (const auto &item : stored) {
        const int64_t index = item.index_ + 1;
        ObRawExpr *value = raw->get_param_expr(index);
        const auto *logical = value->get_plugin_type();
        if (!logical || !logical->stored_) return OB_STATE_NOT_MATCH;
        if (OB_FAIL(PluginTypeValueExpr::build(*factory, logical->sql_name_, value,
            type_context.get_session()))) return ret;
        const auto *converted = value->get_plugin_type();
        if (!converted || converted->stored_ || converted->catalog_epoch_ != binding.catalog_epoch ||
            converted->logical_id_ != ObString::make_string(item.binding_.object_id)) return OB_STATE_NOT_MATCH;
        decoded.emplace_back(index, value);
      }
      PluginFunctionExtraInfo info(factory->get_allocator(), T_FUN_SYS_PLUGIN_FUNCTION);
      if (OB_FAIL(info.initialize(binding, logical_arguments))) return ret;
      const int64_t size = info.get_serialize_size();
      if (size <= 0 || size > MAX_FUNCTION_BINDING_BYTES) return OB_SIZE_OVERFLOW;
      auto *wire = static_cast<char *>(factory->get_allocator().alloc(size));
      if (!wire) return OB_ALLOCATE_MEMORY_FAILED;
      int64_t position = 0;
      if (OB_FAIL(info.serialize(wire, size, position))) return ret;
      ObConstRawExpr *metadata = nullptr;
      if (OB_FAIL(factory->create_raw_expr(T_VARCHAR, metadata))) return ret;
      ObObj literal; literal.set_varchar(ObString(size, wire)); literal.set_collation_type(CS_TYPE_BINARY);
      metadata->set_value(literal);
      for (const auto &item : decoded) {
        raw->get_param_expr(item.first) = item.second;
        arguments[item.first] = item.second->get_result_type();
        arguments[item.first].set_calc_meta(arguments[item.first]);
        arguments[item.first].set_calc_accuracy(arguments[item.first].get_accuracy());
      }
      raw->get_param_expr(0) = metadata;
      arguments[0] = metadata->get_result_type();
      arguments[0].set_calc_meta(arguments[0]);
      arguments[0].set_calc_accuracy(arguments[0].get_accuracy());
    }
  } else {
    if (!share::g_mp) return OB_NOT_INIT;
    const ObString sql_name = arguments[0].get_param().get_string();
    std::string owned_name(sql_name.ptr(), sql_name.length());
    ret = share::g_mp->resolve_plugin_sql_object(
        SEEKDB_PLUGIN_EXTENSION_FUNCTION, owned_name.c_str(),
        argument_types.empty() ? nullptr : argument_types.data(),
        static_cast<uint32_t>(argument_types.size()), &binding);
  }
  if (OB_SUCCESS != ret) return ret;

  assign_sql_result_type(type, binding.result_type_id);
  if (type_context.get_raw_expr()) {
    PluginExprType logical;
    logical.logical_id_ = ObString::make_string(binding.result_type_id);
    logical.physical_type_ = type.get_type();
    logical.catalog_epoch_ = binding.catalog_epoch;
    if (OB_FAIL(type_context.get_raw_expr()->set_plugin_type(logical))) return ret;
  }
  return OB_SUCCESS;
} catch (const std::bad_alloc &) {
  return OB_ALLOCATE_MEMORY_FAILED;
} catch (...) {
  return OB_ERR_UNEXPECTED;
}

int PluginFunctionExpr::cg_expr(ObExprCGCtx &cg_context,
                                const ObRawExpr &raw_expression,
                                ObExpr &runtime_expression) const
{
  if (raw_expression.get_param_count() < 1 || !raw_expression.get_param_expr(0) ||
      !raw_expression.get_param_expr(0)->is_const_raw_expr()) return OB_INVALID_ARGUMENT;
  if (!static_cast<const ObConstRawExpr *>(raw_expression.get_param_expr(0))->get_value().is_varbinary()) return OB_STATE_NOT_MATCH;
  if (!cg_context.allocator_) return OB_INVALID_ARGUMENT;
  seekdb_plugin_sql_binding_v1_t binding = {};
  std::vector<std::string> arguments;
  std::vector<PluginStoredArgument> stored;
  int ret = resolve_raw_binding(raw_expression, binding, arguments, 0, &stored);
  if (OB_FAIL(ret)) return ret;
  ObExprResType result_type;
  assign_sql_result_type(result_type, binding.result_type_id);
  // A catalog change between deduction and codegen must not reinterpret the
  // already assigned physical datum layout.
  if (result_type.get_type() != raw_expression.get_result_type().get_type()) return OB_STATE_NOT_MATCH;
  if (raw_expression.get_plugin_type() &&
      raw_expression.get_plugin_type()->logical_id_ != ObString::make_string(binding.result_type_id)) return OB_STATE_NOT_MATCH;
  ObIExprExtraInfo *base = nullptr;
  if (OB_FAIL(ObExprExtraInfoFactory::alloc(*cg_context.allocator_, T_FUN_SYS_PLUGIN_FUNCTION, base))) return ret;
  auto *info = static_cast<PluginFunctionExtraInfo *>(base);
  if (OB_FAIL(info->initialize(binding, arguments, stored))) { info->~PluginFunctionExtraInfo(); return ret; }
  runtime_expression.extra_info_ = info;
  runtime_expression.eval_func_ = evaluate;
  runtime_expression.eval_batch_func_ = evaluate_batch;
  return OB_SUCCESS;
}

namespace {
// Shared scalar/batch marshalling preserves left-to-right strict-NULL short
// circuiting, stored codecs and the logical type selected during planning.
int prepare_function_arguments(const ObExpr &expression, ObEvalCtx &context,
    const PluginFunctionExtraInfo *info, const seekdb_plugin_sql_binding_v1_t &binding,
    ObIAllocator &allocator, std::vector<seekdb_plugin_execution_value_v1_t> &arguments,
    std::vector<ArgumentStorage> &argument_storage, bool &strict_null)
{
  int ret = OB_SUCCESS;
  const uint32_t argument_count = expression.arg_cnt_ - 1;
  arguments.resize(argument_count);
  argument_storage.resize(argument_count);
  strict_null = false;
  uint32_t stored_position = 0;
  uint64_t decoded_bytes = 0;
  for (uint32_t i = 0; i < argument_count; ++i) {
    const PluginStoredArgument *stored = nullptr;
    if (stored_position < info->stored().count() && info->stored().at(stored_position).index_ == i) {
      stored = &info->stored().at(stored_position++);
    }
    ObDatum *datum = nullptr;
    if (OB_FAIL(expression.args_[i + 1]->eval(context, datum))) return ret;
    if (nullptr == datum) return OB_ERR_UNEXPECTED;

    arguments[i].struct_size = sizeof(arguments[i]);
    // Plan-owned identifiers are NUL-terminated once at codegen/decode time;
    // borrowing them avoids allocating and copying type strings on every row.
    const ObString &logical_type = info->arguments().at(i);
    arguments[i].type_id = logical_type.empty() ? nullptr : logical_type.ptr();
    const auto *nested_info = dynamic_cast<const PluginFunctionExtraInfo *>(expression.args_[i + 1]->extra_info_);
    if (nested_info) {
      seekdb_plugin_sql_binding_v1_t nested_binding = {};
      if (OB_FAIL(nested_info->binding(nested_binding))) return ret;
      if (logical_type != ObString::make_string(nested_binding.result_type_id)) return OB_STATE_NOT_MATCH;
    }
    arguments[i].is_null = datum->is_null() ? 1 : 0;
    if (datum->is_null()) {
      if ((binding.flags &
           SEEKDB_PLUGIN_EXTENSION_FLAG_NULL_PROPAGATING) != 0) {
        strict_null = true;
        return OB_SUCCESS;
      }
      continue;
    }

    const ObObjType type = expression.args_[i + 1]->datum_meta_.type_;
    if (stored) {
      if (!ob_is_string_or_lob_type(type)) return OB_STATE_NOT_MATCH;
      ObString encoded;
      if (OB_FAIL(ObTextStringHelper::read_real_string_data_with_copy(
          context.exec_ctx_, allocator, *datum,
          expression.args_[i + 1]->datum_meta_,
          expression.args_[i + 1]->obj_meta_.has_lob_header(), encoded))) return ret;
      DecodeSink sink{allocator, stored->binding_.object_id, decoded_bytes, {}};
      seekdb_plugin_execution_context_v1_t codec_context = {};
      codec_context.struct_size = sizeof(codec_context);
      codec_context.host = reinterpret_cast<seekdb_plugin_host_handle_t *>(&sink);
      codec_context.emit_result = emit_decoded_argument;
      ret = share::g_mp->decode_bound_plugin_type(&stored->binding_, &codec_context,
          reinterpret_cast<const uint8_t *>(encoded.ptr()), encoded.length());
      if (OB_FAIL(ret)) return ret;
      if (sink.status_ == SEEKDB_PLUGIN_STATUS_NO_MEMORY) return OB_ALLOCATE_MEMORY_FAILED;
      if (!sink.emitted_ || sink.status_ != SEEKDB_PLUGIN_STATUS_OK) return OB_INVALID_DATA;
      arguments[i].is_null = sink.null_;
      arguments[i].data = reinterpret_cast<const uint8_t *>(sink.bytes_.ptr());
      arguments[i].data_size = sink.bytes_.length();
      if (sink.null_ && (binding.flags & SEEKDB_PLUGIN_EXTENSION_FLAG_NULL_PROPAGATING)) {
        strict_null = true; return OB_SUCCESS;
      }
    } else if (ob_is_integer_type(type)) {
      argument_storage[i].integer_ = datum->get_int();
      arguments[i].data = reinterpret_cast<const uint8_t *>(
          &argument_storage[i].integer_);
      arguments[i].data_size = sizeof(argument_storage[i].integer_);
      // Narrow built-in logical results use an int SQL datum but retain their
      // original C ABI representation when passed to another plugin function.
      if (is_builtin_type(arguments[i].type_id, ".bool")) {
        argument_storage[i].boolean_ = datum->get_int() != 0;
        arguments[i].data = &argument_storage[i].boolean_;
        arguments[i].data_size = sizeof(argument_storage[i].boolean_);
      } else if (is_builtin_type(arguments[i].type_id, ".int32")) {
        argument_storage[i].int32_ = static_cast<int32_t>(datum->get_int());
        arguments[i].data = reinterpret_cast<const uint8_t *>(&argument_storage[i].int32_);
        arguments[i].data_size = sizeof(argument_storage[i].int32_);
      } else if (is_builtin_type(arguments[i].type_id, ".uint32")) {
        argument_storage[i].uint32_ = static_cast<uint32_t>(datum->get_int());
        arguments[i].data = reinterpret_cast<const uint8_t *>(&argument_storage[i].uint32_);
        arguments[i].data_size = sizeof(argument_storage[i].uint32_);
      }
    } else if (ob_is_double_type(type) || ob_is_float_type(type)) {
      argument_storage[i].floating_ =
          ob_is_float_type(type) ? datum->get_float() : datum->get_double();
      arguments[i].data = reinterpret_cast<const uint8_t *>(
          &argument_storage[i].floating_);
      arguments[i].data_size = sizeof(argument_storage[i].floating_);
    } else {
      ObString bytes = datum->get_string();
      if (ob_is_geometry(type) &&
          OB_FAIL(ObTextStringHelper::read_real_string_data_with_copy(
              context.exec_ctx_, allocator, *datum,
              expression.args_[i + 1]->datum_meta_,
              expression.args_[i + 1]->obj_meta_.has_lob_header(), bytes))) {
        return ret;
      }
      arguments[i].data = reinterpret_cast<const uint8_t *>(bytes.ptr());
      arguments[i].data_size = static_cast<uint64_t>(bytes.length());
    }
  }

  return OB_SUCCESS;
}
} // namespace

int PluginFunctionExpr::evaluate(const ObExpr &expression, ObEvalCtx &context, ObDatum &result)
try {
  if (expression.arg_cnt_ < 1 || !share::g_mp) return OB_NOT_SUPPORTED;
  int ret = OB_SUCCESS;
  auto &execution = context.exec_ctx_;
  auto *runtime = static_cast<RuntimeContext *>(execution.get_expr_op_ctx(expression.expr_ctx_id_));
  if (!runtime && OB_FAIL(execution.create_expr_op_ctx(expression.expr_ctx_id_, runtime))) return ret;
  if (!runtime) return OB_ALLOCATE_MEMORY_FAILED;
  const auto *info = dynamic_cast<const PluginFunctionExtraInfo *>(expression.extra_info_);
  const uint32_t argument_count = expression.arg_cnt_ - 1;
  if (!info || info->arguments().count() != argument_count) return OB_INVALID_DATA;
  if (!runtime->initialized_) {
    if (OB_FAIL(info->binding(runtime->binding_))) return ret;
    runtime->initialized_ = true;
  }
  std::vector<seekdb_plugin_execution_value_v1_t> arguments;
  std::vector<ArgumentStorage> storage;
  ObEvalCtx::TempAllocGuard temporary(context);
  bool strict_null = false;
  if (OB_FAIL(prepare_function_arguments(expression, context, info, runtime->binding_,
      temporary.get_allocator(), arguments, storage, strict_null))) return ret;
  if (strict_null) { result.set_null(); return OB_SUCCESS; }
  ResultSink sink{&expression, &context, &result, false, runtime->binding_.result_type_id};
  PluginSqlContext sql_context(context.exec_ctx_);
  seekdb_plugin_execution_context_v2_t plugin_context = {};
  plugin_context.v1.struct_size = sizeof(plugin_context);
  plugin_context.v1.host = reinterpret_cast<seekdb_plugin_host_handle_t *>(&sink);
  plugin_context.v1.emit_result = emit_sql_result;
  sql_context.attach(plugin_context);
  ret = share::g_mp->execute_bound_plugin_function(
      &runtime->binding_, &plugin_context.v1,
      arguments.empty() ? nullptr : arguments.data(), argument_count);
  if (OB_SUCCESS != sql_context.error()) return sql_context.error();
  if (OB_SUCCESS == ret && sink.status_ != SEEKDB_PLUGIN_STATUS_OK) {
    return sink.status_ == SEEKDB_PLUGIN_STATUS_NO_MEMORY ? OB_ALLOCATE_MEMORY_FAILED : OB_INVALID_DATA;
  }
  if (OB_SUCCESS == ret && !sink.emitted_) return OB_ERR_UNEXPECTED;
  return ret;
} catch (const std::bad_alloc &) {
  return OB_ALLOCATE_MEMORY_FAILED;
} catch (...) {
  return OB_ERR_UNEXPECTED;
}

namespace {
struct FunctionBatchSink
{
  const ObExpr &expression;
  ObEvalCtx &context;
  const std::vector<int64_t> &indices;
  const char *type;
  std::vector<uint8_t> emitted;
  seekdb_plugin_status_t status = SEEKDB_PLUGIN_STATUS_OK;
  static seekdb_plugin_status_t SEEKDB_PLUGIN_CALL emit(seekdb_plugin_host_handle_t *host,
      uint32_t row, const seekdb_plugin_execution_result_v1_t *value)
  {
    if (!host) return SEEKDB_PLUGIN_STATUS_INVALID_ARGUMENT;
    auto &sink = *reinterpret_cast<FunctionBatchSink *>(host);
    if (sink.status != SEEKDB_PLUGIN_STATUS_OK) return sink.status;
    if (row >= sink.indices.size() || sink.emitted[row])
      return sink.status = SEEKDB_PLUGIN_STATUS_INVALID_ARGUMENT;
    sink.emitted[row] = 1;
    ObEvalCtx::BatchInfoScopeGuard guard(sink.context);
    guard.set_batch_idx(sink.indices[row]);
    auto &datum = sink.expression.locate_batch_datums(sink.context)[sink.indices[row]];
    ResultSink scalar{&sink.expression, &sink.context, &datum, false, sink.type};
    return sink.status = emit_sql_result(reinterpret_cast<seekdb_plugin_host_handle_t *>(&scalar), value);
  }
  static seekdb_plugin_status_t SEEKDB_PLUGIN_CALL reject_scalar(seekdb_plugin_host_handle_t *,
      const seekdb_plugin_execution_result_v1_t *)
  { return SEEKDB_PLUGIN_STATUS_FAILED_PRECONDITION; }
};
}

int PluginFunctionExpr::evaluate_batch(const ObExpr &expression, ObEvalCtx &context,
    const ObBitVector &skip, int64_t size)
try {
  // Frame capacity is established by ObExpr's caller. Standalone expression
  // contexts can leave max_batch_size_ unset, as for other native batch evaluators.
  if (size < 0 || !expression.is_batch_result()) return OB_INVALID_ARGUMENT;
  auto &evaluated = expression.get_evaluated_flags(context);
  // ObExpr::do_eval_batch clears ALL result datums on failure, including rows
  // cached by an earlier partial call. Never retain success bits for those NULLs.
  struct FailureGuard {
    ObBitVector &flags; int64_t size; bool success = false;
    ~FailureGuard() { if (!success) flags.reset(size); }
  } failure{evaluated, size};
  std::vector<int64_t> pending;
  for (int64_t i = 0; i < size; ++i) if (!skip.at(i) && !evaluated.at(i)) pending.push_back(i);
  if (pending.empty()) { failure.success = true; return OB_SUCCESS; }
  if (expression.arg_cnt_ < 1 || !share::g_mp) return OB_NOT_SUPPORTED;
  const auto *info = dynamic_cast<const PluginFunctionExtraInfo *>(expression.extra_info_);
  if (!info || info->arguments().count() != expression.arg_cnt_ - 1) return OB_INVALID_DATA;
  seekdb_plugin_sql_binding_v1_t binding{};
  int ret = info->binding(binding);
  if (OB_FAIL(ret)) return ret;
  ObEvalCtx::BatchInfoScopeGuard frame(context);
  frame.set_batch_size(size);
  // Keep this mask outside the temporary SQL arena: a child can reset that
  // arena. Each argument sees only rows still needed by this parent, including
  // NULLs produced by earlier casts (not merely NULL input column metadata).
  std::vector<uint64_t> mask_words(ObBitVector::memory_size(size) / sizeof(uint64_t), UINT64_MAX);
  auto &argument_skip = *to_bit_vector(mask_words.data());
  for (const auto index : pending) argument_skip.unset(index);
  size_t active = pending.size();
  const bool strict = binding.flags & SEEKDB_PLUGIN_EXTENSION_FLAG_NULL_PROPAGATING;
  for (uint32_t a = 0; a + 1 < expression.arg_cnt_ && active; ++a) {
    if (!expression.args_[a + 1]) return OB_INVALID_DATA;
    if (OB_FAIL(context.exec_ctx_.check_status())) return ret;
    auto &child = *expression.args_[a + 1];
    if (OB_FAIL(child.eval_batch(context, argument_skip, size))) return ret;
    if (strict) {
      auto values = child.locate_expr_datumvector(context);
      for (const auto index : pending) if (!argument_skip.at(index) && values.at(index)->is_null()) {
        argument_skip.set(index); --active;
      }
    }
    // A legacy direct stored argument is decoded during row marshalling. It
    // can fail, or return NULL for non-NULL storage. In either NULL profile,
    // do not pre-evaluate later arguments before the decoder runs. Marshalling
    // completes the suffix once; a decoded value expression needs no fallback.
    bool stored = false;
    for (int64_t s = 0; s < info->stored().count(); ++s)
      if (info->stored().at(s).index_ == a) { stored = true; break; }
    if (stored) break;
  }
  PluginSqlContext sql(context.exec_ctx_);
  seekdb_plugin_execution_context_v2_t query{};
  query.v1.struct_size = sizeof(query);
  query.v1.emit_result = FunctionBatchSink::reject_scalar;
  sql.attach(query);
  struct RowStorage {
    std::vector<seekdb_plugin_execution_value_v1_t> arguments;
    std::vector<ArgumentStorage> numbers;
    std::vector<std::string> bytes;
  };
  std::vector<RowStorage> storage;
  std::vector<seekdb_plugin_batch_row_v1_t> rows;
  std::vector<int64_t> indices;
  uint64_t total_bytes = 0;
  // Do not discover batch sizes by retrying failed calls: parameters, casts and
  // providers may have side effects. A look-ahead row is prepared and owned
  // exactly once, then moved into the next chunk if this chunk is byte-full.
  const auto flush = [&]() -> int {
    if (!rows.empty()) {
      FunctionBatchSink sink{expression, context, indices, binding.result_type_id,
          std::vector<uint8_t>(indices.size(), 0)};
      seekdb_plugin_batch_context_v1_t batch{};
      batch.struct_size = sizeof(batch); batch.query_context = &query.v1;
      batch.host = reinterpret_cast<seekdb_plugin_host_handle_t *>(&sink);
      batch.emit_result = FunctionBatchSink::emit;
      ret = share::g_mp->execute_bound_plugin_function_batch(&binding, &batch, rows.data(), rows.size());
      if (sql.error() != OB_SUCCESS) return sql.error();
      if (OB_FAIL(ret)) return ret;
      if (sink.status != SEEKDB_PLUGIN_STATUS_OK)
        return sink.status == SEEKDB_PLUGIN_STATUS_NO_MEMORY ? OB_ALLOCATE_MEMORY_FAILED : OB_INVALID_DATA;
      for (auto emitted : sink.emitted) if (!emitted) return OB_INVALID_DATA;
    }
    rows.clear(); indices.clear(); storage.clear(); total_bytes = 0;
    return OB_SUCCESS;
  };
  for (const int64_t index : pending) {
    if (OB_FAIL(context.exec_ctx_.check_status())) return ret;
    frame.set_batch_idx(index);
    RowStorage owned;
    uint64_t row_bytes = 0;
    {
      // Release/reset per-row temporary decoding storage when the surrounding
      // expression does not already own a TempAllocGuard. The batch itself owns
      // only copied payloads, never a borrowed child/decoder scratch buffer.
      ObEvalCtx::TempAllocGuard temporary(context);
      bool strict_null = false;
      if (OB_FAIL(prepare_function_arguments(expression, context, info, binding,
          temporary.get_allocator(), owned.arguments, owned.numbers, strict_null))) return ret;
      if (strict_null) { expression.locate_batch_datums(context)[index].set_null(); continue; }
      for (const auto &value : owned.arguments) if (!value.is_null) {
        if (value.data_size > UINT64_C(16777216) || value.data_size > SEEKDB_PLUGIN_MAX_BATCH_BYTES - row_bytes)
          return OB_SIZE_OVERFLOW; // A single row cannot be split across calls.
        row_bytes += value.data_size;
      }
      owned.bytes.resize(owned.arguments.size());
      for (size_t a = 0; a < owned.arguments.size(); ++a) {
        auto &value = owned.arguments[a];
        if (!value.is_null && value.data_size) {
          owned.bytes[a].assign(reinterpret_cast<const char *>(value.data), value.data_size);
          value.data = reinterpret_cast<const uint8_t *>(owned.bytes[a].data());
        }
      }
    }
    if (row_bytes > SEEKDB_PLUGIN_MAX_BATCH_BYTES - total_bytes && OB_FAIL(flush())) return ret;
    storage.push_back(std::move(owned));
    const auto &arguments = storage.back().arguments;
    seekdb_plugin_batch_row_v1_t row{};
    row.struct_size = sizeof(row); row.argument_count = arguments.size();
    row.arguments = arguments.empty() ? nullptr : arguments.data();
    rows.push_back(row); indices.push_back(index); total_bytes += row_bytes;
    if (rows.size() == SEEKDB_PLUGIN_MAX_BATCH_ROWS && OB_FAIL(flush())) return ret;
  }
  if (OB_FAIL(flush())) return ret;
  if (OB_FAIL(context.exec_ctx_.check_status())) return ret;
  for (auto index : pending) evaluated.set(index);
  failure.success = true;
  return OB_SUCCESS;
} catch (const std::bad_alloc &) {
  return OB_ALLOCATE_MEMORY_FAILED;
} catch (...) {
  return OB_ERR_UNEXPECTED;
}

int PluginFunctionExpr::evaluate_argument_batch(const ObExpr &expression, ObEvalCtx &context,
    const ObBitVector &skip, int64_t size)
try {
  if (size < 0 || !expression.is_batch_result() || !expression.arg_cnt_ || !expression.args_[0])
    return OB_INVALID_ARGUMENT;
  if (size == 0) return OB_SUCCESS;
  auto &evaluated = expression.get_evaluated_flags(context);
  struct FailureGuard {
    ObBitVector &flags; int64_t size; bool success = false;
    ~FailureGuard() { if (!success) flags.reset(size); }
  } failure{evaluated, size};
  std::vector<uint64_t> words(ObBitVector::memory_size(size) / sizeof(uint64_t), UINT64_MAX);
  auto &needed = *to_bit_vector(words.data());
  bool any = false;
  for (int64_t i = 0; i < size; ++i) if (!skip.at(i) && !evaluated.at(i)) { needed.unset(i); any = true; }
  if (!any) { failure.success = true; return OB_SUCCESS; }
  ObEvalCtx::BatchInfoScopeGuard frame(context);
  frame.set_batch_size(size);
  int ret = context.exec_ctx_.check_status();
  if (OB_FAIL(ret)) return ret;
  if (OB_FAIL(expression.args_[0]->eval_batch(context, needed, size))) return ret;
  // The cast/codec itself retains its existing per-value protocol. Only its
  // source is prefetched, so a nested batch-capable function is not scalarized.
  if (OB_FAIL(expr_default_eval_batch_func(expression, context, needed, size))) return ret;
  failure.success = true;
  return OB_SUCCESS;
} catch (const std::bad_alloc &) { return OB_ALLOCATE_MEMORY_FAILED;
} catch (...) { return OB_ERR_UNEXPECTED; }

bool PluginCastExtraInfo::valid() const
{
  const auto &b = binding_;
  if (b.struct_size < sizeof(b) || !b.owner_generation || !b.catalog_epoch || b.reserved_word ||
      b.requested_context < SEEKDB_PLUGIN_CAST_EXPLICIT || b.requested_context > SEEKDB_PLUGIN_CAST_IMPLICIT ||
      b.declared_context < b.requested_context || b.declared_context > SEEKDB_PLUGIN_CAST_IMPLICIT || decode_source_ > 1)
    return false;
  for (const char *id : {b.object_id, b.owner_plugin_id, b.source_type_id, b.target_type_id}) {
    if (!id[0] || !std::memchr(id, 0, SEEKDB_PLUGIN_MAX_IDENTIFIER_BYTES + 1)) return false;
  }
  for (const auto word : b.reserved) if (word) return false;
  return !decode_source_ || (source_.valid() && source_.index_ == 0 &&
      source_.binding_.catalog_epoch == b.catalog_epoch && std::strcmp(source_.binding_.object_id, b.source_type_id) == 0);
}

OB_DEF_SERIALIZE(PluginCastExtraInfo)
{
  int ret = valid() ? OB_SUCCESS : OB_INVALID_DATA;
  for (const char *id : {binding_.object_id, binding_.owner_plugin_id, binding_.source_type_id, binding_.target_type_id}) {
    if (OB_SUCC(ret)) { const ObString field = ObString::make_string(id); OB_UNIS_ENCODE(field); }
  }
  LST_DO_CODE(OB_UNIS_ENCODE, binding_.requested_context, binding_.declared_context,
              binding_.owner_generation, binding_.catalog_epoch, decode_source_);
  if (OB_SUCC(ret) && decode_source_) { OB_UNIS_ENCODE(source_); }
  return ret;
}

OB_DEF_DESERIALIZE(PluginCastExtraInfo)
{
  int ret = OB_SUCCESS;
  binding_ = {}; binding_.struct_size = sizeof(binding_); decode_source_ = 0; source_ = {};
  for (char *id : {binding_.object_id, binding_.owner_plugin_id, binding_.source_type_id, binding_.target_type_id}) {
    ObString field; OB_UNIS_DECODE(field);
    if (OB_SUCC(ret)) {
      if (field.length() <= 0 || field.length() > SEEKDB_PLUGIN_MAX_IDENTIFIER_BYTES ||
          !field.ptr() || std::memchr(field.ptr(), 0, field.length())) ret = OB_INVALID_DATA;
      else std::memcpy(id, field.ptr(), field.length());
    }
  }
  LST_DO_CODE(OB_UNIS_DECODE, binding_.requested_context, binding_.declared_context,
              binding_.owner_generation, binding_.catalog_epoch, decode_source_);
  if (OB_SUCC(ret) && decode_source_ == 1) { OB_UNIS_DECODE(source_); }
  if (OB_SUCC(ret) && !valid()) ret = OB_INVALID_DATA;
  if (OB_FAIL(ret)) { binding_ = {}; decode_source_ = 0; source_ = {}; }
  return ret;
}

OB_DEF_SERIALIZE_SIZE(PluginCastExtraInfo)
{
  int64_t len = 0;
  if (!valid()) return len;
  for (const char *id : {binding_.object_id, binding_.owner_plugin_id, binding_.source_type_id, binding_.target_type_id}) {
    const ObString field = ObString::make_string(id); OB_UNIS_ADD_LEN(field);
  }
  LST_DO_CODE(OB_UNIS_ADD_LEN, binding_.requested_context, binding_.declared_context,
              binding_.owner_generation, binding_.catalog_epoch, decode_source_);
  if (decode_source_) { OB_UNIS_ADD_LEN(source_); }
  return len;
}

int PluginCastExtraInfo::deep_copy(ObIAllocator &allocator, ObExprOperatorType type, ObIExprExtraInfo *&out) const
{
  out = nullptr;
  if (type != T_FUN_SYS_PLUGIN_CAST || !valid()) return OB_INVALID_ARGUMENT;
  int ret = ObExprExtraInfoFactory::alloc(allocator, type, out);
  if (OB_SUCC(ret)) {
    auto *copy = static_cast<PluginCastExtraInfo *>(out);
    copy->binding_ = binding_; copy->decode_source_ = decode_source_; copy->source_ = source_;
  }
  return ret;
}

PluginCastExpr::PluginCastExpr(ObIAllocator &allocator)
    : ObFuncExprOperator(allocator, T_FUN_SYS_PLUGIN_CAST, "__seekdb_plugin_cast",
                        2, NOT_VALID_FOR_GENERATED_COL, NOT_ROW_DIMENSION, true)
{}

namespace {
bool cast_input_supported(ObObjType type)
{
  return ob_is_null(type) || ob_is_integer_type(type) || ob_is_float_type(type) ||
      ob_is_double_type(type) || ob_is_string_or_lob_type(type) || ob_is_geometry(type);
}

bool cast_source_matches(const ObRawExpr &value, const PluginCastExtraInfo &info)
{
  const auto *source = value.get_plugin_type();
  if (!cast_input_supported(value.get_data_type()) ||
      (source && source->physical_type_ != value.get_data_type())) return false;
  if (source && source->catalog_epoch_ && source->catalog_epoch_ != info.binding_.catalog_epoch) return false;
  if (info.decode_source_) {
    const auto &b = info.source_.binding_;
    return source && source->stored_ && source->logical_id_ == ObString::make_string(b.object_id) &&
        source->sql_name_ == ObString::make_string(b.sql_name) && source->owner_ == ObString::make_string(b.owner_plugin_id) &&
        source->format_ == ObString::make_string(b.physical_format_id) && source->format_version_ == b.physical_format_version;
  }
  if (source) return !source->stored_ && source->logical_id_ == ObString::make_string(info.binding_.source_type_id);
  const char *id = core_type_identifier(value.get_data_type());
  return id && std::strcmp(id, info.binding_.source_type_id) == 0;
}
} // namespace

int PluginCastExpr::read_binding(const ObRawExpr &raw, PluginCastExtraInfo &info, uint32_t depth)
{
  info.binding_ = {}; info.decode_source_ = 0; info.source_ = {};
  if (depth >= 64) return OB_SIZE_OVERFLOW;
  if (raw.get_expr_type() != T_FUN_SYS_PLUGIN_CAST || raw.get_param_count() != 2 ||
      !raw.get_param_expr(0) || !raw.get_param_expr(1) || !raw.get_param_expr(1)->is_const_raw_expr()) return OB_INVALID_DATA;
  const auto &literal = static_cast<const ObConstRawExpr *>(raw.get_param_expr(1))->get_value();
  // ObObj::is_varchar() specifically excludes binary collation.
  if (!literal.is_varbinary() || literal.is_null()) return OB_INVALID_DATA;
  const ObString wire = literal.get_string();
  if (!wire.ptr() || wire.length() <= 0 || wire.length() > 4096) return OB_INVALID_DATA;
  int64_t pos = 0;
  int ret = info.deserialize(wire.ptr(), wire.length(), pos);
  if (OB_SUCC(ret) && (pos != wire.length() || !info.valid() || !cast_source_matches(*raw.get_param_expr(0), info)))
    ret = OB_STATE_NOT_MATCH;
  if (OB_SUCC(ret) && raw.get_plugin_type() && raw.get_plugin_type()->catalog_epoch_ != info.binding_.catalog_epoch)
    ret = OB_STATE_NOT_MATCH;
  if (OB_SUCC(ret) && raw.get_param_expr(0)->get_expr_type() == T_FUN_SYS_PLUGIN_TYPE_VALUE) {
    ObArenaAllocator temporary;
    PluginTypeValueExtraInfo typed(temporary, T_FUN_SYS_PLUGIN_TYPE_VALUE);
    if (OB_FAIL(PluginTypeValueExpr::read_binding(*raw.get_param_expr(0), typed, depth + 1))) {
    } else if (typed.catalog_epoch_ != info.binding_.catalog_epoch) ret = OB_STATE_NOT_MATCH;
  }
  if (OB_FAIL(ret)) info.binding_ = {};
  return ret;
}

int PluginCastExpr::build(ObRawExprFactory &factory, const ObString &target_type,
    seekdb_plugin_cast_context_t requested_context, ObRawExpr *&value, const ObSQLSessionInfo *session)
try {
  if (!value || !session || !share::g_mp || target_type.empty() || target_type.length() > SEEKDB_PLUGIN_MAX_IDENTIFIER_BYTES ||
      !target_type.ptr() || std::memchr(target_type.ptr(), 0, target_type.length()) ||
      requested_context < SEEKDB_PLUGIN_CAST_EXPLICIT || requested_context > SEEKDB_PLUGIN_CAST_IMPLICIT) return OB_INVALID_ARGUMENT;
  int ret = value->deduce_type(session);
  if (OB_FAIL(ret)) return ret;
  if (ob_is_null(value->get_data_type())) return OB_SUCCESS;
  if (!cast_input_supported(value->get_data_type())) return OB_NOT_SUPPORTED;
  const auto *source = value->get_plugin_type();
  if (source && source->physical_type_ != value->get_data_type()) return OB_STATE_NOT_MATCH;
  const std::string source_id = source ? std::string(source->logical_id_.ptr(), source->logical_id_.length())
                                       : std::string(core_type_identifier(value->get_data_type()));
  const std::string target_id(target_type.ptr(), target_type.length());
  if ((!source || !source->stored_) && source_id == target_id) return OB_SUCCESS;
  PluginCastExtraInfo info(factory.get_allocator(), T_FUN_SYS_PLUGIN_CAST);
  if (OB_FAIL(share::g_mp->resolve_plugin_cast(source_id.c_str(), target_id.c_str(), requested_context,
      &info.binding_, source ? source->catalog_epoch_ : 0))) return ret;
  if (source && source->stored_) {
    info.decode_source_ = 1;
    const std::string name(source->sql_name_.ptr(), source->sql_name_.length());
    if (OB_FAIL(share::g_mp->resolve_plugin_sql_object(SEEKDB_PLUGIN_EXTENSION_TYPE, name.c_str(), nullptr, 0, &info.source_.binding_))) return ret;
  }
  if (!info.valid() || source_id != info.binding_.source_type_id || target_id != info.binding_.target_type_id ||
      requested_context != info.binding_.requested_context || !cast_source_matches(*value, info)) return OB_STATE_NOT_MATCH;
  const int64_t size = info.get_serialize_size();
  if (size <= 0 || size > 4096) return OB_INVALID_DATA;
  auto *wire = static_cast<char *>(factory.get_allocator().alloc(size));
  if (!wire) return OB_ALLOCATE_MEMORY_FAILED;
  int64_t pos = 0;
  if (OB_FAIL(info.serialize(wire, size, pos))) return ret;
  ObConstRawExpr *metadata = nullptr;
  ObSysFunRawExpr *cast = nullptr;
  if (OB_FAIL(factory.create_raw_expr(T_VARCHAR, metadata)) || OB_FAIL(factory.create_raw_expr(T_FUN_SYS_PLUGIN_CAST, cast))) return ret;
  ObObj literal; literal.set_varchar(ObString(size, wire)); literal.set_collation_type(CS_TYPE_BINARY);
  metadata->set_value(literal);
  if (OB_FAIL(cast->init_param_exprs(2)) || OB_FAIL(cast->add_param_expr(value)) || OB_FAIL(cast->add_param_expr(metadata))) return ret;
  cast->set_func_name(ObString::make_string("__seekdb_plugin_cast"));
  if (OB_FAIL(cast->formalize(session))) return ret;
  value = cast;
  return OB_SUCCESS;
} catch (const std::bad_alloc &) { return OB_ALLOCATE_MEMORY_FAILED;
} catch (...) { return OB_ERR_UNEXPECTED; }

int PluginCastExpr::calc_result_type2(ObExprResType &type, ObExprResType &, ObExprResType &, ObExprTypeCtx &context) const
{
  auto *raw = context.get_raw_expr();
  if (!raw || !raw->get_expr_factory()) return OB_INVALID_ARGUMENT;
  PluginCastExtraInfo info(raw->get_expr_factory()->get_allocator(), T_FUN_SYS_PLUGIN_CAST);
  int ret = read_binding(*raw, info);
  if (OB_FAIL(ret)) return ret;
  assign_sql_result_type(type, info.binding_.target_type_id);
  PluginExprType result; result.logical_id_ = ObString::make_string(info.binding_.target_type_id); result.physical_type_ = type.get_type();
  result.catalog_epoch_ = info.binding_.catalog_epoch;
  return raw->set_plugin_type(result);
}

int PluginCastExpr::coerce_sql_cast(ObRawExprFactory *factory, ObSysFunRawExpr &raw,
                                  const ObSQLSessionInfo *session)
try {
  if (raw.get_expr_type() != T_FUN_SYS_CAST || !CM_IS_EXPLICIT_CAST(raw.get_cast_mode())) return OB_SUCCESS;
  if (raw.get_param_count() != 2 || !raw.get_param_expr(0) || !raw.get_param_expr(1)) return OB_INVALID_DATA;
  auto *input = raw.get_param_expr(0);
  const auto *logical = input->get_plugin_type();
  if (!logical) return OB_SUCCESS;
  if (logical->physical_type_ != input->get_data_type()) return OB_STATE_NOT_MATCH;
  // Built-in logical results already have the native SQL datum representation.
  // In particular, the output of an inserted plugin cast must not be rebound
  // during repeated type inference. Do not classify arbitrary *.int64 as core.
  const std::string source(logical->logical_id_.ptr(), logical->logical_id_.length());
  if (!logical->stored_) {
    for (const char *suffix : {".bytes", ".geometry", ".bool", ".int32", ".uint32", ".int64", ".uint64", ".float64"}) {
      if (is_builtin_type(source.c_str(), suffix)) return OB_SUCCESS;
    }
  }
  if (!factory) return OB_INVALID_ARGUMENT;
  const auto *argument = raw.get_param_expr(1);
  if (!argument->is_const_raw_expr()) return OB_INVALID_DATA;
  const auto &packed = static_cast<const ObConstRawExpr *>(argument)->get_value();
  if (!packed.is_int()) return OB_INVALID_DATA;
  ParseNode descriptor;
  descriptor.value_ = packed.get_int();
  const auto target = static_cast<ObObjType>(descriptor.int16_values_[OB_NODE_CAST_TYPE_IDX]);
  const char *target_id = nullptr;
  if (ob_is_string_or_lob_type(target)) target_id = "core.type.bytes";
  else if (ob_is_integer_type(target)) target_id = ob_is_uint_tc(target) ? "core.type.uint64" : "core.type.int64";
  else if (ob_is_float_type(target) || ob_is_double_type(target)) target_id = "core.type.float64";
  else if (ob_is_geometry(target)) target_id = "core.type.geometry";
  if (!target_id) return OB_NOT_SUPPORTED;
  ObRawExpr *converted = input;
  int ret = build(*factory, ObString::make_string(target_id), SEEKDB_PLUGIN_CAST_EXPLICIT, converted, session);
  if (OB_FAIL(ret)) return ret == OB_ENTRY_NOT_EXIST ? OB_ERR_INVALID_TYPE_FOR_OP : ret;
  raw.get_param_expr(0) = converted;
  return OB_SUCCESS;
} catch (const std::bad_alloc &) { return OB_ALLOCATE_MEMORY_FAILED;
} catch (...) { return OB_ERR_UNEXPECTED; }

int PluginCastExpr::cg_expr(ObExprCGCtx &context, const ObRawExpr &raw, ObExpr &runtime) const
{
  if (!context.allocator_) return OB_INVALID_ARGUMENT;
  PluginCastExtraInfo info(*context.allocator_, T_FUN_SYS_PLUGIN_CAST);
  int ret = read_binding(raw, info);
  if (OB_FAIL(ret)) return ret;
  ObExprResType expected; assign_sql_result_type(expected, info.binding_.target_type_id);
  if (raw.get_data_type() != expected.get_type() || !raw.get_plugin_type() || raw.get_plugin_type()->stored_ ||
      raw.get_plugin_type()->logical_id_ != ObString::make_string(info.binding_.target_type_id)) return OB_STATE_NOT_MATCH;
  if (OB_FAIL(info.deep_copy(*context.allocator_, T_FUN_SYS_PLUGIN_CAST, runtime.extra_info_))) return ret;
  runtime.eval_func_ = evaluate;
  runtime.eval_batch_func_ = PluginFunctionExpr::evaluate_argument_batch;
  return OB_SUCCESS;
}

int PluginCastExpr::evaluate(const ObExpr &expression, ObEvalCtx &context, ObDatum &result)
try {
  const auto *info = dynamic_cast<const PluginCastExtraInfo *>(expression.extra_info_);
  if (!share::g_mp || !info || !info->valid() || expression.arg_cnt_ != 2 || !expression.args_[0]) return OB_INVALID_DATA;
  ObDatum *datum = nullptr;
  int ret = expression.args_[0]->eval(context, datum);
  if (OB_FAIL(ret)) return ret;
  if (!datum) return OB_ERR_UNEXPECTED;
  const auto &child = *expression.args_[0];
  const auto physical = child.datum_meta_.type_;
  if (!cast_input_supported(physical)) return OB_NOT_SUPPORTED;
  ObEvalCtx::TempAllocGuard temporary(context);
  seekdb_plugin_execution_value_v1_t value = {};
  value.struct_size = sizeof(value); value.type_id = info->binding_.source_type_id; value.is_null = datum->is_null();
  ArgumentStorage storage;
  if (!datum->is_null()) {
    if (info->decode_source_ || ob_is_string_or_lob_type(physical) || ob_is_geometry(physical)) {
      if (info->decode_source_ && !ob_is_string_or_lob_type(physical)) return OB_STATE_NOT_MATCH;
      ObString bytes;
      if (OB_FAIL(ObTextStringHelper::read_real_string_data_with_copy(context.exec_ctx_, temporary.get_allocator(),
          *datum, child.datum_meta_, child.obj_meta_.has_lob_header(), bytes))) return ret;
      if (info->decode_source_) {
        uint64_t total = 0;
        DecodeSink decoded{temporary.get_allocator(), value.type_id, total, {}};
        seekdb_plugin_execution_context_v1_t codec = {};
        codec.struct_size = sizeof(codec); codec.host = reinterpret_cast<seekdb_plugin_host_handle_t *>(&decoded);
        codec.emit_result = emit_decoded_argument;
        if (OB_FAIL(share::g_mp->decode_bound_plugin_type(&info->source_.binding_, &codec,
            reinterpret_cast<const uint8_t *>(bytes.ptr()), bytes.length()))) return ret;
        if (decoded.status_ == SEEKDB_PLUGIN_STATUS_NO_MEMORY) return OB_ALLOCATE_MEMORY_FAILED;
        if (!decoded.emitted_ || decoded.status_ != SEEKDB_PLUGIN_STATUS_OK) return OB_INVALID_DATA;
        bytes = decoded.bytes_; value.is_null = decoded.null_;
      }
      value.data = reinterpret_cast<const uint8_t *>(bytes.ptr()); value.data_size = bytes.length();
    } else if (ob_is_integer_type(physical)) {
      storage.integer_ = datum->get_int(); value.data = reinterpret_cast<const uint8_t *>(&storage.integer_); value.data_size = sizeof(storage.integer_);
      if (is_builtin_type(value.type_id, ".bool")) {
        storage.boolean_ = storage.integer_ != 0; value.data = &storage.boolean_; value.data_size = sizeof(storage.boolean_);
      } else if (is_builtin_type(value.type_id, ".int32")) {
        storage.int32_ = static_cast<int32_t>(storage.integer_); value.data = reinterpret_cast<const uint8_t *>(&storage.int32_); value.data_size = sizeof(storage.int32_);
      } else if (is_builtin_type(value.type_id, ".uint32")) {
        storage.uint32_ = static_cast<uint32_t>(storage.integer_); value.data = reinterpret_cast<const uint8_t *>(&storage.uint32_); value.data_size = sizeof(storage.uint32_);
      }
    } else {
      storage.floating_ = ob_is_float_type(physical) ? datum->get_float() : datum->get_double();
      value.data = reinterpret_cast<const uint8_t *>(&storage.floating_); value.data_size = sizeof(storage.floating_);
    }
  }
  uint64_t total = 0;
  DecodeSink output{temporary.get_allocator(), info->binding_.target_type_id, total, {}};
  PluginSqlContext sql_context(context.exec_ctx_);
  seekdb_plugin_execution_context_v2_t callback = {};
  callback.v1.struct_size = sizeof(callback); callback.v1.host = reinterpret_cast<seekdb_plugin_host_handle_t *>(&output);
  callback.v1.emit_result = emit_decoded_argument; sql_context.attach(callback);
  ret = share::g_mp->execute_bound_plugin_cast(&info->binding_, &callback.v1, &value);
  if (sql_context.error() != OB_SUCCESS) return sql_context.error();
  if (OB_FAIL(ret)) return ret;
  if (output.status_ == SEEKDB_PLUGIN_STATUS_NO_MEMORY) return OB_ALLOCATE_MEMORY_FAILED;
  if (!output.emitted_ || output.status_ != SEEKDB_PLUGIN_STATUS_OK) return OB_INVALID_DATA;
  seekdb_plugin_execution_result_v1_t converted = {};
  converted.struct_size = sizeof(converted); converted.type_id = info->binding_.target_type_id; converted.is_null = output.null_;
  converted.data = reinterpret_cast<const uint8_t *>(output.bytes_.ptr()); converted.data_size = output.bytes_.length();
  ResultSink sink{&expression, &context, &result, false, info->binding_.target_type_id};
  const auto status = emit_sql_result(reinterpret_cast<seekdb_plugin_host_handle_t *>(&sink), &converted);
  return status == SEEKDB_PLUGIN_STATUS_OK ? OB_SUCCESS : status == SEEKDB_PLUGIN_STATUS_NO_MEMORY ? OB_ALLOCATE_MEMORY_FAILED : OB_INVALID_DATA;
} catch (const std::bad_alloc &) { return OB_ALLOCATE_MEMORY_FAILED;
} catch (...) { return OB_ERR_UNEXPECTED; }

bool PluginTypeValueExtraInfo::valid() const
{
  if (!logical_id_[0] || !std::memchr(logical_id_, 0, sizeof(logical_id_)) ||
      !catalog_epoch_ || mode_ > ORDERED) return false;
  if (mode_ == ORDERED) return ordering_ && ordering_->valid() && !ordering_->null_safe_ &&
      ordering_->binding_.catalog_epoch == catalog_epoch_ &&
      std::strcmp(ordering_->binding_.object_id, logical_id_) == 0;
  return mode_ != DECODE || (source_.valid() && source_.index_ == 0 &&
      source_.binding_.catalog_epoch == catalog_epoch_ && std::strcmp(source_.binding_.object_id, logical_id_) == 0);
}

OB_DEF_SERIALIZE(PluginTypeValueExtraInfo)
{
  int ret = valid() ? OB_SUCCESS : OB_INVALID_DATA;
  if (OB_SUCC(ret)) {
    const ObString logical = ObString::make_string(logical_id_);
    LST_DO_CODE(OB_UNIS_ENCODE, logical, catalog_epoch_, mode_);
    if (mode_ == DECODE) { OB_UNIS_ENCODE(source_); }
    if (mode_ == ORDERED) { OB_UNIS_ENCODE(*ordering_); }
  }
  return ret;
}

OB_DEF_DESERIALIZE(PluginTypeValueExtraInfo)
{
  int ret = OB_SUCCESS;
  std::memset(logical_id_, 0, sizeof(logical_id_)); catalog_epoch_ = 0; mode_ = IDENTITY; source_ = {};
  ordering_ = nullptr;
  ObString logical;
  LST_DO_CODE(OB_UNIS_DECODE, logical, catalog_epoch_, mode_);
  if (OB_SUCC(ret)) {
    if (!logical.ptr() || logical.empty() || logical.length() > SEEKDB_PLUGIN_MAX_IDENTIFIER_BYTES ||
        std::memchr(logical.ptr(), 0, logical.length())) ret = OB_INVALID_DATA;
    else std::memcpy(logical_id_, logical.ptr(), logical.length());
  }
  if (OB_SUCC(ret) && mode_ == DECODE) { OB_UNIS_DECODE(source_); }
  if (OB_SUCC(ret) && mode_ == ORDERED) {
    void *memory = allocator_.alloc(sizeof(PluginTypeComparisonExtraInfo));
    if (!memory) ret = OB_ALLOCATE_MEMORY_FAILED;
    else {
      ordering_ = new (memory) PluginTypeComparisonExtraInfo(allocator_, T_FUN_SYS_PLUGIN_TYPE_COMPARE);
      OB_UNIS_DECODE(*ordering_);
    }
  }
  if (OB_SUCC(ret) && !valid()) ret = OB_INVALID_DATA;
  if (OB_FAIL(ret)) { catalog_epoch_ = 0; logical_id_[0] = 0; source_ = {}; }
  return ret;
}

OB_DEF_SERIALIZE_SIZE(PluginTypeValueExtraInfo)
{
  int64_t len = 0;
  if (valid()) {
    const ObString logical = ObString::make_string(logical_id_);
    LST_DO_CODE(OB_UNIS_ADD_LEN, logical, catalog_epoch_, mode_);
    if (mode_ == DECODE) { OB_UNIS_ADD_LEN(source_); }
    if (mode_ == ORDERED) { OB_UNIS_ADD_LEN(*ordering_); }
  }
  return len;
}

int PluginTypeValueExtraInfo::deep_copy(ObIAllocator &allocator, ObExprOperatorType type, ObIExprExtraInfo *&out) const
{
  out = nullptr;
  if (type != T_FUN_SYS_PLUGIN_TYPE_VALUE || !valid()) return OB_INVALID_ARGUMENT;
  int ret = ObExprExtraInfoFactory::alloc(allocator, type, out);
  if (OB_SUCC(ret)) {
    auto *copy = static_cast<PluginTypeValueExtraInfo *>(out);
    std::memcpy(copy->logical_id_, logical_id_, sizeof(logical_id_));
    copy->catalog_epoch_ = catalog_epoch_; copy->mode_ = mode_; copy->source_ = source_;
    if (mode_ == ORDERED) {
      ObIExprExtraInfo *ordering = nullptr;
      if (OB_FAIL(ordering_->deep_copy(allocator, T_FUN_SYS_PLUGIN_TYPE_COMPARE, ordering))) return ret;
      copy->ordering_ = static_cast<PluginTypeComparisonExtraInfo *>(ordering);
    }
  }
  return ret;
}

PluginTypeValueExpr::PluginTypeValueExpr(ObIAllocator &allocator)
    : ObFuncExprOperator(allocator, T_FUN_SYS_PLUGIN_TYPE_VALUE, "__seekdb_plugin_type_value",
                        2, NOT_VALID_FOR_GENERATED_COL, NOT_ROW_DIMENSION, true)
{}

namespace {
bool type_value_source_matches(const ObRawExpr &value, const PluginTypeValueExtraInfo &info)
{
  if (info.mode_ == PluginTypeValueExtraInfo::TYPED_NULL) return ob_is_null(value.get_data_type());
  const auto *logical = value.get_plugin_type();
  if (!logical || logical->physical_type_ != value.get_data_type() ||
      logical->logical_id_ != ObString::make_string(info.logical_id_)) return false;
  if (logical->catalog_epoch_ && logical->catalog_epoch_ != info.catalog_epoch_) return false;
  if (info.mode_ == PluginTypeValueExtraInfo::DECODE) {
    const auto &b = info.source_.binding_;
    return logical->stored_ && ob_is_string_or_lob_type(value.get_data_type()) &&
        logical->sql_name_ == ObString::make_string(b.sql_name) && logical->owner_ == ObString::make_string(b.owner_plugin_id) &&
        logical->format_ == ObString::make_string(b.physical_format_id) && logical->format_version_ == b.physical_format_version;
  }
  ObExprResType expected; assign_sql_result_type(expected, info.logical_id_);
  return !logical->stored_ && (expected.get_type() == value.get_data_type() ||
      (expected.get_type() == ObVarcharType && ob_is_string_or_lob_type(value.get_data_type())));
}
}

int PluginTypeValueExpr::read_binding(const ObRawExpr &raw, PluginTypeValueExtraInfo &info, uint32_t depth)
{
  info.catalog_epoch_ = 0; info.logical_id_[0] = 0; info.source_ = {};
  if (depth >= 64) return OB_SIZE_OVERFLOW;
  if (raw.get_expr_type() != T_FUN_SYS_PLUGIN_TYPE_VALUE || raw.get_param_count() != 2 ||
      !raw.get_param_expr(0) || !raw.get_param_expr(1) || !raw.get_param_expr(1)->is_const_raw_expr()) return OB_INVALID_DATA;
  const auto &literal = static_cast<const ObConstRawExpr *>(raw.get_param_expr(1))->get_value();
  if (!literal.is_varbinary()) return OB_INVALID_DATA;
  const auto wire = literal.get_string();
  if (!wire.ptr() || wire.length() <= 0 || wire.length() > 4096) return OB_INVALID_DATA;
  int64_t pos = 0;
  int ret = info.deserialize(wire.ptr(), wire.length(), pos);
  if (OB_SUCC(ret) && (pos != wire.length() || !info.valid() || !type_value_source_matches(*raw.get_param_expr(0), info)))
    ret = OB_STATE_NOT_MATCH;
  if (OB_SUCC(ret) && raw.get_plugin_type() && raw.get_plugin_type()->catalog_epoch_ != info.catalog_epoch_)
    ret = OB_STATE_NOT_MATCH;
  if (OB_SUCC(ret) && raw.get_param_expr(0)->get_expr_type() == T_FUN_SYS_PLUGIN_CAST) {
    ObArenaAllocator temporary;
    PluginCastExtraInfo cast(temporary, T_FUN_SYS_PLUGIN_CAST);
    if (OB_FAIL(PluginCastExpr::read_binding(*raw.get_param_expr(0), cast, depth + 1))) {
    } else if (cast.binding_.catalog_epoch != info.catalog_epoch_) ret = OB_STATE_NOT_MATCH;
  }
  if (OB_SUCC(ret) && raw.get_param_expr(0)->get_expr_type() == T_FUN_SYS_PLUGIN_TYPE_VALUE) {
    ObArenaAllocator temporary;
    PluginTypeValueExtraInfo nested(temporary, T_FUN_SYS_PLUGIN_TYPE_VALUE);
    if (OB_FAIL(read_binding(*raw.get_param_expr(0), nested, depth + 1))) {
    } else if (nested.catalog_epoch_ != info.catalog_epoch_) ret = OB_STATE_NOT_MATCH;
  }
  if (OB_FAIL(ret)) info.catalog_epoch_ = 0;
  return ret;
}

int PluginTypeValueExpr::build(ObRawExprFactory &factory, const ObString &sql_type,
                              ObRawExpr *&value, const ObSQLSessionInfo *session)
try {
  if (!value || !session || !share::g_mp || sql_type.empty() || !sql_type.ptr() ||
      sql_type.length() > SEEKDB_PLUGIN_MAX_IDENTIFIER_BYTES || std::memchr(sql_type.ptr(), 0, sql_type.length())) return OB_INVALID_ARGUMENT;
  int ret = value->deduce_type(session);
  if (OB_FAIL(ret)) return ret;
  seekdb_plugin_sql_binding_v1_t target = {};
  const std::string name(sql_type.ptr(), sql_type.length());
  if (OB_FAIL(share::g_mp->resolve_plugin_sql_object(SEEKDB_PLUGIN_EXTENSION_TYPE, name.c_str(), nullptr, 0, &target)))
    return ret == OB_ENTRY_NOT_EXIST ? OB_ERR_INVALID_DATATYPE : ret;
  if (target.struct_size < sizeof(target) || target.kind != SEEKDB_PLUGIN_EXTENSION_TYPE ||
      !target.catalog_epoch || !target.owner_generation || !target.object_id[0] ||
      !std::memchr(target.object_id, 0, sizeof(target.object_id))) return OB_INVALID_DATA;
  for (const char *id : {target.sql_name, target.owner_plugin_id}) {
    if (!id[0] || !std::memchr(id, 0, SEEKDB_PLUGIN_MAX_IDENTIFIER_BYTES + 1)) return OB_INVALID_DATA;
  }
  for (const auto word : target.reserved) if (word) return OB_INVALID_DATA;
  PluginTypeValueExtraInfo info(factory.get_allocator(), T_FUN_SYS_PLUGIN_TYPE_VALUE);
  std::strcpy(info.logical_id_, target.object_id); info.catalog_epoch_ = target.catalog_epoch;
  ObRawExpr *converted = value;
  const auto *source = value->get_plugin_type();
  if (source && source->catalog_epoch_ && source->catalog_epoch_ != target.catalog_epoch) return OB_STATE_NOT_MATCH;
  if (ob_is_null(value->get_data_type())) {
    info.mode_ = PluginTypeValueExtraInfo::TYPED_NULL;
  } else if (source && source->logical_id_ == ObString::make_string(target.object_id)) {
    if (source->stored_) {
      info.mode_ = PluginTypeValueExtraInfo::DECODE;
      const std::string source_name(source->sql_name_.ptr(), source->sql_name_.length());
      if (source_name == name) info.source_.binding_ = target;
      else if (OB_FAIL(share::g_mp->resolve_plugin_sql_object(SEEKDB_PLUGIN_EXTENSION_TYPE,
          source_name.c_str(), nullptr, 0, &info.source_.binding_))) return ret;
    }
  } else {
    if (OB_FAIL(PluginCastExpr::build(factory, ObString::make_string(target.object_id),
        SEEKDB_PLUGIN_CAST_EXPLICIT, converted, session))) return ret == OB_ENTRY_NOT_EXIST ? OB_ERR_INVALID_TYPE_FOR_OP : ret;
  }
  if (!info.valid() || !type_value_source_matches(*converted, info)) return OB_STATE_NOT_MATCH;
  const int64_t size = info.get_serialize_size();
  if (size <= 0 || size > 4096) return OB_INVALID_DATA;
  auto *wire = static_cast<char *>(factory.get_allocator().alloc(size));
  if (!wire) return OB_ALLOCATE_MEMORY_FAILED;
  int64_t pos = 0;
  if (OB_FAIL(info.serialize(wire, size, pos))) return ret;
  ObConstRawExpr *metadata = nullptr;
  ObSysFunRawExpr *typed = nullptr;
  if (OB_FAIL(factory.create_raw_expr(T_VARCHAR, metadata)) || OB_FAIL(factory.create_raw_expr(T_FUN_SYS_PLUGIN_TYPE_VALUE, typed))) return ret;
  ObObj literal; literal.set_varchar(ObString(size, wire)); literal.set_collation_type(CS_TYPE_BINARY); metadata->set_value(literal);
  if (OB_FAIL(typed->init_param_exprs(2)) || OB_FAIL(typed->add_param_expr(converted)) || OB_FAIL(typed->add_param_expr(metadata))) return ret;
  typed->set_func_name(ObString::make_string("__seekdb_plugin_type_value"));
  if (OB_FAIL(typed->formalize(session))) return ret;
  value = typed;
  return OB_SUCCESS;
} catch (const std::bad_alloc &) { return OB_ALLOCATE_MEMORY_FAILED;
} catch (...) { return OB_ERR_UNEXPECTED; }

int PluginTypeValueExpr::prepare(ObRawExprFactory &factory, const ObString &sql_type, ObRawExpr *&value)
{
  if (!value || sql_type.empty() || !sql_type.ptr() || sql_type.length() > SEEKDB_PLUGIN_MAX_IDENTIFIER_BYTES ||
      std::memchr(sql_type.ptr(), 0, sql_type.length())) return OB_INVALID_ARGUMENT;
  int ret = OB_SUCCESS;
  ObString owned;
  ObConstRawExpr *metadata = nullptr;
  ObSysFunRawExpr *typed = nullptr;
  if (OB_FAIL(ob_write_string(factory.get_allocator(), sql_type, owned)) ||
      OB_FAIL(factory.create_raw_expr(T_VARCHAR, metadata)) || OB_FAIL(factory.create_raw_expr(T_FUN_SYS_PLUGIN_TYPE_VALUE, typed))) return ret;
  ObObj literal; literal.set_varchar(owned); literal.set_collation_type(CS_TYPE_UTF8MB4_BIN); metadata->set_value(literal);
  if (OB_FAIL(typed->init_param_exprs(2)) || OB_FAIL(typed->add_param_expr(value)) || OB_FAIL(typed->add_param_expr(metadata))) return ret;
  typed->set_func_name(ObString::make_string("__seekdb_plugin_type_value"));
  value = typed;
  return OB_SUCCESS;
}

int PluginTypeValueExpr::calc_result_type2(ObExprResType &type, ObExprResType &value, ObExprResType &metadata, ObExprTypeCtx &context) const
{
  auto *raw = context.get_raw_expr();
  if (!raw) return OB_INVALID_ARGUMENT;
  if (raw->get_param_count() != 2 || !raw->get_param_expr(0) || !raw->get_param_expr(1) ||
      !raw->get_param_expr(1)->is_const_raw_expr()) return OB_INVALID_DATA;
  const auto &name = static_cast<const ObConstRawExpr *>(raw->get_param_expr(1))->get_value();
  if (!raw->get_plugin_type() && name.is_varchar() && name.get_collation_type() == CS_TYPE_UTF8MB4_BIN) {
    if (!raw->get_expr_factory()) return OB_INVALID_ARGUMENT;
    ObRawExpr *bound = raw->get_param_expr(0);
    int ret = build(*raw->get_expr_factory(), name.get_string(), bound, context.get_session());
    if (OB_FAIL(ret)) return ret;
    raw->get_param_expr(0) = bound->get_param_expr(0);
    raw->get_param_expr(1) = bound->get_param_expr(1);
    // The caller gathered parameter metadata before lowering. Update both
    // calc types so its ordinary implicit-cast pass cannot reinterpret the
    // newly bound wire as text, or cast the converted value back to a LOB.
    value = raw->get_param_expr(0)->get_result_type();
    value.set_calc_meta(value); value.set_calc_accuracy(value.get_accuracy());
    metadata = raw->get_param_expr(1)->get_result_type();
    metadata.set_calc_meta(metadata); metadata.set_calc_accuracy(metadata.get_accuracy());
  }
  ObArenaAllocator temporary;
  PluginTypeValueExtraInfo info(temporary, T_FUN_SYS_PLUGIN_TYPE_VALUE);
  int ret = read_binding(*raw, info);
  if (OB_FAIL(ret)) return ret;
  assign_sql_result_type(type, info.logical_id_);
  PluginExprType logical; logical.logical_id_ = ObString::make_string(info.logical_id_); logical.physical_type_ = type.get_type();
  logical.catalog_epoch_ = info.catalog_epoch_;
  return raw->set_plugin_type(logical);
}

int PluginTypeValueExpr::cg_expr(ObExprCGCtx &context, const ObRawExpr &raw, ObExpr &runtime) const
{
  if (!context.allocator_) return OB_INVALID_ARGUMENT;
  PluginTypeValueExtraInfo info(*context.allocator_, T_FUN_SYS_PLUGIN_TYPE_VALUE);
  int ret = read_binding(raw, info);
  if (OB_FAIL(ret)) return ret;
  ObExprResType expected; assign_sql_result_type(expected, info.logical_id_);
  if (raw.get_data_type() != expected.get_type() || !raw.get_plugin_type() || raw.get_plugin_type()->stored_ ||
      raw.get_plugin_type()->logical_id_ != ObString::make_string(info.logical_id_)) return OB_STATE_NOT_MATCH;
  if (OB_FAIL(info.deep_copy(*context.allocator_, T_FUN_SYS_PLUGIN_TYPE_VALUE, runtime.extra_info_))) return ret;
  runtime.eval_func_ = evaluate;
  runtime.eval_batch_func_ = PluginFunctionExpr::evaluate_argument_batch;
  return OB_SUCCESS;
}

int PluginTypeValueExpr::evaluate(const ObExpr &expression, ObEvalCtx &context, ObDatum &result)
try {
  const auto *info = dynamic_cast<const PluginTypeValueExtraInfo *>(expression.extra_info_);
  if (!info || !info->valid() || expression.arg_cnt_ != 2 || !expression.args_[0]) return OB_INVALID_DATA;
  ObDatum *datum = nullptr;
  int ret = expression.args_[0]->eval(context, datum);
  if (OB_FAIL(ret)) return ret;
  if (!datum) return OB_ERR_UNEXPECTED;
  if (datum->is_null()) { result.set_null(); return OB_SUCCESS; }
  if (info->mode_ == PluginTypeValueExtraInfo::TYPED_NULL) return OB_STATE_NOT_MATCH;
  const auto &child = *expression.args_[0];
  const auto physical = child.datum_meta_.type_;
  ObEvalCtx::TempAllocGuard temporary(context);
  seekdb_plugin_execution_result_v1_t output = {};
  output.struct_size = sizeof(output); output.type_id = info->logical_id_;
  ArgumentStorage storage;
  if (info->mode_ == PluginTypeValueExtraInfo::DECODE || ob_is_string_or_lob_type(physical) || ob_is_geometry(physical)) {
    if (info->mode_ == PluginTypeValueExtraInfo::DECODE && (!share::g_mp || !ob_is_string_or_lob_type(physical))) return OB_STATE_NOT_MATCH;
    ObString bytes;
    if (OB_FAIL(ObTextStringHelper::read_real_string_data_with_copy(context.exec_ctx_, temporary.get_allocator(),
        *datum, child.datum_meta_, child.obj_meta_.has_lob_header(), bytes))) return ret;
    if (info->mode_ == PluginTypeValueExtraInfo::DECODE) {
      uint64_t total = 0;
      DecodeSink decoded{temporary.get_allocator(), info->logical_id_, total, {}};
      seekdb_plugin_execution_context_v1_t callback = {};
      callback.struct_size = sizeof(callback); callback.host = reinterpret_cast<seekdb_plugin_host_handle_t *>(&decoded);
      callback.emit_result = emit_decoded_argument;
      if (OB_FAIL(share::g_mp->decode_bound_plugin_type(&info->source_.binding_, &callback,
          reinterpret_cast<const uint8_t *>(bytes.ptr()), bytes.length()))) return ret;
      if (decoded.status_ == SEEKDB_PLUGIN_STATUS_NO_MEMORY) return OB_ALLOCATE_MEMORY_FAILED;
      if (!decoded.emitted_ || decoded.status_ != SEEKDB_PLUGIN_STATUS_OK) return OB_INVALID_DATA;
      bytes = decoded.bytes_; output.is_null = decoded.null_;
    }
    output.data = reinterpret_cast<const uint8_t *>(bytes.ptr()); output.data_size = bytes.length();
  } else if (ob_is_integer_type(physical)) {
    storage.integer_ = datum->get_int(); output.data = reinterpret_cast<const uint8_t *>(&storage.integer_); output.data_size = sizeof(storage.integer_);
    if (is_builtin_type(output.type_id, ".bool")) {
      storage.boolean_ = storage.integer_ != 0; output.data = &storage.boolean_; output.data_size = sizeof(storage.boolean_);
    } else if (is_builtin_type(output.type_id, ".int32")) {
      storage.int32_ = static_cast<int32_t>(storage.integer_); output.data = reinterpret_cast<const uint8_t *>(&storage.int32_); output.data_size = sizeof(storage.int32_);
    } else if (is_builtin_type(output.type_id, ".uint32")) {
      storage.uint32_ = static_cast<uint32_t>(storage.integer_); output.data = reinterpret_cast<const uint8_t *>(&storage.uint32_); output.data_size = sizeof(storage.uint32_);
    }
  } else if (ob_is_double_type(physical)) {
    storage.floating_ = datum->get_double(); output.data = reinterpret_cast<const uint8_t *>(&storage.floating_); output.data_size = sizeof(storage.floating_);
  } else return OB_NOT_SUPPORTED;
  ResultSink sink{&expression, &context, &result, false, info->logical_id_};
  const auto status = emit_sql_result(reinterpret_cast<seekdb_plugin_host_handle_t *>(&sink), &output);
  return status == SEEKDB_PLUGIN_STATUS_OK ? OB_SUCCESS : status == SEEKDB_PLUGIN_STATUS_NO_MEMORY ? OB_ALLOCATE_MEMORY_FAILED : OB_INVALID_DATA;
} catch (const std::bad_alloc &) { return OB_ALLOCATE_MEMORY_FAILED;
} catch (...) { return OB_ERR_UNEXPECTED; }

OB_SERIALIZE_MEMBER(PluginTypeEncodeExtraInfo, target_);

namespace {
bool native_case_identity(const ObString &id)
{
  const std::string text(id.ptr(), id.length());
  for (const char *suffix : {".bytes", ".geometry", ".float64", ".int64", ".uint64", ".int32", ".uint32", ".bool"})
    if (is_builtin_type(text.c_str(), suffix)) return true;
  return false;
}
template <typename Branch>
int prepare_plugin_branches(ObRawExprFactory *factory, const ObSQLSessionInfo *session,
    int64_t count, Branch branch, const PluginExprType *prior, std::string &type_id, uint64_t &epoch)
{
  type_id.clear(); epoch = 0;
  bool plugin = false, custom = false;
  for (int64_t i = 0; i < count; ++i) {
    if (!branch(i)) return OB_INVALID_DATA;
    if (const auto *logical = branch(i)->get_plugin_type()) {
      plugin = true;
      if (logical->physical_type_ != branch(i)->get_data_type()) return OB_STATE_NOT_MATCH;
      if (logical->catalog_epoch_) {
        if (epoch && epoch != logical->catalog_epoch_) return OB_STATE_NOT_MATCH;
        epoch = logical->catalog_epoch_;
      }
      custom |= logical->stored_ || !native_case_identity(logical->logical_id_);
    }
  }
  if (!plugin) return OB_SUCCESS;
  if (prior && epoch && prior->catalog_epoch_ != epoch) return OB_STATE_NOT_MATCH;
  // Built-in numeric promotion/collation must not be replaced by the plugin
  // cast graph. finish() can propagate the resulting native identity/epoch.
  if (!custom) return OB_SUCCESS;
  if (!factory || !session || !share::g_mp) return OB_NOT_INIT;
  if (count > SEEKDB_PLUGIN_MAX_ARGUMENTS) return OB_SIZE_OVERFLOW;
  std::vector<std::string> ids;
  ids.reserve(count);
  for (int64_t i = 0; i < count; ++i) {
    const auto *logical = branch(i)->get_plugin_type();
    if (logical) ids.emplace_back(logical->logical_id_.ptr(), logical->logical_id_.length());
    else {
      if (!cast_input_supported(branch(i)->get_data_type())) return OB_NOT_SUPPORTED;
      const char *id = core_type_identifier(branch(i)->get_data_type());
      ids.emplace_back(id ? id : "");
    }
  }
  int ret = OB_SUCCESS;
  const bool bound = prior != nullptr;
  if (bound) {
    const auto *logical = prior;
    type_id.assign(logical->logical_id_.ptr(), logical->logical_id_.length());
    epoch = logical->catalog_epoch_;
  } else {
    std::vector<const char *> inputs;
    for (const auto &id : ids) inputs.push_back(id.empty() ? nullptr : id.c_str());
    uint64_t selected_epoch = 0;
    if (OB_FAIL(share::g_mp->resolve_plugin_common_type(inputs.data(), inputs.size(), type_id, selected_epoch)))
      return ret == OB_ENTRY_NOT_EXIST ? OB_ERR_INVALID_TYPE_FOR_OP : ret;
    if (!selected_epoch || (epoch && epoch != selected_epoch)) return OB_STATE_NOT_MATCH;
    epoch = selected_epoch;
  }
  // Build replacements off to the side. No partial branch publication
  // when a later conversion/decoder fails or observes a different snapshot.
  std::vector<ObRawExpr *> converted;
  for (int64_t i = 0; i < count; ++i) {
    ObRawExpr *value = branch(i);
    const auto *logical = value->get_plugin_type();
    if (ids[i].empty()) { converted.push_back(value); continue; }
    if (bound && (ids[i] != type_id || (logical && logical->stored_))) return OB_STATE_NOT_MATCH;
    if (ids[i] == type_id) {
      if (logical && logical->stored_) {
        if (OB_FAIL(PluginTypeValueExpr::build(*factory, logical->sql_name_, value, session))) return ret;
      }
    } else if (OB_FAIL(PluginCastExpr::build(*factory, ObString(type_id.size(), type_id.data()),
        SEEKDB_PLUGIN_CAST_IMPLICIT, value, session))) return ret;
    if (value->get_plugin_type() && (value->get_plugin_type()->stored_ ||
        value->get_plugin_type()->catalog_epoch_ != epoch ||
        value->get_plugin_type()->logical_id_ != ObString(type_id.size(), type_id.data()))) return OB_STATE_NOT_MATCH;
    converted.push_back(value);
  }
  for (int64_t i = 0; i < count; ++i) branch(i) = converted[i];
  return OB_SUCCESS;
}
} // namespace

bool PluginTypeComparisonExtraInfo::valid() const
{
  const auto &b = binding_;
  if (null_safe_ > 1 || b.struct_size < sizeof(b) || b.kind != SEEKDB_PLUGIN_EXTENSION_TYPE ||
      !b.owner_generation || !b.catalog_epoch || !b.physical_format_version) return false;
  for (const char *id : {b.sql_name, b.object_id, b.owner_plugin_id, b.physical_format_id}) {
    if (!id[0] || !std::memchr(id, 0, SEEKDB_PLUGIN_MAX_IDENTIFIER_BYTES + 1)) return false;
  }
  for (const auto word : b.reserved) if (word) return false;
  return true;
}

OB_DEF_SERIALIZE(PluginTypeComparisonExtraInfo)
{
  int ret = valid() ? OB_SUCCESS : OB_INVALID_DATA;
  OB_UNIS_ENCODE(null_safe_);
  for (const char *id : {binding_.sql_name, binding_.object_id, binding_.owner_plugin_id, binding_.physical_format_id}) {
    if (OB_SUCC(ret)) { const ObString field = ObString::make_string(id); OB_UNIS_ENCODE(field); }
  }
  LST_DO_CODE(OB_UNIS_ENCODE, binding_.owner_generation, binding_.catalog_epoch, binding_.flags, binding_.physical_format_version);
  return ret;
}

OB_DEF_DESERIALIZE(PluginTypeComparisonExtraInfo)
{
  int ret = OB_SUCCESS;
  binding_ = {}; binding_.struct_size = sizeof(binding_); binding_.kind = SEEKDB_PLUGIN_EXTENSION_TYPE;
  null_safe_ = 0;
  OB_UNIS_DECODE(null_safe_);
  for (char *id : {binding_.sql_name, binding_.object_id, binding_.owner_plugin_id, binding_.physical_format_id}) {
    ObString field;
    OB_UNIS_DECODE(field);
    if (OB_SUCC(ret)) {
      if (!field.ptr() || field.length() <= 0 || field.length() > SEEKDB_PLUGIN_MAX_IDENTIFIER_BYTES ||
          std::memchr(field.ptr(), 0, field.length())) ret = OB_INVALID_DATA;
      else std::memcpy(id, field.ptr(), field.length());
    }
  }
  LST_DO_CODE(OB_UNIS_DECODE, binding_.owner_generation, binding_.catalog_epoch, binding_.flags, binding_.physical_format_version);
  if (OB_SUCC(ret) && !valid()) ret = OB_INVALID_DATA;
  if (OB_FAIL(ret)) binding_ = {};
  return ret;
}

OB_DEF_SERIALIZE_SIZE(PluginTypeComparisonExtraInfo)
{
  int64_t len = 0;
  if (!valid()) return len;
  OB_UNIS_ADD_LEN(null_safe_);
  for (const char *id : {binding_.sql_name, binding_.object_id, binding_.owner_plugin_id, binding_.physical_format_id}) {
    const ObString field = ObString::make_string(id); OB_UNIS_ADD_LEN(field);
  }
  LST_DO_CODE(OB_UNIS_ADD_LEN, binding_.owner_generation, binding_.catalog_epoch, binding_.flags, binding_.physical_format_version);
  return len;
}

int PluginTypeComparisonExtraInfo::deep_copy(ObIAllocator &allocator, ObExprOperatorType type,
    ObIExprExtraInfo *&out) const
{
  out = nullptr;
  if ((type != T_FUN_SYS_PLUGIN_TYPE_COMPARE && type != T_FUN_SYS_PLUGIN_TYPE_BETWEEN && type != T_FUN_SYS_PLUGIN_TYPE_IN) ||
      !valid() || (type != T_FUN_SYS_PLUGIN_TYPE_COMPARE && null_safe_)) return OB_INVALID_ARGUMENT;
  const int ret = ObExprExtraInfoFactory::alloc(allocator, type, out);
  if (ret == OB_SUCCESS) {
    auto *copy = static_cast<PluginTypeComparisonExtraInfo *>(out);
    copy->binding_ = binding_; copy->null_safe_ = null_safe_;
  }
  return ret;
}

PluginTypeComparisonExpr::PluginTypeComparisonExpr(ObIAllocator &allocator)
    : ObFuncExprOperator(allocator, T_FUN_SYS_PLUGIN_TYPE_COMPARE, "__seekdb_plugin_type_compare",
                        3, NOT_VALID_FOR_GENERATED_COL, NOT_ROW_DIMENSION, true)
{}

namespace {
int read_type_comparison_binding(const ObRawExpr &raw, PluginTypeComparisonExtraInfo &info,
    ObItemType kind, int arguments)
{
  info.binding_ = {}; info.null_safe_ = 0;
  if (raw.get_expr_type() != kind || raw.get_param_count() != arguments + 1) return OB_INVALID_DATA;
  for (int i = 0; i <= arguments; ++i) if (!raw.get_param_expr(i)) return OB_INVALID_DATA;
  if (!raw.get_param_expr(arguments)->is_const_raw_expr()) return OB_INVALID_DATA;
  const auto &literal = static_cast<const ObConstRawExpr *>(raw.get_param_expr(arguments))->get_value();
  if (!literal.is_varbinary()) return OB_INVALID_DATA;
  const auto wire = literal.get_string();
  if (!wire.ptr() || wire.length() <= 0 || wire.length() > 4096) return OB_INVALID_DATA;
  int64_t pos = 0;
  int ret = info.deserialize(wire.ptr(), wire.length(), pos);
  if (OB_SUCC(ret) && (pos != wire.length() || !info.valid() ||
      (kind != T_FUN_SYS_PLUGIN_TYPE_COMPARE && info.null_safe_))) ret = OB_INVALID_DATA;
  for (int i = 0; OB_SUCC(ret) && i < arguments; ++i) {
    const auto &value = *raw.get_param_expr(i);
    const auto *logical = value.get_plugin_type();
    if (ob_is_null(value.get_data_type()) && !logical) continue;
    if (!logical || logical->stored_ || logical->physical_type_ != value.get_data_type() ||
        !ob_is_string_or_lob_type(value.get_data_type()) ||
        logical->logical_id_ != ObString::make_string(info.binding_.object_id) ||
        (logical->catalog_epoch_ && logical->catalog_epoch_ != info.binding_.catalog_epoch)) {
      ret = OB_STATE_NOT_MATCH;
    } else if (value.get_expr_type() == T_FUN_SYS_PLUGIN_CAST) {
      ObArenaAllocator temporary;
      PluginCastExtraInfo cast(temporary, T_FUN_SYS_PLUGIN_CAST);
      ret = PluginCastExpr::read_binding(value, cast);
      if (OB_SUCC(ret) && cast.binding_.catalog_epoch != info.binding_.catalog_epoch) ret = OB_STATE_NOT_MATCH;
    } else if (value.get_expr_type() == T_FUN_SYS_PLUGIN_TYPE_VALUE) {
      ObArenaAllocator temporary;
      PluginTypeValueExtraInfo typed(temporary, T_FUN_SYS_PLUGIN_TYPE_VALUE);
      ret = PluginTypeValueExpr::read_binding(value, typed);
      if (OB_SUCC(ret) && typed.catalog_epoch_ != info.binding_.catalog_epoch) ret = OB_STATE_NOT_MATCH;
    }
  }
  if (OB_FAIL(ret)) info.binding_ = {};
  return ret;
}
} // namespace

int PluginTypeComparisonExpr::read_binding(const ObRawExpr &raw, PluginTypeComparisonExtraInfo &info)
{
  return read_type_comparison_binding(raw, info, T_FUN_SYS_PLUGIN_TYPE_COMPARE, 2);
}

namespace {
// Validate the complete row shape before discarding grouping. Equality and
// lexicographic row comparison then consume corresponding scalar leaves in
// depth-first order; a different nesting is not merely a different spelling.
int collect_plugin_row_leaves(const ObRawExpr &shape, ObRawExpr &value,
    std::vector<ObRawExpr *> &leaves, uint32_t depth = 0)
{
  if (depth >= 64) return OB_SIZE_OVERFLOW;
  const bool row = shape.get_expr_type() == T_OP_ROW;
  if (row != (value.get_expr_type() == T_OP_ROW)) return OB_ERR_INVALID_COLUMN_NUM;
  if (!row) {
    leaves.push_back(&value);
    return OB_SUCCESS;
  }
  if (!shape.get_param_count() || shape.get_param_count() != value.get_param_count()) return OB_ERR_INVALID_COLUMN_NUM;
  for (int64_t i = 0; i < shape.get_param_count(); ++i) {
    if (!shape.get_param_expr(i) || !value.get_param_expr(i)) return OB_INVALID_DATA;
    const int ret = collect_plugin_row_leaves(*shape.get_param_expr(i), *value.get_param_expr(i), leaves, depth + 1);
    if (ret != OB_SUCCESS) return ret;
  }
  return OB_SUCCESS;
}

int prepare_plugin_row_comparison(ObRawExprFactory &factory, ObOpRawExpr &raw,
    const ObSQLSessionInfo *session)
{
  auto *left = raw.get_param_expr(0), *right = raw.get_param_expr(1);
  if (left->get_expr_type() != T_OP_ROW || right->get_expr_type() != T_OP_ROW ||
      left->get_param_count() != right->get_param_count() || !left->get_param_count()) return OB_ERR_INVALID_COLUMN_NUM;
  const auto op = raw.get_expr_type();
  const bool equality = op == T_OP_EQ || op == T_OP_NE || op == T_OP_NSEQ;
  int ret = OB_SUCCESS;
  std::vector<ObRawExpr *> operands[2];
  if (OB_FAIL(collect_plugin_row_leaves(*left, *left, operands[0])) ||
      OB_FAIL(collect_plugin_row_leaves(*left, *right, operands[1]))) return ret;
  const int64_t columns = operands[0].size();
  ObConstRawExpr *one = nullptr, *zero = nullptr;
  if (OB_FAIL(factory.create_raw_expr(T_INT, one)) || OB_FAIL(factory.create_raw_expr(T_INT, zero))) return ret;
  ObObj number; number.set_int(1); one->set_value(number); number.set_int(0); zero->set_value(number);
  if (OB_FAIL(one->formalize(session)) || OB_FAIL(zero->formalize(session))) return ret;
  const auto binary = [&](ObItemType kind, ObRawExpr *a, ObRawExpr *b, ObOpRawExpr *&result) -> int {
    int status = factory.create_raw_expr(kind, result);
    if (status == OB_SUCCESS) status = result->set_param_exprs(a, b);
    if (status == OB_SUCCESS) status = result->formalize(session);
    return status;
  };
  ObOpRawExpr *combined = nullptr;
  ObCaseOpRawExpr *ordered = nullptr;
  if (equality) {
    if (OB_FAIL(factory.create_raw_expr(op == T_OP_NE ? T_OP_OR : T_OP_AND, combined)) ||
        OB_FAIL(combined->init_param_exprs(columns))) return ret;
  } else if (OB_FAIL(factory.create_raw_expr(T_OP_CASE, ordered))) return ret;
  for (int64_t i = 0; i < columns; ++i) {
    ObRawExpr *pair[] = {operands[0][i], operands[1][i]};
    const auto operand = [&](int64_t j) -> ObRawExpr *& { return pair[j]; };
    std::string type_id; uint64_t epoch = 0;
    if (OB_FAIL(prepare_plugin_branches(&factory, session, 2, operand, nullptr, type_id, epoch))) return ret;
    ObOpRawExpr *comparison = nullptr;
    if (OB_FAIL(binary(op, pair[0], pair[1], comparison))) return ret;
    if (equality) {
      if (OB_FAIL(combined->add_param_expr(comparison))) return ret;
    } else if (i + 1 == columns) {
      ordered->set_default_param_expr(comparison);
    } else {
      ObOpRawExpr *equal = nullptr, *is_true = nullptr, *decisive = nullptr;
      if (comparison->get_param_expr(0)->get_expr_type() == T_FUN_SYS_PLUGIN_TYPE_COMPARE) {
        // Share the bound ordering result: equality and the chosen relation
        // must not invoke a plugin comparator/decoder twice for this column.
        if (OB_FAIL(binary(T_OP_EQ, comparison->get_param_expr(0), zero, equal))) return ret;
      } else if (OB_FAIL(binary(T_OP_EQ, pair[0], pair[1], equal))) return ret;
      // A false OR unknown equality decides lexicographic comparison here.
      // Only true equality advances to the next column. Use a flat CASE to
      // avoid expression depth growing once per tuple column.
      if (OB_FAIL(binary(T_OP_NSEQ, equal, one, is_true)) ||
          OB_FAIL(binary(T_OP_EQ, is_true, zero, decisive))) return ret;
      if (OB_FAIL(ordered->add_when_param_expr(decisive)) ||
          OB_FAIL(ordered->add_then_param_expr(comparison))) return ret;
    }
  }
  ObRawExpr *result = equality ? static_cast<ObRawExpr *>(combined) : static_cast<ObRawExpr *>(ordered);
  if (equality && combined->get_param_count() == 1) result = combined->get_param_expr(0);
  if (!equality && !ordered->get_when_expr_size()) result = ordered->get_default_param_expr();
  if (OB_FAIL(result->formalize(session))) return ret;
  // The tuple result is already boolean/NULL. Keep the original raw C++ class
  // but remove the old row operator and its cached carrier comparison state.
  raw.get_param_expr(0) = result;
  raw.get_param_expr(1) = one;
  raw.set_expr_type(T_OP_EQ); raw.free_op(); raw.clear_plugin_type();
  return raw.extract_info();
}

int prepare_plugin_row_in(ObRawExprFactory &factory, ObOpRawExpr &raw,
    const ObSQLSessionInfo *session)
{
  auto *left = raw.get_param_expr(0), *list = raw.get_param_expr(1);
  if (left->get_expr_type() != T_OP_ROW || list->get_expr_type() != T_OP_ROW ||
      left->get_param_count() < 1 || list->get_param_count() < 1) return OB_ERR_INVALID_COLUMN_NUM;
  const int64_t rows = list->get_param_count() + 1;
  if (rows > SEEKDB_PLUGIN_MAX_ARGUMENTS) return OB_SIZE_OVERFLOW;
  std::vector<std::vector<ObRawExpr *>> values(rows);
  int ret = OB_SUCCESS;
  for (int64_t i = 0; i < rows; ++i) {
    auto *row = i ? list->get_param_expr(i - 1) : left;
    if (!row) return OB_INVALID_DATA;
    if (OB_FAIL(collect_plugin_row_leaves(*left, *row, values[i]))) return ret;
  }
  const int64_t columns = values[0].size();
  // Like scalar IN, choose a common type across all candidates, independently
  // for each tuple column. Build casts/decoders before publishing any operands.
  for (int64_t j = 0; j < columns; ++j) {
    const auto operand = [&](int64_t i) -> ObRawExpr *& { return values[i][j]; };
    std::string type_id; uint64_t epoch = 0;
    if (OB_FAIL(prepare_plugin_branches(&factory, session, rows, operand, nullptr, type_id, epoch))) return ret;
  }
  ObOpRawExpr *membership = nullptr, *selector = nullptr;
  if (OB_FAIL(factory.create_raw_expr(T_OP_OR, membership)) ||
      OB_FAIL(membership->init_param_exprs(rows - 1))) return ret;
  for (int64_t i = 0; i < rows; ++i) {
    ObOpRawExpr *row = nullptr;
    if (OB_FAIL(factory.create_raw_expr(T_OP_ROW, row)) || OB_FAIL(row->init_param_exprs(columns))) return ret;
    for (auto *value : values[i]) if (OB_FAIL(row->add_param_expr(value))) return ret;
    if (OB_FAIL(row->formalize(session))) return ret;
    if (!i) {
      selector = row;
    } else {
      ObOpRawExpr *equal = nullptr;
      if (OB_FAIL(factory.create_raw_expr(T_OP_EQ, equal)) ||
          OB_FAIL(equal->set_param_exprs(selector, row))) return ret;
      // All row comparisons share the converted selector, including each
      // stored decoder. OR retains row-equality's SQL three-valued result;
      // unlike scalar IN, a NULL in one selector column cannot skip the list.
      if (OB_FAIL(prepare_plugin_row_comparison(factory, *equal, session)) ||
          OB_FAIL(equal->formalize(session)) || OB_FAIL(membership->add_param_expr(equal))) return ret;
    }
  }
  // AND/OR code generation requires at least two children. A singleton list
  // already has exactly the required boolean/NULL result in its row equality.
  ObRawExpr *result = membership->get_param_count() == 1 ? membership->get_param_expr(0) : membership;
  if (OB_FAIL(result->formalize(session))) return ret;
  ObConstRawExpr *one = nullptr;
  if (OB_FAIL(factory.create_raw_expr(T_INT, one))) return ret;
  ObObj literal; literal.set_int(1); one->set_value(literal);
  if (OB_FAIL(one->formalize(session))) return ret;
  const auto result_op = raw.get_expr_type() == T_OP_IN ? T_OP_EQ : T_OP_NE;
  raw.get_param_expr(0) = result; raw.get_param_expr(1) = one;
  raw.set_expr_type(result_op); raw.free_op(); raw.clear_plugin_type();
  return raw.extract_info();
}
} // namespace

bool PluginTypeComparisonExpr::has_nested_row_operands(const ObRawExpr &raw)
{
  const auto op = raw.get_expr_type();
  const bool in = op == T_OP_IN || op == T_OP_NOT_IN;
  if (raw.get_expr_class() != ObRawExpr::EXPR_OPERATOR || raw.get_param_count() != 2 ||
      (op != T_OP_EQ && op != T_OP_NE && op != T_OP_LT && op != T_OP_LE &&
       op != T_OP_GT && op != T_OP_GE && op != T_OP_NSEQ && !in)) return false;
  const auto nested = [](const ObRawExpr *value) {
    if (value && value->get_expr_type() == T_OP_ROW) {
      for (int64_t i = 0; i < value->get_param_count(); ++i)
        if (value->get_param_expr(i) && value->get_param_expr(i)->get_expr_type() == T_OP_ROW) return true;
    }
    return false;
  };
  if (nested(raw.get_param_expr(0))) return true;
  const auto *right = raw.get_param_expr(1);
  if (in && right && right->get_expr_type() == T_OP_ROW) {
    for (int64_t i = 0; i < right->get_param_count(); ++i)
      if (nested(right->get_param_expr(i))) return true;
    return false;
  }
  return nested(right);
}

int PluginTypeComparisonExpr::prepare(ObRawExprFactory *factory, ObOpRawExpr &raw,
    const ObSQLSessionInfo *session)
try {
  const auto op = raw.get_expr_type();
  const bool between = op == T_OP_BTW || op == T_OP_NOT_BTW;
  const bool in_list = op == T_OP_IN || op == T_OP_NOT_IN;
  const bool subquery = IS_SUBQUERY_COMPARISON_OP(op);
  if (op != T_OP_EQ && op != T_OP_NE && op != T_OP_LT && op != T_OP_LE &&
      op != T_OP_GT && op != T_OP_GE && op != T_OP_NSEQ && !between && !in_list && !subquery) return OB_SUCCESS;
  if (raw.get_param_count() != (between ? 3 : 2)) return OB_INVALID_DATA;
  for (int64_t i = 0; i < raw.get_param_count(); ++i) if (!raw.get_param_expr(i)) return OB_INVALID_DATA;
  ObOpRawExpr *list = nullptr;
  // Ordinary binary comparisons and BETWEEN stay in the inline storage.
  ObSEArray<ObRawExpr *, 3> values;
  int ret = OB_SUCCESS;
  if (in_list && raw.get_param_expr(1)->get_expr_type() == T_OP_ROW) {
    list = static_cast<ObOpRawExpr *>(raw.get_param_expr(1));
    if (list->get_param_count() < 1) return OB_INVALID_DATA;
    if (OB_FAIL(values.push_back(raw.get_param_expr(0)))) return ret;
    for (int64_t i = 0; i < list->get_param_count(); ++i)
      if (OB_FAIL(values.push_back(list->get_param_expr(i)))) return ret;
  } else {
    for (int64_t i = 0; i < raw.get_param_count(); ++i)
      if (OB_FAIL(values.push_back(raw.get_param_expr(i)))) return ret;
  }
  const int64_t count = values.count();
  bool tuple = false;
  // Info extraction removes ANY/ALL wrappers and changes the operator to SQ_*.
  bool quantified = subquery || (in_list && !list);
  for (int64_t i = 0; i < count; ++i) {
    if (!values[i]) return OB_INVALID_DATA;
    tuple |= values[i]->get_expr_type() == T_OP_ROW;
    quantified |= values[i]->get_expr_type() == T_ANY || values[i]->get_expr_type() == T_ALL;
  }
  if (tuple || quantified) {
    bool custom = false;
    const auto inspect = [&](auto &&self, const ObRawExpr &value, uint32_t depth) -> int {
      if (depth >= 64) return OB_SIZE_OVERFLOW;
      const auto *logical = value.get_plugin_type();
      custom |= logical && (logical->stored_ || !native_case_identity(logical->logical_id_));
      if (value.get_expr_type() == T_REF_QUERY) {
        const auto *statement = static_cast<const ObQueryRefRawExpr &>(value).get_ref_stmt();
        if (!statement) return OB_INVALID_DATA;
        for (int64_t i = 0; i < statement->get_select_item_size(); ++i) {
          if (!statement->get_select_item(i).expr_) return OB_INVALID_DATA;
          const int ret = self(self, *statement->get_select_item(i).expr_, depth + 1);
          if (ret != OB_SUCCESS) return ret;
        }
      }
      if (value.get_expr_type() == T_OP_ROW || value.get_expr_type() == T_ANY || value.get_expr_type() == T_ALL) {
        for (int64_t i = 0; i < value.get_param_count(); ++i) {
          if (!value.get_param_expr(i)) return OB_INVALID_DATA;
          const int ret = self(self, *value.get_param_expr(i), depth + 1);
          if (ret != OB_SUCCESS) return ret;
        }
      }
      return OB_SUCCESS;
    };
    for (int64_t i = 0; i < count; ++i) {
      const int ret = inspect(inspect, *values[i], 0);
      if (ret != OB_SUCCESS) return ret;
    }
    if (!custom) return OB_SUCCESS;
    if (quantified || between) return OB_NOT_SUPPORTED;
    if (!factory || !session || !share::g_mp) return OB_NOT_INIT;
    if (in_list) return prepare_plugin_row_in(*factory, raw, session);
    return prepare_plugin_row_comparison(*factory, raw, session);
  }
  // Repeated deduction sees native comparison of the already-bound result.
  const auto branch = [&](int64_t i) -> ObRawExpr *& { return values[i]; };
  std::string type_id;
  uint64_t epoch = 0;
  ret = prepare_plugin_branches(factory, session, count, branch, nullptr, type_id, epoch);
  if (OB_FAIL(ret)) return ret;
  if (type_id.empty()) return OB_SUCCESS; // No custom operands: native SQL rules.
  if (native_case_identity(ObString(type_id.size(), type_id.data()))) {
    if (in_list) {
      raw.get_param_expr(0) = values[0];
      for (int64_t i = 1; i < count; ++i) list->get_param_expr(i - 1) = values[i];
      if (OB_FAIL(list->extract_info())) return ret;
    } else for (int64_t i = 0; i < count; ++i) raw.get_param_expr(i) = values[i];
    return raw.extract_info();
  }
  if (!factory || !session || !share::g_mp) return OB_NOT_INIT;
  const auto kind = between ? T_FUN_SYS_PLUGIN_TYPE_BETWEEN : in_list ? T_FUN_SYS_PLUGIN_TYPE_IN : T_FUN_SYS_PLUGIN_TYPE_COMPARE;
  PluginTypeComparisonExtraInfo info(factory->get_allocator(), kind);
  info.null_safe_ = op == T_OP_NSEQ;
  if (OB_FAIL(share::g_mp->resolve_plugin_type_by_id(type_id.c_str(), &info.binding_, epoch))) return ret;
  if (!info.valid() || type_id != info.binding_.object_id || info.binding_.catalog_epoch != epoch) return OB_STATE_NOT_MATCH;
  if (OB_FAIL(share::g_mp->check_bound_plugin_type_comparison(info.binding_))) return ret;
  const int64_t size = info.get_serialize_size();
  if (size <= 0 || size > 4096) return OB_INVALID_DATA;
  auto *wire = static_cast<char *>(factory->get_allocator().alloc(size));
  if (!wire) return OB_ALLOCATE_MEMORY_FAILED;
  int64_t pos = 0;
  if (OB_FAIL(info.serialize(wire, size, pos))) return ret;
  ObConstRawExpr *metadata = nullptr, *zero = nullptr;
  ObSysFunRawExpr *comparison = nullptr;
  if (OB_FAIL(factory->create_raw_expr(T_VARCHAR, metadata)) ||
      OB_FAIL(factory->create_raw_expr(T_INT, zero)) ||
      OB_FAIL(factory->create_raw_expr(kind, comparison))) return ret;
  ObObj literal; literal.set_varchar(ObString(size, wire)); literal.set_collation_type(CS_TYPE_BINARY);
  metadata->set_value(literal);
  ObObj integer; integer.set_int(between || in_list ? 1 : 0); zero->set_value(integer);
  if (OB_FAIL(comparison->init_param_exprs(count + 1))) return ret;
  for (int i = 0; i < count; ++i) if (OB_FAIL(comparison->add_param_expr(values[i]))) return ret;
  if (OB_FAIL(comparison->add_param_expr(metadata))) return ret;
  comparison->set_func_name(ObString::make_string(between ? "__seekdb_plugin_type_between" :
      in_list ? "__seekdb_plugin_type_in" : "__seekdb_plugin_type_compare"));
  if (OB_FAIL(comparison->formalize(session)) || OB_FAIL(zero->formalize(session))) return ret;
  // Publish all operands only after every binding and conversion succeeds.
  raw.get_param_expr(0) = comparison; raw.get_param_expr(1) = zero;
  if (between) raw.get_param_expr(2) = zero;
  if (in_list) {
    raw.set_expr_type(op == T_OP_IN ? T_OP_EQ : T_OP_NE);
    raw.free_op(); // An already-allocated IN operator must not survive the change.
  }
  raw.clear_plugin_type();
  return raw.extract_info();
} catch (const std::bad_alloc &) { return OB_ALLOCATE_MEMORY_FAILED;
} catch (...) { return OB_ERR_UNEXPECTED; }

int PluginTypeComparisonExpr::calc_result_type3(ObExprResType &type, ObExprResType &left,
    ObExprResType &right, ObExprResType &metadata, ObExprTypeCtx &context) const
{
  const auto *raw = context.get_raw_expr();
  if (!raw) return OB_INVALID_ARGUMENT;
  ObArenaAllocator temporary;
  PluginTypeComparisonExtraInfo info(temporary, T_FUN_SYS_PLUGIN_TYPE_COMPARE);
  const int ret = read_binding(*raw, info);
  if (ret != OB_SUCCESS) return ret;
  for (auto *argument : {&left, &right, &metadata}) {
    argument->set_calc_meta(*argument);
    argument->set_calc_accuracy(argument->get_accuracy());
  }
  type.set_int(); type.set_precision(19); type.set_scale(0);
  return OB_SUCCESS;
}

int PluginTypeComparisonExpr::cg_expr(ObExprCGCtx &context, const ObRawExpr &raw, ObExpr &runtime) const
{
  if (!context.allocator_) return OB_INVALID_ARGUMENT;
  PluginTypeComparisonExtraInfo info(*context.allocator_, T_FUN_SYS_PLUGIN_TYPE_COMPARE);
  int ret = read_binding(raw, info);
  if (OB_FAIL(ret)) return ret;
  if (raw.get_data_type() != ObIntType || raw.get_plugin_type()) return OB_STATE_NOT_MATCH;
  if (OB_FAIL(info.deep_copy(*context.allocator_, T_FUN_SYS_PLUGIN_TYPE_COMPARE, runtime.extra_info_))) return ret;
  runtime.eval_func_ = evaluate;
  runtime.eval_batch_func_ = evaluate_batch;
  return OB_SUCCESS;
}

int PluginTypeComparisonExpr::evaluate(const ObExpr &expression, ObEvalCtx &context, ObDatum &result)
try {
  const auto *info = dynamic_cast<const PluginTypeComparisonExtraInfo *>(expression.extra_info_);
  if (!share::g_mp || !info || !info->valid() || expression.arg_cnt_ != 3 ||
      !expression.args_[0] || !expression.args_[1]) return OB_INVALID_DATA;
  int ret = context.exec_ctx_.check_status();
  if (OB_FAIL(ret)) return ret;
  ObDatum *values[2] = {};
  for (int i = 0; i < 2; ++i) {
    if (OB_FAIL(expression.args_[i]->eval(context, values[i]))) return ret;
    if (!values[i]) return OB_ERR_UNEXPECTED;
  }
  if (values[0]->is_null() || values[1]->is_null()) {
    if (info->null_safe_) result.set_int(values[0]->is_null() == values[1]->is_null() ? 0 : 1);
    else result.set_null();
    return OB_SUCCESS;
  }
  ObEvalCtx::TempAllocGuard temporary(context);
  seekdb_plugin_execution_value_v1_t inputs[2] = {};
  for (int i = 0; i < 2; ++i) {
    const auto &child = *expression.args_[i];
    if (!ob_is_string_or_lob_type(child.datum_meta_.type_)) return OB_STATE_NOT_MATCH;
    ObString bytes;
    if (OB_FAIL(ObTextStringHelper::read_real_string_data_with_copy(context.exec_ctx_, temporary.get_allocator(),
        *values[i], child.datum_meta_, child.obj_meta_.has_lob_header(), bytes))) return ret;
    inputs[i].struct_size = sizeof(inputs[i]); inputs[i].type_id = info->binding_.object_id;
    inputs[i].data = reinterpret_cast<const uint8_t *>(bytes.ptr()); inputs[i].data_size = bytes.length();
  }
  int32_t ordering = 0;
  if (OB_FAIL(share::g_mp->compare_bound_plugin_type(info->binding_, inputs[0], inputs[1], ordering))) return ret;
  if (OB_FAIL(context.exec_ctx_.check_status())) return ret;
  result.set_int(ordering);
  return OB_SUCCESS;
} catch (const std::bad_alloc &) { return OB_ALLOCATE_MEMORY_FAILED;
} catch (...) { return OB_ERR_UNEXPECTED; }

int PluginTypeComparisonExpr::evaluate_batch(const ObExpr &expression, ObEvalCtx &context,
    const ObBitVector &skip, int64_t size)
try {
  if (size < 0 || !expression.is_batch_result()) return OB_INVALID_ARGUMENT;
  if (!size) return OB_SUCCESS;
  auto &evaluated = expression.get_evaluated_flags(context);
  struct FailureGuard {
    ObBitVector &flags; int64_t size; bool success = false;
    ~FailureGuard() { if (!success) flags.reset(size); }
  } failure{evaluated, size};
  const bool between = expression.type_ == T_FUN_SYS_PLUGIN_TYPE_BETWEEN;
  const auto *info = dynamic_cast<const PluginTypeComparisonExtraInfo *>(expression.extra_info_);
  if (!share::g_mp || (!between && expression.type_ != T_FUN_SYS_PLUGIN_TYPE_COMPARE) || !info || !info->valid() ||
      (between && info->null_safe_) || expression.arg_cnt_ != (between ? 4 : 3) || !expression.args_)
    return OB_INVALID_DATA;
  std::vector<uint64_t> words(ObBitVector::memory_size(size) / sizeof(uint64_t), UINT64_MAX);
  auto &needed = *to_bit_vector(words.data());
  int64_t active = 0;
  for (int64_t i = 0; i < size; ++i) if (!skip.at(i) && !evaluated.at(i)) { needed.unset(i); ++active; }
  if (!active) { failure.success = true; return OB_SUCCESS; }
  ObEvalCtx::BatchInfoScopeGuard frame(context);
  frame.set_batch_size(size);
  int ret = OB_SUCCESS;
  for (uint32_t a = 0; active && a + 1 < expression.arg_cnt_; ++a) {
    if (!expression.args_[a]) return OB_INVALID_DATA;
    if (OB_FAIL(context.exec_ctx_.check_status())) return ret;
    const auto &child = *expression.args_[a];
    if (OB_FAIL(child.eval_batch(context, needed, size))) return ret;
    if (between && a == 0) {
      // Only a NULL main value skips bounds. A NULL lower bound must not
      // suppress the upper bound: FALSE AND UNKNOWN can still be FALSE.
      auto values = child.locate_expr_datumvector(context);
      for (int64_t i = 0; i < size; ++i) if (!needed.at(i) && values.at(i)->is_null()) {
        expression.locate_batch_datums(context)[i].set_null();
        evaluated.set(i); needed.set(i); --active;
      }
    }
  }
  // Comparison callbacks remain per value. All operand results are cached;
  // this loop cannot scalarize a nested function or repeat a decoder.
  if (active && OB_FAIL(expr_default_eval_batch_func(expression, context, needed, size))) return ret;
  if (OB_FAIL(context.exec_ctx_.check_status())) return ret;
  failure.success = true;
  return OB_SUCCESS;
} catch (const std::bad_alloc &) { return OB_ALLOCATE_MEMORY_FAILED;
} catch (...) { return OB_ERR_UNEXPECTED; }

PluginTypeBetweenExpr::PluginTypeBetweenExpr(ObIAllocator &allocator)
    : ObFuncExprOperator(allocator, T_FUN_SYS_PLUGIN_TYPE_BETWEEN, "__seekdb_plugin_type_between",
                        4, NOT_VALID_FOR_GENERATED_COL, NOT_ROW_DIMENSION, true)
{}

int PluginTypeBetweenExpr::read_binding(const ObRawExpr &raw, PluginTypeComparisonExtraInfo &info)
{
  return read_type_comparison_binding(raw, info, T_FUN_SYS_PLUGIN_TYPE_BETWEEN, 3);
}

int PluginTypeBetweenExpr::calc_result_typeN(ObExprResType &type, ObExprResType *types,
    int64_t count, ObExprTypeCtx &context) const
{
  if (!context.get_raw_expr() || !types || count != 4) return OB_INVALID_ARGUMENT;
  ObArenaAllocator temporary;
  PluginTypeComparisonExtraInfo info(temporary, T_FUN_SYS_PLUGIN_TYPE_BETWEEN);
  const int ret = read_binding(*context.get_raw_expr(), info);
  if (ret != OB_SUCCESS) return ret;
  for (int64_t i = 0; i < count; ++i) {
    types[i].set_calc_meta(types[i]); types[i].set_calc_accuracy(types[i].get_accuracy());
  }
  type.set_int(); type.set_precision(19); type.set_scale(0);
  return OB_SUCCESS;
}

int PluginTypeBetweenExpr::cg_expr(ObExprCGCtx &context, const ObRawExpr &raw, ObExpr &runtime) const
{
  if (!context.allocator_) return OB_INVALID_ARGUMENT;
  PluginTypeComparisonExtraInfo info(*context.allocator_, T_FUN_SYS_PLUGIN_TYPE_BETWEEN);
  int ret = read_binding(raw, info);
  if (OB_FAIL(ret)) return ret;
  if (raw.get_data_type() != ObIntType || raw.get_plugin_type()) return OB_STATE_NOT_MATCH;
  if (OB_FAIL(info.deep_copy(*context.allocator_, T_FUN_SYS_PLUGIN_TYPE_BETWEEN, runtime.extra_info_))) return ret;
  runtime.eval_func_ = evaluate;
  runtime.eval_batch_func_ = PluginTypeComparisonExpr::evaluate_batch;
  return OB_SUCCESS;
}

int PluginTypeBetweenExpr::evaluate(const ObExpr &expression, ObEvalCtx &context, ObDatum &result)
try {
  const auto *info = dynamic_cast<const PluginTypeComparisonExtraInfo *>(expression.extra_info_);
  if (!share::g_mp || !info || !info->valid() || info->null_safe_ || expression.arg_cnt_ != 4 ||
      !expression.args_ || !expression.args_[0] || !expression.args_[1] || !expression.args_[2]) return OB_INVALID_DATA;
  int ret = context.exec_ctx_.check_status();
  if (OB_FAIL(ret)) return ret;
  ObDatum *values[3] = {};
  for (int i = 0; i < 3; ++i) {
    if (OB_FAIL(expression.args_[i]->eval(context, values[i]))) return ret;
    if (!values[i]) return OB_ERR_UNEXPECTED;
    if (OB_FAIL(context.exec_ctx_.check_status())) return ret;
    // Like native BETWEEN, a NULL value does not evaluate either bound.
    if (i == 0 && values[i]->is_null()) { result.set_null(); return OB_SUCCESS; }
  }
  // Both bounds have been evaluated, even when the first comparison is false.
  // Materialize non-NULL values once; neither buffer nor TYPE binding escapes.
  ObEvalCtx::TempAllocGuard temporary(context);
  seekdb_plugin_execution_value_v1_t inputs[3] = {};
  for (int i = 0; i < 3; ++i) {
    if (values[i]->is_null()) continue;
    const auto &child = *expression.args_[i];
    if (!ob_is_string_or_lob_type(child.datum_meta_.type_)) return OB_STATE_NOT_MATCH;
    ObString bytes;
    if (OB_FAIL(ObTextStringHelper::read_real_string_data_with_copy(context.exec_ctx_, temporary.get_allocator(),
        *values[i], child.datum_meta_, child.obj_meta_.has_lob_header(), bytes))) return ret;
    inputs[i].struct_size = sizeof(inputs[i]); inputs[i].type_id = info->binding_.object_id;
    inputs[i].data = reinterpret_cast<const uint8_t *>(bytes.ptr()); inputs[i].data_size = bytes.length();
  }
  const auto compare = [&](int left, int right, int32_t &ordering) -> int {
    int status = context.exec_ctx_.check_status();
    if (status == OB_SUCCESS) status = share::g_mp->compare_bound_plugin_type(info->binding_, inputs[left], inputs[right], ordering);
    if (status == OB_SUCCESS) status = context.exec_ctx_.check_status();
    return status;
  };
  int32_t ordering = 0;
  if (!values[1]->is_null()) {
    if (OB_FAIL(compare(1, 0, ordering))) return ret;
    if (ordering > 0) { result.set_int(0); return OB_SUCCESS; }
  }
  if (!values[2]->is_null()) {
    if (OB_FAIL(compare(0, 2, ordering))) return ret;
    if (ordering > 0) { result.set_int(0); return OB_SUCCESS; }
  }
  if (values[1]->is_null() || values[2]->is_null()) result.set_null();
  else result.set_int(1);
  return OB_SUCCESS;
} catch (const std::bad_alloc &) { return OB_ALLOCATE_MEMORY_FAILED;
} catch (...) { return OB_ERR_UNEXPECTED; }

PluginTypeInExpr::PluginTypeInExpr(ObIAllocator &allocator)
    : ObFuncExprOperator(allocator, T_FUN_SYS_PLUGIN_TYPE_IN, "__seekdb_plugin_type_in",
                        PARAM_NUM_UNKNOWN, NOT_VALID_FOR_GENERATED_COL, NOT_ROW_DIMENSION, true)
{}

int PluginTypeInExpr::read_binding(const ObRawExpr &raw, PluginTypeComparisonExtraInfo &info)
{
  info.binding_ = {}; info.null_safe_ = 0;
  if (raw.get_param_count() < 3 || raw.get_param_count() > SEEKDB_PLUGIN_MAX_ARGUMENTS + 1) return OB_INVALID_DATA;
  return read_type_comparison_binding(raw, info, T_FUN_SYS_PLUGIN_TYPE_IN, raw.get_param_count() - 1);
}

int PluginTypeInExpr::calc_result_typeN(ObExprResType &type, ObExprResType *types,
    int64_t count, ObExprTypeCtx &context) const
{
  if (!context.get_raw_expr() || !types || count != context.get_raw_expr()->get_param_count()) return OB_INVALID_ARGUMENT;
  ObArenaAllocator temporary;
  PluginTypeComparisonExtraInfo info(temporary, T_FUN_SYS_PLUGIN_TYPE_IN);
  const int ret = read_binding(*context.get_raw_expr(), info);
  if (ret != OB_SUCCESS) return ret;
  for (int64_t i = 0; i < count; ++i) {
    types[i].set_calc_meta(types[i]); types[i].set_calc_accuracy(types[i].get_accuracy());
  }
  type.set_int(); type.set_precision(19); type.set_scale(0);
  return OB_SUCCESS;
}

int PluginTypeInExpr::cg_expr(ObExprCGCtx &context, const ObRawExpr &raw, ObExpr &runtime) const
{
  if (!context.allocator_) return OB_INVALID_ARGUMENT;
  PluginTypeComparisonExtraInfo info(*context.allocator_, T_FUN_SYS_PLUGIN_TYPE_IN);
  int ret = read_binding(raw, info);
  if (OB_FAIL(ret)) return ret;
  if (raw.get_data_type() != ObIntType || raw.get_plugin_type()) return OB_STATE_NOT_MATCH;
  if (OB_FAIL(info.deep_copy(*context.allocator_, T_FUN_SYS_PLUGIN_TYPE_IN, runtime.extra_info_))) return ret;
  runtime.eval_func_ = evaluate;
  runtime.eval_batch_func_ = evaluate_batch;
  return OB_SUCCESS;
}

int PluginTypeInExpr::evaluate(const ObExpr &expression, ObEvalCtx &context, ObDatum &result)
try {
  const auto *info = dynamic_cast<const PluginTypeComparisonExtraInfo *>(expression.extra_info_);
  if (!share::g_mp || !info || !info->valid() || info->null_safe_ || expression.arg_cnt_ < 3 ||
      expression.arg_cnt_ > SEEKDB_PLUGIN_MAX_ARGUMENTS + 1 || !expression.args_) return OB_INVALID_DATA;
  for (uint32_t i = 0; i < expression.arg_cnt_; ++i) if (!expression.args_[i]) return OB_INVALID_DATA;
  int ret = context.exec_ctx_.check_status();
  if (OB_FAIL(ret)) return ret;
  ObDatum *value = nullptr;
  if (OB_FAIL(expression.args_[0]->eval(context, value))) return ret;
  if (!value) return OB_ERR_UNEXPECTED;
  if (OB_FAIL(context.exec_ctx_.check_status())) return ret;
  if (value->is_null()) { result.set_null(); return OB_SUCCESS; }
  // Keep the left bytes outside the resettable expression scratch allocator.
  // Candidate scratch is reused after each synchronous comparison, not retained
  // once per list entry. No native byte hash/equality shortcuts are permitted.
  ObArenaAllocator value_memory(ObModIds::OB_SQL_EXPR_CALC), candidate_memory(ObModIds::OB_SQL_EXPR_CALC);
  const auto materialize = [&](const ObExpr &child, const ObDatum &datum, ObArenaAllocator &memory,
      seekdb_plugin_execution_value_v1_t &input) -> int {
    if (!ob_is_string_or_lob_type(child.datum_meta_.type_)) return OB_STATE_NOT_MATCH;
    ObString bytes;
    const int status = ObTextStringHelper::read_real_string_data_with_copy(context.exec_ctx_, memory,
        datum, child.datum_meta_, child.obj_meta_.has_lob_header(), bytes);
    if (status == OB_SUCCESS) {
      input.struct_size = sizeof(input); input.type_id = info->binding_.object_id;
      input.data = reinterpret_cast<const uint8_t *>(bytes.ptr()); input.data_size = bytes.length();
    }
    return status;
  };
  seekdb_plugin_execution_value_v1_t left{};
  if (OB_FAIL(materialize(*expression.args_[0], *value, value_memory, left))) return ret;
  bool contains_null = false;
  for (uint32_t i = 1; i + 1 < expression.arg_cnt_; ++i) {
    if (OB_FAIL(context.exec_ctx_.check_status())) return ret;
    ObDatum *candidate = nullptr;
    if (OB_FAIL(expression.args_[i]->eval(context, candidate))) return ret;
    if (!candidate) return OB_ERR_UNEXPECTED;
    if (OB_FAIL(context.exec_ctx_.check_status())) return ret;
    if (candidate->is_null()) { contains_null = true; continue; }
    candidate_memory.reuse();
    seekdb_plugin_execution_value_v1_t right{};
    if (OB_FAIL(materialize(*expression.args_[i], *candidate, candidate_memory, right))) return ret;
    int32_t ordering = 0;
    if (OB_FAIL(context.exec_ctx_.check_status())) return ret;
    if (OB_FAIL(share::g_mp->compare_bound_plugin_type(info->binding_, left, right, ordering))) return ret;
    if (OB_FAIL(context.exec_ctx_.check_status())) return ret;
    if (!ordering) { result.set_int(1); return OB_SUCCESS; }
  }
  if (contains_null) result.set_null();
  else result.set_int(0);
  return OB_SUCCESS;
} catch (const std::bad_alloc &) { return OB_ALLOCATE_MEMORY_FAILED;
} catch (...) { return OB_ERR_UNEXPECTED; }

int PluginTypeInExpr::evaluate_batch(const ObExpr &expression, ObEvalCtx &context,
    const ObBitVector &skip, int64_t size)
try {
  if (size < 0 || !expression.is_batch_result()) return OB_INVALID_ARGUMENT;
  if (!size) return OB_SUCCESS;
  auto &evaluated = expression.get_evaluated_flags(context);
  struct FailureGuard {
    ObBitVector &flags; int64_t size; bool success = false;
    ~FailureGuard() { if (!success) flags.reset(size); }
  } failure{evaluated, size};
  const auto *info = dynamic_cast<const PluginTypeComparisonExtraInfo *>(expression.extra_info_);
  if (!share::g_mp || !info || !info->valid() || info->null_safe_ || expression.arg_cnt_ < 3 ||
      expression.arg_cnt_ > SEEKDB_PLUGIN_MAX_ARGUMENTS + 1 || !expression.args_) return OB_INVALID_DATA;
  for (uint32_t i = 0; i < expression.arg_cnt_; ++i) if (!expression.args_[i]) return OB_INVALID_DATA;
  std::vector<uint64_t> words(ObBitVector::memory_size(size) / sizeof(uint64_t), UINT64_MAX);
  auto &needed = *to_bit_vector(words.data());
  std::vector<int64_t> pending;
  for (int64_t i = 0; i < size; ++i) if (!skip.at(i) && !evaluated.at(i)) {
    needed.unset(i); pending.push_back(i);
  }
  if (pending.empty()) { failure.success = true; return OB_SUCCESS; }
  struct Row { std::string left; bool contains_null = false; };
  std::vector<Row> rows(size);
  ObEvalCtx::BatchInfoScopeGuard frame(context);
  frame.set_batch_size(size);
  int ret = context.exec_ctx_.check_status();
  if (OB_FAIL(ret)) return ret;
  if (OB_FAIL(expression.args_[0]->eval_batch(context, needed, size))) return ret;
  ObArenaAllocator memory(ObModIds::OB_SQL_EXPR_CALC);
  const auto materialize = [&](const ObExpr &child, const ObDatum &datum, ObString &bytes) -> int {
    if (!ob_is_string_or_lob_type(child.datum_meta_.type_)) return OB_STATE_NOT_MATCH;
    return ObTextStringHelper::read_real_string_data_with_copy(context.exec_ctx_, memory,
        datum, child.datum_meta_, child.obj_meta_.has_lob_header(), bytes);
  };
  auto left_values = expression.args_[0]->locate_expr_datumvector(context);
  int64_t active = pending.size();
  for (auto i : pending) {
    if (OB_FAIL(context.exec_ctx_.check_status())) return ret;
    frame.set_batch_idx(i);
    if (left_values.at(i)->is_null()) {
      expression.locate_batch_datums(context)[i].set_null(); needed.set(i); --active;
    } else {
      memory.reuse();
      ObString bytes;
      if (OB_FAIL(materialize(*expression.args_[0], *left_values.at(i), bytes))) return ret;
      // A later candidate may reset SQL scratch or alias the left source.
      // Own each active left value once, not once per list entry.
      if (bytes.length()) rows[i].left.assign(bytes.ptr(), bytes.length());
    }
  }
  for (uint32_t a = 1; active && a + 1 < expression.arg_cnt_; ++a) {
    if (OB_FAIL(context.exec_ctx_.check_status())) return ret;
    const auto &child = *expression.args_[a];
    if (OB_FAIL(child.eval_batch(context, needed, size))) return ret;
    auto candidates = child.locate_expr_datumvector(context);
    for (auto i : pending) if (!needed.at(i)) {
      if (OB_FAIL(context.exec_ctx_.check_status())) return ret;
      frame.set_batch_idx(i);
      const auto &datum = *candidates.at(i);
      if (datum.is_null()) { rows[i].contains_null = true; continue; }
      memory.reuse();
      ObString bytes;
      if (OB_FAIL(materialize(child, datum, bytes))) return ret;
      seekdb_plugin_execution_value_v1_t left{}, right{};
      left.struct_size = right.struct_size = sizeof(left);
      left.type_id = right.type_id = info->binding_.object_id;
      left.data = reinterpret_cast<const uint8_t *>(rows[i].left.data()); left.data_size = rows[i].left.size();
      right.data = reinterpret_cast<const uint8_t *>(bytes.ptr()); right.data_size = bytes.length();
      int32_t ordering = 0;
      if (OB_FAIL(share::g_mp->compare_bound_plugin_type(info->binding_, left, right, ordering))) return ret;
      if (OB_FAIL(context.exec_ctx_.check_status())) return ret;
      if (!ordering) {
        expression.locate_batch_datums(context)[i].set_int(1); needed.set(i); --active;
      }
    }
  }
  for (auto i : pending) {
    if (!needed.at(i)) {
      auto &result = expression.locate_batch_datums(context)[i];
      if (rows[i].contains_null) result.set_null();
      else result.set_int(0);
    }
  }
  if (OB_FAIL(context.exec_ctx_.check_status())) return ret;
  for (auto i : pending) evaluated.set(i);
  failure.success = true;
  return OB_SUCCESS;
} catch (const std::bad_alloc &) { return OB_ALLOCATE_MEMORY_FAILED;
} catch (...) { return OB_ERR_UNEXPECTED; }

int PluginTypeValueExpr::prepare_ordering(ObRawExprFactory &factory, ObRawExpr *&value,
    const ObSQLSessionInfo *session)
try {
  if (!value || !session) return OB_INVALID_ARGUMENT;
  const auto *logical = value->get_plugin_type();
  if (!logical || (!logical->stored_ && native_case_identity(logical->logical_id_))) return OB_SUCCESS;
  if (!share::g_mp) return OB_NOT_INIT;
  if (value->get_expr_type() == T_FUN_SYS_PLUGIN_TYPE_VALUE) {
    PluginTypeValueExtraInfo existing(factory.get_allocator(), T_FUN_SYS_PLUGIN_TYPE_VALUE);
    int ret = read_binding(*value, existing);
    if (OB_FAIL(ret)) return ret;
    if (existing.mode_ == PluginTypeValueExtraInfo::ORDERED) return OB_SUCCESS;
  }
  const std::string id(logical->logical_id_.ptr(), logical->logical_id_.length());
  PluginTypeComparisonExtraInfo ordering(factory.get_allocator(), T_FUN_SYS_PLUGIN_TYPE_COMPARE);
  int ret = share::g_mp->resolve_plugin_type_by_id(id.c_str(), &ordering.binding_, logical->catalog_epoch_);
  if (OB_FAIL(ret)) return ret;
  if (!ordering.valid() || id != ordering.binding_.object_id) return OB_STATE_NOT_MATCH;
  if (OB_FAIL(share::g_mp->check_bound_plugin_type_comparison(ordering.binding_))) return ret;
  ObRawExpr *converted = value;
  const ObString sql_name = ObString::make_string(ordering.binding_.sql_name);
  if (logical->stored_ && OB_FAIL(build(factory, sql_name, converted, session))) return ret;
  // An independent identity carrier owns the comparator, while its child owns
  // any decoder. Neither node is published to the operator until all checks pass.
  if (OB_FAIL(build(factory, sql_name, converted, session))) return ret;
  PluginTypeValueExtraInfo info(factory.get_allocator(), T_FUN_SYS_PLUGIN_TYPE_VALUE);
  if (OB_FAIL(read_binding(*converted, info))) return ret;
  if (info.mode_ != PluginTypeValueExtraInfo::IDENTITY || info.catalog_epoch_ != ordering.binding_.catalog_epoch)
    return OB_STATE_NOT_MATCH;
  info.mode_ = PluginTypeValueExtraInfo::ORDERED; info.ordering_ = &ordering;
  const int64_t size = info.get_serialize_size();
  if (size <= 0 || size > 4096) return OB_INVALID_DATA;
  auto *wire = static_cast<char *>(factory.get_allocator().alloc(size));
  if (!wire) return OB_ALLOCATE_MEMORY_FAILED;
  int64_t pos = 0;
  if (OB_FAIL(info.serialize(wire, size, pos))) return ret;
  ObConstRawExpr *metadata = nullptr;
  if (OB_FAIL(factory.create_raw_expr(T_VARCHAR, metadata))) return ret;
  ObObj literal; literal.set_varchar(ObString(size, wire)); literal.set_collation_type(CS_TYPE_BINARY);
  metadata->set_value(literal); converted->get_param_expr(1) = metadata;
  if (OB_FAIL(converted->formalize(session))) return ret;
  value = converted;
  return OB_SUCCESS;
} catch (const std::bad_alloc &) { return OB_ALLOCATE_MEMORY_FAILED;
} catch (...) { return OB_ERR_UNEXPECTED; }

int PluginTypeValueExpr::compare_ordered(const ObExpr *value, ObEvalCtx &context,
    const ObDatum &left, const ObDatum &right, bool &handled, int &ordering)
{
  handled = false; ordering = 0;
  if (!value || value->type_ != T_FUN_SYS_PLUGIN_TYPE_VALUE) return OB_SUCCESS;
  const auto *info = dynamic_cast<const PluginTypeValueExtraInfo *>(value->extra_info_);
  if (!info || !info->valid()) return OB_INVALID_DATA;
  if (info->mode_ != PluginTypeValueExtraInfo::ORDERED) return OB_SUCCESS;
  handled = true;
  if (!share::g_mp || left.is_null() || right.is_null() || value->datum_meta_.type_ != ObVarcharType)
    return OB_INVALID_ARGUMENT;
  int ret = context.exec_ctx_.check_status();
  if (OB_FAIL(ret)) return ret;
  seekdb_plugin_execution_value_v1_t a{}, b{};
  a.struct_size = b.struct_size = sizeof(a); a.type_id = b.type_id = info->logical_id_;
  const auto x = left.get_string(), y = right.get_string();
  a.data = reinterpret_cast<const uint8_t *>(x.ptr()); a.data_size = x.length();
  b.data = reinterpret_cast<const uint8_t *>(y.ptr()); b.data_size = y.length();
  int32_t result = 0;
  if (OB_FAIL(share::g_mp->compare_bound_plugin_type(info->ordering_->binding_, a, b, result))) return ret;
  if (OB_FAIL(context.exec_ctx_.check_status())) return ret;
  ordering = result;
  return OB_SUCCESS;
}

int PluginBranchType::prepare_case(ObRawExprFactory *factory, ObCaseOpRawExpr &raw,
    const ObSQLSessionInfo *session, std::string &type_id, uint64_t &epoch)
try {
  // Simple CASE matches each WHEN using that pair's type/cast rules. Do not
  // force every WHEN to share a type: independent legal operator bindings may
  // have different common types. Native-only matching retains the old path.
  ObSEArray<ObRawExpr *, 4> predicates;
  if (raw.get_arg_param_expr()) {
    if (raw.get_expr_type() != T_OP_ARG_CASE || raw.get_when_expr_size() < 1 ||
        raw.get_when_expr_size() != raw.get_then_expr_size()) return OB_INVALID_DATA;
    const auto custom = [](const ObRawExpr &value) {
      const auto *logical = value.get_plugin_type();
      return logical && (logical->stored_ || !native_case_identity(logical->logical_id_));
    };
    bool plugin_match = custom(*raw.get_arg_param_expr());
    for (int64_t i = 0; i < raw.get_when_expr_size(); ++i) {
      if (!raw.get_when_param_expr(i)) return OB_INVALID_DATA;
      plugin_match |= custom(*raw.get_when_param_expr(i));
    }
    if (plugin_match) {
      if (!factory || !session || !share::g_mp) return OB_NOT_INIT;
      ObRawExpr *selector = raw.get_arg_param_expr();
      int ret = OB_SUCCESS;
      // All predicates share this node, including its decoder. The normal
      // expression DAG/copy/frame cache evaluates it once per row; pair-specific
      // casts and WHEN expressions remain lazy behind the searched CASE.
      if (const auto *logical = selector->get_plugin_type(); logical && logical->stored_) {
        if (OB_FAIL(PluginTypeValueExpr::build(*factory, logical->sql_name_, selector, session))) return ret;
      }
      for (int64_t i = 0; i < raw.get_when_expr_size(); ++i) {
        ObOpRawExpr *equal = nullptr;
        if (OB_FAIL(factory->create_raw_expr(T_OP_EQ, equal))) return ret;
        if (OB_FAIL(equal->set_param_exprs(selector, raw.get_when_param_expr(i)))) return ret;
        if (OB_FAIL(equal->formalize(session))) return ret;
        if (OB_FAIL(predicates.push_back(equal))) return ret;
      }
    }
  }
  const int64_t count = raw.get_then_expr_size() + (raw.get_default_param_expr() ? 1 : 0);
  const auto branch = [&](int64_t i) -> ObRawExpr *& {
    return i < raw.get_then_expr_size() ? raw.get_then_param_expr(i) : raw.get_default_param_expr();
  };
  int ret = prepare_plugin_branches(factory, session, count, branch, raw.get_plugin_type(), type_id, epoch);
  if (OB_SUCC(ret)) {
    if (!epoch) raw.clear_plugin_type();
    // Do not publish a partially bound match list if a later pair/result fails.
    if (!predicates.empty()) {
      for (int64_t i = 0; i < predicates.count(); ++i) raw.get_when_param_expr(i) = predicates.at(i);
      raw.set_arg_param_expr(nullptr);
      raw.set_expr_type(T_OP_CASE);
      raw.free_op(); // Discard a cached ARG_CASE operator before type inference.
      ret = raw.extract_info();
    }
  }
  return ret;
} catch (const std::bad_alloc &) { return OB_ALLOCATE_MEMORY_FAILED;
} catch (...) { return OB_ERR_UNEXPECTED; }

int PluginBranchType::prepare_set(ObRawExprFactory *factory, const ObSQLSessionInfo *session,
    ObIArray<ObSelectStmt *> &statements, bool distinct, std::vector<Result> &results, bool recursive)
try {
  results.clear();
  if (!factory || !session || statements.empty() || !statements.at(0)) return OB_INVALID_ARGUMENT;
  const int64_t columns = statements.at(0)->get_select_item_size();
  bool plugin = false;
  for (int64_t j = 0; j < statements.count(); ++j) {
    auto *stmt = statements.at(j);
    if (!stmt) return OB_INVALID_ARGUMENT;
    if (stmt->get_select_item_size() != columns) return OB_ERR_COLUMN_SIZE;
    for (int64_t i = 0; i < columns; ++i) {
      const auto *value = stmt->get_select_item(i).expr_;
      if (!value) return OB_INVALID_DATA;
      plugin |= value->get_plugin_type() != nullptr;
    }
  }
  if (!plugin) return OB_SUCCESS;
  // Recursive CTE anchor coercion and custom comparison/hash semantics need
  // separate contracts; never reinterpret opaque plugin carriers for either.
  if (recursive) return OB_NOT_SUPPORTED;
  std::vector<Result> chosen(columns);
  std::vector<std::vector<ObRawExpr *>> converted(columns);
  for (int64_t i = 0; i < columns; ++i) {
    for (int64_t j = 0; j < statements.count(); ++j)
      converted[i].push_back(statements.at(j)->get_select_item(i).expr_);
    const auto branch = [&](int64_t j) -> ObRawExpr *& { return converted[i][j]; };
    int ret = prepare_plugin_branches(factory, session, statements.count(), branch, nullptr,
                                     chosen[i].type_id_, chosen[i].epoch_);
    if (OB_FAIL(ret)) return ret;
    if (distinct && !chosen[i].type_id_.empty() &&
        !native_case_identity(ObString(chosen[i].type_id_.size(), chosen[i].type_id_.data()))) return OB_NOT_SUPPORTED;
  }
  // A later column failure must not expose partially converted projections.
  for (int64_t i = 0; i < columns; ++i)
    for (int64_t j = 0; j < statements.count(); ++j) statements.at(j)->get_select_item(i).expr_ = converted[i][j];
  results = std::move(chosen);
  return OB_SUCCESS;
} catch (const std::bad_alloc &) { results.clear(); return OB_ALLOCATE_MEMORY_FAILED;
} catch (...) { results.clear(); return OB_ERR_UNEXPECTED; }

int PluginBranchType::finish(ObRawExpr &raw, const std::string &type_id, uint64_t epoch)
{
  if (!epoch) return OB_SUCCESS;
  const auto physical = raw.get_data_type();
  if (!cast_input_supported(physical)) {
    if (!type_id.empty()) return OB_NOT_SUPPORTED;
    raw.clear_plugin_type(); // An ordinary native result outside the plugin value API.
    return OB_SUCCESS;
  }
  const char *id = type_id.empty() ? core_type_identifier(physical) : type_id.c_str();
  if (!id) { raw.clear_plugin_type(); return OB_SUCCESS; }
  if (!type_id.empty()) {
    ObExprResType expected; assign_sql_result_type(expected, id);
    if (expected.get_type() != physical && !(expected.is_varchar() && ob_is_string_or_lob_type(physical)))
      return OB_STATE_NOT_MATCH;
  }
  PluginExprType logical;
  logical.logical_id_ = ObString::make_string(id); logical.physical_type_ = physical; logical.catalog_epoch_ = epoch;
  return raw.set_plugin_type(logical);
}

int PluginBranchType::preserve_native_cast(const ObRawExpr &source, ObRawExpr &target)
try {
  const auto *logical = source.get_plugin_type();
  if (!logical) return OB_SUCCESS;
  if (logical->stored_ || logical->physical_type_ != source.get_data_type()) return OB_STATE_NOT_MATCH;
  const std::string id = native_case_identity(logical->logical_id_) ? std::string() :
      std::string(logical->logical_id_.ptr(), logical->logical_id_.length());
  return finish(target, id, logical->catalog_epoch_);
} catch (const std::bad_alloc &) { return OB_ALLOCATE_MEMORY_FAILED;
} catch (...) { return OB_ERR_UNEXPECTED; }

int PluginBranchType::finish_set(ObIArray<ObSelectStmt *> &statements, int64_t column, const Result &result)
{
  int ret = OB_SUCCESS;
  for (int64_t j = 0; OB_SUCC(ret) && j < statements.count(); ++j) {
    if (!statements.at(j) || column < 0 || column >= statements.at(j)->get_select_item_size())
      return OB_INVALID_ARGUMENT;
    auto *value = statements.at(j)->get_select_item(column).expr_;
    if (!value) return OB_INVALID_DATA;
    // Keep built-in callback IDs on unchanged expressions. Only a new native
    // cast or set output needs the canonical identity for its physical type.
    if (!result.type_id_.empty() || !value->get_plugin_type())
      ret = finish(*value, result.type_id_, result.epoch_);
  }
  return ret;
}

int PluginTypeEncodeExtraInfo::deep_copy(ObIAllocator &allocator, ObExprOperatorType type,
                                        ObIExprExtraInfo *&out) const
{
  out = nullptr;
  if (type != T_FUN_SYS_PLUGIN_TYPE_ENCODE || !valid()) return OB_INVALID_ARGUMENT;
  int ret = ObExprExtraInfoFactory::alloc(allocator, type, out);
  if (OB_SUCC(ret)) static_cast<PluginTypeEncodeExtraInfo *>(out)->target_ = target_;
  return ret;
}

PluginTypeEncodeExpr::PluginTypeEncodeExpr(ObIAllocator &allocator)
    : ObFuncExprOperator(allocator, T_FUN_SYS_PLUGIN_TYPE_ENCODE, "__seekdb_plugin_type_encode",
                        2, NOT_VALID_FOR_GENERATED_COL, NOT_ROW_DIMENSION, true)
{}

int PluginTypeEncodeExpr::build(ObRawExprFactory &factory, const ObRawExpr &target,
                                ObRawExpr *&value, const ObSQLSessionInfo *session)
try {
  const auto *destination = target.get_plugin_type();
  if (!destination) return OB_SUCCESS;
  if (!value || !session || !destination->stored_) return OB_INVALID_ARGUMENT;
  int ret = value->deduce_type(session);
  if (OB_FAIL(ret)) return ret;
  const auto *source = value->get_plugin_type();
  if (source && source->physical_type_ != value->get_data_type()) return OB_STATE_NOT_MATCH;
  if (source && source->catalog_epoch_ && destination->catalog_epoch_ &&
      source->catalog_epoch_ != destination->catalog_epoch_) return OB_STATE_NOT_MATCH;
  if (source && source->stored_) {
    // Copying the same durable format does not encode a second time. Other
    // stored values must use a registered conversion, never reinterpret bytes.
    if (source->logical_id_ == destination->logical_id_ && source->owner_ == destination->owner_ &&
        source->format_ == destination->format_ && source->format_version_ == destination->format_version_) return OB_SUCCESS;
  }
  if (!share::g_mp) return OB_NOT_INIT;
  PluginTypeEncodeExtraInfo info(factory.get_allocator(), T_FUN_SYS_PLUGIN_TYPE_ENCODE);
  const std::string name(destination->sql_name_.ptr(), destination->sql_name_.length());
  if (OB_FAIL(share::g_mp->resolve_plugin_sql_object(SEEKDB_PLUGIN_EXTENSION_TYPE,
      name.c_str(), nullptr, 0, &info.target_.binding_))) return ret;
  const auto &binding = info.target_.binding_;
  if (!info.valid() || destination->logical_id_ != ObString::make_string(binding.object_id) ||
      destination->owner_ != ObString::make_string(binding.owner_plugin_id) ||
      destination->format_ != ObString::make_string(binding.physical_format_id) ||
      destination->format_version_ != binding.physical_format_version ||
      (destination->catalog_epoch_ && destination->catalog_epoch_ != binding.catalog_epoch) ||
      (source && source->catalog_epoch_ && source->catalog_epoch_ != binding.catalog_epoch)) return OB_STATE_NOT_MATCH;
  ObRawExpr *converted = value;
  if (!ob_is_null(value->get_data_type()) &&
      (!source || source->stored_ || source->logical_id_ != destination->logical_id_)) {
    ret = PluginCastExpr::build(factory, destination->logical_id_, SEEKDB_PLUGIN_CAST_ASSIGNMENT, converted, session);
    if (OB_FAIL(ret)) return ret == OB_ENTRY_NOT_EXIST ? OB_ERR_INVALID_TYPE_FOR_OP : ret;
  }
  ObSysFunRawExpr *encoded = nullptr;
  ObConstRawExpr *metadata = nullptr;
  const int64_t size = info.get_serialize_size();
  if (size <= 0 || size > 4096) return OB_INVALID_DATA;
  auto *wire = static_cast<char *>(factory.get_allocator().alloc(size));
  if (!wire) return OB_ALLOCATE_MEMORY_FAILED;
  int64_t pos = 0;
  if (OB_FAIL(info.serialize(wire, size, pos))) return ret;
  if (OB_FAIL(factory.create_raw_expr(T_VARCHAR, metadata))) return ret;
  ObObj literal; literal.set_varchar(ObString(size, wire)); literal.set_collation_type(CS_TYPE_BINARY);
  metadata->set_value(literal);
  if (OB_FAIL(factory.create_raw_expr(T_FUN_SYS_PLUGIN_TYPE_ENCODE, encoded))) return ret;
  if (OB_FAIL(encoded->init_param_exprs(2)) || OB_FAIL(encoded->add_param_expr(converted)) ||
      OB_FAIL(encoded->add_param_expr(metadata))) return ret;
  PluginExprType storage = *destination;
  storage.physical_type_ = ObVarcharType;
  storage.catalog_epoch_ = binding.catalog_epoch;
  storage.sql_name_ = ObString::make_string(binding.sql_name);
  if (OB_FAIL(encoded->set_plugin_type(storage))) return ret;
  encoded->set_func_name(ObString::make_string("__seekdb_plugin_type_encode"));
  if (OB_FAIL(encoded->formalize(session))) return ret;
  value = encoded;
  return OB_SUCCESS;
} catch (const std::bad_alloc &) { return OB_ALLOCATE_MEMORY_FAILED;
} catch (...) { return OB_ERR_UNEXPECTED; }

int PluginTypeEncodeExpr::read_binding(const ObRawExpr &raw, PluginTypeEncodeExtraInfo &info)
{
  info.target_ = {};
  if (raw.get_expr_type() != T_FUN_SYS_PLUGIN_TYPE_ENCODE || raw.get_param_count() != 2 ||
      !raw.get_param_expr(0) || !raw.get_param_expr(1) || !raw.get_param_expr(1)->is_const_raw_expr())
    return OB_INVALID_DATA;
  const auto &literal = static_cast<const ObConstRawExpr *>(raw.get_param_expr(1))->get_value();
  if (!literal.is_varbinary() || literal.is_null()) return OB_INVALID_DATA;
  const auto wire = literal.get_string();
  if (!wire.ptr() || wire.length() <= 0 || wire.length() > 4096) return OB_INVALID_DATA;
  int64_t pos = 0;
  int ret = info.deserialize(wire.ptr(), wire.length(), pos);
  const auto *target = raw.get_plugin_type();
  const auto *child = raw.get_param_expr(0);
  const auto *source = child->get_plugin_type();
  const auto &binding = info.target_.binding_;
  if (OB_SUCC(ret) && (pos != wire.length() || !info.valid() || !target || !target->stored_ ||
      target->physical_type_ != ObVarcharType || target->catalog_epoch_ != binding.catalog_epoch ||
      target->logical_id_ != ObString::make_string(binding.object_id) ||
      target->sql_name_ != ObString::make_string(binding.sql_name) ||
      target->owner_ != ObString::make_string(binding.owner_plugin_id) ||
      target->format_ != ObString::make_string(binding.physical_format_id) ||
      target->format_version_ != binding.physical_format_version)) ret = OB_STATE_NOT_MATCH;
  if (OB_SUCC(ret) && source && (source->stored_ || source->physical_type_ != child->get_data_type() ||
      source->logical_id_ != target->logical_id_ || source->catalog_epoch_ != binding.catalog_epoch)) ret = OB_STATE_NOT_MATCH;
  if (OB_SUCC(ret) && !ob_is_null(child->get_data_type()) && !source) ret = OB_ERR_INVALID_TYPE_FOR_OP;
  if (OB_SUCC(ret) && child->get_expr_type() == T_FUN_SYS_PLUGIN_CAST) {
    ObArenaAllocator temporary;
    PluginCastExtraInfo cast(temporary, T_FUN_SYS_PLUGIN_CAST);
    if (OB_FAIL(PluginCastExpr::read_binding(*child, cast))) {
    } else if (cast.binding_.catalog_epoch != binding.catalog_epoch) ret = OB_STATE_NOT_MATCH;
  }
  if (OB_SUCC(ret) && child->get_expr_type() == T_FUN_SYS_PLUGIN_TYPE_VALUE) {
    ObArenaAllocator temporary;
    PluginTypeValueExtraInfo typed(temporary, T_FUN_SYS_PLUGIN_TYPE_VALUE);
    if (OB_FAIL(PluginTypeValueExpr::read_binding(*child, typed))) {
    } else if (typed.catalog_epoch_ != binding.catalog_epoch) ret = OB_STATE_NOT_MATCH;
  }
  if (OB_FAIL(ret)) info.target_ = {};
  return ret;
}

int PluginTypeEncodeExpr::calc_result_type2(ObExprResType &type, ObExprResType &argument,
                                          ObExprResType &metadata, ObExprTypeCtx &context) const
{
  UNUSED(argument); UNUSED(metadata);
  const auto *raw = context.get_raw_expr();
  if (!raw) return OB_INVALID_ARGUMENT;
  ObArenaAllocator temporary;
  PluginTypeEncodeExtraInfo info(temporary, T_FUN_SYS_PLUGIN_TYPE_ENCODE);
  const int ret = read_binding(*raw, info);
  if (ret != OB_SUCCESS) return ret;
  type.set_varchar(); type.set_collation_type(CS_TYPE_BINARY); type.set_collation_level(CS_LEVEL_IMPLICIT);
  type.set_length(16777216);
  return OB_SUCCESS;
}

int PluginTypeEncodeExpr::cg_expr(ObExprCGCtx &context, const ObRawExpr &raw, ObExpr &runtime) const
try {
  if (!context.allocator_ || raw.get_data_type() != ObVarcharType) return OB_INVALID_ARGUMENT;
  PluginTypeEncodeExtraInfo info(*context.allocator_, T_FUN_SYS_PLUGIN_TYPE_ENCODE);
  int ret = read_binding(raw, info);
  if (OB_FAIL(ret)) return ret;
  ObIExprExtraInfo *base = nullptr;
  if (OB_FAIL(ObExprExtraInfoFactory::alloc(*context.allocator_, T_FUN_SYS_PLUGIN_TYPE_ENCODE, base))) return ret;
  static_cast<PluginTypeEncodeExtraInfo *>(base)->target_ = info.target_;
  runtime.extra_info_ = base; runtime.eval_func_ = evaluate;
  runtime.eval_batch_func_ = PluginFunctionExpr::evaluate_argument_batch;
  return OB_SUCCESS;
} catch (const std::bad_alloc &) { return OB_ALLOCATE_MEMORY_FAILED;
} catch (...) { return OB_ERR_UNEXPECTED; }

int PluginTypeEncodeExpr::evaluate(const ObExpr &expression, ObEvalCtx &context, ObDatum &result)
try {
  const auto *info = dynamic_cast<const PluginTypeEncodeExtraInfo *>(expression.extra_info_);
  if (!share::g_mp || !info || !info->valid() || expression.arg_cnt_ != 2) return OB_INVALID_DATA;
  ObDatum *input = nullptr;
  int ret = expression.args_[0]->eval(context, input);
  if (OB_FAIL(ret)) return ret;
  if (!input) return OB_ERR_UNEXPECTED;
  if (input->is_null()) { result.set_null(); return OB_SUCCESS; }
  const auto &child = *expression.args_[0];
  if (!ob_is_string_or_lob_type(child.datum_meta_.type_) && !ob_is_geometry(child.datum_meta_.type_))
    return OB_ERR_INVALID_TYPE_FOR_OP;
  ObEvalCtx::TempAllocGuard temporary(context);
  ObString bytes;
  if (OB_FAIL(ObTextStringHelper::read_real_string_data_with_copy(context.exec_ctx_,
      temporary.get_allocator(), *input, child.datum_meta_, child.obj_meta_.has_lob_header(), bytes))) return ret;
  seekdb_plugin_execution_value_v1_t value = {};
  value.struct_size = sizeof(value); value.type_id = info->target_.binding_.object_id;
  value.data = reinterpret_cast<const uint8_t *>(bytes.ptr()); value.data_size = bytes.length();
  // Use the bounded, sticky byte sink, then move its host-owned bytes into the
  // expression result buffer before the temporary arena is released.
  uint64_t total = 0;
  DecodeSink sink{temporary.get_allocator(), "core.type.bytes", total, {}};
  seekdb_plugin_execution_context_v1_t codec = {};
  codec.struct_size = sizeof(codec); codec.host = reinterpret_cast<seekdb_plugin_host_handle_t *>(&sink);
  codec.emit_result = emit_decoded_argument;
  if (OB_FAIL(share::g_mp->encode_bound_plugin_type(&info->target_.binding_, &codec, &value))) return ret;
  if (sink.status_ == SEEKDB_PLUGIN_STATUS_NO_MEMORY) return OB_ALLOCATE_MEMORY_FAILED;
  if (!sink.emitted_ || sink.status_ != SEEKDB_PLUGIN_STATUS_OK) return OB_INVALID_DATA;
  if (sink.null_) { result.set_null(); return OB_SUCCESS; }
  char *out = expression.get_str_res_mem(context, sink.bytes_.length());
  if (sink.bytes_.length() && !out) return OB_ALLOCATE_MEMORY_FAILED;
  if (sink.bytes_.length()) std::memcpy(out, sink.bytes_.ptr(), sink.bytes_.length());
  result.set_string(ObString(sink.bytes_.length(), out));
  return OB_SUCCESS;
} catch (const std::bad_alloc &) { return OB_ALLOCATE_MEMORY_FAILED;
} catch (...) { return OB_ERR_UNEXPECTED; }

namespace {
constexpr int64_t MAX_TABLE_BINDING_BYTES = 2 * 1024 * 1024;
bool table_identifier(const char *id, bool empty = false)
{
  return (empty || id[0]) && std::memchr(id, 0, SEEKDB_PLUGIN_MAX_IDENTIFIER_BYTES + 1);
}
}

bool PluginTableFunctionExtraInfo::valid() const
{
  const auto &b = binding_;
  if (b.struct_size < sizeof(b) || b.kind != SEEKDB_PLUGIN_EXTENSION_TABLE_FUNCTION ||
      !b.owner_generation || !b.catalog_epoch || !table_identifier(b.object_id) ||
      !table_identifier(b.sql_name) || !table_identifier(b.owner_plugin_id) ||
      !table_identifier(b.result_type_id, true) || !table_identifier(b.physical_format_id, true) ||
      b.minimum_arity > b.maximum_arity || b.maximum_arity > SEEKDB_PLUGIN_MAX_ARGUMENTS ||
      arguments_.count() < b.minimum_arity || arguments_.count() > b.maximum_arity ||
      !b.column_count || b.column_count > OB_MAX_COLUMN_NUMBER || b.column_count != columns_.count()) return false;
  for (auto word : b.reserved) if (word) return false;
  for (int64_t i = 0; i < arguments_.count(); ++i) {
    const auto &id = arguments_.at(i);
    if (id.length() < 0 || id.length() > SEEKDB_PLUGIN_MAX_IDENTIFIER_BYTES ||
        (id.length() && (!id.ptr() || std::memchr(id.ptr(), 0, id.length())))) return false;
  }
  for (int64_t i = 0; i < columns_.count(); ++i) {
    const auto &c = columns_.at(i);
    if (c.struct_size < sizeof(c) || !table_identifier(c.sql_name) || !table_identifier(c.type_id) || c.nullable > 1)
      return false;
    for (auto byte : c.reserved_bytes) if (byte) return false;
    for (auto word : c.reserved) if (word) return false;
  }
  return true;
}

OB_DEF_SERIALIZE(PluginTableFunctionExtraInfo)
{
  int ret = valid() ? OB_SUCCESS : OB_INVALID_DATA;
  for (const char *id : {binding_.object_id, binding_.sql_name, binding_.owner_plugin_id,
                         binding_.result_type_id, binding_.physical_format_id}) {
    if (OB_SUCC(ret)) { const ObString field = ObString::make_string(id); OB_UNIS_ENCODE(field); }
  }
  LST_DO_CODE(OB_UNIS_ENCODE, binding_.owner_generation, binding_.catalog_epoch, binding_.flags,
              binding_.minimum_arity, binding_.maximum_arity, binding_.column_count, binding_.physical_format_version);
  const uint32_t count = arguments_.count(); OB_UNIS_ENCODE(count);
  for (int64_t i = 0; OB_SUCC(ret) && i < arguments_.count(); ++i) { OB_UNIS_ENCODE(arguments_.at(i)); }
  for (int64_t i = 0; OB_SUCC(ret) && i < columns_.count(); ++i) {
    const auto &c = columns_.at(i);
    const ObString name = ObString::make_string(c.sql_name), id = ObString::make_string(c.type_id);
    LST_DO_CODE(OB_UNIS_ENCODE, name, id, c.nullable);
  }
  return ret;
}

OB_DEF_DESERIALIZE(PluginTableFunctionExtraInfo)
{
  int ret = OB_SUCCESS;
  binding_ = {}; binding_.struct_size = sizeof(binding_); binding_.kind = SEEKDB_PLUGIN_EXTENSION_TABLE_FUNCTION;
  arguments_.reset(); columns_.reset();
  for (char *id : {binding_.object_id, binding_.sql_name, binding_.owner_plugin_id,
                  binding_.result_type_id, binding_.physical_format_id}) {
    ObString field; OB_UNIS_DECODE(field);
    if (OB_SUCC(ret)) {
      if (field.length() < 0 || field.length() > SEEKDB_PLUGIN_MAX_IDENTIFIER_BYTES ||
          (field.length() && (!field.ptr() || std::memchr(field.ptr(), 0, field.length())))) ret = OB_INVALID_DATA;
      else if (field.length()) std::memcpy(id, field.ptr(), field.length());
    }
  }
  LST_DO_CODE(OB_UNIS_DECODE, binding_.owner_generation, binding_.catalog_epoch, binding_.flags,
              binding_.minimum_arity, binding_.maximum_arity, binding_.column_count, binding_.physical_format_version);
  uint32_t count = 0; OB_UNIS_DECODE(count);
  if (OB_SUCC(ret) && (count > SEEKDB_PLUGIN_MAX_ARGUMENTS || !binding_.column_count ||
      binding_.column_count > OB_MAX_COLUMN_NUMBER)) ret = OB_INVALID_DATA;
  if (OB_SUCC(ret)) ret = arguments_.prepare_allocate(count);
  for (uint32_t i = 0; OB_SUCC(ret) && i < count; ++i) {
    ObString id; OB_UNIS_DECODE(id);
    if (OB_SUCC(ret)) {
      if (id.length() < 0 || id.length() > SEEKDB_PLUGIN_MAX_IDENTIFIER_BYTES ||
          (id.length() && (!id.ptr() || std::memchr(id.ptr(), 0, id.length())))) ret = OB_INVALID_DATA;
      else ret = ob_write_string(allocator_, id, arguments_.at(i), true);
    }
  }
  if (OB_SUCC(ret)) ret = columns_.prepare_allocate(binding_.column_count);
  for (int64_t i = 0; OB_SUCC(ret) && i < columns_.count(); ++i) {
    auto &c = columns_.at(i); c = {}; c.struct_size = sizeof(c);
    for (char *id : {c.sql_name, c.type_id}) {
      ObString field; OB_UNIS_DECODE(field);
      if (OB_SUCC(ret)) {
        if (field.length() <= 0 || field.length() > SEEKDB_PLUGIN_MAX_IDENTIFIER_BYTES ||
            !field.ptr() || std::memchr(field.ptr(), 0, field.length())) ret = OB_INVALID_DATA;
        else std::memcpy(id, field.ptr(), field.length());
      }
    }
    OB_UNIS_DECODE(c.nullable);
  }
  if (OB_SUCC(ret) && !valid()) ret = OB_INVALID_DATA;
  if (OB_FAIL(ret)) { binding_ = {}; arguments_.reset(); columns_.reset(); }
  return ret;
}

OB_DEF_SERIALIZE_SIZE(PluginTableFunctionExtraInfo)
{
  int64_t len = 0;
  if (!valid()) return len;
  for (const char *id : {binding_.object_id, binding_.sql_name, binding_.owner_plugin_id,
                         binding_.result_type_id, binding_.physical_format_id}) {
    const ObString field = ObString::make_string(id); OB_UNIS_ADD_LEN(field);
  }
  LST_DO_CODE(OB_UNIS_ADD_LEN, binding_.owner_generation, binding_.catalog_epoch, binding_.flags,
              binding_.minimum_arity, binding_.maximum_arity, binding_.column_count, binding_.physical_format_version);
  const uint32_t count = arguments_.count(); OB_UNIS_ADD_LEN(count);
  for (int64_t i = 0; i < arguments_.count(); ++i) { OB_UNIS_ADD_LEN(arguments_.at(i)); }
  for (int64_t i = 0; i < columns_.count(); ++i) {
    const auto &c = columns_.at(i);
    const ObString name = ObString::make_string(c.sql_name), id = ObString::make_string(c.type_id);
    LST_DO_CODE(OB_UNIS_ADD_LEN, name, id, c.nullable);
  }
  return len;
}

int PluginTableFunctionExtraInfo::deep_copy(ObIAllocator &allocator, ObExprOperatorType type, ObIExprExtraInfo *&out) const
{
  out = nullptr;
  if (type != T_FUN_SYS_PLUGIN_TABLE_FUNCTION || !valid()) return OB_INVALID_DATA;
  const int64_t size = get_serialize_size();
  if (size <= 0 || size > MAX_TABLE_BINDING_BYTES) return OB_SIZE_OVERFLOW;
  auto *wire = static_cast<char *>(allocator.alloc(size));
  if (!wire) return OB_ALLOCATE_MEMORY_FAILED;
  int64_t pos = 0;
  int ret = serialize(wire, size, pos);
  ObIExprExtraInfo *copy = nullptr;
  if (OB_SUCC(ret)) ret = ObExprExtraInfoFactory::alloc(allocator, type, copy);
  if (OB_SUCC(ret)) {
    pos = 0;
    ret = static_cast<PluginTableFunctionExtraInfo *>(copy)->deserialize(wire, size, pos);
    if (OB_SUCC(ret) && pos != size) ret = OB_INVALID_DATA;
    if (OB_FAIL(ret)) static_cast<PluginTableFunctionExtraInfo *>(copy)->~PluginTableFunctionExtraInfo(); else out = copy;
  }
  return ret;
}

int PluginTableFunctionExpr::column_type(const seekdb_plugin_sql_column_v1_t &column, ObExprResType &type)
{
  if (!table_identifier(column.type_id)) return OB_INVALID_DATA;
  assign_sql_result_type(type, column.type_id);
  return OB_SUCCESS;
}

int PluginTableFunctionExpr::read_binding(const ObRawExpr &raw, PluginTableFunctionExtraInfo &info)
{
  info.binding_ = {}; info.arguments_.reset(); info.columns_.reset();
  if (raw.get_expr_type() != T_FUN_SYS_PLUGIN_TABLE_FUNCTION || raw.get_param_count() < 1 ||
      !raw.get_param_expr(0) || !raw.get_param_expr(0)->is_const_raw_expr()) return OB_INVALID_DATA;
  const auto &literal = static_cast<const ObConstRawExpr *>(raw.get_param_expr(0))->get_value();
  if (!literal.is_varbinary() || literal.is_null()) return OB_INVALID_DATA;
  const auto wire = literal.get_string();
  if (!wire.ptr() || wire.length() <= 0 || wire.length() > MAX_TABLE_BINDING_BYTES) return OB_INVALID_DATA;
  int64_t pos = 0;
  int ret = info.deserialize(wire.ptr(), wire.length(), pos);
  if (OB_SUCC(ret) && (pos != wire.length() || !info.valid() ||
      info.arguments_.count() != raw.get_param_count() - 1)) ret = OB_INVALID_DATA;
  const auto *result = raw.get_plugin_type();
  if (OB_SUCC(ret) && (!result || result->stored_ || result->catalog_epoch_ != info.binding_.catalog_epoch ||
      result->logical_id_ != ObString::make_string(info.columns_.at(0).type_id))) ret = OB_STATE_NOT_MATCH;
  if (OB_SUCC(ret)) {
    ObExprResType physical;
    if (OB_FAIL(column_type(info.columns_.at(0), physical))) {
    } else if (result->physical_type_ != physical.get_type()) ret = OB_STATE_NOT_MATCH;
  }
  for (int64_t i = 1; OB_SUCC(ret) && i < raw.get_param_count(); ++i) {
    const auto *arg = raw.get_param_expr(i);
    if (!arg) { ret = OB_INVALID_DATA; break; }
    const auto *logical = arg->get_plugin_type();
    const char *native = core_type_identifier(arg->get_data_type());
    const ObString id = logical ? logical->logical_id_ : native ? ObString::make_string(native) : ObString();
    if (id != info.arguments_.at(i - 1) || (logical && (logical->stored_ ||
        logical->physical_type_ != arg->get_data_type() || logical->catalog_epoch_ != info.binding_.catalog_epoch)))
      ret = OB_STATE_NOT_MATCH;
  }
  if (OB_FAIL(ret)) { info.binding_ = {}; info.arguments_.reset(); info.columns_.reset(); }
  return ret;
}

PluginTableFunctionExpr::PluginTableFunctionExpr(ObIAllocator &allocator)
    : ObFuncExprOperator(allocator, T_FUN_SYS_PLUGIN_TABLE_FUNCTION,
                         SQL_DISPATCH_NAME, PARAM_NUM_UNKNOWN,
                         NOT_VALID_FOR_GENERATED_COL, NOT_ROW_DIMENSION, true)
{}

int PluginTableFunctionExpr::calc_result_typeN(
    ObExprResType &type,
    ObExprResType *arguments,
    const int64_t argument_count,
    ObExprTypeCtx &type_context) const
try {
  auto *raw = type_context.get_raw_expr();
  auto *factory = raw ? raw->get_expr_factory() : nullptr;
  if (!arguments || !raw || !factory || argument_count < 1 || argument_count > SEEKDB_PLUGIN_MAX_ARGUMENTS + 1 ||
      raw->get_param_count() != argument_count || !raw->get_param_expr(0) ||
      !raw->get_param_expr(0)->is_const_raw_expr()) return OB_INVALID_ARGUMENT;
  const auto &literal = static_cast<const ObConstRawExpr *>(raw->get_param_expr(0))->get_value();
  PluginTableFunctionExtraInfo info(factory->get_allocator(), T_FUN_SYS_PLUGIN_TABLE_FUNCTION);
  int ret = OB_SUCCESS;
  if (literal.is_varbinary()) {
    if (OB_FAIL(read_binding(*raw, info))) return ret;
  } else {
    if (!share::g_mp || !literal.is_varchar() || literal.is_null()) return OB_INVALID_ARGUMENT;
    const auto name = literal.get_string();
    if (!name.ptr() || name.empty() || name.length() > SEEKDB_PLUGIN_MAX_IDENTIFIER_BYTES ||
        std::memchr(name.ptr(), 0, name.length())) return OB_INVALID_DATA;
    if (OB_FAIL(info.arguments_.prepare_allocate(argument_count - 1))) return ret;
    std::vector<const char *> types;
    std::vector<ObRawExpr *> converted;
    uint64_t epoch = 0;
    for (int64_t i = 1; i < argument_count; ++i) {
      auto *value = raw->get_param_expr(i);
      if (!value || !cast_input_supported(value->get_data_type())) return OB_NOT_SUPPORTED;
      auto *logical = value->get_plugin_type();
      if (logical && logical->physical_type_ != value->get_data_type()) return OB_STATE_NOT_MATCH;
      if (logical && logical->stored_) {
        if (OB_FAIL(PluginTypeValueExpr::build(*factory, logical->sql_name_, value, type_context.get_session()))) return ret;
        logical = value->get_plugin_type();
        if (!logical || logical->stored_) return OB_STATE_NOT_MATCH;
      }
      converted.push_back(value);
      if (logical) {
        if (epoch && epoch != logical->catalog_epoch_) return OB_STATE_NOT_MATCH;
        epoch = logical->catalog_epoch_;
      }
      const char *native = core_type_identifier(value->get_data_type());
      const ObString id = logical ? logical->logical_id_ : native ? ObString::make_string(native) : ObString();
      if (OB_FAIL(ob_write_string(factory->get_allocator(), id, info.arguments_.at(i - 1), true))) return ret;
      types.push_back(id.empty() ? nullptr : info.arguments_.at(i - 1).ptr());
    }
    const std::string owned_name(name.ptr(), name.length());
    if (OB_FAIL(share::g_mp->resolve_plugin_sql_object(SEEKDB_PLUGIN_EXTENSION_TABLE_FUNCTION,
        owned_name.c_str(), types.data(), types.size(), &info.binding_))) return ret;
    if (!info.binding_.column_count || info.binding_.column_count > OB_MAX_COLUMN_NUMBER ||
        !info.binding_.catalog_epoch || (epoch && epoch != info.binding_.catalog_epoch)) return OB_STATE_NOT_MATCH;
    if (OB_FAIL(info.columns_.prepare_allocate(info.binding_.column_count))) return ret;
    for (int64_t i = 0; i < info.columns_.count(); ++i) {
      info.columns_.at(i) = {};
      if (OB_FAIL(share::g_mp->describe_plugin_sql_column(&info.binding_, i, &info.columns_.at(i)))) return ret;
    }
    if (!info.valid()) return OB_INVALID_DATA;
    const int64_t size = info.get_serialize_size();
    if (size <= 0 || size > MAX_TABLE_BINDING_BYTES) return OB_SIZE_OVERFLOW;
    auto *wire = static_cast<char *>(factory->get_allocator().alloc(size));
    if (!wire) return OB_ALLOCATE_MEMORY_FAILED;
    int64_t pos = 0;
    if (OB_FAIL(info.serialize(wire, size, pos))) return ret;
    ObConstRawExpr *metadata = nullptr;
    if (OB_FAIL(factory->create_raw_expr(T_VARCHAR, metadata))) return ret;
    ObObj binary; binary.set_varchar(ObString(size, wire)); binary.set_collation_type(CS_TYPE_BINARY);
    metadata->set_value(binary);
    ObExprResType first;
    if (OB_FAIL(column_type(info.columns_.at(0), first))) return ret;
    PluginExprType identity;
    identity.logical_id_ = ObString::make_string(info.columns_.at(0).type_id);
    identity.physical_type_ = first.get_type(); identity.catalog_epoch_ = info.binding_.catalog_epoch;
    if (OB_FAIL(raw->set_plugin_type(identity))) return ret;
    raw->get_param_expr(0) = metadata;
    arguments[0] = metadata->get_result_type();
    // The resolver initialized calc metadata before this replacement. Restore
    // it for the new binary constant, just as the scalar binding path does.
    arguments[0].set_calc_meta(arguments[0]);
    arguments[0].set_calc_accuracy(arguments[0].get_accuracy());
    for (int64_t i = 1; i < argument_count; ++i) {
      raw->get_param_expr(i) = converted[i - 1];
      arguments[i] = converted[i - 1]->get_result_type();
      arguments[i].set_calc_meta(arguments[i]); arguments[i].set_calc_accuracy(arguments[i].get_accuracy());
    }
  }
  return column_type(info.columns_.at(0), type);
} catch (const std::bad_alloc &) { return OB_ALLOCATE_MEMORY_FAILED;
} catch (...) { return OB_ERR_UNEXPECTED; }

int PluginTableFunctionExpr::cg_expr(ObExprCGCtx &cg_context,
                                     const ObRawExpr &raw_expression,
                                     ObExpr &runtime_expression) const
{
  if (!cg_context.allocator_) return OB_INVALID_ARGUMENT;
  PluginTableFunctionExtraInfo info(*cg_context.allocator_, T_FUN_SYS_PLUGIN_TABLE_FUNCTION);
  int ret = read_binding(raw_expression, info);
  if (OB_FAIL(ret)) return ret;
  if (raw_expression.get_data_type() != raw_expression.get_plugin_type()->physical_type_)
    return OB_STATE_NOT_MATCH;
  if (OB_FAIL(info.deep_copy(*cg_context.allocator_, T_FUN_SYS_PLUGIN_TABLE_FUNCTION,
                            runtime_expression.extra_info_))) return ret;
  runtime_expression.eval_func_ = evaluate;
  return OB_SUCCESS;
}

int PluginTableFunctionExpr::evaluate(const ObExpr &expression,
                                      ObEvalCtx &context,
                                      ObDatum &result)
{
  UNUSED(expression);
  UNUSED(context);
  UNUSED(result);
  // Table streams are consumed by ObFunctionTableOp, never as scalar values.
  return OB_NOT_SUPPORTED;
}

int PluginTableFunctionExpr::resolve_binding(
    const ObRawExpr &expression,
    seekdb_plugin_sql_binding_v1_t &binding)
{
  binding = {};
  ObArenaAllocator temporary;
  PluginTableFunctionExtraInfo info(temporary, T_FUN_SYS_PLUGIN_TABLE_FUNCTION);
  const int ret = read_binding(expression, info);
  if (ret == OB_SUCCESS) binding = info.binding_;
  return ret;
}

int PluginTableFunctionExpr::fetch_row(
    const ObExpr &expression,
    ObEvalCtx &context,
    const ObIArray<ObExpr *> &columns)
{
  uint32_t emitted_rows = 0;
  return fetch(expression, context, columns, 1, false, emitted_rows);
}

int PluginTableFunctionExpr::fetch_batch(const ObExpr &expression, ObEvalCtx &context,
    const ObIArray<ObExpr *> &columns, uint32_t maximum_rows, uint32_t &emitted_rows)
{
  emitted_rows = 0;
  if (!maximum_rows || maximum_rows > context.max_batch_size_) return OB_INVALID_ARGUMENT;
  for (int64_t i = 0; i < columns.count(); ++i) {
    auto *column = columns.at(i);
    if (column && maximum_rows > 1 && !column->is_batch_result()) return OB_INVALID_ARGUMENT;
  }
  ObEvalCtx::BatchInfoScopeGuard batch(context);
  batch.set_batch_size(maximum_rows); batch.set_batch_idx(0);
  return fetch(expression, context, columns, maximum_rows, true, emitted_rows);
}

int PluginTableFunctionExpr::fetch(const ObExpr &expression, ObEvalCtx &context,
    const ObIArray<ObExpr *> &columns, uint32_t maximum_rows, bool batch, uint32_t &emitted_rows)
try {
  emitted_rows = 0;
  const auto *info = dynamic_cast<const PluginTableFunctionExtraInfo *>(expression.extra_info_);
  if (expression.arg_cnt_ < 1 || nullptr == share::g_mp || columns.empty() || !info || !info->valid() ||
      expression.arg_cnt_ - 1 != info->arguments_.count()) {
    return OB_INVALID_ARGUMENT;
  }
  int ret = OB_SUCCESS;
  ObExecContext &execution = context.exec_ctx_;
  RuntimeContext *runtime = static_cast<RuntimeContext *>(
      execution.get_expr_op_ctx(expression.expr_ctx_id_));
  if (nullptr == runtime &&
      OB_FAIL(execution.create_expr_op_ctx(expression.expr_ctx_id_, runtime))) {
    return ret;
  }
  if (nullptr == runtime) return OB_ALLOCATE_MEMORY_FAILED;

  const uint32_t argument_count = expression.arg_cnt_ - 1;
  std::vector<const char *> argument_types(argument_count, nullptr);
  for (uint32_t i = 0; i < argument_count; ++i) {
    argument_types[i] = info->arguments_.at(i).empty() ? nullptr : info->arguments_.at(i).ptr();
  }
  if (!runtime->initialized_) {
    runtime->binding_ = info->binding_;
    runtime->initialized_ = true;
  }
  if (static_cast<uint32_t>(columns.count()) != runtime->binding_.column_count) {
    return OB_INVALID_DATA;
  }
  if (runtime->error_ != OB_SUCCESS) return runtime->error_;
  if (runtime->ended_) return OB_ITER_END;

  TableResultSink sink{&context, &columns, 0, info, maximum_rows, batch};
  PluginSqlContext query_control(execution);
  std::vector<uint8_t> requested_columns(columns.count());
  for (int64_t i = 0; i < columns.count(); ++i) requested_columns[i] = columns.at(i) ? 1 : 0;
  seekdb_plugin_table_execution_context_v4_t plugin_context = {};
  plugin_context.v3.v2.v1.struct_size = sizeof(plugin_context);
  plugin_context.v3.v2.v1.host = reinterpret_cast<seekdb_plugin_host_handle_t *>(&sink);
  plugin_context.v3.v2.v1.emit_row = emit_sql_row;
  query_control.attach(plugin_context.v3);
  plugin_context.column_count = requested_columns.size();
  plugin_context.requested_columns = requested_columns.data();
  const auto fail = [&](int code) {
    runtime->error_ = code;
    runtime->cursor_.reset(); // close while its implementation lease is held
    return code;
  };

  if (!runtime->cursor_) {
    ObEvalCtx::TempAllocGuard temporary(context);
    std::vector<seekdb_plugin_execution_value_v1_t> arguments(argument_count);
    std::vector<ArgumentStorage> storage(argument_count);
    for (uint32_t i = 0; i < argument_count; ++i) {
      ObDatum *datum = nullptr;
      if (OB_FAIL(expression.args_[i + 1]->eval(context, datum))) return ret;
      if (nullptr == datum) return OB_ERR_UNEXPECTED;
      arguments[i].struct_size = sizeof(arguments[i]);
      arguments[i].type_id = argument_types[i];
      arguments[i].is_null = datum->is_null();
      // The loader applies casts before strictness. Do not inspect a NULL's
      // physical payload or discard a non-strict plugin invocation here.
      if (datum->is_null()) continue;
      const ObObjType type = expression.args_[i + 1]->datum_meta_.type_;
      if (ob_is_integer_type(type)) {
        storage[i].integer_ = datum->get_int();
        arguments[i].data =
            reinterpret_cast<const uint8_t *>(&storage[i].integer_);
        arguments[i].data_size = sizeof(storage[i].integer_);
        if (is_builtin_type(argument_types[i], ".int32")) {
          storage[i].int32_ = static_cast<int32_t>(storage[i].integer_);
          arguments[i].data = reinterpret_cast<const uint8_t *>(&storage[i].int32_); arguments[i].data_size = sizeof(storage[i].int32_);
        } else if (is_builtin_type(argument_types[i], ".uint32")) {
          storage[i].uint32_ = static_cast<uint32_t>(storage[i].integer_);
          arguments[i].data = reinterpret_cast<const uint8_t *>(&storage[i].uint32_); arguments[i].data_size = sizeof(storage[i].uint32_);
        } else if (is_builtin_type(argument_types[i], ".bool")) {
          storage[i].boolean_ = storage[i].integer_ != 0;
          arguments[i].data = &storage[i].boolean_; arguments[i].data_size = sizeof(storage[i].boolean_);
        }
      } else if (ob_is_double_type(type) || ob_is_float_type(type)) {
        storage[i].floating_ =
            ob_is_float_type(type) ? datum->get_float() : datum->get_double();
        arguments[i].data =
            reinterpret_cast<const uint8_t *>(&storage[i].floating_);
        arguments[i].data_size = sizeof(storage[i].floating_);
      } else {
        ObString bytes;
        if (OB_FAIL(ObTextStringHelper::read_real_string_data_with_copy(execution, temporary.get_allocator(),
            *datum, expression.args_[i + 1]->datum_meta_, expression.args_[i + 1]->obj_meta_.has_lob_header(), bytes))) return ret;
        arguments[i].data = reinterpret_cast<const uint8_t *>(bytes.ptr());
        arguments[i].data_size = static_cast<uint64_t>(bytes.length());
      }
    }
    ret = share::g_mp->open_bound_plugin_table_function(
        &runtime->binding_, &plugin_context.v3.v2.v1,
        arguments.empty() ? nullptr : arguments.data(), argument_count,
        runtime->cursor_);
    if (query_control.error() != OB_SUCCESS) return fail(query_control.error());
    if (OB_ITER_END == ret) runtime->ended_ = true;
    if (OB_SUCCESS != ret) return ret == OB_ITER_END ? ret : fail(ret);
    if (!runtime->cursor_) return OB_INVALID_DATA;
  }

  uint32_t callback_rows = 0;
  ret = runtime->cursor_->next(&plugin_context.v3.v2.v1, maximum_rows, &callback_rows);
  if (query_control.error() != OB_SUCCESS) return fail(query_control.error());
  if (ret != OB_SUCCESS && ret != OB_ITER_END) return fail(ret);
  if (sink.status_ != SEEKDB_PLUGIN_STATUS_OK) return fail(sink.status_ == SEEKDB_PLUGIN_STATUS_NO_MEMORY
      ? OB_ALLOCATE_MEMORY_FAILED : OB_INVALID_DATA);
  if (callback_rows != sink.emitted_ || callback_rows > maximum_rows ||
      (OB_SUCCESS == ret && callback_rows == 0) || (OB_ITER_END == ret && callback_rows != 0)) {
    return fail(OB_INVALID_DATA);
  }
  if (OB_ITER_END == ret) runtime->ended_ = true;
  emitted_rows = callback_rows;
  return ret;
} catch (const std::bad_alloc &) { return OB_ALLOCATE_MEMORY_FAILED;
} catch (...) { return OB_ERR_UNEXPECTED; }

int PluginTableFunctionExpr::rescan(const ObExpr &expression,
                                    ObEvalCtx &context)
{
  return close(expression, context);
}

int PluginTableFunctionExpr::close(const ObExpr &expression,
                                   ObEvalCtx &context)
{
  RuntimeContext *runtime = static_cast<RuntimeContext *>(
      context.exec_ctx_.get_expr_op_ctx(expression.expr_ctx_id_));
  if (nullptr == runtime) return OB_SUCCESS;
  runtime->ended_ = false;
  runtime->error_ = OB_SUCCESS;
  if (!runtime->cursor_) return OB_SUCCESS;
  const int ret = runtime->cursor_->close();
  runtime->cursor_.reset();
  return ret;
}

} // namespace sql
} // namespace oceanbase
