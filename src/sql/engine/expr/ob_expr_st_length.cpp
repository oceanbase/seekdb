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
#include "sql/engine/expr/ob_expr_st_length.h"
#include <cstring>
#if SEEKDB_ENABLE_CORE_GIS
#include "sql/engine/expr/ob_geo_expr_utils.h"
#include "share/geo/ob_geo_func_register.h"
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
struct LengthPluginSink { ObDatum *result_; };
seekdb_plugin_status_t SEEKDB_PLUGIN_CALL emit_length_plugin_result(
    seekdb_plugin_host_handle_t *host,
    const seekdb_plugin_execution_result_v1_t *result)
{
  if (nullptr == host || nullptr == result || result->struct_size != sizeof(*result) ||
      result->is_null != 0 || result->data_size != sizeof(double) || nullptr == result->data ||
      nullptr == result->type_id ||
      0 != std::strcmp(result->type_id, "org.seekdb.gis.scalar.float64")) {
    return SEEKDB_PLUGIN_STATUS_INVALID_ARGUMENT;
  }
  LengthPluginSink *sink = reinterpret_cast<LengthPluginSink *>(host);
  if (nullptr == sink->result_) return SEEKDB_PLUGIN_STATUS_FAILED_PRECONDITION;
  double value = 0.0;
  std::memcpy(&value, result->data, sizeof(value));
  sink->result_->set_double(value);
  return SEEKDB_PLUGIN_STATUS_OK;
}
}
#endif
ObExprSTLength::ObExprSTLength(common::ObIAllocator &alloc)
    : ObFuncExprOperator(alloc, T_FUN_SYS_ST_LENGTH, N_ST_LENGTH, ONE_OR_TWO,
          NOT_VALID_FOR_GENERATED_COL, NOT_ROW_DIMENSION)
{}
ObExprSTLength::~ObExprSTLength()
{}
int ObExprSTLength::calc_result_typeN(ObExprResType &type, ObExprResType *types_stack,
    int64_t param_num, ObExprTypeCtx &type_ctx) const
{
  UNUSED(type_ctx);
  INIT_SUCC(ret);
  ObObjType geo_tp = types_stack[0].get_type();
  if (!ob_is_geometry(geo_tp) && !ob_is_string_type(geo_tp) && !ob_is_null(geo_tp)) {
    types_stack[0].set_calc_type(ObVarcharType);
    types_stack[0].set_calc_collation_type(CS_TYPE_BINARY);
  } else if (param_num == 2) {
    ObObjType unit_tp = types_stack[1].get_type();
    if (ob_is_string_type(unit_tp) || ob_is_null(unit_tp)) {
      // do nothing
    } else {
      types_stack[1].set_calc_type(ObVarcharType);
      types_stack[1].set_calc_collation_type(types_stack[1].get_collation_type());
      types_stack[1].set_calc_collation_level(types_stack[1].get_collation_level());
    }
  }
  if (OB_SUCC(ret)) {
    type.set_double();
  }
  return ret;
}

