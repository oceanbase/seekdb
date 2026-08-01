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
#include "sql/engine/expr/ob_expr_st_astext.h"
#include <cstring>
#if SEEKDB_ENABLE_CORE_GIS
#include "sql/engine/expr/ob_geo_expr_utils.h"
#include "share/geo/ob_geo_to_wkt_visitor.h"
#include "share/geo/ob_geo_3d.h"
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
struct AsTextPluginSink
{
  const ObExpr *expr_;
  ObEvalCtx *ctx_;
  ObDatum *result_;
};

seekdb_plugin_status_t SEEKDB_PLUGIN_CALL emit_astext_plugin_result(
    seekdb_plugin_host_handle_t *host,
    const seekdb_plugin_execution_result_v1_t *result)
{
  if (nullptr == host || nullptr == result || result->struct_size != sizeof(*result) ||
      result->is_null != 0 || result->data_size == 0 || nullptr == result->data ||
      nullptr == result->type_id ||
      0 != std::strcmp(result->type_id, "org.seekdb.gis.scalar.bytes")) {
    return SEEKDB_PLUGIN_STATUS_INVALID_ARGUMENT;
  }
  AsTextPluginSink *sink = reinterpret_cast<AsTextPluginSink *>(host);
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
ObExprSTAsText::ObExprSTAsText(ObIAllocator &alloc)
    : ObFuncExprOperator(alloc, T_FUN_SYS_ST_ASTEXT, N_ST_ASTEXT, MORE_THAN_ZERO, VALID_FOR_GENERATED_COL, NOT_ROW_DIMENSION)
{
}

ObExprSTAsText::ObExprSTAsText(ObIAllocator &alloc,
                               ObExprOperatorType type,
                               const char *name,
                               int32_t param_num,
                               int32_t dimension) : ObFuncExprOperator(alloc, type, name, param_num, VALID_FOR_GENERATED_COL, dimension)
{
}

int ObExprSTAsText::calc_result_typeN(ObExprResType& type,
                                      ObExprResType* types_stack,
                                      int64_t param_num,
                                      ObExprTypeCtx& type_ctx) const
{
  UNUSED(type_ctx); 
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(param_num > 2)) {
    ObString fun_name(get_name());
    ret = OB_ERR_PARAM_SIZE;
    LOG_USER_ERROR(OB_ERR_PARAM_SIZE, fun_name.length(), fun_name.ptr());
  } else {
    if (1 == param_num) {
      ObObjType data_type = types_stack[0].get_type();
      if (ob_is_geometry(data_type) || ob_is_null(data_type)) {
        // do nothing
      } else {
        types_stack[0].set_calc_type(ObLongTextType);
        types_stack[0].set_calc_collation_type(CS_TYPE_BINARY);
        types_stack[0].set_calc_collation_level(CS_LEVEL_IMPLICIT);
      }
    }

    if (2 == param_num) {
      ObObjType option_type = types_stack[1].get_type();
      if (ob_is_string_type(option_type) || ob_is_null(option_type)) {
        // do nothing
      } else {
        types_stack[1].set_calc_type(ObVarcharType);
        types_stack[1].set_calc_collation_type(types_stack[1].get_collation_type());
        types_stack[1].set_calc_collation_level(types_stack[1].get_collation_level());
      }
    }

    if (OB_SUCC(ret)) {
      type.set_type(ObLongTextType);
      type.set_collation_type(CS_TYPE_UTF8MB4_GENERAL_CI);
      type.set_collation_level(CS_LEVEL_IMPLICIT);
      type.set_accuracy(ObAccuracy::DDL_DEFAULT_ACCURACY[ObLongTextType]);
    }
  }
  return ret;
}

