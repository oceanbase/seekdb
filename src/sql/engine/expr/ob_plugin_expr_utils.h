/*
 * Copyright (c) 2026 OceanBase.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 */

#ifndef OCEANBASE_SQL_OB_PLUGIN_EXPR_UTILS_H_
#define OCEANBASE_SQL_OB_PLUGIN_EXPR_UTILS_H_

#include "lib/ob_errno.h"
#include "lib/string/ob_string.h"
#include "sql/engine/expr/ob_expr.h"
#include "sql/engine/expr/ob_expr_lob_utils.h"
#include "seekdb/plugin/execution_spi.h"
#include "seekdb/plugin/extension_spi.h"
#include "share/rc/ob_module_provider.h"
#include <cstring>

namespace oceanbase
{
namespace sql
{

// Copy a byte-oriented plugin result into the normal SQL string/geometry
// datum without depending on the legacy GIS object model.  This is the small
// host-side bridge retained by the core-only profile.
inline int pack_plugin_expr_result(const ObExpr &expr,
                                   ObEvalCtx &ctx,
                                   ObDatum &result,
                                   const char *data,
                                   const int64_t size)
{
  int ret = OB_SUCCESS;
  if (nullptr == data || size <= 0) {
    ret = OB_INVALID_ARGUMENT;
  } else {
    ObTextStringDatumResult text_result(expr.datum_meta_.type_, &expr, &ctx, &result);
    if (OB_FAIL(text_result.init(size))) {
    } else if (OB_FAIL(text_result.append(data, size))) {
    } else {
      text_result.set_result();
    }
  }
  return ret;
}

struct PluginBoolResultSink
{
  ObDatum *result_;
};

inline seekdb_plugin_status_t SEEKDB_PLUGIN_CALL emit_plugin_bool_result(
    seekdb_plugin_host_handle_t *host,
    const seekdb_plugin_execution_result_v1_t *result)
{
  if (nullptr == host || nullptr == result || result->struct_size != sizeof(*result) ||
      result->is_null != 0 || result->data_size != sizeof(uint8_t) ||
      nullptr == result->data || nullptr == result->type_id ||
      0 != std::strcmp(result->type_id, "org.seekdb.gis.scalar.bool")) {
    return SEEKDB_PLUGIN_STATUS_INVALID_ARGUMENT;
  }
  PluginBoolResultSink *sink = reinterpret_cast<PluginBoolResultSink *>(host);
  if (nullptr == sink->result_) return SEEKDB_PLUGIN_STATUS_FAILED_PRECONDITION;
  sink->result_->set_bool(result->data[0] != 0);
  return SEEKDB_PLUGIN_STATUS_OK;
}

// Execute a point-geometry relation in the GIS plugin.  The adapter accepts
// geometry byte payloads for the first two arguments and an optional numeric
// distance for ST_DWithin; no core geometry object crosses the SPI boundary.
inline int execute_plugin_geometry_relation(const char *service_name,
                                            const ObExpr &expr,
                                            ObEvalCtx &ctx,
                                            ObDatum &result)
{
  int ret = OB_SUCCESS;
  const uint32_t argument_count = static_cast<uint32_t>(expr.arg_cnt_);
  if (argument_count != 2 && argument_count != 3) {
    ret = OB_NOT_SUPPORTED;
  } else if (nullptr == share::g_mp) {
    ret = OB_NOT_SUPPORTED;
  } else {
    ObDatum *datums[3] = {nullptr, nullptr, nullptr};
    for (uint32_t i = 0; OB_SUCC(ret) && i < argument_count; ++i) {
      if (OB_FAIL(expr.args_[i]->eval(ctx, datums[i]))) {
      } else if (datums[i]->is_null()) {
        result.set_null();
        return OB_SUCCESS;
      }
    }
    if (OB_SUCC(ret)) {
      seekdb_plugin_execution_value_v1_t arguments[3] = {};
      double distance = 0.0;
      for (uint32_t i = 0; i < argument_count; ++i) {
        arguments[i].struct_size = sizeof(arguments[i]);
        if (i < 2) {
          const ObString geometry = datums[i]->get_string();
          arguments[i].type_id = "org.seekdb.gis.geometry";
          arguments[i].data = reinterpret_cast<const uint8_t *>(geometry.ptr());
          arguments[i].data_size = static_cast<uint64_t>(geometry.length());
        } else {
          distance = datums[i]->get_double();
          arguments[i].type_id = "org.seekdb.gis.scalar.float64";
          arguments[i].data = reinterpret_cast<const uint8_t *>(&distance);
          arguments[i].data_size = sizeof(double);
        }
      }
      PluginBoolResultSink sink{&result};
      seekdb_plugin_execution_context_v1_t plugin_ctx = {};
      plugin_ctx.struct_size = sizeof(plugin_ctx);
      plugin_ctx.host = reinterpret_cast<seekdb_plugin_host_handle_t *>(&sink);
      plugin_ctx.emit_result = emit_plugin_bool_result;
      const int plugin_ret = share::g_mp->execute_plugin_extension(
          SEEKDB_PLUGIN_EXTENSION_FUNCTION, service_name,
          &plugin_ctx, arguments, argument_count);
      if (OB_SUCCESS != plugin_ret) ret = OB_NOT_SUPPORTED;
    }
  }
  return ret;
}

struct PluginBytesResultSink
{
  const ObExpr *expr_;
  ObEvalCtx *ctx_;
  ObDatum *result_;
};

inline seekdb_plugin_status_t SEEKDB_PLUGIN_CALL emit_plugin_bytes_result(
    seekdb_plugin_host_handle_t *host,
    const seekdb_plugin_execution_result_v1_t *plugin_result)
{
  if (nullptr == host || nullptr == plugin_result ||
      plugin_result->struct_size != sizeof(*plugin_result) ||
      plugin_result->is_null != 0 || nullptr == plugin_result->data ||
      plugin_result->data_size == 0 || nullptr == plugin_result->type_id) {
    return SEEKDB_PLUGIN_STATUS_INVALID_ARGUMENT;
  }
  PluginBytesResultSink *sink = reinterpret_cast<PluginBytesResultSink *>(host);
  if (nullptr == sink->expr_ || nullptr == sink->ctx_ || nullptr == sink->result_) {
    return SEEKDB_PLUGIN_STATUS_FAILED_PRECONDITION;
  }
  const int ret = pack_plugin_expr_result(*sink->expr_, *sink->ctx_, *sink->result_,
                                          reinterpret_cast<const char *>(plugin_result->data),
                                          static_cast<int64_t>(plugin_result->data_size));
  return OB_SUCCESS == ret ? SEEKDB_PLUGIN_STATUS_OK : SEEKDB_PLUGIN_STATUS_INTERNAL;
}

inline int execute_plugin_geometry_bytes(const char *service_name,
                                         const ObExpr &expr,
                                         ObEvalCtx &ctx,
                                         ObDatum &result,
                                         bool geometry_input)
{
  int ret = OB_SUCCESS;
  const uint32_t argument_count = static_cast<uint32_t>(expr.arg_cnt_);
  if (nullptr == share::g_mp || (argument_count != 1 && argument_count != 2)) {
    ret = OB_NOT_SUPPORTED;
  } else {
    ObDatum *datums[2] = {nullptr, nullptr};
    for (uint32_t i = 0; OB_SUCC(ret) && i < argument_count; ++i) {
      if (OB_FAIL(expr.args_[i]->eval(ctx, datums[i]))) {
      } else if (datums[i]->is_null()) {
        result.set_null();
        return OB_SUCCESS;
      }
    }
    if (OB_SUCC(ret)) {
      seekdb_plugin_execution_value_v1_t arguments[2] = {};
      const ObString first = datums[0]->get_string();
      arguments[0].struct_size = sizeof(arguments[0]);
      arguments[0].type_id = geometry_input ? "org.seekdb.gis.geometry" : "org.seekdb.gis.scalar.bytes";
      arguments[0].data = reinterpret_cast<const uint8_t *>(first.ptr());
      arguments[0].data_size = static_cast<uint64_t>(first.length());
      if (argument_count == 2) {
        const uint32_t srid = datums[1]->get_uint32();
        arguments[1].struct_size = sizeof(arguments[1]);
        arguments[1].type_id = "org.seekdb.gis.scalar.uint32";
        arguments[1].data = reinterpret_cast<const uint8_t *>(&srid);
        arguments[1].data_size = sizeof(srid);
      }
      PluginBytesResultSink sink{&expr, &ctx, &result};
      seekdb_plugin_execution_context_v1_t plugin_ctx = {};
      plugin_ctx.struct_size = sizeof(plugin_ctx);
      plugin_ctx.host = reinterpret_cast<seekdb_plugin_host_handle_t *>(&sink);
      plugin_ctx.emit_result = emit_plugin_bytes_result;
      const int plugin_ret = share::g_mp->execute_plugin_extension(
          SEEKDB_PLUGIN_EXTENSION_FUNCTION, service_name, &plugin_ctx, arguments, argument_count);
      if (OB_SUCCESS != plugin_ret) ret = OB_NOT_SUPPORTED;
    }
  }
  return ret;
}

// Execute a variadic geometry constructor.  All arguments are opaque geometry
// payloads; the plugin owns validation and WKB assembly.  This is used by the
// collection constructors so no legacy ObGeometry object enters the core-only
// profile.
inline int execute_plugin_geometry_variadic(const char *service_name,
                                            const ObExpr &expr,
                                            ObEvalCtx &ctx,
                                            ObDatum &result)
{
  int ret = OB_SUCCESS;
  const uint32_t argument_count = static_cast<uint32_t>(expr.arg_cnt_);
  if (argument_count == 0 || argument_count > 64 || nullptr == share::g_mp) {
    ret = OB_NOT_SUPPORTED;
  } else {
    ObDatum *datums[64] = {nullptr};
    seekdb_plugin_execution_value_v1_t arguments[64] = {};
    for (uint32_t i = 0; OB_SUCC(ret) && i < argument_count; ++i) {
      if (OB_FAIL(expr.args_[i]->eval(ctx, datums[i]))) {
      } else if (datums[i]->is_null()) {
        result.set_null();
        return OB_SUCCESS;
      } else {
        const ObString geometry = datums[i]->get_string();
        arguments[i].struct_size = sizeof(arguments[i]);
        arguments[i].type_id = "org.seekdb.gis.geometry";
        arguments[i].data = reinterpret_cast<const uint8_t *>(geometry.ptr());
        arguments[i].data_size = static_cast<uint64_t>(geometry.length());
      }
    }
    if (OB_SUCC(ret)) {
      PluginBytesResultSink sink{&expr, &ctx, &result};
      seekdb_plugin_execution_context_v1_t plugin_ctx = {};
      plugin_ctx.struct_size = sizeof(plugin_ctx);
      plugin_ctx.host = reinterpret_cast<seekdb_plugin_host_handle_t *>(&sink);
      plugin_ctx.emit_result = emit_plugin_bytes_result;
      const int plugin_ret = share::g_mp->execute_plugin_extension(
          SEEKDB_PLUGIN_EXTENSION_FUNCTION, service_name, &plugin_ctx,
          arguments, argument_count);
      if (OB_SUCCESS != plugin_ret) ret = OB_NOT_SUPPORTED;
    }
  }
  return ret;
}

// Generic value bridge for GIS operators whose optional arguments are scalar
// controls (distance, extent, strategy, SRID, ...).  The plugin receives a
// stable type-tagged value array and returns an opaque byte/geometry result.
inline int execute_plugin_gis_values(const char *service_name,
                                     const ObExpr &expr,
                                     ObEvalCtx &ctx,
                                     ObDatum &result)
{
  int ret = OB_SUCCESS;
  const uint32_t argument_count = static_cast<uint32_t>(expr.arg_cnt_);
  if (argument_count == 0 || argument_count > 8 || nullptr == share::g_mp) {
    return OB_NOT_SUPPORTED;
  }
  ObDatum *datums[8] = {nullptr};
  seekdb_plugin_execution_value_v1_t arguments[8] = {};
  uint64_t unsigned_values[8] = {};
  double numeric_values[8] = {};
  for (uint32_t i = 0; OB_SUCC(ret) && i < argument_count; ++i) {
    if (OB_FAIL(expr.args_[i]->eval(ctx, datums[i]))) {
    } else if (datums[i]->is_null()) {
      result.set_null();
      return OB_SUCCESS;
    } else {
      arguments[i].struct_size = sizeof(arguments[i]);
      const ObObjType type = expr.args_[i]->datum_meta_.type_;
      if (ob_is_geometry(type)) {
        const ObString value = datums[i]->get_string();
        arguments[i].type_id = "org.seekdb.gis.geometry";
        arguments[i].data = reinterpret_cast<const uint8_t *>(value.ptr());
        arguments[i].data_size = static_cast<uint64_t>(value.length());
      } else if (ob_is_string_type(type)) {
        const ObString value = datums[i]->get_string();
        arguments[i].type_id = "org.seekdb.gis.scalar.bytes";
        arguments[i].data = reinterpret_cast<const uint8_t *>(value.ptr());
        arguments[i].data_size = static_cast<uint64_t>(value.length());
      } else if (ob_is_unsigned_type(type)) {
        unsigned_values[i] = datums[i]->get_uint64();
        arguments[i].type_id = "org.seekdb.gis.scalar.uint64";
        arguments[i].data = reinterpret_cast<const uint8_t *>(&unsigned_values[i]);
        arguments[i].data_size = sizeof(unsigned_values[i]);
      } else if (ob_is_numeric_type(type)) {
        numeric_values[i] = datums[i]->get_double();
        arguments[i].type_id = "org.seekdb.gis.scalar.float64";
        arguments[i].data = reinterpret_cast<const uint8_t *>(&numeric_values[i]);
        arguments[i].data_size = sizeof(numeric_values[i]);
      } else {
        ret = OB_NOT_SUPPORTED;
      }
    }
  }
  if (OB_SUCC(ret)) {
    PluginBytesResultSink sink{&expr, &ctx, &result};
    seekdb_plugin_execution_context_v1_t plugin_ctx = {};
    plugin_ctx.struct_size = sizeof(plugin_ctx);
    plugin_ctx.host = reinterpret_cast<seekdb_plugin_host_handle_t *>(&sink);
    plugin_ctx.emit_result = emit_plugin_bytes_result;
    const int plugin_ret = share::g_mp->execute_plugin_extension(
        SEEKDB_PLUGIN_EXTENSION_FUNCTION, service_name, &plugin_ctx,
        arguments, argument_count);
    if (OB_SUCCESS != plugin_ret) ret = OB_NOT_SUPPORTED;
  }
  return ret;
}

inline int execute_plugin_geometry_transform(const char *service_name,
                                             const ObExpr &expr,
                                             ObEvalCtx &ctx,
                                             ObDatum &result)
{
  int ret = OB_SUCCESS;
  if (expr.arg_cnt_ != 2 || nullptr == share::g_mp) return OB_NOT_SUPPORTED;
  ObDatum *geometry = nullptr;
  ObDatum *srid = nullptr;
  if (OB_FAIL(expr.args_[0]->eval(ctx, geometry)) || OB_FAIL(expr.args_[1]->eval(ctx, srid))) {
    return OB_NOT_SUPPORTED;
  }
  if (geometry->is_null() || srid->is_null()) {
    result.set_null();
    return OB_SUCCESS;
  }
  const uint32_t target_srid = static_cast<uint32_t>(srid->get_int());
  const ObString wkb = geometry->get_string();
  seekdb_plugin_execution_value_v1_t arguments[2] = {};
  arguments[0].struct_size = sizeof(arguments[0]);
  arguments[0].type_id = "org.seekdb.gis.geometry";
  arguments[0].data = reinterpret_cast<const uint8_t *>(wkb.ptr());
  arguments[0].data_size = static_cast<uint64_t>(wkb.length());
  arguments[1].struct_size = sizeof(arguments[1]);
  arguments[1].type_id = "org.seekdb.gis.scalar.uint32";
  arguments[1].data = reinterpret_cast<const uint8_t *>(&target_srid);
  arguments[1].data_size = sizeof(target_srid);
  PluginBytesResultSink sink{&expr, &ctx, &result};
  seekdb_plugin_execution_context_v1_t plugin_ctx = {};
  plugin_ctx.struct_size = sizeof(plugin_ctx);
  plugin_ctx.host = reinterpret_cast<seekdb_plugin_host_handle_t *>(&sink);
  plugin_ctx.emit_result = emit_plugin_bytes_result;
  const int plugin_ret = share::g_mp->execute_plugin_extension(
      SEEKDB_PLUGIN_EXTENSION_FUNCTION, service_name, &plugin_ctx, arguments, 2);
  return OB_SUCCESS == plugin_ret ? OB_SUCCESS : OB_NOT_SUPPORTED;
}

struct PluginInt32ResultSink
{
  ObDatum *result_;
};

struct PluginUint64ResultSink
{
  ObDatum *result_;
};

inline seekdb_plugin_status_t SEEKDB_PLUGIN_CALL emit_plugin_uint64_result(
    seekdb_plugin_host_handle_t *host,
    const seekdb_plugin_execution_result_v1_t *plugin_result)
{
  if (nullptr == host || nullptr == plugin_result ||
      plugin_result->struct_size != sizeof(*plugin_result) || plugin_result->is_null != 0 ||
      nullptr == plugin_result->data || plugin_result->data_size != sizeof(uint64_t) ||
      nullptr == plugin_result->type_id ||
      0 != std::strcmp(plugin_result->type_id, "org.seekdb.gis.scalar.uint64")) {
    return SEEKDB_PLUGIN_STATUS_INVALID_ARGUMENT;
  }
  PluginUint64ResultSink *sink = reinterpret_cast<PluginUint64ResultSink *>(host);
  if (nullptr == sink->result_) return SEEKDB_PLUGIN_STATUS_FAILED_PRECONDITION;
  uint64_t value = 0;
  std::memcpy(&value, plugin_result->data, sizeof(value));
  sink->result_->set_uint(value);
  return SEEKDB_PLUGIN_STATUS_OK;
}

inline int execute_plugin_geometry_uint64(const char *service_name,
                                          const ObExpr &expr,
                                          ObEvalCtx &ctx,
                                          ObDatum &result)
{
  int ret = OB_SUCCESS;
  if (expr.arg_cnt_ != 1 || nullptr == share::g_mp) return OB_NOT_SUPPORTED;
  ObDatum *datum = nullptr;
  if (OB_FAIL(expr.args_[0]->eval(ctx, datum))) return OB_NOT_SUPPORTED;
  if (datum->is_null()) { result.set_null(); return OB_SUCCESS; }
  const ObString geometry = datum->get_string();
  seekdb_plugin_execution_value_v1_t argument = {};
  argument.struct_size = sizeof(argument);
  argument.type_id = "org.seekdb.gis.geometry";
  argument.data = reinterpret_cast<const uint8_t *>(geometry.ptr());
  argument.data_size = static_cast<uint64_t>(geometry.length());
  PluginUint64ResultSink sink{&result};
  seekdb_plugin_execution_context_v1_t plugin_ctx = {};
  plugin_ctx.struct_size = sizeof(plugin_ctx);
  plugin_ctx.host = reinterpret_cast<seekdb_plugin_host_handle_t *>(&sink);
  plugin_ctx.emit_result = emit_plugin_uint64_result;
  return OB_SUCCESS == share::g_mp->execute_plugin_extension(
      SEEKDB_PLUGIN_EXTENSION_FUNCTION, service_name, &plugin_ctx, &argument, 1)
      ? OB_SUCCESS : OB_NOT_SUPPORTED;
}

inline seekdb_plugin_status_t SEEKDB_PLUGIN_CALL emit_plugin_int32_result(
    seekdb_plugin_host_handle_t *host,
    const seekdb_plugin_execution_result_v1_t *plugin_result)
{
  if (nullptr == host || nullptr == plugin_result ||
      plugin_result->struct_size != sizeof(*plugin_result) ||
      nullptr == plugin_result->type_id ||
      0 != std::strcmp(plugin_result->type_id, "org.seekdb.gis.scalar.int32")) {
    return SEEKDB_PLUGIN_STATUS_INVALID_ARGUMENT;
  }
  PluginInt32ResultSink *sink = reinterpret_cast<PluginInt32ResultSink *>(host);
  if (nullptr == sink->result_) return SEEKDB_PLUGIN_STATUS_FAILED_PRECONDITION;
  if (plugin_result->is_null != 0) {
    sink->result_->set_null();
  } else if (nullptr == plugin_result->data || plugin_result->data_size != sizeof(int32_t)) {
    return SEEKDB_PLUGIN_STATUS_INVALID_ARGUMENT;
  } else {
    int32_t value = 0;
    std::memcpy(&value, plugin_result->data, sizeof(value));
    sink->result_->set_int32(value);
  }
  return SEEKDB_PLUGIN_STATUS_OK;
}

inline int execute_plugin_geometry_scalar(const char *service_name,
                                          const ObExpr &expr,
                                          ObEvalCtx &ctx,
                                          ObDatum &result,
                                          seekdb_plugin_emit_result_v1_fn emit_result,
                                          seekdb_plugin_host_handle_t *sink)
{
  int ret = OB_SUCCESS;
  if (expr.arg_cnt_ != 1 || nullptr == share::g_mp) {
    ret = OB_NOT_SUPPORTED;
  } else {
    ObDatum *datum = nullptr;
    if (OB_FAIL(expr.args_[0]->eval(ctx, datum))) {
    } else if (datum->is_null()) {
      result.set_null();
    } else {
      const ObString geometry = datum->get_string();
      seekdb_plugin_execution_value_v1_t argument = {};
      argument.struct_size = sizeof(argument);
      argument.type_id = "org.seekdb.gis.geometry";
      argument.data = reinterpret_cast<const uint8_t *>(geometry.ptr());
      argument.data_size = static_cast<uint64_t>(geometry.length());
      seekdb_plugin_execution_context_v1_t plugin_ctx = {};
      plugin_ctx.struct_size = sizeof(plugin_ctx);
      plugin_ctx.host = sink;
      plugin_ctx.emit_result = emit_result;
      const int plugin_ret = share::g_mp->execute_plugin_extension(
          SEEKDB_PLUGIN_EXTENSION_FUNCTION, service_name, &plugin_ctx, &argument, 1);
      if (OB_SUCCESS != plugin_ret) ret = OB_NOT_SUPPORTED;
    }
  }
  return ret;
}

inline int execute_plugin_geometry_int32(const char *service_name,
                                         const ObExpr &expr,
                                         ObEvalCtx &ctx,
                                         ObDatum &result)
{
  int ret = OB_SUCCESS;
  const uint32_t count = static_cast<uint32_t>(expr.arg_cnt_);
  if ((count != 1 && count != 2) || nullptr == share::g_mp) return OB_NOT_SUPPORTED;
  ObDatum *datums[2] = {nullptr, nullptr};
  seekdb_plugin_execution_value_v1_t arguments[2] = {};
  for (uint32_t i = 0; OB_SUCC(ret) && i < count; ++i) {
    if (OB_FAIL(expr.args_[i]->eval(ctx, datums[i]))) {
    } else if (datums[i]->is_null()) {
      result.set_null();
      return OB_SUCCESS;
    } else {
      const ObString geometry = datums[i]->get_string();
      arguments[i].struct_size = sizeof(arguments[i]);
      arguments[i].type_id = "org.seekdb.gis.geometry";
      arguments[i].data = reinterpret_cast<const uint8_t *>(geometry.ptr());
      arguments[i].data_size = static_cast<uint64_t>(geometry.length());
    }
  }
  if (OB_SUCC(ret)) {
    PluginInt32ResultSink sink{&result};
    seekdb_plugin_execution_context_v1_t plugin_ctx = {};
    plugin_ctx.struct_size = sizeof(plugin_ctx);
    plugin_ctx.host = reinterpret_cast<seekdb_plugin_host_handle_t *>(&sink);
    plugin_ctx.emit_result = emit_plugin_int32_result;
    if (OB_SUCCESS != share::g_mp->execute_plugin_extension(
        SEEKDB_PLUGIN_EXTENSION_FUNCTION, service_name, &plugin_ctx, arguments, count)) {
      ret = OB_NOT_SUPPORTED;
    }
  }
  return ret;
}

struct PluginDoubleResultSink
{
  ObDatum *result_;
};

inline seekdb_plugin_status_t SEEKDB_PLUGIN_CALL emit_plugin_double_result(
    seekdb_plugin_host_handle_t *host,
    const seekdb_plugin_execution_result_v1_t *plugin_result)
{
  if (nullptr == host || nullptr == plugin_result ||
      plugin_result->struct_size != sizeof(*plugin_result) ||
      plugin_result->is_null != 0 || nullptr == plugin_result->data ||
      plugin_result->data_size != sizeof(double) || nullptr == plugin_result->type_id ||
      0 != std::strcmp(plugin_result->type_id, "org.seekdb.gis.scalar.float64")) {
    return SEEKDB_PLUGIN_STATUS_INVALID_ARGUMENT;
  }
  PluginDoubleResultSink *sink = reinterpret_cast<PluginDoubleResultSink *>(host);
  if (nullptr == sink->result_) return SEEKDB_PLUGIN_STATUS_FAILED_PRECONDITION;
  double value = 0.0;
  std::memcpy(&value, plugin_result->data, sizeof(value));
  sink->result_->set_double(value);
  return SEEKDB_PLUGIN_STATUS_OK;
}

inline int execute_plugin_geometry_double(const char *service_name,
                                          const ObExpr &expr,
                                          ObEvalCtx &ctx,
                                          ObDatum &result)
{
  int ret = OB_SUCCESS;
  const uint32_t argument_count = static_cast<uint32_t>(expr.arg_cnt_);
  if (nullptr == share::g_mp || (argument_count != 2 && argument_count != 3)) {
    ret = OB_NOT_SUPPORTED;
  } else {
    ObDatum *datums[3] = {nullptr, nullptr, nullptr};
    for (uint32_t i = 0; OB_SUCC(ret) && i < argument_count; ++i) {
      if (OB_FAIL(expr.args_[i]->eval(ctx, datums[i]))) {
      } else if (datums[i]->is_null()) {
        result.set_null();
        return OB_SUCCESS;
      }
    }
    if (OB_SUCC(ret)) {
      seekdb_plugin_execution_value_v1_t arguments[3] = {};
      for (uint32_t i = 0; i < 2; ++i) {
        const ObString geometry = datums[i]->get_string();
        arguments[i].struct_size = sizeof(arguments[i]);
        arguments[i].type_id = "org.seekdb.gis.geometry";
        arguments[i].data = reinterpret_cast<const uint8_t *>(geometry.ptr());
        arguments[i].data_size = static_cast<uint64_t>(geometry.length());
      }
      if (argument_count == 3) {
        const double radius = datums[2]->get_double();
        arguments[2].struct_size = sizeof(arguments[2]);
        arguments[2].type_id = "org.seekdb.gis.scalar.float64";
        arguments[2].data = reinterpret_cast<const uint8_t *>(&radius);
        arguments[2].data_size = sizeof(radius);
      }
      PluginDoubleResultSink sink{&result};
      seekdb_plugin_execution_context_v1_t plugin_ctx = {};
      plugin_ctx.struct_size = sizeof(plugin_ctx);
      plugin_ctx.host = reinterpret_cast<seekdb_plugin_host_handle_t *>(&sink);
      plugin_ctx.emit_result = emit_plugin_double_result;
      const int plugin_ret = share::g_mp->execute_plugin_extension(
          SEEKDB_PLUGIN_EXTENSION_FUNCTION, service_name, &plugin_ctx, arguments, argument_count);
      if (OB_SUCCESS != plugin_ret) ret = OB_NOT_SUPPORTED;
    }
  }
  return ret;
}

} // namespace sql
} // namespace oceanbase

#endif // OCEANBASE_SQL_OB_PLUGIN_EXPR_UTILS_H_
