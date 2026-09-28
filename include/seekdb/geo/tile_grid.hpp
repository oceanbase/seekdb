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

#include <cmath>
#include <cstddef>

namespace seekdb { namespace geo { namespace cartesian {

// Shared with ObGeoAffineVisitor. Read both coordinates before writing either.
inline void affine_xy(double &x, double &y, double xx, double xy, double yx,
                      double yy, double x_offset, double y_offset)
{
  const double old_x = x, old_y = y;
  x = xx * old_x + xy * old_y + x_offset;
  y = yx * old_x + yy * old_y + y_offset;
}

inline double snap_coordinate(double value, double origin, double size, bool use_floor)
{
  if (size > 0) {
    const double scaled = (value - origin) / size;
    value = (use_floor ? std::floor(scaled) : std::rint(scaled)) * size + origin;
  }
  return value;
}

// ObGeoGridVisitor's adjacent-only deduplication; not global point uniqueness.
inline bool duplicate_grid_point(double x, double y, double &last_x, double &last_y)
{
  if ((std::isnan(last_x) && std::isnan(last_y)) ||
      !(std::abs(x - last_x) <= 1e-12) || !(std::abs(y - last_y) <= 1e-12)) {
    last_x = x;
    last_y = y;
    return false;
  }
  return true;
}

// The original zero-tolerance O(n) simplifier. Callback access keeps allocator
// and point representation out of the algorithm. A reversal is not collinear
// redundancy: the middle point must lie between the retained and next points.
template <typename X, typename Y, typename Copy>
std::size_t simplify_collinear(std::size_t size, X x, Y y, Copy copy)
{
  if (size < 3) return size;
  std::size_t kept = 0;
  for (std::size_t i = 1; i + 1 < size; ++i) {
    const double ba_x = x(i + 1) - x(kept), ba_y = y(i + 1) - y(kept);
    const double ca_x = x(i) - x(kept), ca_y = y(i) - y(kept);
    const double length = ba_x * ba_x + ba_y * ba_y;
    const double dot = ca_x * ba_x + ca_y * ba_y;
    const double cross = ca_x * ba_y - ca_y * ba_x;
    if (dot < 0 || dot > length || cross != 0) {
      ++kept;
      if (kept != i) copy(kept, i);
    }
  }
  ++kept;
  if (kept != size - 1) copy(kept, size - 1);
  return kept + 1;
}

}}} // namespace seekdb::geo::cartesian
