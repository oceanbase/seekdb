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

static void validate_geohash_input(const Geometry &geometry, uint32_t srid)
{
  // The original expression checks SRS existence, then builds with a NULL SRS:
  // raw X/Y degrees, no axis swap, projection or ellipsoidal/geodesic box. Keep
  // known IDs until the general host SRS lookup is available.
  if ((srid != 0 && srid != 4326 && srid != 3857) || geometry.srid != srid ||
      (geometry.dimensions != 2 && geometry.dimensions != 3) ||
      geometry.type < 1 || geometry.type > 7) throw CartesianInputError{};
  const auto point = [](const Point &p) {
    if (!std::isfinite(p.x) || !std::isfinite(p.y) || !std::isfinite(p.z)) throw CartesianInputError{};
  };
  if (geometry.type <= 2) {
    if ((geometry.type == 1 && geometry.points.size() != 1) ||
        (geometry.type == 2 && geometry.points.size() < 2)) throw CartesianInputError{};
    for (const auto &p : geometry.points) point(p);
  } else if (geometry.type == 3) {
    for (const auto &ring : geometry.rings) {
      if (ring.size() < 4) throw CartesianInputError{};
      for (const auto &p : ring) point(p);
    }
  } else for (const auto &child : geometry.children) {
    if (geometry.type != 7 && child.type != geometry.type - 3) throw CartesianInputError{};
    validate_geohash_input(child, srid);
  }
}

static void geohash_input_bounds(const Geometry &geometry, Box &box)
{
  // Box's PG-expression mode uses only polygon exteriors, not holes. In
  // particular an invalid external hole must not move the geohash center.
  if (geometry.type == 3) {
    if (geometry.rings.empty()) throw CartesianInputError{};
    for (const auto &point : geometry.rings[0]) add_point(box, point);
  } else if (geometry.type <= 2) {
    for (const auto &point : geometry.points) add_point(box, point);
  } else {
    // Empty members of a nonempty collection have no defined legacy Box path
    // (some dispatches dereference begin() or leave the box uninitialized).
    // Do not manufacture an origin or silently skip them.
    if (geometry.children.empty()) throw CartesianInputError{};
    for (const auto &child : geometry.children) geohash_input_bounds(child, box);
  }
}

static std::optional<std::string> geometry_geohash(const Geometry &input, int64_t precision)
{
  validate_geohash_input(input, input.srid);
  if (tile_empty(input)) return std::nullopt;
  if (precision < INT32_MIN || precision > INT32_MAX) throw CartesianInputError{};
  Box input_box;
  geohash_input_bounds(input, input_box);
  namespace hash = seekdb::geo::geohash;
  const hash::Bounds box{input_box.min_x, input_box.min_y, input_box.max_x, input_box.max_y};
  if (!hash::valid_bounds(box)) throw CartesianInputError{};
  if (precision <= 0) {
    hash::Bounds cell{};
    precision = hash::automatic_precision(box, cell);
  }
  // Match the plugin's existing per-payload budget, not the old arbitrary
  // 32-character precision limit. Report exhaustion rather than truncate.
  if (precision > 16 * 1024 * 1024) throw std::bad_alloc{};
  std::string result;
  result.reserve(static_cast<size_t>(precision));
  hash::encode(box, static_cast<uint32_t>(precision), [&](char c) {
    result.push_back(c);
    return 0;
  });
  return result;
}
