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

// Owns the same stage ordering as ObExprPrivSTAsMVTGeom. Numeric algorithms
// come from the original visitors; geometry ownership stays entirely in DSO.
static bool tile_empty(const Geometry &geometry)
{
  if (geometry.type <= 2) return geometry.points.empty();
  if (geometry.type == 3) return geometry.rings.empty() || geometry.rings[0].empty();
  return std::all_of(geometry.children.begin(), geometry.children.end(), tile_empty);
}

static void validate_tile_input(const Geometry &geometry, uint32_t srid)
{
  require_cartesian(geometry, srid);
  if (geometry.type == 3) {
    // GEO_CHECK_RING, but not GEO_CORRECT: preserve winding before Y inversion.
    (void)cartesian_polygon(geometry, false, false, false);
  } else if (geometry.type >= 4 && geometry.type <= 7) {
    for (const auto &child : geometry.children) {
      if (geometry.type != 7 && child.type != geometry.type - 3) throw CartesianInputError{};
      validate_tile_input(child, srid);
    }
  } else {
    (void)to_cartesian(geometry, false);
  }
}

static void tile_simplify_multi(Geometry &geometry)
{
  if (geometry.type >= 4 && geometry.type <= 6 && geometry.children.size() == 1) {
    // Move through a temporary; the RHS must not be a subobject of the LHS.
    Geometry child = std::move(geometry.children.front());
    geometry = std::move(child);
  }
}

static uint32_t tile_basic_type(const Geometry &geometry)
{
  if (geometry.type <= 3) return geometry.type;
  if (geometry.type <= 6) return geometry.type - 3;
  uint32_t result = 0;
  for (const auto &child : geometry.children) result = std::max(result, tile_basic_type(child));
  return result;
}

static void tile_collect(const Geometry &geometry, uint32_t type, std::vector<Geometry> &parts)
{
  if (geometry.type == type && !tile_empty(geometry)) parts.push_back(geometry);
  else if (geometry.type >= 4) {
    for (const auto &child : geometry.children) tile_collect(child, type, parts);
  }
}

template <typename Operation>
static void tile_points(Geometry &geometry, Operation operation)
{
  for (auto &point : geometry.points) operation(point);
  for (auto &ring : geometry.rings) for (auto &point : ring) operation(point);
  for (auto &child : geometry.children) tile_points(child, operation);
}

static void tile_grid_sequence(std::vector<Point> &points, size_t minimum, bool use_floor)
{
  double last_x = std::numeric_limits<double>::quiet_NaN(), last_y = last_x;
  size_t kept = 0;
  for (const auto &point : points) {
    const double x = cartesian::snap_coordinate(point.x, 0, 1, use_floor);
    const double y = cartesian::snap_coordinate(point.y, 0, 1, use_floor);
    if (!std::isfinite(x) || !std::isfinite(y)) throw CartesianInputError{};
    if (!cartesian::duplicate_grid_point(x, y, last_x, last_y)) points[kept++] = {x, y, 0};
  }
  points.resize(kept < minimum ? 0 : kept);
}

static void tile_simplify_sequence(std::vector<Point> &points, size_t minimum)
{
  const size_t kept = cartesian::simplify_collinear(points.size(),
      [&](size_t i) { return points[i].x; }, [&](size_t i) { return points[i].y; },
      [&](size_t dst, size_t src) { points[dst] = points[src]; });
  points.resize(kept < minimum ? 0 : kept);
}

// Container traversal follows the grid and zero-tolerance simplify visitors.
// MultiPoint only deduplicates adjacent points and is not line-simplified.
static void tile_reduce(Geometry &geometry, bool grid, bool use_floor = false)
{
  const auto reduce = [&](std::vector<Point> &points, size_t minimum) {
    if (grid) tile_grid_sequence(points, minimum, use_floor);
    else tile_simplify_sequence(points, minimum);
  };
  if (geometry.type == 1) {
    if (grid) reduce(geometry.points, 1);
  } else if (geometry.type == 2) {
    reduce(geometry.points, 2);
  } else if (geometry.type == 3) {
    for (auto &ring : geometry.rings) reduce(ring, 4);
    if (!geometry.rings.empty() && geometry.rings[0].empty()) geometry.rings.clear();
    else geometry.rings.erase(std::remove_if(geometry.rings.begin(), geometry.rings.end(),
        [](const auto &ring) { return ring.empty(); }), geometry.rings.end());
  } else if (geometry.type == 4) {
    if (grid) {
      std::vector<Point> points;
      for (const auto &child : geometry.children) points.push_back(child.points[0]);
      reduce(points, 1);
      geometry.children.resize(points.size());
      for (size_t i = 0; i < points.size(); ++i) geometry.children[i].points[0] = points[i];
    }
  } else {
    for (auto &child : geometry.children) tile_reduce(child, grid, use_floor);
    geometry.children.erase(std::remove_if(geometry.children.begin(), geometry.children.end(), tile_empty),
                            geometry.children.end());
  }
}

