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

#define USING_LOG_PREFIX LIB
#include "ob_geo_segment_intersect_analyzer.h"
#include "ob_geo_segment_collect_visitor.h"

namespace oceanbase {
namespace common {

bool ObLineIntersectionAnalyzer::is_check_done(LineIntersect inter_res)
{
  bool res = false;
  if (check_all_intesect_) {
    res = has_internal_intersect_ && has_external_intersect_;
  } else {
    res = is_intersect_;
  }
  return res;
}

int ObLineIntersectionAnalyzer::segment_intersection_query(ObGeometry *geo)
{
  int ret = OB_SUCCESS;
  is_intersect_ = false;
  if (!rtree_index_.is_built()) {
    ObLineSegments* line_segs = cache_geo_->get_line_segments();
    if (OB_ISNULL(line_segs)) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("should not be null", K(ret));
    } else if (OB_FAIL(rtree_index_.construct_rtree_index(line_segs->segs_))) {
      LOG_WARN("construct rtree index failed", K(ret));
    }
  } 
  if (OB_SUCC(ret)) {
    ObLineSegments input_segments;
    ObGeoSegmentCollectVisitor seg_visitor(&input_segments);
    if (OB_FAIL(geo->do_visit(seg_visitor))) {
      LOG_WARN("failed to get input segment", K(ret));
    } else {
      LineIntersect inter_res = LineIntersect::NO_INTERSCT;
      // query rtree to check intersesctiion
      for (uint32_t i = 0; i < input_segments.segs_.size() && OB_SUCC(ret) && !is_check_done(inter_res); i++) {
        ObCartesianBox box;
        std::vector<RtreeNodeValue> res;
        inter_res = LineIntersect::NO_INTERSCT;
        if (OB_FAIL(input_segments.segs_[i].get_box(box))) {
          LOG_WARN("failed to get segment box", K(ret));
        } else if (OB_FAIL(rtree_index_.query(QueryRelation::INTERSECTS, box, res))) {
          LOG_WARN("failed to query rtree", K(ret));
        } else {
          for (uint32_t j = 0; j < res.size() && OB_SUCC(ret) && !is_check_done(inter_res); j++) {
            const ObLineSegment* seg1 = res[j].second;
            if (OB_FAIL(ObGeoTopology::calculate_line_segments_intersect(*seg1, input_segments.segs_[i], inter_res))) {
              LOG_WARN("failed to get segment box", K(ret));
            } else if (inter_res != LineIntersect::NO_INTERSCT) {
              is_intersect_ = true;
              if (inter_res == LineIntersect::POINT_INTERSECT) {
                has_external_intersect_ = 1;
              } else {
                has_internal_intersect_ = 1;
              }
            }
          }
        }
      }
    }

  }
  return ret;
}

} // namespace common
} // namespace oceanbase
