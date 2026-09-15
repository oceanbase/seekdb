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
#include "sql/engine/expr/ob_expr_point.h"
#if SEEKDB_ENABLE_CORE_GIS
#include "sql/engine/expr/ob_geo_expr_utils.h"
#endif
#include "sql/engine/expr/ob_plugin_expr_utils.h"
#include "seekdb/plugin/execution_spi.h"
#include "seekdb/plugin/extension_spi.h"
#include "share/rc/ob_module_provider.h"

using namespace oceanbase::common;
using namespace oceanbase::sql;

namespace oceanbase
{
namespace sql
{

namespace
{

struct PointPluginSink
{
  const ObExpr *expr_;
  ObEvalCtx *ctx_;
  ObDatum *result_;
};

#if !SEEKDB_ENABLE_CORE_GIS
struct PointObjPluginSink
{
  ObIAllocator *allocator_;
  ObObj *result_;
};

seekdb_plugin_status_t SEEKDB_PLUGIN_CALL emit_point_obj_plugin_result(
    seekdb_plugin_host_handle_t *host,
    const seekdb_plugin_execution_result_v1_t *plugin_result)
{
  if (nullptr == host || nullptr == plugin_result ||
      plugin_result->struct_size != sizeof(*plugin_result) ||
      plugin_result->is_null != 0 || nullptr == plugin_result->data ||
      plugin_result->data_size == 0 || nullptr == plugin_result->type_id ||
      0 != strcmp(plugin_result->type_id, "org.seekdb.gis.geometry")) {
    return SEEKDB_PLUGIN_STATUS_INVALID_ARGUMENT;
  }
  PointObjPluginSink *sink = reinterpret_cast<PointObjPluginSink *>(host);
  if (nullptr == sink->allocator_ || nullptr == sink->result_) {
    return SEEKDB_PLUGIN_STATUS_FAILED_PRECONDITION;
  }
  char *buf = reinterpret_cast<char *>(sink->allocator_->alloc(plugin_result->data_size));
  if (nullptr == buf) return SEEKDB_PLUGIN_STATUS_NO_MEMORY;
  MEMMOVE(buf, plugin_result->data, plugin_result->data_size);
  sink->result_->set_string(ObGeometryType, buf,
                            static_cast<int32_t>(plugin_result->data_size));
  sink->result_->set_collation_level(CS_LEVEL_IMPLICIT);
  return SEEKDB_PLUGIN_STATUS_OK;
}
#endif

seekdb_plugin_status_t SEEKDB_PLUGIN_CALL emit_point_plugin_result(
    seekdb_plugin_host_handle_t *host,
    const seekdb_plugin_execution_result_v1_t *result)
{
  if (nullptr == host || nullptr == result ||
      result->struct_size < sizeof(*result) || nullptr == result->data ||
      result->data_size == 0 || result->is_null != 0) {
    return SEEKDB_PLUGIN_STATUS_INVALID_ARGUMENT;
  }
  PointPluginSink *sink = reinterpret_cast<PointPluginSink *>(host);
  if (nullptr == sink->expr_ || nullptr == sink->ctx_ || nullptr == sink->result_) {
    return SEEKDB_PLUGIN_STATUS_FAILED_PRECONDITION;
  }
  const int ret = pack_plugin_expr_result(
      *sink->expr_, *sink->ctx_, *sink->result_,
      reinterpret_cast<const char *>(result->data),
      static_cast<int64_t>(result->data_size));
  switch (ret) {
    case OB_SUCCESS: return SEEKDB_PLUGIN_STATUS_OK;
    case OB_ALLOCATE_MEMORY_FAILED: return SEEKDB_PLUGIN_STATUS_NO_MEMORY;
    case OB_INVALID_ARGUMENT: return SEEKDB_PLUGIN_STATUS_INVALID_ARGUMENT;
    default: return SEEKDB_PLUGIN_STATUS_INTERNAL;
  }
}

} // namespace

ObExprPoint::ObExprPoint(common::ObIAllocator &alloc)
    : ObFuncExprOperator(alloc,
                         T_FUN_SYS_POINT,
                         N_POINT,
                         2,
                         VALID_FOR_GENERATED_COL,
                         NOT_ROW_DIMENSION)
{
}

ObExprPoint::~ObExprPoint()
{

}

int ObExprPoint::calc_result_type2(ObExprResType &type,
                                   ObExprResType &type1,
                                   ObExprResType &type2,
                                   common::ObExprTypeCtx &type_ctx) const
{
  int ret = OB_SUCCESS;
  UNUSED(type_ctx);
  ObObjType type_x = type1.get_type();
  ObObjType type_y = type2.get_type();

  if (ob_is_geometry_tc(type_x) || ob_is_geometry_tc(type_y)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_USER_ERROR(OB_INVALID_ARGUMENT, "input type should not be geometry type");
  } else {
    if (!ob_is_double_tc(type_x)) {
      type1.set_calc_type(ObDoubleType);
    }
    if (!ob_is_double_tc(type_y)) {
      type2.set_calc_type(ObDoubleType);
    }
    type.set_geometry();
    type.set_length((ObAccuracy::DDL_DEFAULT_ACCURACY[ObGeometryType]).get_length());
  }

  return ret;
}

// for old sql engine
int ObExprPoint::calc_result2(common::ObObj &result,
                              const common::ObObj &obj1,
                              const common::ObObj &obj2,
                              common::ObExprCtx &expr_ctx) const
{
#if !SEEKDB_ENABLE_CORE_GIS
  int ret = OB_SUCCESS;
  ObIAllocator *allocator = expr_ctx.calc_buf_;
  if (OB_ISNULL(allocator)) {
    ret = OB_NOT_INIT;
  } else if (obj1.is_null() || obj2.is_null() ||
             ob_is_null(obj1.get_type()) || ob_is_null(obj2.get_type())) {
    result.set_null();
  } else if (nullptr == share::g_mp) {
    ret = OB_NOT_SUPPORTED;
  } else {
    const double x = obj1.get_double();
    const double y = obj2.get_double();
    seekdb_plugin_execution_value_v1_t arguments[2] = {};
    for (int i = 0; i < 2; ++i) {
      arguments[i].struct_size = sizeof(arguments[i]);
      arguments[i].type_id = "org.seekdb.gis.scalar.float64";
      arguments[i].data_size = sizeof(double);
    }
    arguments[0].data = reinterpret_cast<const uint8_t *>(&x);
    arguments[1].data = reinterpret_cast<const uint8_t *>(&y);
    PointObjPluginSink sink{allocator, &result};
    seekdb_plugin_execution_context_v1_t plugin_ctx = {};
    plugin_ctx.struct_size = sizeof(plugin_ctx);
    plugin_ctx.host = reinterpret_cast<seekdb_plugin_host_handle_t *>(&sink);
    plugin_ctx.emit_result = emit_point_obj_plugin_result;
    ret = share::g_mp->execute_plugin_extension(
        SEEKDB_PLUGIN_EXTENSION_FUNCTION, "st_point", &plugin_ctx, arguments, 2);
    // Keep the old row engine usable while an extension catalog is being
    // reconciled on an already initialized data directory.  The service is
    // the executable SPI identity; the extension path remains preferred.
    if (OB_SUCCESS != ret) {
      ret = share::g_mp->execute_plugin_function(
          "org.seekdb.gis.function", SEEKDB_PLUGIN_EXECUTION_SPI_MAJOR,
          SEEKDB_PLUGIN_EXECUTION_SPI_MINOR, &plugin_ctx, arguments, 2);
    }
  }
  return ret;
#else
  int ret = OB_SUCCESS;
  bool is_null_result = false;
  ObIAllocator *allocator = expr_ctx.calc_buf_;
  ObWkbBuffer res_wkb_buf(*allocator);

  if (OB_ISNULL(allocator)) { // check allocator
    ret = OB_NOT_INIT;
    LOG_WARN("buffer not init", K(ret));
  } else {
    ObObjType type_x = obj1.get_type();
    ObObjType type_y = obj2.get_type();
    if (ob_is_null(type_x) || ob_is_null(type_y)) {
      is_null_result = true;
    } else if (obj1.is_null() || obj2.is_null()) {
      is_null_result = true;
    } else {
      double x = obj1.get_double();
      double y = obj2.get_double();
      uint32_t srid = 0;
      if (OB_FAIL(res_wkb_buf.append(srid))) {
        LOG_WARN("fail to append srid to point wkb buf", K(ret), K(srid));
      } else if (OB_FAIL(res_wkb_buf.append(static_cast<char>(ENCODE_GEO_VERSION(GEO_VESION_1))))) {
         LOG_WARN("fail to append version to point wkb buf", K(ret));
      } else if (OB_FAIL(res_wkb_buf.append(static_cast<char>(ObGeoWkbByteOrder::LittleEndian)))) {
        LOG_WARN("fail to append little endian byte order to point wkb buf", K(ret));
      } else if (OB_FAIL(res_wkb_buf.append(static_cast<uint32_t>(ObGeoType::POINT)))) {
        LOG_WARN("fail to append geo type to point wkb buf", K(ret));
      } else if (OB_FAIL(res_wkb_buf.append(x))) {
        LOG_WARN("fail to append x to point wkb buf", K(ret), K(x));
      } else if (OB_FAIL(res_wkb_buf.append(y))) {
        LOG_WARN("fail to append y to point wkb buf", K(ret), K(y));
      }
    }
  }

  if (OB_SUCC(ret)) {
    if (is_null_result) {
      result.set_null();
    } else {
      char *buf = reinterpret_cast<char *>(allocator->alloc(res_wkb_buf.length()));
      if (OB_ISNULL(buf)) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_WARN("fail to alloc memory for result buf", K(ret), K(res_wkb_buf.length()));
      } else {
        MEMMOVE(buf, res_wkb_buf.ptr(), res_wkb_buf.length());
        result.set_collation_type(result_type_.get_collation_type());
        result.set_string(ObGeometryType, buf, res_wkb_buf.length());
        result.set_collation_level(CS_LEVEL_IMPLICIT);
      }
    }
  }

