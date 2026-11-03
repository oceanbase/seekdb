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

#include <boost/geometry/core/access.hpp>
#include <cmath>

// Allocation-independent extraction of ObGeoInteriorPointVisitor. The core
// retains its arena/ObArray and the plugin uses owning vectors. Callbacks
// return false on allocation/admission failure; no server types cross here.
namespace seekdb {
namespace geo {
namespace cartesian {

inline double surface_midpoint(double lo, double hi)
{
  // Preserve the original expression for ordinary values while avoiding an
  // overflowing difference between two finite endpoints of opposite sign.
  const double delta = hi - lo;
  return std::isfinite(delta) ? lo + delta / 2 : lo / 2 + hi / 2;
}

template <typename Ring>
void surface_narrow_y(const Ring &ring, double centre, double &hi, double &lo)
{
  for (auto it = ring.begin(); it != ring.end(); ++it) {
    const double y = boost::geometry::get<1>(*it);
    if (y > lo && y <= centre) lo = y;
    else if (y > centre && y < hi) hi = y;
  }
}

template <typename Ring, typename InnerRings>
bool surface_scanline_y(const Ring &outer, const InnerRings &holes, double &y)
{
  auto it = outer.begin();
  if (it == outer.end()) return false;
  double lo = boost::geometry::get<1>(*it), hi = lo;
  for (++it; it != outer.end(); ++it) {
    const double value = boost::geometry::get<1>(*it);
    if (value < lo) lo = value;
    if (value > hi) hi = value;
  }
  const double centre = surface_midpoint(lo, hi);
  surface_narrow_y(outer, centre, hi, lo);
  for (auto ring = holes.begin(); ring != holes.end(); ++ring) {
    surface_narrow_y(*ring, centre, hi, lo);
  }
  y = surface_midpoint(lo, hi);
  return std::isfinite(y);
}

inline bool surface_crosses_y(double start, double end, double y)
{
  // Count vertices on the scanline once, ignoring horizontal edges.
  return start != end && !((start > y && end > y) || (start < y && end < y) ||
                          (start == y && end < y) || (end == y && start < y));
}

template <typename Ring, typename Append>
bool surface_ring_crossings(const Ring &ring, double y, Append append)
{
  auto first = ring.begin();
  if (first == ring.end()) return true;
  auto second = first;
  for (++second; second != ring.end(); ++first, ++second) {
    const double y0 = boost::geometry::get<1>(*first), y1 = boost::geometry::get<1>(*second);
    if (surface_crosses_y(y0, y1, y)) {
      const double x0 = boost::geometry::get<0>(*first), x1 = boost::geometry::get<0>(*second);
      const double fraction = (y - y0) / (y1 - y0);
      const double x = x0 == x1 ? x0 : x0 + fraction * (x1 - x0);
      if (!std::isfinite(x) || !append(x)) return false;
    }
  }
  return true;
}

template <typename Iterator>
bool surface_widest_interval(Iterator begin, Iterator end, double &width, double &x)
{
  bool improved = false;
  // Caller checks even cardinality and sorts the crossing coordinates.
  while (begin != end) {
    const double lo = *begin++;
    const double hi = *begin++;
    const double candidate = hi - lo;
    if (candidate != 0 && candidate > width) {
      width = candidate;
      x = surface_midpoint(lo, hi);
      improved = true;
    }
  }
  return improved;
}

inline double surface_distance(double x, double y, double cx, double cy)
{
  const double dx = cx - x, dy = cy - y;
  return std::sqrt(dx * dx + dy * dy);
}

template <typename Line, typename Vertex, typename Endpoint>
bool surface_line_candidates(const Line &line, bool has_centroid, Vertex vertex, Endpoint endpoint)
{
  auto it = line.begin();
  if (it == line.end()) return true;
  const auto emit = [](const auto &point, auto &callback) {
    return callback(boost::geometry::get<0>(point), boost::geometry::get<1>(point));
  };
  if (!has_centroid) {
    if (!emit(*it, vertex)) return false;
    auto last = it;
    for (++it; it != line.end(); ++it) last = it;
    return emit(*last, vertex);
  }
  if (line.size() <= 2) return emit(*it, endpoint);
  // Interior vertices take priority over endpoints, just as in the visitor.
  ++it;
  auto next = it;
  for (++next; next != line.end(); ++it, ++next) if (!emit(*it, vertex)) return false;
  return true;
}

} // namespace cartesian
} // namespace geo
} // namespace seekdb
