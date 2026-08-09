/*
 * Copyright (c) 2025 OceanBase.
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
#include "sql/engine/expr/ob_expr_st_srid.h"
#include <cstring>
#if SEEKDB_ENABLE_CORE_GIS
#include "sql/engine/expr/ob_geo_expr_utils.h"
#else
#include "sql/engine/expr/ob_plugin_expr_utils.h"
#include "seekdb/plugin/execution_spi.h"
#include "seekdb/plugin/extension_spi.h"
#include "share/rc/ob_module_provider.h"
#endif

using namespace oceanbase::common;
using namespace oceanbase::sql;
using namespace oceanbase::omt;

namespace oceanbase
{
namespace sql
{

#if !SEEKDB_ENABLE_CORE_GIS
namespace
{
struct SridPluginSink
{
  ObDatum *result_;
};

struct SetSridPluginSink
{
  const ObExpr *expr_;
  ObEvalCtx *ctx_;
  ObDatum *result_;
};

seekdb_plugin_status_t SEEKDB_PLUGIN_CALL emit_srid_plugin_result(
    seekdb_plugin_host_handle_t *host,
    const seekdb_plugin_execution_result_v1_t *result)
{
  if (nullptr == host || nullptr == result || result->struct_size != sizeof(*result) ||
      result->is_null != 0 || result->data_size != sizeof(uint32_t) ||
      nullptr == result->data || nullptr == result->type_id ||
      0 != std::strcmp(result->type_id, "org.seekdb.gis.scalar.uint32")) {
    return SEEKDB_PLUGIN_STATUS_INVALID_ARGUMENT;
  }
  SridPluginSink *sink = reinterpret_cast<SridPluginSink *>(host);
  if (nullptr == sink->result_) {
    return SEEKDB_PLUGIN_STATUS_FAILED_PRECONDITION;
  }
  uint32_t srid = 0;
  std::memcpy(&srid, result->data, sizeof(srid));
  sink->result_->set_int32(static_cast<int32_t>(srid));
  return SEEKDB_PLUGIN_STATUS_OK;
}

seekdb_plugin_status_t SEEKDB_PLUGIN_CALL emit_set_srid_plugin_result(
    seekdb_plugin_host_handle_t *host,
    const seekdb_plugin_execution_result_v1_t *result)
{
  if (nullptr == host || nullptr == result || result->struct_size != sizeof(*result) ||
      result->is_null != 0 || result->data_size == 0 || nullptr == result->data ||
      nullptr == result->type_id ||
      0 != std::strcmp(result->type_id, "org.seekdb.gis.geometry")) {
    return SEEKDB_PLUGIN_STATUS_INVALID_ARGUMENT;
  }
  SetSridPluginSink *sink = reinterpret_cast<SetSridPluginSink *>(host);
  if (nullptr == sink->expr_ || nullptr == sink->ctx_ || nullptr == sink->result_) {
    return SEEKDB_PLUGIN_STATUS_FAILED_PRECONDITION;
  }
  const int ret = pack_plugin_expr_result(
      *sink->expr_, *sink->ctx_, *sink->result_,
      reinterpret_cast<const char *>(result->data),
      static_cast<int64_t>(result->data_size));
  return ret == OB_SUCCESS ? SEEKDB_PLUGIN_STATUS_OK : SEEKDB_PLUGIN_STATUS_INTERNAL;
}
}
#endif
ObExprSTSRID::ObExprSTSRID(ObIAllocator &alloc)
    : ObFuncExprOperator(alloc, T_FUN_SYS_ST_SRID, N_ST_SRID, ONE_OR_TWO, VALID_FOR_GENERATED_COL, NOT_ROW_DIMENSION)
{
}

ObExprSTSRID::ObExprSTSRID(ObIAllocator &alloc,
                           ObExprOperatorType type,
                           const char *name,
                           int32_t param_num, 
                           int32_t dimension) : ObFuncExprOperator(alloc, type, name, param_num, VALID_FOR_GENERATED_COL, dimension) 
{
}

ObExprSTSRID::~ObExprSTSRID()
{
}

int ObExprSTSRID::calc_result_typeN(ObExprResType& type,
                                    ObExprResType* types_stack,
                                    int64_t param_num,
                                    ObExprTypeCtx& type_ctx) const
{
  UNUSED(type_ctx);
  int ret = OB_SUCCESS;

  if (OB_UNLIKELY(param_num > 2)) {
    ObString func_name_(get_name());
    ret = OB_ERR_PARAM_SIZE;
    LOG_USER_ERROR(OB_ERR_PARAM_SIZE, func_name_.length(), func_name_.ptr());
  } else {
    if (ob_is_null(types_stack[0].get_type())) {
    } else if (ob_is_numeric_type(types_stack[0].get_type())) {
      types_stack[0].set_calc_type(ObLongTextType);
    } else if (!ob_is_geometry(types_stack[0].get_type()) && !ob_is_string_type(types_stack[0].get_type())) {
      ret = OB_ERR_GIS_INVALID_DATA;
      LOG_USER_ERROR(OB_ERR_GIS_INVALID_DATA, get_name());
    }
    if (OB_SUCC(ret)) {
      type.set_int32();
      if (param_num > 1) {
        types_stack[1].set_calc_type(ObIntType);
        type_ctx.set_cast_mode(type_ctx.get_cast_mode() | CM_STRING_INTEGER_TRUNC);
        type.set_geometry();
        type.set_length((ObAccuracy::DDL_DEFAULT_ACCURACY[ObGeometryType]).get_length());
      }
    }
  }
  
  return ret;
}

int ObExprSTSRID::eval_st_srid(const ObExpr &expr, ObEvalCtx &ctx, ObDatum &res)
{
  return eval_st_srid_common(expr, ctx, res, N_ST_SRID);
}

int ObExprSTSRID::eval_st_srid_common(const ObExpr &expr, ObEvalCtx &ctx, ObDatum &res, const char *func_name)
{
#if !SEEKDB_ENABLE_CORE_GIS
  UNUSED(func_name);
  int ret = OB_SUCCESS;
  if (expr.arg_cnt_ != 1 && expr.arg_cnt_ != 2) {
    return OB_NOT_SUPPORTED;
  }
  ObDatum *datum = nullptr;
  if (OB_FAIL(expr.args_[0]->eval(ctx, datum))) {
    return ret;
  } else if (datum->is_null()) {
    res.set_null();
    return OB_SUCCESS;
  } else if (nullptr == share::g_mp) {
    return OB_NOT_SUPPORTED;
  }
  ObString wkb = datum->get_string();
  const char *service_id = "st_srid";
  seekdb_plugin_execution_value_v1_t arguments[2] = {};
  uint32_t srid = 0;
  uint32_t argument_count = 1;
  if (expr.arg_cnt_ == 2) {
    if (OB_FAIL(expr.args_[1]->eval(ctx, datum))) {
      return ret;
    } else if (datum->is_null()) {
      res.set_null();
      return OB_SUCCESS;
    }
    srid = datum->get_uint32();
    service_id = "_st_setsrid";
    arguments[1].struct_size = sizeof(arguments[1]);
    arguments[1].type_id = "org.seekdb.gis.scalar.uint32";
    arguments[1].data = reinterpret_cast<const uint8_t *>(&srid);
    arguments[1].data_size = sizeof(srid);
    argument_count = 2;
  }
  arguments[0].struct_size = sizeof(arguments[0]);
  arguments[0].type_id = "org.seekdb.gis.geometry";
  arguments[0].data = reinterpret_cast<const uint8_t *>(wkb.ptr());
  arguments[0].data_size = static_cast<uint64_t>(wkb.length());
  SetSridPluginSink set_sink{&expr, &ctx, &res};
  SridPluginSink get_sink{&res};
  seekdb_plugin_host_handle_t *host = nullptr;
  seekdb_plugin_execution_context_v1_t plugin_ctx = {};
  plugin_ctx.struct_size = sizeof(plugin_ctx);
  if (expr.arg_cnt_ == 1) {
    host = reinterpret_cast<seekdb_plugin_host_handle_t *>(&get_sink);
    plugin_ctx.emit_result = emit_srid_plugin_result;
  } else {
    host = reinterpret_cast<seekdb_plugin_host_handle_t *>(&set_sink);
    plugin_ctx.emit_result = emit_set_srid_plugin_result;
  }
  plugin_ctx.host = host;
  const int plugin_ret = share::g_mp->execute_plugin_extension(
      SEEKDB_PLUGIN_EXTENSION_FUNCTION, service_id, &plugin_ctx, arguments, argument_count);
  return OB_SUCCESS == plugin_ret ? OB_SUCCESS : OB_NOT_SUPPORTED;
#else
  int ret = OB_SUCCESS;
  ObEvalCtx::TempAllocGuard tmp_alloc_g(ctx);
  
  MultimodeAlloctor tmp_allocator(tmp_alloc_g.get_allocator());
  ObDatum *datum = NULL;
  int num_args = expr.arg_cnt_;
  bool is_null_result = false;
  ObGeoSrid srid = 0;
  ObString wkb;
  ObString res_wkb;
  const ObSrsItem *srs = NULL;
  ObSQLSessionInfo *session = ctx.exec_ctx_.get_my_session();
  common::ObSrsCacheGuard srs_guard;
  ObGeometry *geo = NULL;
  bool is_geog = false;

  // get srid
  if (num_args > 1) {
    if (expr.args_[1]->is_boolean_ && T_FUN_SYS_PRIV_ST_SETSRID == expr.type_) {
      ret = OB_ERR_INVALID_TYPE_FOR_OP;   
      LOG_WARN("invalid type", K(ret));
    } else if (OB_FAIL(tmp_allocator.eval_arg(expr.args_[1], ctx, datum))) {
    } else if (datum->is_null()) {
      is_null_result = true;
    } else if (datum->get_int() < 0 || datum->get_int() > UINT_MAX32) {
      ret = OB_OPERATE_OVERFLOW;
      LOG_USER_ERROR(OB_OPERATE_OVERFLOW, "SRID", func_name);
      LOG_WARN("srid input value out of range", K(ret), K(datum->get_int()));
    } else if (0 != (srid = datum->get_uint32())) {
      if (OB_FAIL(ObGeoExprUtils::get_srs_item(
              ctx, srs_guard, srid, srs))) {
      } else if (OB_ISNULL(srs)) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("unexpected null srs item", K(ret));
      }
    }
  }

  // get geometry
  if (OB_SUCC(ret)) {
    if (OB_FAIL(tmp_allocator.eval_arg(expr.args_[0], ctx, datum))) {
    } else if (datum->is_null()) {
      is_null_result = true;
    } else if (!is_null_result) { // srid might be null, fix 42538503
      wkb = datum->get_string();
      if (OB_FAIL(ObTextStringHelper::read_real_string_data_with_copy(ctx.exec_ctx_, tmp_allocator, *datum,
                expr.args_[0]->datum_meta_, expr.args_[0]->obj_meta_.has_lob_header(), wkb))) {
      } else if (num_args == 1) {
        if (OB_FAIL(ObGeoExprUtils::get_srs_item(ctx, srs_guard, wkb, srs, true, func_name))) {
          LOG_WARN("fail to get srs item", K(ret), K(wkb));
          if (OB_ERR_SRS_NOT_FOUND == ret) {
            ret = OB_SUCCESS; // adapt mysql, treat unknown srid as cartesian
          } 
        }
        if (OB_FAIL(ret)) {
          // do nothing
        } else if (OB_FAIL(ObGeoExprUtils::build_geometry(tmp_allocator, wkb, geo, srs, func_name, ObGeoBuildFlag::GEO_CHECK_RANGE | GEO_NOT_COPY_WKB))) {
        } else if (OB_FAIL(ObGeoTypeUtil::get_srid_from_wkb(wkb, srid))) {
        } else if (ObGeoTypeUtil::need_get_srs(srid) && srs == NULL) {
          LOG_USER_WARN(OB_ERR_WARN_SRS_NOT_FOUND, srid);
          LOG_WARN("srs not found");
        }
      } else {
        if (OB_FAIL(ObGeoExprUtils::build_geometry(tmp_allocator, wkb, geo, srs, func_name, ObGeoBuildFlag::GEO_CHECK_RANGE | GEO_NOT_COPY_WKB))) {
        } else if (OB_FAIL(ObGeoExprUtils::geo_to_wkb(*geo, expr, ctx, srs, res_wkb))) {
        }
      } 
    }
  }

  if (OB_FAIL(ret)) {
  } else if (is_null_result) {
    res.set_null();
  } else if (num_args == 1) {
    res.set_int32(srid);
  } else if (OB_ISNULL(geo)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("unexpected null geometry", K(ret));
  } else {
    res.set_string(res_wkb);
  }

  return ret;
#endif
}

int ObExprSTSRID::cg_expr(ObExprCGCtx &expr_cg_ctx,
                          const ObRawExpr &raw_expr,
                          ObExpr &rt_expr) const
{
  UNUSEDx(expr_cg_ctx, raw_expr);
  rt_expr.eval_func_ = eval_st_srid;
  return OB_SUCCESS;
}

} // namespace sql
} // namespace oceanbase
