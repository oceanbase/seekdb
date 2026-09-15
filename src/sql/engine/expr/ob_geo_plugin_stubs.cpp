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

/*
 * Core-only ABI bridge for GIS functionality supplied by the GIS plugin.
 * These entry points deliberately fail closed so the core remains linkable
 * without pulling the GIS implementation into ob_sql.
 */
#include "sql/engine/expr/ob_geo_expr_utils.h"

namespace oceanbase
{
namespace sql
{

#if !SEEKDB_ENABLE_CORE_GIS
int ObGeoExprUtils::get_srs_item(ObEvalCtx &ctx,
                                 common::ObSrsCacheGuard &srs_guard,
                                 const uint32_t srid,
                                 const common::ObSrsItem *&srs)
{
  (void)ctx;
  (void)srs_guard;
  (void)srid;
  srs = nullptr;
  return OB_NOT_SUPPORTED;
}

int ObGeoExprUtils::get_srs_item(ObEvalCtx &ctx,
                                 common::ObSrsCacheGuard &srs_guard,
                                 const common::ObString &wkb,
                                 const common::ObSrsItem *&srs,
                                 bool use_little_bo,
                                 const char *func_name)
{
  (void)ctx;
  (void)srs_guard;
  (void)wkb;
  (void)use_little_bo;
  (void)func_name;
  srs = nullptr;
  return OB_NOT_SUPPORTED;
}

int ObGeoExprUtils::build_geometry(common::ObIAllocator &allocator,
                                   const common::ObString &wkb,
                                   common::ObGeometry *&geo,
                                   const common::ObSrsItem *srs,
                                   const char *func_name,
                                   uint8_t build_flag)
{
  (void)allocator;
  (void)wkb;
  (void)srs;
  (void)func_name;
  (void)build_flag;
  geo = nullptr;
  return OB_NOT_SUPPORTED;
}

int ObGeoExprUtils::check_empty(common::ObGeometry *geo, bool &is_empty)
{
  (void)geo;
  is_empty = true;
  return OB_NOT_SUPPORTED;
}
#endif

} // namespace sql
} // namespace oceanbase

namespace oceanbase
{
namespace common
{

#if !SEEKDB_ENABLE_CORE_GIS
int ObGeoTypeUtil::get_type_srid_from_wkb(const ObString &wkb,
                                          ObGeoType &type,
                                          uint32_t &srid)
{
  (void)wkb;
  type = ObGeoType::GEOMETRY;
  srid = 0;
  return OB_NOT_SUPPORTED;
}
#endif

} // namespace common
} // namespace oceanbase
