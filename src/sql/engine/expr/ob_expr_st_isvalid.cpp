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

#include <cstring>
#if SEEKDB_ENABLE_CORE_GIS
#include "share/geo/ob_geo_func_register.h"
#endif
#include "ob_expr_st_isvalid.h"
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

namespace oceanbase
{
namespace sql
{

#if !SEEKDB_ENABLE_CORE_GIS
namespace
{
struct IsValidPluginSink
{
  ObDatum *result_;
};

seekdb_plugin_status_t SEEKDB_PLUGIN_CALL emit_isvalid_plugin_result(
    seekdb_plugin_host_handle_t *host,
    const seekdb_plugin_execution_result_v1_t *result)
{
  if (nullptr == host || nullptr == result || result->struct_size != sizeof(*result) ||
      result->is_null != 0 || result->data_size != sizeof(uint8_t) ||
      nullptr == result->data || nullptr == result->type_id ||
      0 != std::strcmp(result->type_id, "org.seekdb.gis.scalar.bool")) {
    return SEEKDB_PLUGIN_STATUS_INVALID_ARGUMENT;
  }
  IsValidPluginSink *sink = reinterpret_cast<IsValidPluginSink *>(host);
  if (nullptr == sink->result_) return SEEKDB_PLUGIN_STATUS_FAILED_PRECONDITION;
  sink->result_->set_bool(result->data[0] != 0);
  return SEEKDB_PLUGIN_STATUS_OK;
}
}
#endif
ObExprSTIsValid::ObExprSTIsValid(ObIAllocator &alloc)
    : ObFuncExprOperator(alloc, T_FUN_SYS_ST_ISVALID, N_ST_ISVALID, 1, VALID_FOR_GENERATED_COL, NOT_ROW_DIMENSION)
{
}

int ObExprSTIsValid::calc_result_type1(ObExprResType &type,
                                       ObExprResType &type1,
                                       common::ObExprTypeCtx &type_ctx) const
{
  UNUSED(type_ctx); 
  int ret = OB_SUCCESS;
  if (ob_is_null(type1.get_type())) {
    // do nothing
  } else if (ob_is_numeric_type(type1.get_type())) {
    type1.set_calc_type(ObLongTextType);
  } else if (!ob_is_geometry(type1.get_type()) && !ob_is_string_type(type1.get_type())) {
    ret = OB_ERR_GIS_INVALID_DATA;
    LOG_USER_ERROR(OB_ERR_GIS_INVALID_DATA, get_name());
  }
  type.set_int32();
  type.set_scale(common::ObAccuracy::DDL_DEFAULT_ACCURACY[common::ObIntType].scale_);
  type.set_precision(common::ObAccuracy::DDL_DEFAULT_ACCURACY[common::ObIntType].precision_);
  return ret;
}

int ObExprSTIsValid::eval_st_isvalid(const ObExpr &expr, ObEvalCtx &ctx, ObDatum &res)
{
#if !SEEKDB_ENABLE_CORE_GIS
  int ret = OB_SUCCESS;
  ObDatum *datum = nullptr;
  if (OB_FAIL(expr.args_[0]->eval(ctx, datum))) {
  } else if (datum->is_null()) {
    res.set_null();
  } else if (nullptr == share::g_mp) {
    ret = OB_NOT_SUPPORTED;
  } else {
    const ObString geometry = datum->get_string();
    IsValidPluginSink sink{&res};
    seekdb_plugin_execution_context_v1_t plugin_ctx = {};
    plugin_ctx.struct_size = sizeof(plugin_ctx);
    plugin_ctx.host = reinterpret_cast<seekdb_plugin_host_handle_t *>(&sink);
    plugin_ctx.emit_result = emit_isvalid_plugin_result;
    seekdb_plugin_execution_value_v1_t argument = {};
    argument.struct_size = sizeof(argument);
    argument.type_id = "org.seekdb.gis.geometry";
    argument.data = reinterpret_cast<const uint8_t *>(geometry.ptr());
    argument.data_size = static_cast<uint64_t>(geometry.length());
    const int plugin_ret = share::g_mp->execute_plugin_extension(
        SEEKDB_PLUGIN_EXTENSION_FUNCTION, "st_isvalid",
        &plugin_ctx, &argument, 1);
    if (OB_SUCCESS != plugin_ret) ret = OB_NOT_SUPPORTED;
  }
  return ret;
#else
  int ret = OB_SUCCESS;
  ObEvalCtx::TempAllocGuard tmp_alloc_g(ctx);
  
  MultimodeAlloctor tmp_allocator(tmp_alloc_g.get_allocator());
  ObDatum *datum = NULL;
  int num_args = expr.arg_cnt_;
  bool is_null_result = false;
  ObGeoSrid srid = 0;
  ObString wkb;
  common::ObSrsCacheGuard srs_guard;
  const ObSrsItem *srs = NULL;
  ObGeometry *geo = NULL;
  bool is_geog = false;
  bool isvalid_res = false;
  
  if (OB_FAIL(tmp_allocator.eval_arg(expr.args_[0], ctx, datum))) {
  } else if (datum->is_null()) {
    is_null_result = true;
  } else {
    wkb = datum->get_string();
    if (OB_FAIL(ObTextStringHelper::read_real_string_data_with_copy(ctx.exec_ctx_, tmp_allocator, *datum,
              expr.args_[0]->datum_meta_, expr.args_[0]->obj_meta_.has_lob_header(), wkb))) {
    } else if (OB_FAIL(ObGeoExprUtils::get_srs_item(ctx, srs_guard, wkb, srs, true, N_ST_ISVALID))) {
    } else if (OB_FAIL(ObGeoExprUtils::build_geometry(tmp_allocator, wkb, geo, srs, N_ST_ISVALID, ObGeoBuildFlag::GEO_ALLOW_3D_DEFAULT | GEO_NOT_COPY_WKB))) {
    }
  }

  if (OB_SUCC(ret)) {
    ObGeoBoostAllocGuard guard{};
    lib::MemoryContext *mem_ctx = nullptr;
    if (is_null_result) {
      res.set_null();
    } else if (OB_FAIL(guard.init())) {
    } else if (OB_ISNULL(mem_ctx = guard.get_memory_ctx())) {
      ret = OB_ERR_NULL_VALUE;
      LOG_WARN("fail to get mem ctx", K(ret));
    } else {
      ObGeoEvalCtx gis_context(*mem_ctx, srs);
      if (OB_FAIL(gis_context.append_geo_arg(geo))) {
      } else if (OB_FAIL(ObGeoFunc<ObGeoFuncType::IsValid>::geo_func::eval(gis_context, isvalid_res))) {
        LOG_WARN("eval geo func isvalid failed", K(ret));
        ObGeoExprUtils::geo_func_error_handle(ret, N_ST_ISVALID);
      } else {
        res.set_bool(isvalid_res);
      }
    }
  }

  return ret;
#endif
}

int ObExprSTIsValid::cg_expr(ObExprCGCtx &expr_cg_ctx, const ObRawExpr &raw_expr, ObExpr &rt_expr) const
{
  UNUSEDx(expr_cg_ctx, raw_expr);
  rt_expr.eval_func_ = eval_st_isvalid;
  return OB_SUCCESS;
}

} // namespace sql
} // namespace oceanbase
