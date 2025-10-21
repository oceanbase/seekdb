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

#ifndef OCEANBASE_LIB_GEO_POINT_LOCATION_ANALYZER_
#define OCEANBASE_LIB_GEO_POINT_LOCATION_ANALYZER_

#include "lib/geo/ob_geo_cache.h"
#include "lib/geo/ob_geo_rstar_tree.h"

namespace oceanbase {
namespace common {

class ObPointLocationAnalyzer {
public:
  typedef std::pair<ObCartesianBox, ObSegment *> RtreeNodeValue;
public:
  ObPointLocationAnalyzer(ObCachedGeomBase *cache_geo, ObRstarTree<ObSegment> &rtree_index)
    : cache_geo_(cache_geo),
      rtree_index_(rtree_index),
      position_(ObPointLocation::INVALID),
      intersect_cnt_(0) {}
    
  virtual ~ObPointLocationAnalyzer() {}
  int calculate_point_position(const ObPoint2d &test_point);
  ObPointLocation get_position() { return position_; }
  inline void clear_result() { position_ = ObPointLocation::INVALID; intersect_cnt_ = 0;}
  void update_farthest_position();
private:
  ObCachedGeomBase *cache_geo_;
  ObRstarTree<ObSegment> &rtree_index_;
  ObPointLocation position_;
  uint32_t intersect_cnt_;
};

} // namespace common
} // namespace oceanbase

#endif // OCEANBASE_LIB_GEO_POINT_LOCATION_ANALYZER_
