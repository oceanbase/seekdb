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
#include "sql/engine/expr/ob_expr_priv_st_geometrytype.h"
#include <cstring>
#include "sql/engine/expr/ob_expr_lob_utils.h"
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
struct GeometryTypePluginSink
{
  const ObExpr *expr_;
  ObEvalCtx *ctx_;
  ObDatum *result_;
};

seekdb_plugin_status_t SEEKDB_PLUGIN_CALL emit_geometrytype_plugin_result(
    seekdb_plugin_host_handle_t *host,
    const seekdb_plugin_execution_result_v1_t *result)
{
  if (nullptr == host || nullptr == result || result->struct_size != sizeof(*result) ||
      result->is_null != 0 || result->data_size == 0 || nullptr == result->data ||
      nullptr == result->type_id ||
      0 != std::strcmp(result->type_id, "org.seekdb.gis.scalar.bytes")) {
    return SEEKDB_PLUGIN_STATUS_INVALID_ARGUMENT;
  }
  GeometryTypePluginSink *sink = reinterpret_cast<GeometryTypePluginSink *>(host);
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
ObExprPrivSTGeometryType::ObExprPrivSTGeometryType(common::ObIAllocator &alloc)
    : ObFuncExprOperator(alloc, T_FUN_SYS_PRIV_ST_GEOMETRYTYPE, N_PRIV_ST_GEOMETRYTYPE, 1, NOT_VALID_FOR_GENERATED_COL,
        NOT_ROW_DIMENSION)
{}

ObExprPrivSTGeometryType::~ObExprPrivSTGeometryType()
{}

int ObExprPrivSTGeometryType::calc_result_type1(
    ObExprResType &type, ObExprResType &type1, common::ObExprTypeCtx &type_ctx) const
{
  int ret = OB_SUCCESS;
  ObObjType obj_type1 = type1.get_type();

  if (!ob_is_string_type(obj_type1) && !ob_is_geometry(obj_type1) && !ob_is_null(obj_type1)) {
    ret = OB_ERR_GIS_INVALID_DATA;
    LOG_USER_ERROR(OB_ERR_GIS_INVALID_DATA, N_PRIV_ST_GEOMETRYTYPE);
    LOG_WARN("invalid type", K(ret), K(obj_type1));
  } else {
    ObCastMode cast_mode = type_ctx.get_cast_mode();
    cast_mode &= ~CM_WARN_ON_FAIL;      // make cast return error when fail
    type_ctx.set_cast_mode(cast_mode);  // cast mode only do work in new sql engine cast frame.
    type.set_varchar();
    type.set_collation_type(type_ctx.get_coll_type());
    type.set_collation_level(CS_LEVEL_IMPLICIT);
    type.set_length(MAX_TYPE_LEN);
  }

  return ret;
}

int ObExprPrivSTGeometryType::eval_priv_st_geometrytype(const ObExpr &expr, ObEvalCtx &ctx, ObDatum &res)
{
#if !SEEKDB_ENABLE_CORE_GIS
  ObDatum *datum = nullptr;
  int ret = OB_SUCCESS;
  if (OB_FAIL(expr.args_[0]->eval(ctx, datum))) {
  } else if (datum->is_null()) {
    res.set_null();
  } else if (nullptr == share::g_mp) {
    ret = OB_NOT_SUPPORTED;
  } else {
    const ObString geometry = datum->get_string();
    GeometryTypePluginSink sink{&expr, &ctx, &res};
    seekdb_plugin_execution_context_v1_t plugin_ctx = {};
    plugin_ctx.struct_size = sizeof(plugin_ctx);
    plugin_ctx.host = reinterpret_cast<seekdb_plugin_host_handle_t *>(&sink);
    plugin_ctx.emit_result = emit_geometrytype_plugin_result;
    seekdb_plugin_execution_value_v1_t argument = {};
    argument.struct_size = sizeof(argument);
    argument.type_id = "org.seekdb.gis.geometry";
    argument.data = reinterpret_cast<const uint8_t *>(geometry.ptr());
    argument.data_size = static_cast<uint64_t>(geometry.length());
    const int plugin_ret = share::g_mp->execute_plugin_extension(
        SEEKDB_PLUGIN_EXTENSION_FUNCTION, "_st_geometrytype",
        &plugin_ctx, &argument, 1);
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
  ObString res_type;

  if (ob_is_null(type1)) {
    is_null_res = true;
  } else if (OB_FAIL(temp_allocator.eval_arg(arg1, ctx, datum1))) {
    LOG_WARN("fail to eval args", K(ret));
  } else if (datum1->is_null()) {
    is_null_res = true;
  } else {
    ObString wkb = datum1->get_string();
    ObGeoType gtype = ObGeoType::GEOTYPEMAX;

    if (OB_FAIL(ObTextStringHelper::read_real_string_data(
            temp_allocator, *datum1, arg1->datum_meta_, arg1->obj_meta_.has_lob_header(), wkb))) {
      LOG_WARN("fail to read real string data", K(ret), K(arg1->obj_meta_.has_lob_header()));
    } else if (OB_FAIL(ObGeoTypeUtil::get_type_from_wkb(wkb, gtype))) {
      LOG_WARN("fail to get geo type from wkb", K(ret), K(gtype));
    } else if (OB_FAIL(ObGeoTypeUtil::get_st_geo_name_by_type(gtype, res_type))) {
      LOG_WARN("fail to get geo type name", K(ret), K(gtype));
    }
  }

  if (OB_SUCC(ret)) {
    if (is_null_res) {
      res.set_null();
    } else {
      res.set_string(res_type);
    }
  }

  return ret;
#endif
}

int ObExprPrivSTGeometryType::cg_expr(ObExprCGCtx &expr_cg_ctx, const ObRawExpr &raw_expr, ObExpr &rt_expr) const
{
  UNUSED(expr_cg_ctx);
  UNUSED(raw_expr);
  rt_expr.eval_func_ = eval_priv_st_geometrytype;
  return OB_SUCCESS;
}

}  // namespace sql
}  // namespace oceanbase
