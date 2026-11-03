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

// Included after the plugin-owned Geometry model, inside its private namespace.
// This is an ownership/representation adapter, not a second geometry algorithm.
namespace bg = boost::geometry;
namespace cartesian = seekdb::geo::cartesian;
using CartesianPoint = bg::model::d2::point_xy<double>;
using CartesianLine = bg::model::linestring<CartesianPoint>;
using CartesianPolygon = bg::model::polygon<CartesianPoint>;
using CartesianPoints = bg::model::multi_point<CartesianPoint>;
using CartesianLines = bg::model::multi_linestring<CartesianLine>;
using CartesianPolygons = bg::model::multi_polygon<CartesianPolygon>;
using CartesianGeometry = std::variant<CartesianPoints, CartesianLines, CartesianPolygons>;

struct CartesianInputError {};

static void require_cartesian(const Geometry &geometry, uint32_t srid)
{
  // No SRS lookup service yet: only unreferenced Cartesian and Web Mercator
  // are known here. Never silently treat geographic/unknown SRS as Cartesian,
  // or discard Z. Extend this admission rule when the SRS adapter is ready.
  if ((srid != 0 && srid != 3857) || geometry.srid != srid || geometry.dimensions != 2) {
    throw CartesianInputError{};
  }
}

static CartesianPoint cartesian_point(const Point &point)
{
  if (!std::isfinite(point.x) || !std::isfinite(point.y)) throw CartesianInputError{};
  return CartesianPoint(point.x, point.y);
}

static CartesianPolygon cartesian_polygon(const Geometry &geometry, bool validate = true,
                                         bool close_rings = false, bool correct_winding = true)
{
  CartesianPolygon polygon;
  for (size_t i = 0; i < geometry.rings.size(); ++i) {
    const auto &source = geometry.rings[i];
    if (source.size() < 4 || (!close_rings && (source.front().x != source.back().x ||
        source.front().y != source.back().y))) throw CartesianInputError{};
    if (i != 0) polygon.inners().emplace_back();
    auto &ring = i == 0 ? polygon.outer() : polygon.inners().back();
    for (const auto &point : source) ring.push_back(cartesian_point(point));
  }
  // WKB ring order identifies shell/holes; accept either winding direction.
  if (correct_winding) cartesian::correct(polygon);
  if (validate && !bg::is_empty(polygon) && !cartesian::is_valid(polygon)) throw CartesianInputError{};
  return polygon;
}

static CartesianGeometry to_cartesian(const Geometry &geometry, bool validate = true)
{
  require_cartesian(geometry, geometry.srid);
  CartesianPoints points;
  CartesianLines lines;
  CartesianPolygons polygons;
  const uint32_t primitive = geometry.type <= 3 ? geometry.type : geometry.type - 3;
  if (primitive < 1 || primitive > 3) throw CartesianInputError{};
  const auto append = [&](const Geometry &part) {
    require_cartesian(part, geometry.srid);
    if (part.type != primitive) throw CartesianInputError{};
    if (primitive == 1) {
      if (part.points.size() != 1) throw CartesianInputError{};
      points.push_back(cartesian_point(part.points.front()));
    } else if (primitive == 2) {
      if (part.points.size() < 2) throw CartesianInputError{};
      CartesianLine line;
      for (const auto &point : part.points) line.push_back(cartesian_point(point));
      if (validate && !cartesian::is_valid(line)) throw CartesianInputError{};
      lines.push_back(std::move(line));
    } else {
      auto polygon = cartesian_polygon(part, validate);
      if (!bg::is_empty(polygon)) polygons.push_back(std::move(polygon));
    }
  };
  if (geometry.type <= 3) append(geometry);
  else for (const auto &part : geometry.children) append(part);
  if (primitive == 1) return points;
  if (primitive == 2) return lines;
  if (validate && !polygons.empty() && !cartesian::is_valid(polygons)) throw CartesianInputError{};
  return polygons;
}

static bool valid_geometry(const Geometry &geometry)
{
  // Mirrors ObGeoFuncIsValid's primitive/Multi dispatch. A collection validates
  // each member independently, but a MultiPolygon must also have valid topology
  // between its members. Do not flatten these two cases into the same container.
  require_cartesian(geometry, geometry.srid);
  if (geometry.type == 7) {
    bool valid = true;
    for (const auto &child : geometry.children) {
      require_cartesian(child, geometry.srid);
      // Still check later members for malformed representations. Otherwise a
      // topologically invalid prefix could hide a structural error in a tail.
      if (!valid_geometry(child)) valid = false;
    }
    return valid;
  }
  if (geometry.type == 3) {
    // The original GEO_DEFAULT build path corrects winding and closes rings
    // before IsValid. This does not repair self-intersections or hole topology.
    return cartesian::is_valid(cartesian_polygon(geometry, false, true));
  }
  if (geometry.type == 6) {
    CartesianPolygons polygons;
    for (const auto &child : geometry.children) {
      require_cartesian(child, geometry.srid);
      if (child.type != 3) throw CartesianInputError{};
      polygons.push_back(cartesian_polygon(child, false, true));
    }
    return cartesian::is_valid(polygons);
  }
  const auto converted = to_cartesian(geometry, false);
  return std::visit([](const auto &value) { return cartesian::is_valid(value); }, converted);
}

