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

#ifndef OCEANBASE_LIB_GEO_OB_CACHE_POINT_
#define OCEANBASE_LIB_GEO_OB_CACHE_POINT_

#include "lib/geo/ob_geo_cache.h"
#include "lib/geo/ob_geo_rstar_tree.h"

namespace oceanbase {
namespace common {

class ObCachedGeoPoint : public ObCachedGeomBase {
public:
  ObCachedGeoPoint(ObGeometry *geom, ObIAllocator &allocator, const ObSrsItem *srs)
    : ObCachedGeomBase(geom, allocator, srs) {}
  virtual ~ObCachedGeoPoint() {};
  virtual ObGeoCacheType get_cache_type() { return ObGeoCacheType::GEO_POINT_CACHE;}
  virtual void destroy_cache() { this->~ObCachedGeoPoint(); }
  virtual int intersects(ObGeometry& geo, ObGeoEvalCtx& gis_context, bool &res) override;
};

} // namespace common
} // namespace oceanbase

#endif // OCEANBASE_LIB_GEO_OB_CACHE_Point_
