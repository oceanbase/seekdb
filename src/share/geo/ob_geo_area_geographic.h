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

#pragma once

#include <boost/geometry/core/radian_access.hpp>
#include <boost/geometry/core/radius.hpp>
#include <boost/geometry/srs/spheroid.hpp>
#include <boost/geometry/strategies/area.hpp>
#include <boost/geometry/util/math.hpp>
#include "share/geo/geodesic.h"

namespace oceanbase
{
namespace common
{

// Boost's ring traversal and correct() share this signed, ellipsoidal area
// strategy. GeographicLib computes each edge's area without subtracting
// independently rounded azimuths and accumulates with compensation, including
// the meridian crossings needed for rings enclosing a pole.
class ObGeoAreaGeographic
{
public:
  template <typename Geometry>
  struct result_type { typedef double type; };

  template <typename Geometry>
  struct state
  {
    state() { geodesic::geod_polygon_init(&polygon, false); }
    geodesic::geod_polygon polygon;
  };

  explicit ObGeoAreaGeographic(
      const boost::geometry::srs::spheroid<double> &spheroid =
          boost::geometry::srs::spheroid<double>())
  {
    const double a = boost::geometry::get_radius<0>(spheroid);
    const double b = boost::geometry::get_radius<2>(spheroid);
    geodesic::geod_init(&geodesic_, a, (a - b) / a);
  }

  template <typename Point, typename Geometry>
  void apply(const Point &p1, const Point &, state<Geometry> &st) const
  {
    // Boost visits every edge, including the closing one; add its first point
    // once. Polygon compute closes the ring, so no duplicate endpoint is needed.
    const double r2d = boost::geometry::math::r2d<double>();
    geodesic::geod_polygon_addpoint(
        &geodesic_, &st.polygon,
        boost::geometry::get_as_radian<1>(p1) * r2d,
        boost::geometry::get_as_radian<0>(p1) * r2d);
  }

  template <typename Geometry>
  double result(const state<Geometry> &st) const
  {
    double area = 0;
    // Boost traverses counterclockwise rings in reverse. Keep signed clockwise
    // area here so its polygon/inner-ring dispatch retains the proper signs.
    geodesic::geod_polygon_compute(&geodesic_, &st.polygon, true, true, &area, nullptr);
    return area;
  }

private:
  geodesic::geod_geodesic geodesic_;
};

template <typename = void>
using ObGeoAreaStrategy = ObGeoAreaGeographic;

} // namespace common
} // namespace oceanbase
