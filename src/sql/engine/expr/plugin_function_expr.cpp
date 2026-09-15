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

bool type_ends_with(const char *type_id, const char *suffix)
{
  if (nullptr == type_id || nullptr == suffix) return false;
  const size_t type_size = std::strlen(type_id);
  const size_t suffix_size = std::strlen(suffix);
  return type_size >= suffix_size &&
         std::memcmp(type_id + type_size - suffix_size, suffix,
                     suffix_size) == 0;
}

struct ResultSink
{
  const ObExpr *expression_;
  ObEvalCtx *context_;
  ObDatum *result_;
  bool emitted_;
};

seekdb_plugin_status_t SEEKDB_PLUGIN_CALL emit_sql_result(
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

  if (type_ends_with(result->type_id, ".float64")) {
    if (result->data_size != sizeof(double)) {
      return SEEKDB_PLUGIN_STATUS_INVALID_ARGUMENT;
    }
    double value = 0;
    std::memcpy(&value, result->data, sizeof(value));
    sink.result_->set_double(value);
  } else if (type_ends_with(result->type_id, ".bool")) {
    if (result->data_size != sizeof(uint8_t)) {
      return SEEKDB_PLUGIN_STATUS_INVALID_ARGUMENT;
    }
    sink.result_->set_int(result->data[0] != 0 ? 1 : 0);
  } else if (type_ends_with(result->type_id, ".int32")) {
    if (result->data_size != sizeof(int32_t)) {
      return SEEKDB_PLUGIN_STATUS_INVALID_ARGUMENT;
    }
    int32_t value = 0;
    std::memcpy(&value, result->data, sizeof(value));
    sink.result_->set_int(static_cast<int64_t>(value));
  } else if (type_ends_with(result->type_id, ".uint32")) {
    if (result->data_size != sizeof(uint32_t)) {
      return SEEKDB_PLUGIN_STATUS_INVALID_ARGUMENT;
    }
    uint32_t value = 0;
    std::memcpy(&value, result->data, sizeof(value));
    sink.result_->set_int(static_cast<int64_t>(value));
  } else if (type_ends_with(result->type_id, ".int64") ||
             type_ends_with(result->type_id, ".uint64")) {
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

struct ArgumentStorage
{
  int64_t integer_ = 0;
  double floating_ = 0;
};

struct TableResultSink
{
  ObEvalCtx *context_;
  const ObIArray<ObExpr *> *columns_;
  bool emitted_;
};

seekdb_plugin_status_t SEEKDB_PLUGIN_CALL emit_sql_row(
    seekdb_plugin_host_handle_t *host,
    const seekdb_plugin_table_row_v1_t *row)
{
  if (nullptr == host || nullptr == row || row->struct_size < sizeof(*row) ||
      nullptr == row->columns) {
    return SEEKDB_PLUGIN_STATUS_INVALID_ARGUMENT;
  }
  TableResultSink &sink = *reinterpret_cast<TableResultSink *>(host);
  if (sink.emitted_ || nullptr == sink.context_ || nullptr == sink.columns_ ||
      static_cast<int64_t>(row->column_count) != sink.columns_->count()) {
    return SEEKDB_PLUGIN_STATUS_FAILED_PRECONDITION;
  }
  for (uint32_t i = 0; i < row->column_count; ++i) {
    ObExpr *column = sink.columns_->at(i);
    if (nullptr == column) return SEEKDB_PLUGIN_STATUS_INTERNAL;
    ObDatum &datum = column->locate_datum_for_write(*sink.context_);
    ResultSink column_sink{column, sink.context_, &datum, false};
    const seekdb_plugin_status_t status = emit_sql_result(
        reinterpret_cast<seekdb_plugin_host_handle_t *>(&column_sink),
        &row->columns[i]);
    if (SEEKDB_PLUGIN_STATUS_OK != status) return status;
    column->set_evaluated_projected(*sink.context_);
  }
  sink.emitted_ = true;
  return SEEKDB_PLUGIN_STATUS_OK;
}

void assign_sql_result_type(ObExprResType &type, const char *type_id)
{
  if (type_ends_with(type_id, ".geometry")) {
    type.set_geometry();
    type.set_length(
        ObAccuracy::DDL_DEFAULT_ACCURACY[ObGeometryType].get_length());
  } else if (type_ends_with(type_id, ".float64")) {
    type.set_double();
  } else if (type_ends_with(type_id, ".int32") ||
             type_ends_with(type_id, ".uint32") ||
             type_ends_with(type_id, ".int64") ||
             type_ends_with(type_id, ".uint64") ||
             type_ends_with(type_id, ".bool")) {
    type.set_int();
  } else {
    type.set_varchar();
    type.set_length(OB_MAX_VARCHAR_LENGTH);
    type.set_collation_type(CS_TYPE_UTF8MB4_BIN);
    type.set_collation_level(CS_LEVEL_IMPLICIT);
  }
}

} // namespace

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
{
  UNUSED(type_context);
  if (nullptr == arguments || argument_count < 1 || nullptr == share::g_mp ||
      !arguments[0].is_literal()) {
    return OB_INVALID_ARGUMENT;
  }
  const ObString sql_name = arguments[0].get_param().get_string();
  std::string owned_name(sql_name.ptr(), sql_name.length());
  std::vector<const char *> argument_types;
  argument_types.reserve(static_cast<size_t>(argument_count - 1));
  for (int64_t i = 1; i < argument_count; ++i) {
    argument_types.push_back(core_type_identifier(arguments[i].get_type()));
  }

  seekdb_plugin_sql_binding_v1_t binding = {};
  int ret = share::g_mp->resolve_plugin_sql_object(
      SEEKDB_PLUGIN_EXTENSION_FUNCTION, owned_name.c_str(),
      argument_types.empty() ? nullptr : argument_types.data(),
      static_cast<uint32_t>(argument_types.size()), &binding);
  if (OB_SUCCESS != ret) return ret;

  assign_sql_result_type(type, binding.result_type_id);
  return OB_SUCCESS;
}

int PluginFunctionExpr::cg_expr(ObExprCGCtx &cg_context,
                                const ObRawExpr &raw_expression,
                                ObExpr &runtime_expression) const
{
  UNUSED(cg_context);
  if (raw_expression.get_param_count() < 1) return OB_INVALID_ARGUMENT;
  runtime_expression.eval_func_ = evaluate;
  return OB_SUCCESS;
}

int PluginFunctionExpr::evaluate(const ObExpr &expression,
                                 ObEvalCtx &context,
                                 ObDatum &result)
{
  if (expression.arg_cnt_ < 1 || nullptr == share::g_mp) {
    return OB_NOT_SUPPORTED;
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

  ObDatum *name_datum = nullptr;
  if (OB_FAIL(expression.args_[0]->eval(context, name_datum))) return ret;
  if (nullptr == name_datum || name_datum->is_null()) {
    return OB_INVALID_ARGUMENT;
  }
  const ObString sql_name = name_datum->get_string();
  const std::string owned_name(sql_name.ptr(), sql_name.length());
  const uint32_t argument_count = expression.arg_cnt_ - 1;

  std::vector<const char *> argument_types(argument_count, nullptr);
  for (uint32_t i = 0; i < argument_count; ++i) {
    argument_types[i] =
        core_type_identifier(expression.args_[i + 1]->datum_meta_.type_);
  }
  if (!runtime->initialized_) {
    ret = share::g_mp->resolve_plugin_sql_object(
        SEEKDB_PLUGIN_EXTENSION_FUNCTION, owned_name.c_str(),
        argument_types.empty() ? nullptr : argument_types.data(),
        argument_count, &runtime->binding_);
    if (OB_SUCCESS != ret) return ret;
    runtime->initialized_ = true;
  }

  std::vector<seekdb_plugin_execution_value_v1_t> arguments(argument_count);
  std::vector<ArgumentStorage> argument_storage(argument_count);
  ObEvalCtx::TempAllocGuard temporary_allocator(context);
  for (uint32_t i = 0; i < argument_count; ++i) {
    ObDatum *datum = nullptr;
    if (OB_FAIL(expression.args_[i + 1]->eval(context, datum))) return ret;
    if (nullptr == datum) return OB_ERR_UNEXPECTED;

    arguments[i].struct_size = sizeof(arguments[i]);
    arguments[i].type_id = argument_types[i];
    arguments[i].is_null = datum->is_null() ? 1 : 0;
    if (datum->is_null()) {
      if ((runtime->binding_.flags &
           SEEKDB_PLUGIN_EXTENSION_FLAG_NULL_PROPAGATING) != 0) {
        result.set_null();
        return OB_SUCCESS;
      }
      continue;
    }

    const ObObjType type = expression.args_[i + 1]->datum_meta_.type_;
    if (ob_is_integer_type(type)) {
      argument_storage[i].integer_ = datum->get_int();
      arguments[i].data = reinterpret_cast<const uint8_t *>(
          &argument_storage[i].integer_);
      arguments[i].data_size = sizeof(argument_storage[i].integer_);
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
              context.exec_ctx_, temporary_allocator.get_allocator(), *datum,
              expression.args_[i + 1]->datum_meta_,
              expression.args_[i + 1]->obj_meta_.has_lob_header(), bytes))) {
        return ret;
      }
      arguments[i].data = reinterpret_cast<const uint8_t *>(bytes.ptr());
      arguments[i].data_size = static_cast<uint64_t>(bytes.length());
    }
  }

  ResultSink sink{&expression, &context, &result, false};
  seekdb_plugin_execution_context_v1_t plugin_context = {};
  plugin_context.struct_size = sizeof(plugin_context);
  plugin_context.host = reinterpret_cast<seekdb_plugin_host_handle_t *>(&sink);
  plugin_context.emit_result = emit_sql_result;
  ret = share::g_mp->execute_bound_plugin_function(
      &runtime->binding_, &plugin_context,
      arguments.empty() ? nullptr : arguments.data(), argument_count);
  if (OB_SUCCESS == ret && !sink.emitted_) return OB_ERR_UNEXPECTED;
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
{
  UNUSED(type_context);
  if (nullptr == arguments || argument_count < 1 || nullptr == share::g_mp ||
      !arguments[0].is_literal()) {
    return OB_INVALID_ARGUMENT;
  }
  const ObString sql_name = arguments[0].get_param().get_string();
  const std::string owned_name(sql_name.ptr(), sql_name.length());
  std::vector<const char *> argument_types;
  argument_types.reserve(static_cast<size_t>(argument_count - 1));
  for (int64_t i = 1; i < argument_count; ++i) {
    argument_types.push_back(core_type_identifier(arguments[i].get_type()));
  }
  seekdb_plugin_sql_binding_v1_t binding = {};
  int ret = share::g_mp->resolve_plugin_sql_object(
      SEEKDB_PLUGIN_EXTENSION_TABLE_FUNCTION, owned_name.c_str(),
      argument_types.empty() ? nullptr : argument_types.data(),
      static_cast<uint32_t>(argument_types.size()), &binding);
  if (OB_SUCCESS != ret) return ret;
  if (binding.column_count == 0) return OB_INVALID_DATA;

  seekdb_plugin_sql_column_v1_t column = {};
  ret = share::g_mp->describe_plugin_sql_column(&binding, 0, &column);
  if (OB_SUCCESS != ret) return ret;
  assign_sql_result_type(type, column.type_id);
  return OB_SUCCESS;
}