static void require_same_srid(const Geometry &left, const Geometry &right)
{
  if (left.srid != right.srid) throw CartesianInputError{};
}

// Equivalent to ObGeoFuncUtils::ob_geo_gc_split: retain primitive dimensions
// and flatten nested collections. In particular a polygon's holes are not
// flattened into independent polygon members.
struct CartesianParts {
  CartesianPoints points;
  CartesianLines lines;
  CartesianPolygons polygons;

  bool empty() const { return points.empty() && lines.empty() && polygons.empty(); }
};

static void split_cartesian(const Geometry &geometry, uint32_t srid, CartesianParts &parts, bool validate = true)
{
  require_cartesian(geometry, srid);
  if (geometry.type == 7) {
    for (const auto &child : geometry.children) split_cartesian(child, srid, parts, validate);
  } else {
    const auto converted = to_cartesian(geometry, validate);
    std::visit([&](const auto &value) {
      using Value = std::decay_t<decltype(value)>;
      if constexpr (std::is_same_v<Value, CartesianPoints>) {
        parts.points.insert(parts.points.end(), value.begin(), value.end());
      } else if constexpr (std::is_same_v<Value, CartesianLines>) {
        parts.lines.insert(parts.lines.end(), value.begin(), value.end());
      } else {
        parts.polygons.insert(parts.polygons.end(), value.begin(), value.end());
      }
    }, converted);
  }
}

static CartesianParts split_cartesian(const Geometry &geometry, bool validate = true)
{
  CartesianParts parts;
  split_cartesian(geometry, geometry.srid, parts, validate);
  return parts;
}

static double geometry_area(const Geometry &geometry)
{
  const auto parts = split_cartesian(geometry);
  const double result = cartesian::area(parts.polygons);
  if (!std::isfinite(result)) throw CartesianInputError{};
  return result;
}

static double geometry_length(const Geometry &geometry)
{
  const auto parts = split_cartesian(geometry);
  const double result = cartesian::length(parts.lines);
  if (!std::isfinite(result)) throw CartesianInputError{};
  return result;
}

static Geometry centroid(const Geometry &geometry)
{
  const auto parts = split_cartesian(geometry);
  Geometry result;
  result.type = 7; // Empty centroid is converted to SQL NULL at the C boundary.
  result.srid = geometry.srid;
  if (parts.empty()) return result;
  CartesianPoint point;
  // Same dimension precedence and weighting as ObGeoFuncCentroidImpl.
  if (!parts.polygons.empty()) cartesian::centroid(parts.polygons, point);
  else if (!parts.lines.empty()) cartesian::centroid(parts.lines, point);
  else cartesian::centroid(parts.points, point);
  const double x = bg::get<0>(point), y = bg::get<1>(point);
  if (!std::isfinite(x) || !std::isfinite(y)) throw CartesianInputError{};
  result.type = 1;
  result.points.push_back({x, y, 0});
  return result;
}

