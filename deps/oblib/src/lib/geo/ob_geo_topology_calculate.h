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

#ifndef OCEANBASE_LIB_GEO_OB_TOPOLOGY_
#define OCEANBASE_LIB_GEO_OB_TOPOLOGY_

#include "lib/string/ob_string.h"
#include "lib/geo/ob_geo_common.h"
#include "lib/geo/ob_geo_utils.h"

namespace oceanbase {
namespace common {

// point relative position to line
enum PointPosition {
  LEFT = 0,
  RIGHT = 1,
  ON = 2,
  UNKNOWN = 3,
};

enum LineIntersect {
  NO_INTERSCT = 0,
  POINT_INTERSECT = 1,
  END_POINT_INTERSECT = 2, // Intersects at a endpoint.
  LINE_INTERSECT = 3,
};

class ObGeoTopology {
public:
  static LineIntersect calculate_point_inersect_horizontally(const ObPoint2d &start, const ObPoint2d &end,
                                                             const ObPoint2d &target, bool &is_on_boundary);
  static PointPosition calculate_point_position(const ObPoint2d &start, const ObPoint2d &end,
                                                const ObPoint2d &target);
  static int calculate_segment_intersect(const ObPoint2d &start0, const ObPoint2d &end0,
                                         const ObPoint2d &start1, const ObPoint2d &end1,
                                         LineIntersect &res);
  static int calculate_line_segments_intersect(const ObLineSegment& seg1, const ObLineSegment& seg2,
                                               LineIntersect &res);
  static bool is_same(const ObPoint2d &point, const ObPoint2d &target) {return point.x == target.x && point.y == target.y;}
};

} // namespace common
} // namespace oceanbase

#endif // OCEANBASE_LIB_GEO_OB_TOPOLOGY_
