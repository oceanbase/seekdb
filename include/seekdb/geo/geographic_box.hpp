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

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>

namespace seekdb { namespace geo { namespace spherical {

// Source-level extraction of ObGeoBoxUtil. This box bounds Cartesian unit-
// sphere coordinates, NOT longitude/latitude min/max or a planar envelope.
struct Vector2 { double x, y; };
struct Vector3 { double x, y, z; };
struct Box { double xmin, xmax, ymin, ymax, zmin, zmax; };
constexpr double pi = 3.14159265358979323846;
constexpr double tolerance = 5e-14;
constexpr int32_t world_mercator = 999000;
constexpr int32_t north_utm = 999001;
constexpr int32_t north_lambert = 999061;
constexpr int32_t south_utm = 999101;
constexpr int32_t south_lambert = 999161;
constexpr int32_t laea_start = 999163;

inline bool equal(double a, double b) { return std::abs(a - b) <= tolerance; }
template <typename A, typename B> double dot(const A &a, const B &b)
{ return a.x * b.x + a.y * b.y + a.z * b.z; }
template <typename A, typename B> bool same(const A &a, const B &b)
{ return equal(a.x, b.x) && equal(a.y, b.y) && equal(a.z, b.z); }
template <typename A, typename B> bool opposite(const A &a, const B &b)
{ return equal(a.x, -b.x) && equal(a.y, -b.y) && equal(a.z, -b.z); }
template <typename Point> void normalize3(Point &p)
{
  const double length = std::sqrt(p.x * p.x + p.y * p.y + p.z * p.z);
  if (equal(length, 0)) p.x = p.y = p.z = 0;
  else { p.x /= length; p.y /= length; p.z /= length; }
}
template <typename Point> void normalize2(Point &p)
{
  const double length = std::sqrt(p.x * p.x + p.y * p.y);
  if (equal(length, 0)) p.x = p.y = 0;
  else { p.x /= length; p.y /= length; }
}
template <typename A, typename B> Vector3 normal(const A &a, const B &b)
{
  Vector3 temporary{b.x, b.y, b.z};
  const double product = dot(a, b);
  if (product < 0) {
    temporary = {a.x + b.x, a.y + b.y, a.z + b.z};
    normalize3(temporary);
  } else if (product > 0.95) {
    temporary = {a.x - b.x, a.y - b.y, a.z - b.z};
    normalize3(temporary);
  }
  Vector3 result{a.y * temporary.z - a.z * temporary.y,
                 a.z * temporary.x - a.x * temporary.z,
                 a.x * temporary.y - a.y * temporary.x};
  normalize3(result);
  return result;
}
inline Vector3 from_degrees(double longitude, double latitude)
{
  const double x = pi * longitude / 180, y = pi * latitude / 180;
  const double cos_y = std::cos(y);
  return {std::cos(x) * cos_y, std::sin(x) * cos_y, std::sin(y)};
}
template <typename Point, typename Bounds> void point_box(const Point &p, Bounds &box)
{
  box.xmin = box.xmax = p.x; box.ymin = box.ymax = p.y; box.zmin = box.zmax = p.z;
}
template <typename Point, typename Bounds> void include_point(const Point &p, Bounds &box)
{
  box.xmin = std::min(box.xmin, p.x); box.xmax = std::max(box.xmax, p.x);
  box.ymin = std::min(box.ymin, p.y); box.ymax = std::max(box.ymax, p.y);
  box.zmin = std::min(box.zmin, p.z); box.zmax = std::max(box.zmax, p.z);
}
template <typename A, typename B> void merge(const A &part, B &box)
{
  box.xmin = std::min(box.xmin, part.xmin); box.xmax = std::max(box.xmax, part.xmax);
  box.ymin = std::min(box.ymin, part.ymin); box.ymax = std::max(box.ymax, part.ymax);
  box.zmin = std::min(box.zmin, part.zmin); box.zmax = std::max(box.zmax, part.zmax);
}
inline int side(const Vector2 &a, const Vector2 &b, const Vector2 &p)
{
  const double d = (b.y - a.y) * (p.x - a.x) - (b.x - a.x) * (p.y - a.y);
  return d > 0 ? 1 : d < 0 ? -1 : 0;
}

// Original great-circle extrema construction, with the Z dot-product, same-
// point predicate, uninitialized 2D Y, and swallowed antipodal error corrected.
// False means no unique minor arc (antipodal endpoints). No partial publication.
template <typename Point, typename Bounds>
bool line_box(const Point &start, const Point &end, Bounds &box)
{
  if (opposite(start, end)) return false;
  Bounds result;
  point_box(start, result);
  include_point(end, result);
  if (!same(start, end)) {
    const auto perpendicular = normal(start, end);
    const auto tangent = normal(perpendicular, start);
    const Vector2 a{1, 0}, b{dot(start, end), dot(end, tangent)}, origin{0, 0};
    const int location = side(a, b, origin);
    const Vector3 axes[] = {{1,0,0},{-1,0,0},{0,1,0},{0,-1,0},{0,0,1},{0,0,-1}};
    for (const auto &axis : axes) {
      Vector2 projected{dot(axis, start), dot(axis, tangent)};
      normalize2(projected);
      if (side(a, b, projected) != location) {
        const Vector3 point{projected.x * start.x + projected.y * tangent.x,
                            projected.x * start.y + projected.y * tangent.y,
                            projected.x * start.z + projected.y * tangent.z};
        include_point(point, result);
      }
    }
  }
  box = result;
  return true;
}

inline void set_poles(double xmin, double xmax, double ymin, double ymax, double &zmin, double &zmax)
{
  if (xmin < 0 && xmax > 0 && ymin < 0 && ymax > 0) {
    if (zmin > 0 && zmax > 0) zmax = 1;
    else if (zmin < 0 && zmax < 0) zmin = -1;
    else { zmin = -1; zmax = 1; }
  }
}
template <typename Bounds> void check_poles(Bounds &box)
{
  set_poles(box.xmin, box.xmax, box.ymin, box.ymax, box.zmin, box.zmax);
  set_poles(box.xmin, box.xmax, box.zmin, box.zmax, box.ymin, box.ymax);
  set_poles(box.ymin, box.ymax, box.zmin, box.zmax, box.xmin, box.xmax);
}
inline double longitude(double value)
{
  if (value > 360) value = std::remainder(value, 360);
  else if (value < -360) value = std::remainder(value, -360);
  if (value > 180) value -= 360;
  else if (value < -180) value += 360;
  if (value == -180) value = 180;
  else if (value == -360) value = 0;
  return value;
}
inline double latitude(double value)
{
  if (value > 360) value = std::remainder(value, 360);
  else if (value < -360) value = std::remainder(value, -360);
  if (value > 180) value = 180 - value;
  else if (value < -180) value = -180 - value;
  if (value > 90) value = 180 - value;
  else if (value < -90) value = -180 - value;
  return value;
}
template <typename Bounds> Vector3 corner(const Bounds &box, unsigned i)
{
  return {i / 4 ? box.xmax : box.xmin, (i % 4) / 2 ? box.ymax : box.ymin,
          i % 2 ? box.zmax : box.zmin};
}
inline double unit_range(double value) { return std::max(-1.0, std::min(1.0, value)); }
template <typename Bounds> Vector2 center(const Bounds &box)
{
  Vector3 result{0,0,0};
  for (unsigned i = 0; i < 8; ++i) {
    auto p = corner(box, i); normalize3(p);
    result.x += p.x; result.y += p.y; result.z += p.z;
  }
  result.x /= 8; result.y /= 8; result.z /= 8;
  normalize3(result);
  return {longitude(180 * std::atan2(result.y, result.x) / pi),
          latitude(180 * std::asin(unit_range(result.z)) / pi)};
}
template <typename Bounds> double angular_height(const Bounds &box)
{
  double minimum = 1, maximum = -1;
  for (unsigned i = 0; i < 8; ++i) {
    auto p = corner(box, i); normalize3(p);
    minimum = std::min(minimum, p.z); maximum = std::max(maximum, p.z);
  }
  return std::asin(unit_range(maximum)) - std::asin(unit_range(minimum));
}
template <typename Bounds> double angular_width(const Bounds &box)
{
  Vector2 current{box.xmin, box.ymin};
  // A point exactly on the axis has zero longitudinal extent. A box with an
  // axis corner but nonzero XY extent is conservatively a half-world span.
  if (current.x == 0 && current.y == 0) {
    return box.xmax == 0 && box.ymax == 0 ? 0 : pi;
  }
  const auto normalize_xy = [](Vector2 &p) {
    const double length = std::sqrt(p.x * p.x + p.y * p.y);
    p.x /= length; p.y /= length;
  };
  normalize_xy(current);
  double maximum = 0;
  for (unsigned pass = 0; pass < 2; ++pass) {
    maximum = -1;
    Vector2 furthest = current;
    for (unsigned i = 0; i < 4; ++i) {
      Vector2 p{i / 2 ? box.xmax : box.xmin, i % 2 ? box.ymax : box.ymin};
      if (p.x == 0 && p.y == 0) return pi;
      normalize_xy(p);
      const double angle = std::acos(unit_range(p.x * current.x + p.y * current.y));
      if (angle > maximum) { maximum = angle; furthest = p; }
    }
    current = furthest;
  }
  return maximum;
}

// ObGeoExprUtils::get_box_bestsrid's branch order and private PG SRID space.
inline int32_t select_srid(double x, double y, double width, double height)
{
  if (height < 45 && y > 70) return north_lambert;
  if (height < 45 && y <= -70) return south_lambert;
  if (width < 6) {
    const auto zone = std::min(uint32_t(std::floor((x + 180) / 6)), uint32_t(59));
    return (y < 0 ? south_utm : north_utm) + zone;
  }
  if (height < 25) {
    int32_t x_zone = -1, y_zone = static_cast<int32_t>(std::floor(y / 30)) + 3;
    if (width < 30 && (y_zone == 2 || y_zone == 3)) x_zone = static_cast<int32_t>(std::floor(x / 30)) + 6;
    else if (width < 45 && (y_zone == 1 || y_zone == 4)) x_zone = static_cast<int32_t>(std::floor(x / 45)) + 4;
    else if (width < 90 && (y_zone == 0 || y_zone == 5)) x_zone = static_cast<int32_t>(std::floor(x / 90)) + 2;
    if (x_zone != -1) return laea_start + 20 * y_zone + x_zone;
  }
  return world_mercator;
}
template <typename Bounds> int32_t best_srid(const Bounds &box)
{
  const auto middle = center(box);
  return select_srid(middle.x, middle.y, 180 * angular_width(box) / pi,
                     180 * angular_height(box) / pi);
}

}}} // namespace seekdb::geo::spherical