static Geometry point_on_surface(const Geometry &geometry)
{
  // The original visitor accepts collapsed polygons and has a first-vertex
  // fallback. Do not run the stricter overlay validity gate on this path.
  const auto parts = split_cartesian(geometry, false);
  Geometry result;
  result.type = 7;
  result.srid = geometry.srid;
  if (parts.empty()) return result;
  Point selected;
  bool found = false;
  if (!parts.polygons.empty()) {
    double max_width = -1;
    for (const auto &polygon : parts.polygons) {
      double y = 0;
      if (!cartesian::surface_scanline_y(polygon.outer(), polygon.inners(), y)) throw CartesianInputError{};
      std::vector<double> crossings;
      const auto append = [&](double x) { crossings.push_back(x); return true; };
      if (!cartesian::surface_ring_crossings(polygon.outer(), y, append)) throw CartesianInputError{};
      for (const auto &ring : polygon.inners()) {
        if (!cartesian::surface_ring_crossings(ring, y, append)) throw CartesianInputError{};
      }
      if (crossings.size() % 2 != 0) throw CartesianInputError{};
      std::sort(crossings.begin(), crossings.end());
      double x = 0;
      if (cartesian::surface_widest_interval(crossings.begin(), crossings.end(), max_width, x)) {
        selected = {x, y, 0};
        found = true;
      }
      if (max_width == -1) {
        selected = {bg::get<0>(polygon.outer().front()), bg::get<1>(polygon.outer().front()), 0};
        max_width = 0;
        found = true;
      }
    }
  } else {
    CartesianPoint centre(0, 0);
    bool has_centroid = true;
    try {
      if (!parts.lines.empty()) cartesian::centroid(parts.lines, centre);
      else cartesian::centroid(parts.points, centre);
    } catch (const bg::centroid_exception &) { has_centroid = false; }
    if (!std::isfinite(bg::get<0>(centre)) || !std::isfinite(bg::get<1>(centre))) throw CartesianInputError{};
    double best = std::numeric_limits<double>::max();
    double best_endpoint = best;
    Point endpoint;
    bool found_endpoint = false;
    const auto consider = [&](double x, double y) {
      const double distance = cartesian::surface_distance(x, y, bg::get<0>(centre), bg::get<1>(centre));
      if (!std::isfinite(distance)) throw CartesianInputError{};
      if (distance < best) { best = distance; selected = {x, y, 0}; found = true; }
      return true;
    };
    const auto consider_endpoint = [&](double x, double y) {
      if (!found) {
        const double distance = cartesian::surface_distance(x, y, bg::get<0>(centre), bg::get<1>(centre));
        if (!std::isfinite(distance)) throw CartesianInputError{};
        if (distance < best_endpoint) { best_endpoint = distance; endpoint = {x, y, 0}; found_endpoint = true; }
      }
      return true;
    };
    if (!parts.lines.empty()) {
      for (const auto &line : parts.lines) {
        if (!cartesian::surface_line_candidates(line, has_centroid, consider, consider_endpoint)) {
          throw CartesianInputError{};
        }
      }
    } else if (has_centroid) {
      for (const auto &point : parts.points) consider(bg::get<0>(point), bg::get<1>(point));
    }
    if (!found && found_endpoint) { selected = endpoint; found = true; }
  }
  if (found) {
    if (!std::isfinite(selected.x) || !std::isfinite(selected.y)) throw CartesianInputError{};
    result.type = 1;
    result.points.push_back(selected);
  }
  return result;
}

static Geometry from_cartesian(const CartesianPolygons &polygons, uint32_t srid)
{
  Geometry result;
  result.type = 6;
  result.srid = srid;
  for (const auto &polygon : polygons) {
    Geometry part;
    part.type = 3;
    part.srid = srid;
    const auto append = [&](const auto &ring) {
      part.rings.emplace_back();
      for (const auto &point : ring) {
        const double x = bg::get<0>(point), y = bg::get<1>(point);
        if (!std::isfinite(x) || !std::isfinite(y)) throw CartesianInputError{};
        part.rings.back().push_back({x, y, 0});
      }
    };
    if (!bg::is_empty(polygon)) {
      append(polygon.outer());
      for (const auto &ring : polygon.inners()) append(ring);
    }
    result.children.push_back(std::move(part));
  }
  if (result.children.size() == 1) return std::move(result.children.front());
  return result;
}

static Geometry correct_geometry(const Geometry &geometry)
{
  require_cartesian(geometry, geometry.srid);
  if (geometry.type == 3) {
    return from_cartesian(CartesianPolygons{cartesian_polygon(geometry, false, true)}, geometry.srid);
  }
  Geometry result = geometry;
  if (geometry.type >= 4 && geometry.type <= 7) {
    for (auto &child : result.children) {
      require_cartesian(child, geometry.srid);
      if (geometry.type != 7 && child.type != geometry.type - 3) throw CartesianInputError{};
      child = correct_geometry(child);
    }
  } else {
    // Check shape/coordinates without rejecting topology before repair.
    (void)to_cartesian(geometry, false);
  }
  return result;
}