int ObExprSTAsText::eval_st_astext_common(const ObExpr &expr,
                                          ObEvalCtx &ctx,
                                          ObDatum &res,
                                          const char *func_name)
{
#if !SEEKDB_ENABLE_CORE_GIS
  if (expr.arg_cnt_ != 1) return OB_NOT_SUPPORTED;
  ObDatum *datum = nullptr;
  int ret = OB_SUCCESS;
  if (OB_FAIL(expr.args_[0]->eval(ctx, datum))) {
  } else if (datum->is_null()) {
    res.set_null();
  } else if (nullptr == share::g_mp) {
    ret = OB_NOT_SUPPORTED;
  } else {
    const ObString geometry = datum->get_string();
    AsTextPluginSink sink{&expr, &ctx, &res};
    seekdb_plugin_execution_context_v1_t plugin_ctx = {};
    plugin_ctx.struct_size = sizeof(plugin_ctx);
    plugin_ctx.host = reinterpret_cast<seekdb_plugin_host_handle_t *>(&sink);
    plugin_ctx.emit_result = emit_astext_plugin_result;
    seekdb_plugin_execution_value_v1_t argument = {};
    argument.struct_size = sizeof(argument);
    argument.type_id = "org.seekdb.gis.geometry";
    argument.data = reinterpret_cast<const uint8_t *>(geometry.ptr());
    argument.data_size = static_cast<uint64_t>(geometry.length());
    const int plugin_ret = share::g_mp->execute_plugin_extension(
        SEEKDB_PLUGIN_EXTENSION_FUNCTION, func_name,
        &plugin_ctx, &argument, 1);
    if (OB_SUCCESS != plugin_ret) ret = OB_NOT_SUPPORTED;
  }
  return ret;
#else
  int ret = OB_SUCCESS;
  ObEvalCtx::TempAllocGuard tmp_alloc_g(ctx);
  
  MultimodeAlloctor tmp_allocator(tmp_alloc_g.get_allocator());
  int num_args = expr.arg_cnt_;
  bool is_null_result = false;
  ObString res_wkt;
  omt::ObSrsCacheGuard srs_guard;
  ObSQLSessionInfo *session = ctx.exec_ctx_.get_my_session();
  const ObSrsItem *srs = NULL;
  ObGeometry *geo = NULL;
  bool is_geog = false;
  bool need_reverse = false;
  ObDatum *gis_datum = NULL;
  ObString wkb;
  bool is_3d_geo = false;
  // get geo
  if (OB_FAIL(tmp_allocator.eval_arg(expr.args_[0], ctx, gis_datum))) {
    LOG_WARN("eval geo args failed", K(ret));
  } else if (gis_datum->is_null()) {
    is_null_result = true;
  } else if (FALSE_IT(wkb = gis_datum->get_string())) {
  } else if (OB_FAIL(ObTextStringHelper::read_real_string_data_with_copy(tmp_allocator, *gis_datum,
             expr.args_[0]->datum_meta_, expr.args_[0]->obj_meta_.has_lob_header(), wkb))) {
    LOG_WARN("fail to get real string data", K(ret), K(wkb));
  } else if (OB_FAIL(ObGeoExprUtils::construct_geometry(tmp_allocator,
      wkb, srs_guard, srs, geo, func_name, true, false))) {
    LOG_WARN("fail to create geo", K(ret), K(wkb));
  } else if (OB_NOT_NULL(srs)){
    is_geog = srs->is_geographical_srs();
    need_reverse = is_geog && (srs->is_lat_long_order());
  }
  
  // get axis_order
  if (!is_null_result && OB_SUCC(ret) && num_args > 1 ) {
    ObGeoAxisOrder axis_order = ObGeoAxisOrder::INVALID;
    ObDatum *datum = NULL;
    ObString dstr;
    if (OB_FAIL(tmp_allocator.eval_arg(expr.args_[1], ctx, datum))) { 
      LOG_WARN("eval axis_order axis_order failed", K(ret));
    } else if (datum->is_null()){
      is_null_result = true;
    } else if (!ob_is_string_type(expr.args_[1]->datum_meta_.type_) ||
               ObCharset::is_cs_nonascii(expr.args_[1]->datum_meta_.cs_type_)) {
      ret = OB_ERR_GIS_INVALID_DATA;
      LOG_USER_ERROR(OB_ERR_GIS_INVALID_DATA, func_name);
    } else if (FALSE_IT(dstr = datum->get_string())) {
    } else if (OB_FAIL(ObTextStringHelper::read_real_string_data_with_copy(tmp_allocator, *datum,
              expr.args_[1]->datum_meta_, expr.args_[1]->obj_meta_.has_lob_header(), dstr))) {
      LOG_WARN("fail to get real string data", K(ret), K(dstr));
    } else if (OB_FAIL(ObGeoExprUtils::parse_axis_order(dstr, func_name, axis_order))) {
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
    if (OB_ISNULL(geo)) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("unexpected null geo", K(ret));
    } else {
      if (is_geog && need_reverse && OB_FAIL(ObGeoExprUtils::reverse_coordinate(geo, func_name))) {
        LOG_WARN("failed to reverse geometry coordinate", K(ret));
      }
      if (OB_SUCC(ret)) {
        if (OB_FAIL(to_wkt(tmp_allocator, geo, res_wkt, func_name))) {
          LOG_WARN("failed to transform geo to wkt", K(ret));
        }
      }
    }
  }

  if (OB_FAIL(ret)) {
  } else if (is_null_result) {
    res.set_null();
  } else if (OB_FAIL(ObGeoExprUtils::pack_geo_res(expr, ctx, res, res_wkt))) {
    LOG_WARN("fail to pack geo res", K(ret));
  }

  return ret;
#endif
}