int PluginTableFunctionExpr::cg_expr(ObExprCGCtx &cg_context,
                                     const ObRawExpr &raw_expression,
                                     ObExpr &runtime_expression) const
{
  UNUSED(cg_context);
  if (raw_expression.get_param_count() < 1) return OB_INVALID_ARGUMENT;
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
  if (nullptr == share::g_mp ||
      expression.get_expr_type() != T_FUN_SYS_PLUGIN_TABLE_FUNCTION ||
      expression.get_param_count() < 1 ||
      nullptr == expression.get_param_expr(0) ||
      !expression.get_param_expr(0)->is_const_raw_expr()) {
    return OB_INVALID_ARGUMENT;
  }
  const auto *identity =
      static_cast<const ObConstRawExpr *>(expression.get_param_expr(0));
  const ObString sql_name = identity->get_value().get_string();
  const std::string owned_name(sql_name.ptr(), sql_name.length());
  std::vector<const char *> argument_types;
  argument_types.reserve(static_cast<size_t>(expression.get_param_count() - 1));
  for (int64_t i = 1; i < expression.get_param_count(); ++i) {
    const ObRawExpr *argument = expression.get_param_expr(i);
    if (nullptr == argument) return OB_INVALID_ARGUMENT;
    argument_types.push_back(
        core_type_identifier(argument->get_result_type().get_type()));
  }
  return share::g_mp->resolve_plugin_sql_object(
      SEEKDB_PLUGIN_EXTENSION_TABLE_FUNCTION, owned_name.c_str(),
      argument_types.empty() ? nullptr : argument_types.data(),
      static_cast<uint32_t>(argument_types.size()), &binding);
}

