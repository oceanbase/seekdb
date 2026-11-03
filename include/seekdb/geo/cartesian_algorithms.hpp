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

#include <boost/geometry.hpp>

// Source-level algorithm sharing, not part of the installed plugin C ABI.
// Extracted from the Cartesian branches of src/share/geo/ob_geo_func_*.
// Both the legacy WKB/tree adapters and the plugin's owning models instantiate
// these functions with the project's Boost headers. No server allocator,
// session, SQL expression or dynamically resolved core symbol is required.
namespace seekdb {
namespace geo {
namespace cartesian {

template <typename Geometry>
bool is_valid(const Geometry &geometry, boost::geometry::validity_failure_type &reason)
{
  return boost::geometry::is_valid(geometry, reason);
}

template <typename Geometry>
bool is_valid(const Geometry &geometry)
{
  boost::geometry::validity_failure_type reason;
  return seekdb::geo::cartesian::is_valid(geometry, reason);
}

template <typename Geometry>
void correct(Geometry &geometry)
{
  boost::geometry::correct(geometry);
}

template <typename Left, typename Right>
bool equals(const Left &left, const Right &right)
{
  return boost::geometry::equals(left, right);
}

template <typename Left, typename Right>
bool intersects(const Left &left, const Right &right)
{
  return boost::geometry::intersects(left, right);
}

template <typename Left, typename Right>
double distance(const Left &left, const Right &right)
{
  return boost::geometry::distance(left, right);
}

template <typename Left, typename Right, typename Output>
void union_(const Left &left, const Right &right, Output &output)
{
  boost::geometry::union_(left, right, output);
}

template <typename Geometry>
double area(const Geometry &geometry)
{
  return boost::geometry::area(geometry);
}

template <typename Geometry>
double length(const Geometry &geometry)
{
  return boost::geometry::length(geometry);
}

template <typename Geometry, typename Point>
void centroid(const Geometry &geometry, Point &point)
{
  boost::geometry::centroid(geometry, point);
}

template <typename Geometry, typename Output, typename Distance, typename Side,
          typename Join, typename End, typename Point>
void buffer(const Geometry &geometry, Output &output, const Distance &distance,
            const Side &side, const Join &join, const End &end, const Point &point)
{
  boost::geometry::buffer(geometry, output, distance, side, join, end, point);
}

} // namespace cartesian
} // namespace geo
} // namespace seekdb
