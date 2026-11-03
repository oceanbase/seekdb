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

namespace spherical = seekdb::geo::spherical;

static spherical::Vector3 best_srid_point(const Point &point)
{
  if (point.x < -180 || point.x > 180 || point.y < -90 || point.y > 90) throw CartesianInputError{};
  return spherical::from_degrees(point.x, point.y);
}

static spherical::Box best_srid_line(const std::vector<Point> &points)
{
  if (points.size() < 2) throw CartesianInputError{};
  auto previous = best_srid_point(points[0]);
  spherical::Box box{};
  for (size_t i = 1; i < points.size(); ++i) {
    const auto next = best_srid_point(points[i]);
    spherical::Box segment{};
    if (!spherical::line_box(previous, next, segment)) throw CartesianInputError{};
    if (i == 1) box = segment;
    else spherical::merge(segment, box);
    previous = next;
  }
  return box;
}

// Geometry ownership adapter for ObGeoFuncBox's geographic dispatch. Lines
// include great-circle interior extrema; polygons include rings and poles.
static spherical::Box best_srid_bounds(const Geometry &input)
{
  spherical::Box box{};
  if (input.type == 1) {
    spherical::point_box(best_srid_point(input.points[0]), box);
  } else if (input.type == 2) {
    box = best_srid_line(input.points);
  } else if (input.type == 3) {
    if (input.rings.empty()) throw CartesianInputError{};
    for (size_t i = 0; i < input.rings.size(); ++i) {
      const auto ring = best_srid_line(input.rings[i]);
      if (i == 0) box = ring;
      else spherical::merge(ring, box);
    }
    spherical::check_poles(box);
  } else {
    if (input.children.empty()) throw CartesianInputError{};
    for (size_t i = 0; i < input.children.size(); ++i) {
      const auto part = best_srid_bounds(input.children[i]);
      if (i == 0) box = part;
      else spherical::merge(part, box);
    }
  }
  return box;
}

static std::optional<spherical::Box> best_srid_input(const Geometry &input)
{
  // Same temporary known-SRS and structural admission as GeoHash, but unlike
  // GeoHash this operation requires geographic input for nonempty geometries.
  validate_geohash_input(input, input.srid);
  if (tile_empty(input)) return std::nullopt;
  if (input.srid != 4326) throw CartesianInputError{};
  return best_srid_bounds(input);
}

static int32_t geometry_best_srid(const Geometry &first, const Geometry *second = nullptr)
{
  auto box = best_srid_input(first);
  if (second != nullptr) {
    auto other = best_srid_input(*second);
    if (!box) box = other;
    else if (other) spherical::merge(*other, *box);
  }
  return box ? spherical::best_srid(*box) : spherical::world_mercator;
}