int ObExprSTLength::eval_st_length(const ObExpr &expr, ObEvalCtx &ctx, ObDatum &res)
{
#if !SEEKDB_ENABLE_CORE_GIS
  int ret = OB_SUCCESS;
  ObDatum *datum = nullptr;
  if (OB_FAIL(expr.args_[0]->eval(ctx, datum))) {
  } else if (datum->is_null()) {
    res.set_null();
  } else if (expr.arg_cnt_ != 1 || nullptr == share::g_mp) {
    ret = OB_NOT_SUPPORTED;
  } else {
    const ObString geometry = datum->get_string();
    LengthPluginSink sink{&res};
    seekdb_plugin_execution_context_v1_t plugin_ctx = {};
    plugin_ctx.struct_size = sizeof(plugin_ctx);
    plugin_ctx.host = reinterpret_cast<seekdb_plugin_host_handle_t *>(&sink);
    plugin_ctx.emit_result = emit_length_plugin_result;
    seekdb_plugin_execution_value_v1_t argument = {};
    argument.struct_size = sizeof(argument);
    argument.type_id = "org.seekdb.gis.geometry";
    argument.data = reinterpret_cast<const uint8_t *>(geometry.ptr());
    argument.data_size = static_cast<uint64_t>(geometry.length());
    const int plugin_ret = share::g_mp->execute_plugin_extension(
        SEEKDB_PLUGIN_EXTENSION_FUNCTION, "st_length", &plugin_ctx, &argument, 1);
    if (OB_SUCCESS != plugin_ret) ret = OB_NOT_SUPPORTED;
  }
  return ret;
#else
  int ret = OB_SUCCESS;
  bool is_null_res = false;
  ObDatum *datum1 = nullptr;
  ObExpr *arg1 = expr.args_[0];
  ObObjType type1 = arg1->datum_meta_.type_;
  ObEvalCtx::TempAllocGuard tmp_alloc_g(ctx);
  
  MultimodeAlloctor temp_allocator(tmp_alloc_g.get_allocator());
  double res_num = 0;
  ObDatum *gis_unit = NULL;
  if (ob_is_null(type1)) {
    is_null_res = true;
  } else if (OB_FAIL(temp_allocator.eval_arg(arg1, ctx, datum1))) {
    LOG_WARN("fail to eval args", K(ret));
  } else if (datum1->is_null()) {
    is_null_res = true;
  } else if (type1 == ObIntType) {
    // bugfix 53283098, should allow double type in calc_result_type2
    ret = OB_ERR_GIS_INVALID_DATA;
    LOG_USER_ERROR(OB_ERR_GIS_INVALID_DATA, N_ST_CROSSES);
    LOG_WARN("invalid type", K(ret), K(type1));
  } else if (expr.arg_cnt_ == 2 && OB_FAIL(temp_allocator.eval_arg(expr.args_[1], ctx, gis_unit))) {
    LOG_WARN("eval geo unit arg failed", K(ret));
  } else if (expr.arg_cnt_ == 2 && gis_unit->is_null()) {
    is_null_res = true;
  } else {
    // construct geometry
    ObString wkb = datum1->get_string();
    ObGeoType gtype = ObGeoType::GEOTYPEMAX;
    ObGeometry *geo = nullptr;
    const ObSrsItem *srs = NULL;
    omt::ObSrsCacheGuard srs_guard;
    ObGeoBoostAllocGuard guard{};
    lib::MemoryContext *mem_ctx = nullptr;
    if (OB_FAIL(ObTextStringHelper::read_real_string_data_with_copy(
            temp_allocator, *datum1, arg1->datum_meta_, arg1->obj_meta_.has_lob_header(), wkb))) {
      LOG_WARN("fail to read real string data", K(ret), K(arg1->obj_meta_.has_lob_header()));
    } else if (OB_FAIL(ObGeoExprUtils::get_srs_item(ctx, srs_guard, wkb, srs, true, N_ST_LENGTH))) {
      LOG_WARN("fail to get srs item", K(ret), K(wkb));
    } else if (OB_FAIL(ObGeoExprUtils::build_geometry(
                   temp_allocator, wkb, geo, srs, N_ST_LENGTH, ObGeoBuildFlag::GEO_ALLOW_3D_DEFAULT | GEO_NOT_COPY_WKB))) {  // ObIWkbGeom
      LOG_WARN("fail to build geometry from wkb", K(ret), K(wkb));
    } else if (geo->type() != ObGeoType::LINESTRING && geo->type() != ObGeoType::MULTILINESTRING) {
      is_null_res = true;
    } else if (OB_FAIL(guard.init())) {
      LOG_WARN("fail to init geo allocator guard", K(ret));
    } else if (OB_ISNULL(mem_ctx = guard.get_memory_ctx())) {
      ret = OB_ERR_NULL_VALUE;
      LOG_WARN("fail to get mem ctx", K(ret));
    } else {
      // cal length
      ObGeoEvalCtx gis_context(*mem_ctx, srs);
      if (OB_FAIL(gis_context.append_geo_arg(geo))) {
        LOG_WARN("build gis context failed", K(ret));
      } else if (OB_FAIL(ObGeoFunc<ObGeoFuncType::Length>::geo_func::eval(gis_context, res_num))) {
        LOG_WARN("eval st distance failed", K(ret));
        if (OB_ERR_GIS_INVALID_DATA == ret) {
          LOG_USER_ERROR(OB_ERR_GIS_INVALID_DATA, N_ST_LENGTH);
        } else {
          ObGeoExprUtils::geo_func_error_handle(ret, N_ST_LENGTH);
        }
      } else if (std::isinf(res_num)) {
        ret = OB_OPERATE_OVERFLOW;
        LOG_WARN("Length value is out of range in st_length", K(ret));
        LOG_USER_ERROR(OB_OPERATE_OVERFLOW, "Length", N_ST_LENGTH);
      } else if (expr.arg_cnt_ == 2) {
        // transfer to unit
        if (OB_FAIL(ObGeoExprUtils::length_unit_conversion(
                       gis_unit->get_string(), srs, res_num, res_num))) {
          LOG_WARN("fail to do unit conversion", K(ret), K(res_num));
        } else if (std::isinf(res_num)) {
          ret = OB_OPERATE_OVERFLOW;
          LOG_WARN("Length value is out of range in st_length", K(ret));
          LOG_USER_ERROR(OB_OPERATE_OVERFLOW, "Length", N_ST_LENGTH);
        } 
      }
    }
  }
  // set result
  if (OB_SUCC(ret)) {
    if (is_null_res) {
      res.set_null();
    } else {
      res.set_double(res_num);
    }
  }
  return ret;
#endif
}
int ObExprSTLength::cg_expr(
    ObExprCGCtx &expr_cg_ctx, const ObRawExpr &raw_expr, ObExpr &rt_expr) const
{
  UNUSED(expr_cg_ctx);
  UNUSED(raw_expr);
  rt_expr.eval_func_ = eval_st_length;
  return OB_SUCCESS;
}
}  // namespace sql
}  // namespace oceanbase