  return ret;
#endif
}

int ObExprPoint::eval_point(const ObExpr &expr,
                            ObEvalCtx &ctx,
                            ObDatum &res)
{
	int ret = OB_SUCCESS;
  bool is_null_result = false;
  ObDatum *datum_x = NULL;
  ObDatum *datum_y = NULL;
  ObExpr *arg_x = expr.args_[0];
  ObExpr *arg_y = expr.args_[1];
  ObObjType type_x = arg_x->datum_meta_.type_;
  ObObjType type_y = arg_y->datum_meta_.type_;
  ObEvalCtx::TempAllocGuard tmp_alloc_g(ctx);
  common::ObArenaAllocator &tmp_allocator = tmp_alloc_g.get_allocator();
#if SEEKDB_ENABLE_CORE_GIS
  ObWkbBuffer res_wkb_buf(tmp_allocator);
#endif

  if (ob_is_null(type_x) || ob_is_null(type_y)) {
    is_null_result = true;
  } else if (OB_FAIL(arg_x->eval(ctx, datum_x))) {
	  LOG_WARN("fail to eval point x arg", K(ret), K(type_x));
  } else if (OB_FAIL(arg_y->eval(ctx, datum_y))) {
	  LOG_WARN("fail to eval point y arg", K(ret), K(type_y));
  } else if (datum_x->is_null() || datum_y->is_null()) {
    is_null_result = true;
  } else {
    double x = datum_x->get_double();
    double y = datum_y->get_double();

    // Prefer the active GIS function service.  The byte-oriented ABI keeps
    // ObDatum and execution state inside the host; the callback copies the
    // plugin-owned result into the normal geometry datum.  A missing or
    // incompatible plugin remains compatible with the legacy in-core path
    // until the complete GIS surface has been migrated.
    PointPluginSink sink{&expr, &ctx, &res};
    seekdb_plugin_execution_context_v1_t plugin_ctx = {};
    plugin_ctx.struct_size = sizeof(plugin_ctx);
    plugin_ctx.host = reinterpret_cast<seekdb_plugin_host_handle_t *>(&sink);
    plugin_ctx.emit_result = emit_point_plugin_result;
    seekdb_plugin_execution_value_v1_t arguments[2] = {};
    arguments[0].struct_size = sizeof(arguments[0]);
    arguments[0].type_id = "org.seekdb.gis.scalar.float64";
    arguments[0].data = reinterpret_cast<const uint8_t *>(&x);
    arguments[0].data_size = sizeof(x);
    arguments[1] = arguments[0];
    arguments[1].data = reinterpret_cast<const uint8_t *>(&y);
    if (nullptr != share::g_mp) {
      int plugin_ret = share::g_mp->execute_plugin_extension(
          SEEKDB_PLUGIN_EXTENSION_FUNCTION, "st_point", &plugin_ctx, arguments, 2);
      if (OB_SUCCESS != plugin_ret) {
        plugin_ret = share::g_mp->execute_plugin_function(
            "org.seekdb.gis.function", SEEKDB_PLUGIN_EXECUTION_SPI_MAJOR,
            SEEKDB_PLUGIN_EXECUTION_SPI_MINOR, &plugin_ctx, arguments, 2);
      }
      if (OB_SUCCESS == plugin_ret) {
        return OB_SUCCESS;
      }
    } else {
      LOG_WARN("GIS module provider is null in POINT evaluation", K(ret));
    }

#if SEEKDB_ENABLE_CORE_GIS
    uint32_t srid = 0;
    if (OB_FAIL(res_wkb_buf.append(srid))) {
      LOG_WARN("fail to append srid to point wkb buf", K(ret), K(srid));
    } else if (OB_FAIL(res_wkb_buf.append(static_cast<char>(ENCODE_GEO_VERSION(GEO_VESION_1))))) {
      LOG_WARN("fail to append version to point wkb buf", K(ret));
    } else if (OB_FAIL(res_wkb_buf.append(static_cast<char>(ObGeoWkbByteOrder::LittleEndian)))) {
      LOG_WARN("fail to append little endian byte order to point wkb buf", K(ret));
    } else if (OB_FAIL(res_wkb_buf.append(static_cast<uint32_t>(ObGeoType::POINT)))) {
      LOG_WARN("fail to append geo type to point wkb buf", K(ret));
    } else if (OB_FAIL(res_wkb_buf.append(x))) {
      LOG_WARN("fail to append x to point wkb buf", K(ret), K(x));
    } else if (OB_FAIL(res_wkb_buf.append(y))) {
      LOG_WARN("fail to append y to point wkb buf", K(ret), K(y));
    }
#else
    ret = OB_NOT_SUPPORTED;
#endif
  }

  if (OB_SUCC(ret)) {
    if (is_null_result) {
      res.set_null();
#if SEEKDB_ENABLE_CORE_GIS
    } else if (OB_FAIL(pack_plugin_expr_result(expr, ctx, res,
                                                res_wkb_buf.string().ptr(),
                                                res_wkb_buf.string().length()))) {
      LOG_WARN("fail to pack geo res", K(ret));
#endif
    }
  }

  return ret;
}

int ObExprPoint::cg_expr(ObExprCGCtx &expr_cg_ctx,
                         const ObRawExpr &raw_expr,
                         ObExpr &rt_expr) const
{
  UNUSED(expr_cg_ctx);
  UNUSED(raw_expr);
  rt_expr.eval_func_ = eval_point;
  return OB_SUCCESS;
}

} // namespace sql
} // namespace oceanbase