static CartesianPolygons repair_polygon_model(const CartesianPolygon &polygon)
{
  CartesianPolygons result;
  if (polygon.inners().empty()) {
    if (cartesian::is_valid(polygon)) result.push_back(polygon);
    else {
      auto reversed = polygon;
      cartesian::dissolve_invalid_polygon(polygon, reversed, result);
    }
  } else {
    // Optional distinguishes an absent group from an existing empty result,
    // exactly as the original arena-pointer orchestration does.
    using Handle = std::optional<CartesianPolygons>;
    CartesianPolygon exterior;
    exterior.outer() = polygon.outer();
    Handle shell(CartesianPolygons{std::move(exterior)}), repaired;
    const auto hole_at = [&](unsigned long i, Handle &hole) {
      CartesianPolygon part;
      part.outer() = polygon.inners()[i];
      cartesian::correct(part);
      hole = CartesianPolygons{std::move(part)};
      return 0;
    };
    const auto intersects = [](const Handle &hole, const Handle &shell, bool &value) {
      value = cartesian::intersects(*hole, *shell);
      return 0;
    };
    const auto merge = [](const Handle &part, Handle &group) {
      CartesianPolygons output;
      cartesian::union_(*part, *group, output);
      group = std::move(output);
      return 0;
    };
    const auto sym_difference = [](const Handle &shell, const Handle &holes, Handle &output) {
      CartesianPolygons difference;
      bg::sym_difference(*shell, *holes, difference);
      output = std::move(difference);
      return 0;
    };
    if (cartesian::repair_polygon_holes(shell, polygon.inners().size(), hole_at,
          intersects, merge, sym_difference, repaired) != 0 || !repaired) throw CartesianInputError{};
    result = std::move(*repaired);
  }
  return result;
}

static Geometry make_valid_geometry(const Geometry &geometry)
{
  // Match ObExprPrivSTMakeValid: default geometry correction, validity test,
  // polygon repair (each MultiPolygon member followed by union), or failure
  // when correction cannot fix a non-polygon. A collection is not recursively
  // upgraded into a different, more permissive polygon-repair contract.
  Geometry normalized = correct_geometry(geometry);
  if (valid_geometry(normalized)) return normalized;
  CartesianPolygons result;
  if (normalized.type == 3) {
    result = repair_polygon_model(cartesian_polygon(normalized, false));
  } else if (normalized.type == 6) {
    bool have_result = false;
    for (const auto &child : normalized.children) {
      auto part = repair_polygon_model(cartesian_polygon(child, false));
      if (!have_result) {
        result = std::move(part);
        have_result = true;
      } else if (!bg::is_empty(part)) {
        CartesianPolygons merged;
        cartesian::union_(part, result, merged);
        result = std::move(merged);
      }
    }
  } else {
    throw CartesianInputError{};
  }
  // The legacy SQL expression keeps the corrected input if dissolution is
  // empty, e.g. a fully collapsed polygon. Do not invent a successful empty
  // geometry or promise that this legacy operation always makes IsValid true.
  return bg::is_empty(result) ? normalized : from_cartesian(result, geometry.srid);
}

struct BufferOptions {
  // Defaults and bit assignments match ObGeoBufferStrategy.
  double distance = 0;
  size_t point_count = 32;
  size_t join_count = 32;
  size_t end_count = 32;
  double miter_limit = 5;
  uint8_t state = 0;
  bool has_point = false;
  bool has_join = false;
  bool has_end = false;
};

template <typename Input>
static void buffer_part(const Input &input, CartesianPolygons &result, const BufferOptions &options)
{
  bg::strategy::buffer::distance_symmetric<double> distance(options.distance);
  bg::strategy::buffer::side_straight side;
  bg::strategy::buffer::join_round join_round(options.join_count);
  bg::strategy::buffer::join_miter join_miter(options.miter_limit);
  bg::strategy::buffer::end_round end_round(options.end_count);
  bg::strategy::buffer::end_flat end_flat;
  bg::strategy::buffer::point_circle point_circle(options.point_count);
  bg::strategy::buffer::point_square point_square;
  switch (options.state) {
    case 0: cartesian::buffer(input, result, distance, side, join_round, end_round, point_circle); break;
    case 1: cartesian::buffer(input, result, distance, side, join_round, end_round, point_square); break;
    case 2: cartesian::buffer(input, result, distance, side, join_round, end_flat, point_circle); break;
    case 3: cartesian::buffer(input, result, distance, side, join_round, end_flat, point_square); break;
    case 4: cartesian::buffer(input, result, distance, side, join_miter, end_round, point_circle); break;
    case 5: cartesian::buffer(input, result, distance, side, join_miter, end_round, point_square); break;
    case 6: cartesian::buffer(input, result, distance, side, join_miter, end_flat, point_circle); break;
    case 7: cartesian::buffer(input, result, distance, side, join_miter, end_flat, point_square); break;
    default: throw CartesianInputError{};
  }
}

