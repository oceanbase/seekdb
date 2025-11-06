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

#ifndef OCEANBASE_LIB_GEO_OB_GEO_SEGMENT_COLLECT_VISITOR_
#define OCEANBASE_LIB_GEO_OB_GEO_SEGMENT_COLLECT_VISITOR_
#include "lib/geo/ob_geo_visitor.h"
#include "lib/geo/ob_geo_cache.h"

namespace oceanbase
{
namespace common
{

class ObGeoSegmentCollectVisitor : public ObEmptyGeoVisitor
{
public:
  ObGeoSegmentCollectVisitor(ObLineSegments *segments)
  : line_segments_(segments),
    segments_(nullptr),
    is_collect_mono_(true) {}
  ObGeoSegmentCollectVisitor(ObSegments *segments)
  : line_segments_(nullptr),
    segments_(segments),
    is_collect_mono_(false) {}

  virtual ~ObGeoSegmentCollectVisitor() {}
  bool prepare(ObGeometry *geo);  
  int visit(ObIWkbGeomLineString *geo);
  int visit(ObIWkbGeogLineString *geo);
  int visit(ObIWkbGeomLinearRing *geo);
  int visit(ObIWkbGeogLinearRing *geo);
  int visit(ObIWkbGeometry *geo) { UNUSED(geo); return OB_SUCCESS; }
  bool is_end(ObIWkbGeomLineString *geo) override { UNUSED(geo); return true;}
  bool is_end(ObIWkbGeogLineString *geo) override { UNUSED(geo); return true;}
  bool is_end(ObIWkbGeomLinearRing *geo) override { UNUSED(geo); return true;}
  bool is_end(ObIWkbGeogLinearRing *geo) override { UNUSED(geo); return true;}
  bool set_after_visitor() { return false; }
private:
  template<typename T_IBIN>
  int collect_line_segment(T_IBIN *geo);
  template<typename T_IBIN>
  int collect_segment(T_IBIN *geo);
  ObLineSegments *line_segments_;
  ObSegments *segments_;
  bool is_collect_mono_;
  DISALLOW_COPY_AND_ASSIGN(ObGeoSegmentCollectVisitor);
};

} // namespace common
} // namespace oceanbase

#endif
