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
#include "sql/engine/expr/ob_expr_st_geomfromtext.h"
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
struct GeomFromTextPluginSink
{
  const ObExpr *expr_;
  ObEvalCtx *ctx_;
  ObDatum *result_;
};

seekdb_plugin_status_t SEEKDB_PLUGIN_CALL emit_geomfromtext_plugin_result(
    seekdb_plugin_host_handle_t *host,
    const seekdb_plugin_execution_result_v1_t *result)
{
  if (nullptr == host || nullptr == result || result->struct_size != sizeof(*result) ||
      result->is_null != 0 || result->data_size == 0 || nullptr == result->data ||
      nullptr == result->type_id ||
      0 != std::strcmp(result->type_id, "org.seekdb.gis.geometry")) {
    return SEEKDB_PLUGIN_STATUS_INVALID_ARGUMENT;
  }
  GeomFromTextPluginSink *sink = reinterpret_cast<GeomFromTextPluginSink *>(host);
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
ObExprSTGeomFromText::ObExprSTGeomFromText(ObIAllocator &alloc)
    : ObFuncExprOperator(alloc, T_FUN_SYS_ST_GEOMFROMTEXT, N_ST_GEOMFROMTEXT, MORE_THAN_ZERO, VALID_FOR_GENERATED_COL, NOT_ROW_DIMENSION)
{
}

ObExprSTGeomFromText::ObExprSTGeomFromText(ObIAllocator &alloc,
                                           ObExprOperatorType type,
                                           const char *name,
                                           int32_t param_num, 
                                           int32_t dimension) : ObFuncExprOperator(alloc, type, name, param_num, VALID_FOR_GENERATED_COL, dimension) 
{
}

ObExprSTGeomFromText::~ObExprSTGeomFromText()
{
}

int ObExprSTGeomFromText::calc_result_typeN(ObExprResType& type,
                                            ObExprResType* types_stack,
                                            int64_t param_num,
                                            ObExprTypeCtx& type_ctx) const
{
  UNUSED(type_ctx); 
  UNUSED(types_stack);
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(param_num > 3)) {
    ObString func_name_(get_name());
    ret = OB_ERR_PARAM_SIZE;
    LOG_USER_ERROR(OB_ERR_PARAM_SIZE, func_name_.length(), func_name_.ptr());
  } else {
    for (uint8_t i = 0; i < param_num && OB_SUCC(ret); i++) {
      if (i == 0 || i == 2) {
        if (ob_is_null(types_stack[i].get_type())) {
        } else if (!ob_is_string_type(types_stack[i].get_type())
                   || ObCharset::is_cs_nonascii(types_stack[i].get_collation_type())) {
          types_stack[i].set_calc_type(common::ObVarcharType);
          types_stack[i].set_calc_collation_type(CS_TYPE_BINARY);
        }
      }
      // srid
      if (i == 1) {
        types_stack[i].set_calc_type(ObIntType);
        type_ctx.set_cast_mode(type_ctx.get_cast_mode() | CM_STRING_INTEGER_TRUNC);
      }
    }
    type.set_geometry();
    type.set_length((ObAccuracy::DDL_DEFAULT_ACCURACY[ObGeometryType]).get_length());
  }
  
  return ret;
}

int ObExprSTGeomFromText::eval_st_geomfromtext(const ObExpr &expr,
                                               ObEvalCtx &ctx,
                                               ObDatum &res)
{
  return eval_st_geomfromtext_common(expr, ctx, res, N_ST_GEOMFROMTEXT);
}

