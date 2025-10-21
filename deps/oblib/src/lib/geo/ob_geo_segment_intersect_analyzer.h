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

#ifndef OCEANBASE_LIB_GEO_SEG_INTERSECTS_ANALYZER_
#define OCEANBASE_LIB_GEO_SEG_INTERSECTS_ANALYZER_

#include "lib/geo/ob_geo_cache.h"
#include "ob_geo_topology_calculate.h"
#include "ob_geo_rstar_tree.h"

namespace oceanbase {
namespace common {
class ObLineIntersectionAnalyzer {
public:
  typedef std::pair<ObCartesianBox, ObLineSegment *> RtreeNodeValue;
public:
  ObLineIntersectionAnalyzer(ObCachedGeomBase *cache_geo, ObRstarTree<ObLineSegment> &rtree_index)
    : cache_geo_(cache_geo),
      rtree_index_(rtree_index),
      flags_(0) {}
    
  virtual ~ObLineIntersectionAnalyzer() {}
  int segment_intersection_query(ObGeometry *geo);
  bool is_intersects() { return is_intersect_; }
  void set_intersects_analyzer_type(bool check_all_intesect) { check_all_intesect_ = check_all_intesect;}
  void reset_flag() { flags_ = 0; }
  bool is_check_done(LineIntersect inter_res);
  bool has_external_intersects() { return check_all_intesect_ && has_external_intersect_;}
  bool has_internal_intersects() { return check_all_intesect_ && has_internal_intersect_;}
  bool set_after_visitor() { return false; }
private:
  ObCachedGeomBase *cache_geo_;
  ObRstarTree<ObLineSegment> &rtree_index_;
  union {
    struct {
      uint8_t is_intersect_ : 1;
      uint8_t has_internal_intersect_ : 1; // the intersection point is a endpoints
      uint8_t has_external_intersect_ : 1; // the intersection point is not endpoints
      uint8_t check_all_intesect_: 1;      // need to distinguish intersection types
      uint8_t reserved_ : 4;
    };

    uint8_t flags_;
  };
};

} // namespace common
} // namespace oceanbase

#endif // OCEANBASE_LIB_GEO_SEG_INTERSECTS_ANALYZER_