static Geometry tile_repair_polygons(const Geometry &geometry)
{
  // Unlike ST_MakeValid, MVT always runs make_valid_polygon and does not restore
  // a collapsed input when dissolution returns empty.
  CartesianPolygons repaired;
  bool have_result = false;
  const auto append = [&](const Geometry &part) {
    auto polygons = repair_polygon_model(cartesian_polygon(part, false));
    if (!have_result) { repaired = std::move(polygons); have_result = true; }
    else if (!bg::is_empty(polygons)) {
      CartesianPolygons merged;
      cartesian::union_(polygons, repaired, merged);
      repaired = std::move(merged);
    }
  };
  if (geometry.type == 3) append(geometry);
  else if (geometry.type == 6) for (const auto &child : geometry.children) append(child);
  else throw CartesianInputError{};
  return from_cartesian(repaired, geometry.srid);
}

static void validate_tile_controls(double extent, double buffer)
{
  if (!std::isfinite(extent) || extent <= 0 || extent > INT32_MAX || std::trunc(extent) != extent ||
      !std::isfinite(buffer) || buffer < 0 || buffer > INT32_MAX || std::trunc(buffer) != buffer) {
    throw CartesianInputError{};
  }
}

static std::optional<Geometry> as_mvt_geometry(const Geometry &input, const Geometry &tile_bounds,
                                             double extent, double buffer, bool clip)
{
  validate_tile_input(input, input.srid);
  validate_tile_input(tile_bounds, tile_bounds.srid);
  validate_tile_controls(extent, buffer);
  if (tile_empty(input)) return std::nullopt;
  Box box;
  if (!bounds(tile_bounds, box)) throw CartesianInputError{};
  const double width = box.max_x - box.min_x, height = box.max_y - box.min_y;
  if (!std::isfinite(width) || !std::isfinite(height) || width <= 0 || height <= 0) throw CartesianInputError{};
  if (input.type == 2 || input.type == 5) {
    Box fast;
    if (bounds(input, fast) && fast.max_x - fast.min_x < width / extent / 2 &&
        fast.max_y - fast.min_y < height / extent / 2) return std::nullopt;
  }
  Geometry result = input;
  const uint32_t basic_type = tile_basic_type(input);
  if (input.type == 7) {
    result.type = basic_type + 3;
    result.children.clear();
    tile_collect(input, basic_type, result.children);
  }
  tile_simplify_multi(result);
  if (tile_empty(result)) return std::nullopt;
  const double xx = extent / width, yy = -extent / height;
  const double x_offset = -box.min_x * xx, y_offset = -box.max_y * yy;
  if (!std::isfinite(xx) || !std::isfinite(yy) || !std::isfinite(x_offset) || !std::isfinite(y_offset)) {
    throw CartesianInputError{};
  }
  tile_points(result, [&](Point &point) {
    cartesian::affine_xy(point.x, point.y, xx, 0, 0, yy, x_offset, y_offset);
    if (!std::isfinite(point.x) || !std::isfinite(point.y)) throw CartesianInputError{};
  });
  tile_reduce(result, true);
  tile_simplify_multi(result);
  tile_reduce(result, false);
  if (tile_empty(result)) return std::nullopt;
  if (clip) {
    auto clipped = clip_normalized_geometry_box(result, {-buffer, -buffer, extent + buffer, extent + buffer}, false);
    if (!clipped || tile_empty(*clipped)) return std::nullopt;
    result = std::move(*clipped);
  }
  if (basic_type == 3) {
    result = tile_repair_polygons(result);
    tile_reduce(result, true, true);
  } else if (clip) {
    tile_reduce(result, true);
  }
  tile_simplify_multi(result);
  if (tile_empty(result)) return std::nullopt;
  return result;
}
