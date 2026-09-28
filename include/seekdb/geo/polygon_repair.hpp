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

#include "cartesian_algorithms.hpp"
#include <utility>

namespace seekdb {
namespace geo {
namespace cartesian {

// Extracted from ObGeoFuncDissolvePolygon. The caller owns the temporary copy
// and output storage (core arena or plugin containers). Input is not modified.
template <typename Polygon, typename Output>
void dissolve_invalid_polygon(const Polygon &polygon, Polygon &reversed, Output &output)
{
  namespace bg = boost::geometry;
  if (bg::intersects(polygon)) {
    bg::reverse(reversed);
    bg::sym_difference(polygon, reversed, output);
  } else {
    bg::intersection(polygon, polygon, output);
    if (bg::is_empty(output) && !bg::is_empty(polygon)) {
      bg::reverse(reversed);
      bg::intersection(reversed, reversed, output);
    }
    bg::correct(output);
  }
}

// ObGeoExprUtils::make_valid_polygon_inner's ring classification and overlay
// order. Handle may be an arena pointer or an optional owning geometry.
// Callbacks return zero on success and retain their native error code otherwise.
// In particular an exterior "hole" becomes another shell; a crossing hole is
// combined by symmetric difference, not simply clipped away.
template <typename Handle, typename HoleAt, typename Intersects, typename Merge, typename SymDifference>
int repair_polygon_holes(const Handle &shell, unsigned long ring_count, HoleAt hole_at,
                         Intersects intersects, Merge merge, SymDifference sym_difference,
                         Handle &result)
{
  Handle holes{}, shells{};
  for (unsigned long i = 0; i < ring_count; ++i) {
    Handle hole{};
    int status = hole_at(i, hole);
    if (status != 0) return status;
    bool overlapping = false;
    status = intersects(hole, shell, overlapping);
    if (status != 0) return status;
    Handle &group = overlapping ? holes : shells;
    if (!group) group = std::move(hole);
    else if ((status = merge(hole, group)) != 0) return status;
  }
  Handle shape{};
  if (!holes) shape = shell;
  else {
    const int status = sym_difference(shell, holes, shape);
    if (status != 0) return status;
  }
  if (shells) {
    const int status = merge(shells, shape);
    if (status != 0) return status;
  }
  result = std::move(shape);
  return 0;
}

} // namespace cartesian
} // namespace geo
} // namespace seekdb