static Geometry buffer_geometry(const Geometry &geometry, const BufferOptions &options)
{
  if (!std::isfinite(options.distance)) throw CartesianInputError{};
  const auto parts = split_cartesian(geometry);
  if (std::abs(options.distance) < 1e-11) return geometry;
  // Match the legacy geometry-class strategy admission and negative radius rule.
  if ((options.distance < 0 && (!parts.points.empty() || !parts.lines.empty())) ||
      (geometry.type != 7 &&
       ((!parts.points.empty() && (options.has_join || options.has_end)) ||
        (!parts.lines.empty() && options.has_point) ||
        (!parts.polygons.empty() && (options.has_point || options.has_end))))) {
    throw CartesianInputError{};
  }
  CartesianPolygons result;
  const auto append = [&](const auto &input) {
    if (input.empty()) return;
    CartesianPolygons buffered;
    buffer_part(input, buffered, options);
    if (result.empty()) result = std::move(buffered);
    else if (!buffered.empty()) {
      CartesianPolygons combined;
      cartesian::union_(result, buffered, combined);
      result = std::move(combined);
    }
  };
  append(parts.points);
  append(parts.lines);
  append(parts.polygons);
  // The original buffer returns an empty collection after complete erosion.
  if (result.empty()) {
    Geometry empty;
    empty.type = 7;
    empty.srid = geometry.srid;
    return empty;
  }
  return from_cartesian(result, geometry.srid);
}

static Geometry combine_polygons(const Geometry &left, const Geometry &right, uint32_t operation)
{
  require_same_srid(left, right);
  auto a = to_cartesian(left), b = to_cartesian(right);
  const auto *polygons_a = std::get_if<CartesianPolygons>(&a);
  const auto *polygons_b = std::get_if<CartesianPolygons>(&b);
  if (polygons_a == nullptr || polygons_b == nullptr) throw CartesianInputError{};
  CartesianPolygons result;
  switch (operation) {
    case SEEKDB_GIS_OP_UNION: cartesian::union_(*polygons_a, *polygons_b, result); break;
    case SEEKDB_GIS_OP_DIFFERENCE: bg::difference(*polygons_a, *polygons_b, result); break;
    case SEEKDB_GIS_OP_SYMMETRIC_DIFFERENCE: bg::sym_difference(*polygons_a, *polygons_b, result); break;
    default: throw CartesianInputError{};
  }
  return from_cartesian(result, left.srid);
}

static double geometry_distance(const Geometry &left, const Geometry &right)
{
  require_same_srid(left, right);
  const auto a = to_cartesian(left), b = to_cartesian(right);
  return std::visit([](const auto &l, const auto &r) {
    // Empty distance has no numeric value; do not manufacture a zero.
    if (bg::is_empty(l) || bg::is_empty(r)) throw CartesianInputError{};
    const double distance = cartesian::distance(l, r);
    if (!std::isfinite(distance)) throw CartesianInputError{};
    return distance;
  }, a, b);
}

static bool relation_result(uint32_t operation, const Geometry &left, const Geometry &right,
                            double distance_limit)
{
  require_same_srid(left, right);
  if (operation == SEEKDB_GIS_REL_DWITHIN) {
    if (!std::isfinite(distance_limit) || distance_limit < 0) throw CartesianInputError{};
    return geometry_distance(left, right) <= distance_limit;
  }
  const auto a = to_cartesian(left), b = to_cartesian(right);
  return std::visit([&](const auto &l, const auto &r) {
    constexpr int ld = bg::topological_dimension<std::decay_t<decltype(l)>>::value;
    constexpr int rd = bg::topological_dimension<std::decay_t<decltype(r)>>::value;
    switch (operation) {
      case SEEKDB_GIS_REL_EQUALS:
        if constexpr (ld == rd) return cartesian::equals(l, r);
        else return false;
      case SEEKDB_GIS_REL_INTERSECTS: return cartesian::intersects(l, r);
      case SEEKDB_GIS_REL_CONTAINS:
        if constexpr (rd <= ld) return bg::within(r, l);
        else return false;
      case SEEKDB_GIS_REL_WITHIN:
        if constexpr (ld <= rd) return bg::within(l, r);
        else return false;
      case SEEKDB_GIS_REL_COVERS:
        if constexpr (rd <= ld) return bg::covered_by(r, l);
        else return false;
      case SEEKDB_GIS_REL_TOUCHES: return bg::touches(l, r);
      case SEEKDB_GIS_REL_CROSSES:
        if constexpr ((ld == 2 && rd == 2) || (ld == 0 && rd == 0)) return false;
        else return bg::crosses(l, r);
      case SEEKDB_GIS_REL_OVERLAPS:
        if constexpr (ld == rd) return bg::overlaps(l, r);
        else return false;
      default: throw CartesianInputError{};
    }
  }, a, b);
}
