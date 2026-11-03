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

// Private representation/ownership adapter for the original clipping visitor.
struct PluginClipFactory {
  using Point = CartesianPoint;
  using Line = CartesianLine;
  using Ring = CartesianPolygon::ring_type;
  using Polygon = CartesianPolygon;
  using Lines = CartesianLines;
  using Polygons = CartesianPolygons;
  static constexpr int invalid_argument = 1;
  static constexpr int invalid_geometry = 2;
  Line line() const { return {}; }
  Ring ring() const { return {}; }
  Polygon polygon() const { return {}; }
  Lines lines() const { return {}; }
  Polygons polygons() const { return {}; }
  int erase(Lines &lines, size_t index) const { lines.erase(lines.begin() + index); return 0; }
  int within(const Point &point, const Polygon &polygon, bool &value) const
  { value = bg::within(point, polygon); return 0; }
  int covered_by(const Ring &ring, const Polygon &polygon, bool &value) const
  { value = bg::covered_by(Line(ring.begin(), ring.end()), polygon); return 0; }
};

static int clip_components(const Geometry &geometry, const cartesian::ClipBox &box,
                           std::vector<Geometry> &parts, bool correct_winding)
{
  if (geometry.type >= 4) {
    for (const auto &child : geometry.children) {
      const int status = clip_components(child, box, parts, correct_winding);
      if (status != 0) return status;
    }
    return 0;
  }
  if (geometry.type == 1) {
    if (box.position(geometry.points[0].x, geometry.points[0].y) == cartesian::BOX_INSIDE) parts.push_back(geometry);
    return 0;
  }
  PluginClipFactory factory;
  cartesian::BoxClipper<PluginClipFactory> clipper(box, factory);
  if (geometry.type == 2) {
    CartesianLine line;
    for (const auto &point : geometry.points) line.push_back(cartesian_point(point));
    CartesianLines lines;
    bool inside = false;
    const int status = clipper.clip_line(line, lines, inside);
    if (status != 0) return status;
    for (const auto &clipped : lines) {
      if (clipped.size() < 2) return PluginClipFactory::invalid_geometry;
      Geometry part;
      part.type = 2;
      part.srid = geometry.srid;
      for (const auto &point : clipped) part.points.push_back({bg::get<0>(point), bg::get<1>(point), 0});
      parts.push_back(std::move(part));
    }
  } else {
    CartesianPolygons polygons;
    const int status = clipper.clip_polygon(cartesian_polygon(geometry, false, false, correct_winding), polygons);
    if (status != 0) return status;
    auto converted = from_cartesian(polygons, geometry.srid);
    if (converted.type == 3) parts.push_back(std::move(converted));
    else for (auto &part : converted.children) parts.push_back(std::move(part));
  }
  return 0;
}

static std::optional<Geometry> clip_normalized_geometry_box(const Geometry &geometry, const cartesian::ClipBox &clip,
                                                         bool correct_winding = true)
{
  // Match ObGeoBoxUtil: contained input survives even on the boundary; only
  // the partial-overlap visitor uses strict interior point/edge rules.
  Box box;
  if (!bounds(geometry, box)) return geometry;
  if (clip.xmax >= box.max_x && clip.ymax >= box.max_y && clip.xmin <= box.min_x && clip.ymin <= box.min_y) return geometry;
  if (box.max_x < clip.xmin || box.max_y < clip.ymin || box.min_x > clip.xmax || box.min_y > clip.ymax) return empty_geometry(geometry.srid);
  // Original ObGeoBoxUtil::is_box_valid tolerance, after fast paths above.
  if (std::abs(clip.xmax - clip.xmin) <= 5e-14 || std::abs(clip.ymax - clip.ymin) <= 5e-14) return std::nullopt;
  std::vector<Geometry> parts;
  const int status = clip_components(geometry, clip, parts, correct_winding);
  if (status == PluginClipFactory::invalid_geometry) return std::nullopt;
  if (status != 0) throw CartesianInputError{};
  if (parts.size() == 1) return std::move(parts[0]);
  Geometry result = empty_geometry(geometry.srid);
  if (!parts.empty()) {
    const uint32_t type = parts[0].type;
    if (std::all_of(parts.begin(), parts.end(), [&](const auto &part) { return part.type == type; })) result.type = type + 3;
    result.children = std::move(parts);
  }
  return result;
}

static std::optional<Geometry> clip_by_box(const Geometry &geometry, const Geometry &box_geometry)
{
  // The second argument supplies coordinate bounds, not a CRS transform. The
  // legacy expression does not require its SRID to equal the first argument.
  const auto normalized_input = correct_geometry(geometry);
  const auto normalized_box = correct_geometry(box_geometry);
  Box box;
  if (!bounds(normalized_box, box)) return std::nullopt;
  return clip_normalized_geometry_box(normalized_input, {box.min_x, box.min_y, box.max_x, box.max_y});
}