#if SEEKDB_ENABLE_CORE_GIS
int ObExprSTAsText::to_wkt(ObIAllocator &allocator, ObGeometry *geo, ObString &res_wkt, const char *func_name)
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(geo)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("unexpected null geo", K(ret));
  } else if (ObGeoTypeUtil::is_3d_geo_type(geo->type())) {
    ObGeometry3D *geo_3d  = static_cast<ObGeometry3D *>(geo);
    if (OB_FAIL(geo_3d->to_wkt(allocator, res_wkt))) {
      LOG_WARN("fail to reserver coordiante in geo 3d", K(ret));
    }
  } else {
    ObGeoToWktVisitor wkt_visitor(&allocator);
    if (OB_FAIL(geo->do_visit(wkt_visitor))) {
      ret = OB_ERR_GIS_INVALID_DATA;
      LOG_USER_ERROR(OB_ERR_GIS_INVALID_DATA, func_name);
      LOG_WARN("failed to transform geo to wkt", K(ret));
    } else {
      wkt_visitor.get_wkt(res_wkt);
    }
  }
  return ret;
}
#endif

int ObExprSTAsText::eval_st_astext(const ObExpr &expr, ObEvalCtx &ctx, ObDatum &res)
{
  return eval_st_astext_common(expr, ctx, res, N_ST_ASTEXT);
}

int ObExprSTAsWkt::eval_st_astext(const ObExpr &expr, ObEvalCtx &ctx, ObDatum &res)
{
  return eval_st_astext_common(expr, ctx, res, N_ST_ASWKT);
}

int ObExprSTAsText::cg_expr(ObExprCGCtx &expr_cg_ctx,
                            const ObRawExpr &raw_expr,
                            ObExpr &rt_expr) const
{
  UNUSED(expr_cg_ctx);
  UNUSED(raw_expr);
  rt_expr.eval_func_ = eval_st_astext;
  return OB_SUCCESS;
}

int ObExprSTAsWkt::cg_expr(ObExprCGCtx &expr_cg_ctx,
                                  const ObRawExpr &raw_expr,
                                  ObExpr &rt_expr) const
{
  UNUSED(expr_cg_ctx);
  UNUSED(raw_expr);
  rt_expr.eval_func_ = eval_st_astext;
  return OB_SUCCESS;
}

} // namespace sql
} // namespace oceanbase