int PluginTableFunctionExpr::fetch_row(
    const ObExpr &expression,
    ObEvalCtx &context,
    const ObIArray<ObExpr *> &columns)
{
  if (expression.arg_cnt_ < 1 || nullptr == share::g_mp || columns.empty()) {
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

  ObDatum *name_datum = nullptr;
  if (OB_FAIL(expression.args_[0]->eval(context, name_datum))) return ret;
  if (nullptr == name_datum || name_datum->is_null()) {
    return OB_INVALID_ARGUMENT;
  }
  const ObString sql_name = name_datum->get_string();
  const std::string owned_name(sql_name.ptr(), sql_name.length());
  const uint32_t argument_count = expression.arg_cnt_ - 1;
  std::vector<const char *> argument_types(argument_count, nullptr);
  for (uint32_t i = 0; i < argument_count; ++i) {
    argument_types[i] =
        core_type_identifier(expression.args_[i + 1]->datum_meta_.type_);
  }
  if (!runtime->initialized_) {
    ret = share::g_mp->resolve_plugin_sql_object(
        SEEKDB_PLUGIN_EXTENSION_TABLE_FUNCTION, owned_name.c_str(),
        argument_types.empty() ? nullptr : argument_types.data(),
        argument_count, &runtime->binding_);
    if (OB_SUCCESS != ret) return ret;
    runtime->initialized_ = true;
  }
  if (static_cast<uint32_t>(columns.count()) != runtime->binding_.column_count) {
    return OB_INVALID_DATA;
  }

  TableResultSink sink{&context, &columns, false};
  seekdb_plugin_table_execution_context_v1_t plugin_context = {};
  plugin_context.struct_size = sizeof(plugin_context);
  plugin_context.host = reinterpret_cast<seekdb_plugin_host_handle_t *>(&sink);
  plugin_context.emit_row = emit_sql_row;

  if (!runtime->cursor_) {
    std::vector<seekdb_plugin_execution_value_v1_t> arguments(argument_count);
    std::vector<ArgumentStorage> storage(argument_count);
    for (uint32_t i = 0; i < argument_count; ++i) {
      ObDatum *datum = nullptr;
      if (OB_FAIL(expression.args_[i + 1]->eval(context, datum))) return ret;
      if (nullptr == datum) return OB_ERR_UNEXPECTED;
      if (datum->is_null()) return OB_ITER_END;
      arguments[i].struct_size = sizeof(arguments[i]);
      arguments[i].type_id = argument_types[i];
      const ObObjType type = expression.args_[i + 1]->datum_meta_.type_;
      if (ob_is_integer_type(type)) {
        storage[i].integer_ = datum->get_int();
        arguments[i].data =
            reinterpret_cast<const uint8_t *>(&storage[i].integer_);
        arguments[i].data_size = sizeof(storage[i].integer_);
      } else if (ob_is_double_type(type) || ob_is_float_type(type)) {
        storage[i].floating_ =
            ob_is_float_type(type) ? datum->get_float() : datum->get_double();
        arguments[i].data =
            reinterpret_cast<const uint8_t *>(&storage[i].floating_);
        arguments[i].data_size = sizeof(storage[i].floating_);
      } else {
        const ObString bytes = datum->get_string();
        arguments[i].data = reinterpret_cast<const uint8_t *>(bytes.ptr());
        arguments[i].data_size = static_cast<uint64_t>(bytes.length());
      }
    }
    ret = share::g_mp->open_bound_plugin_table_function(
        &runtime->binding_, &plugin_context,
        arguments.empty() ? nullptr : arguments.data(), argument_count,
        runtime->cursor_);
    if (OB_SUCCESS != ret) return ret;
  }

  uint32_t emitted_rows = 0;
  ret = runtime->cursor_->next(&plugin_context, 1, &emitted_rows);
  if (OB_SUCCESS == ret && (emitted_rows != 1 || !sink.emitted_)) {
    return OB_INVALID_DATA;
  }
  return ret;
}

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
  if (nullptr == runtime || !runtime->cursor_) return OB_SUCCESS;
  const int ret = runtime->cursor_->close();
  runtime->cursor_.reset();
  return ret;
}

} // namespace sql
} // namespace oceanbase
