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

#ifndef OCEANBASE_LIB_GEO_OB_CACHE_LINESTRING_
#define OCEANBASE_LIB_GEO_OB_CACHE_LINESTRING_

#include "lib/geo/ob_geo_cache.h"
#include "lib/geo/ob_geo_rstar_tree.h"
#include "lib/geo/ob_geo_segment_intersect_analyzer.h"

namespace oceanbase {
namespace common {
class ObLineSegments;

class ObCachedGeoLinestring : public ObCachedGeomBase {
public:
  ObCachedGeoLinestring(ObGeometry *geom, ObIAllocator &allocator, const ObSrsItem *srs)
    : ObCachedGeomBase(geom, allocator, srs),
      rtree_(this),
      lAnalyzer_(nullptr),
      line_segments_(page_allocator_, point_mode_arena_),
      input_vertexes_(&point_mode_arena_, common::ObModIds::OB_MODULE_PAGE_ALLOCATOR) {}
  virtual ~ObCachedGeoLinestring() {};
  // get segments from origin_geo_
  virtual int init();
  virtual ObGeoCacheType get_cache_type() { return ObGeoCacheType::GEO_LINESTRING_CACHE;}
  virtual int intersects(ObGeometry& geo, ObGeoEvalCtx& gis_context, bool &res) override;
  virtual ObLineSegments* get_line_segments() { return &line_segments_; }
  virtual void destroy_cache() { this->~ObCachedGeoLinestring(); }
private:
  ObRstarTree<ObLineSegment> rtree_;
  ObLineIntersectionAnalyzer *lAnalyzer_;
  ObLineSegments line_segments_;
  ObVertexes input_vertexes_;
};

} // namespace common
} // namespace oceanbase

#endif // OCEANBASE_LIB_GEO_OB_CACHE_LINESTRING_
