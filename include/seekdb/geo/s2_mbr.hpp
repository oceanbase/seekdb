/*
 * Copyright (c) 2026 OceanBase.
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
#pragma once

#include "seekdb/geo/spatial_mbr.hpp"
#include <s2/s2latlng_rect.h>

namespace seekdb::geo::index_mbr {
enum class Relation { covers, intersects, covered_by };

inline bool valid(const Box &box, bool geographic)
{
  if (!finite(box) || box.ymin > box.ymax) return false;
  return geographic ? box.ymin >= -90 && box.ymax <= 90 &&
      box.xmin >= -180 && box.xmin <= 180 && box.xmax >= -180 && box.xmax <= 180
      : box.xmin <= box.xmax;
}

// Original ObSpatialMBR::filter orientations and geographic point tolerance.
// False return means invalid metadata; reject is then unspecified and MUST
// NOT be consumed. A valid true reject means skip the row, not pass it onward.
inline bool filter(const Box &row, const Box &query, bool geographic,
                   bool row_point, bool query_point, Relation relation, bool &reject)
{
  if (!valid(row, geographic) || !valid(query, geographic)) return false;
  if (geographic) {
    const S2LatLngRect left(S2LatLng::FromDegrees(row.ymin, row.xmin),
                           S2LatLng::FromDegrees(row.ymax, row.xmax));
    const S2LatLngRect right(S2LatLng::FromDegrees(query.ymin, query.xmin),
                            S2LatLng::FromDegrees(query.ymax, query.xmax));
    if (!left.is_valid() || !right.is_valid()) return false;
    if (row_point && query_point && left.ApproxEquals(right)) {
      reject = false;
      return true;
    }
    switch (relation) {
      case Relation::covers: reject = !right.Contains(left); return true;
      case Relation::intersects: reject = !left.Intersects(right); return true;
      case Relation::covered_by: reject = !left.Contains(right); return true;
    }
  } else {
    const auto contains = [](const Box &outer, const Box &inner) {
      return outer.xmin <= inner.xmin && outer.xmax >= inner.xmax &&
             outer.ymin <= inner.ymin && outer.ymax >= inner.ymax;
    };
    switch (relation) {
      case Relation::covers: reject = !contains(query, row); return true;
      case Relation::intersects:
        reject = row.xmin > query.xmax || row.xmax < query.xmin ||
                 row.ymin > query.ymax || row.ymax < query.ymin;
        return true;
      case Relation::covered_by: reject = !contains(row, query); return true;
    }
  }
  return false;
}
} // namespace seekdb::geo::index_mbr