int ObExprSTGeomFromText::eval_st_geomfromtext_common(const ObExpr &expr,
                                                      ObEvalCtx &ctx,
                                                      ObDatum &res,
                                                      const char *func_name)
{
#if !SEEKDB_ENABLE_CORE_GIS
  if (expr.arg_cnt_ < 1 || expr.arg_cnt_ > 2) return OB_NOT_SUPPORTED;
  int ret = OB_SUCCESS;
  ObDatum *datum = nullptr;
  if (OB_FAIL(expr.args_[0]->eval(ctx, datum))) {
  } else if (datum->is_null()) {
    res.set_null();
  } else if (nullptr == share::g_mp) {
    ret = OB_NOT_SUPPORTED;
  } else {
    ObString wkt = datum->get_string();
    seekdb_plugin_execution_value_v1_t arguments[2] = {};
    arguments[0].struct_size = sizeof(arguments[0]);
    arguments[0].type_id = "org.seekdb.gis.scalar.bytes";
    arguments[0].data = reinterpret_cast<const uint8_t *>(wkt.ptr());
    arguments[0].data_size = static_cast<uint64_t>(wkt.length());
    uint32_t srid = 0;
    uint32_t argument_count = 1;
    if (expr.arg_cnt_ == 2) {
      if (OB_FAIL(expr.args_[1]->eval(ctx, datum))) {
      } else if (datum->is_null()) {
        res.set_null();
      } else {
        srid = datum->get_uint32();
        arguments[1].struct_size = sizeof(arguments[1]);
        arguments[1].type_id = "org.seekdb.gis.scalar.uint32";
        arguments[1].data = reinterpret_cast<const uint8_t *>(&srid);
        arguments[1].data_size = sizeof(srid);
        argument_count = 2;
      }
    }
    if (OB_SUCC(ret) && !res.is_null()) {
      GeomFromTextPluginSink sink{&expr, &ctx, &res};
      seekdb_plugin_execution_context_v1_t plugin_ctx = {};
      plugin_ctx.struct_size = sizeof(plugin_ctx);
      plugin_ctx.host = reinterpret_cast<seekdb_plugin_host_handle_t *>(&sink);
      plugin_ctx.emit_result = emit_geomfromtext_plugin_result;
      const int plugin_ret = share::g_mp->execute_plugin_extension(
          SEEKDB_PLUGIN_EXTENSION_FUNCTION, func_name,
          &plugin_ctx, arguments, argument_count);
      if (OB_SUCCESS != plugin_ret) ret = OB_NOT_SUPPORTED;
    }
  }
  return ret;
#else
  int ret = OB_SUCCESS;
  ObEvalCtx::TempAllocGuard tmp_alloc_g(ctx);
  
  MultimodeAlloctor tmp_allocator(tmp_alloc_g.get_allocator());
  ObDatum *datum = NULL;
  int num_args = expr.arg_cnt_;
  bool is_null_result = false;
  uint32_t srid = 0;
  ObGeoAxisOrder axis_order = ObGeoAxisOrder::INVALID;
  ObString wkt;
  const ObSrsItem *srs_item = NULL;
  ObSQLSessionInfo *session = ctx.exec_ctx_.get_my_session();
  omt::ObSrsCacheGuard srs_guard;
  ObGeometry *geo = NULL;
  bool is_lat_long = false;
  bool is_geog = false;
  bool need_reverse = false;
  bool is_3d_geo = false;

  // get wkt
  if (OB_FAIL(tmp_allocator.eval_arg(expr.args_[0], ctx, datum))) {
    LOG_WARN("failed to eval first argument", K(ret));
  } else if (datum->is_null()) {
    is_null_result = true;
  } else {
    wkt = datum->get_string();
    if (OB_FAIL(ObTextStringHelper::read_real_string_data_with_copy(tmp_allocator, *datum,
        expr.args_[0]->datum_meta_, expr.args_[0]->obj_meta_.has_lob_header(), wkt))) {
      LOG_WARN("fail to get real string data", K(ret), K(wkt));
    } 
  }
  // get srid
  if (!is_null_result && OB_SUCC(ret) && num_args > 1) {
    if (OB_FAIL(tmp_allocator.eval_arg(expr.args_[1], ctx, datum))) {
      LOG_WARN("failed to eval second argument", K(ret));
    } else if (datum->is_null()) {
      is_null_result = true;
    } else if (datum->get_int() < 0 || datum->get_int() > UINT_MAX32) {
      ret = OB_OPERATE_OVERFLOW;
      LOG_USER_ERROR(OB_OPERATE_OVERFLOW, "SRID", func_name);
      LOG_WARN("srid input value out of range", K(ret), K(datum->get_int()));
    } else if (0 != (srid = datum->get_uint32())) {
      if (OB_FAIL(SRS_SERVICE->get_srs_guard(srs_guard))) {
        LOG_WARN("failed to get srs guard", K(ret));
      } else if (OB_FAIL(srs_guard.get_srs_item(srid, srs_item))) {
        LOG_WARN("failed to get srs item", K(ret));
      } else if (OB_ISNULL(srs_item)) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("unexpected null srs item", K(ret));
      } else {
        is_geog = srs_item->is_geographical_srs();
        is_lat_long = srs_item->is_lat_long_order();
        need_reverse = is_geog && is_lat_long;
      }
    }
  }
  // get axis_order
  if (!is_null_result && OB_SUCC(ret) && num_args > 2 ) {
    ObString axis_str;
    if (OB_FAIL(tmp_allocator.eval_arg(expr.args_[2], ctx, datum))) {
      LOG_WARN("failed to eval third argument", K(ret));
    } else if (datum->is_null()){
      is_null_result = true;
    } else if (FALSE_IT(axis_str = datum->get_string())) {
    } else if (OB_FAIL(ObTextStringHelper::read_real_string_data_with_copy(tmp_allocator, *datum,
              expr.args_[2]->datum_meta_, expr.args_[2]->obj_meta_.has_lob_header(), axis_str))) {
      LOG_WARN("fail to get real string data", K(ret), K(axis_str));
    } else if (OB_FAIL(ObGeoExprUtils::parse_axis_order(axis_str, func_name, axis_order))) {
      LOG_WARN("failed to parse axis order option string", K(ret));
    } else {
      switch (axis_order) {
        case ObGeoAxisOrder::LONG_LAT: {
          need_reverse = false;
          break;
        }
        case ObGeoAxisOrder::LAT_LONG: {
          need_reverse = true;
          break;
        }
        case ObGeoAxisOrder::SRID_DEFINED: {
          break;
        }
        default: {
          ret = OB_ERR_UNEXPECTED;
          LOG_WARN("unexpected axis order parse result", K(ret));
          break;
        }
      }
    }
  }
    
  if (!is_null_result && OB_SUCC(ret)) {
    if (OB_FAIL(ObWktParser::parse_wkt(tmp_allocator, wkt, geo, true, is_geog))) {
      ret = OB_ERR_GIS_INVALID_DATA;
      LOG_USER_ERROR(OB_ERR_GIS_INVALID_DATA, func_name);
      LOG_WARN("failed to parse wkt", K(ret));
    } else if (OB_ISNULL(geo)) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("unexpected null geo after parse_wkt", K(ret), K(wkt));
    } else {
      is_3d_geo = ObGeoTypeUtil::is_3d_geo_type(geo->type());
      if (is_geog && need_reverse && OB_FAIL(ObGeoExprUtils::reverse_coordinate(geo, func_name))) {
        LOG_WARN("failed to reverse geometry coordinate", K(ret));
      }
      if (is_geog && OB_SUCC(ret)) {
        if (OB_FAIL(ObGeoExprUtils::check_coordinate_range(srs_item, geo, func_name))) {
          LOG_WARN("check geo coordinate range failed", K(ret));
        }
      }
    }
  }

  if (OB_FAIL(ret)) {
  } else if (is_null_result) {
    res.set_null();
  } else if (OB_ISNULL(geo)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("unexpected null geometry", K(ret));
  } else {
    ObString res_wkb;
    if (OB_FAIL(ObGeoExprUtils::geo_to_wkb(*geo, expr, ctx, srs_item, res_wkb))) {
      LOG_WARN("failed to write geometry to wkb", K(ret));
    } else {
      res.set_string(res_wkb);
    }
  }

  return ret;
#endif
}

int ObExprSTGeomFromText::cg_expr(ObExprCGCtx &expr_cg_ctx,
                                  const ObRawExpr &raw_expr,
                                  ObExpr &rt_expr) const
{
  UNUSED(expr_cg_ctx);
  UNUSED(raw_expr);
  rt_expr.eval_func_ = eval_st_geomfromtext;
  return OB_SUCCESS;
}

} // namespace sql
} // namespace oceanbase
