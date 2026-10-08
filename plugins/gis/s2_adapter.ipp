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

namespace s2_index = seekdb::geo::s2_index;

static seekdb_plugin_spatial_cover_request_v1_t spatial_request(
    const seekdb_plugin_execution_value_v1_t &value)
{
  seekdb_plugin_spatial_cover_request_v1_t request{};
  if (value.struct_size != sizeof(value) || value.type_id == nullptr ||
      std::strcmp(value.type_id, SEEKDB_PLUGIN_SPATIAL_COVER_REQUEST_TYPE) != 0 ||
      value.is_null || value.data == nullptr || value.data_size != sizeof(request)) {
    throw CartesianInputError{};
  }
  std::memcpy(&request, value.data, sizeof(request));
  if (request.struct_size != sizeof(request) || request.reserved_word != 0 ||
      (request.flags & ~127u) != 0 ||
      std::any_of(std::begin(request.reserved), std::end(request.reserved),
                  [](uint64_t v) { return v != 0; }) ||
      !std::isfinite(request.buffer_radians) || request.buffer_radians < 0 ||
      request.buffer_radians > std::acos(-1.0)) throw CartesianInputError{};
  const bool geographic = request.flags & SEEKDB_PLUGIN_SPATIAL_GEOGRAPHIC;
  const bool query = request.flags & SEEKDB_PLUGIN_SPATIAL_QUERY;
  const bool buffer = request.flags & SEEKDB_PLUGIN_SPATIAL_BUFFER;
  const bool window = request.flags & SEEKDB_PLUGIN_SPATIAL_QUERY_WINDOW;
  const uint32_t all_required = SEEKDB_PLUGIN_SPATIAL_QUERY |
      SEEKDB_PLUGIN_SPATIAL_ANCESTORS | SEEKDB_PLUGIN_SPATIAL_VERTICES;
  if ((request.flags & SEEKDB_PLUGIN_SPATIAL_ALL_VIEWS) &&
      (request.flags & all_required) != all_required) throw CartesianInputError{};
  if ((!buffer && request.buffer_radians != 0) || (buffer && (!query || !geographic)) ||
      (window && (!query || buffer)) ||
      (!query && (request.flags & (SEEKDB_PLUGIN_SPATIAL_ANCESTORS | SEEKDB_PLUGIN_SPATIAL_VERTICES)))) {
    throw CartesianInputError{};
  }
  if (geographic) {
    if (request.xmin != 0 || request.xmax != 0 || request.ymin != 0 || request.ymax != 0) {
      throw CartesianInputError{};
    }
  } else if (!s2_index::valid_bounds({request.xmin, request.xmax, request.ymin, request.ymax})) {
    throw CartesianInputError{};
  }
  return request;
}

// Per-call ownership only: no SRS cache, borrowed input or S2 object survives
// the synchronous invocation. Invalid coordinates never reach S2 CHECKs.
class SpatialCover {
public:
  explicit SpatialCover(const seekdb_plugin_spatial_cover_request_v1_t &request)
      : geographic_(request.flags & SEEKDB_PLUGIN_SPATIAL_GEOGRAPHIC),
        bounds_{request.xmin, request.xmax, request.ymin, request.ymax},
        options_(s2_index::options(request.flags & SEEKDB_PLUGIN_SPATIAL_QUERY_WINDOW)) {}

  void visit(const Geometry &geometry)
  {
    if (geometry.type == 1) {
      const auto point = convert(geometry.points.at(0));
      add_vertex(point);
      if (geographic_) {
        const auto &xy = geometry.points.at(0);
        const S2LatLng ll = S2LatLng::FromDegrees(xy.y, xy.x);
        mbr_ = mbr_.Union(S2LatLngRect(ll, ll));
      }
      regions_.push_back(std::make_unique<S2Cell>(point));
    } else if (geometry.type == 2) {
      auto vertices = convert_line(geometry.points);
      auto line = std::make_unique<S2Polyline>(vertices, S2Debug::DISABLE);
      S2Error error;
      if (line->FindValidationError(&error)) throw CartesianInputError{};
      if (geographic_) mbr_ = mbr_.Union(line->GetRectBound());
      regions_.push_back(std::move(line));
    } else if (geometry.type == 3) {
      std::vector<std::unique_ptr<S2Loop>> loops;
      for (const auto &ring : geometry.rings) {
        if (ring.size() < 4 || ring.front().x != ring.back().x || ring.front().y != ring.back().y) {
          throw CartesianInputError{};
        }
        auto vertices = convert_line(ring);
        // WKB repeats the closing vertex; S2Loop implicitly closes its loop.
        // Keep that vertex in the legacy vertex-cell/bounder accumulation,
        // but do not pass a zero-length closing edge to S2's polygon index.
        vertices.pop_back();
        auto loop = std::make_unique<S2Loop>(vertices, S2Debug::DISABLE);
        S2Error error;
        if (loop->FindValidationError(&error)) throw CartesianInputError{};
        loop->Normalize();
        loops.push_back(std::move(loop));
      }
      auto polygon = std::make_unique<S2Polygon>(std::move(loops), S2Debug::DISABLE);
      S2Error error;
      if (polygon->FindValidationError(&error)) throw CartesianInputError{};
      if (geographic_) mbr_ = mbr_.Union(polygon->GetRectBound());
      regions_.push_back(std::move(polygon));
    } else {
      for (const auto &child : geometry.children) visit(child);
    }
  }

  void cover()
  {
    s2_index::cover_regions(regions_, options_, geographic_, bounder_, cells_, mbr_, vertices_);
  }

  bool geographic_;
  s2_index::ProjectionBounds bounds_;
  S2RegionCoverer::Options options_;
  std::vector<std::unique_ptr<S2Region>> regions_;
  std::vector<S2CellId> vertices_;
  S2CellUnion cells_;
  S2LatLngRect mbr_ = S2LatLngRect::Empty();
  S2LatLngRectBounder bounder_;

private:
  S2Point convert(const Point &point) const
  {
    if (geographic_) return S2LatLng::FromDegrees(point.y, point.x).ToPoint();
    S2Point result;
    if (!s2_index::project_point(bounds_, point.x, point.y, result)) throw CartesianInputError{};
    // Face/ST mapping supplies a direction. S2Loop/S2Polyline require UNIT
    // vectors, unlike S2CellId which can accept a non-unit direction.
    return result.Normalize();
  }

  void add_vertex(const S2Point &point)
  {
    vertices_.push_back(S2CellId(point).parent(options_.max_level()));
    bounder_.AddPoint(point);
  }

  std::vector<S2Point> convert_line(const std::vector<Point> &points)
  {
    std::vector<S2Point> result;
    result.reserve(points.size());
    for (const auto &point : points) {
      result.push_back(convert(point));
      add_vertex(result.back());
    }
    return result;
  }
};

static void validate_spatial_geometry(const Geometry &geometry, uint32_t srid, bool geographic)
{
  if (geometry.srid != srid || geometry.type < 1 || geometry.type > 7) throw CartesianInputError{};
  const auto validate = [geographic](const Point &point) {
    if (!std::isfinite(point.x) || !std::isfinite(point.y) ||
        (geographic && (point.x < -180 || point.x > 180 || point.y < -90 || point.y > 90))) {
      throw CartesianInputError{};
    }
  };
  for (const auto &point : geometry.points) validate(point);
  for (const auto &ring : geometry.rings) for (const auto &point : ring) validate(point);
  for (const auto &child : geometry.children) {
    if (geometry.type != 7 && child.type != geometry.type - 3) throw CartesianInputError{};
    validate_spatial_geometry(child, srid, geographic);
  }
}

static std::vector<uint8_t> spatial_cover(const Geometry &geometry,
    const seekdb_plugin_spatial_cover_request_v1_t &request)
{
  const bool geographic = request.flags & SEEKDB_PLUGIN_SPATIAL_GEOGRAPHIC;
  validate_spatial_geometry(geometry, request.srid, geographic);
  SpatialCover covering(request);
  Box box;
  const bool empty = !bounds(geometry, box);
  bool outside = false;
  bool invalid = false;
  if (!geographic && !empty) {
    outside = s2_index::outside_bounds(covering.bounds_, box.min_x, box.min_y) ||
              s2_index::outside_bounds(covering.bounds_, box.max_x, box.max_y);
  }
  if (outside) {
    // Same get_mbr_polygon retry as ObS2Adapter: intersect the ORIGINAL
    // ENVELOPE with the 1%-inset SRS bounds, not a shape clipping operation.
    const double dx = s2_index::bounds_margin * (request.xmax - request.xmin);
    const double dy = s2_index::bounds_margin * (request.ymax - request.ymin);
    const double xmin = std::max(box.min_x, request.xmin + dx);
    const double xmax = std::min(box.max_x, request.xmax - dx);
    const double ymin = std::max(box.min_y, request.ymin + dy);
    const double ymax = std::min(box.max_y, request.ymax - dy);
    invalid = xmin > xmax || ymin > ymax;
    if (!invalid) {
      Geometry corrected;
      corrected.srid = geometry.srid;
      if (xmin == xmax && ymin == ymax) {
        corrected.type = 1;
        corrected.points = {{xmin, ymin}};
      } else if (xmin == xmax || ymin == ymax) {
        corrected.type = 2;
        corrected.points = {{xmin, ymin}, {xmax, ymax}};
      } else {
        corrected.type = 3;
        corrected.rings = {{{xmin, ymin}, {xmax, ymin}, {xmax, ymax}, {xmin, ymax}, {xmin, ymin}}};
      }
      covering.visit(corrected);
    }
  } else if (!empty) covering.visit(geometry);
  if (!invalid) covering.cover();

  const bool buffer = request.flags & SEEKDB_PLUGIN_SPATIAL_BUFFER;
  const S1Angle distance = S1Angle::Radians(request.buffer_radians);
  s2_index::CellSets sets;
  if (request.flags & SEEKDB_PLUGIN_SPATIAL_ANCESTORS) {
    sets = s2_index::cells_and_ancestors(covering.cells_, covering.options_, covering.regions_.size() > 1,
                                       invalid, outside && !invalid, buffer, distance);
  } else {
    // Index writes need only cover cells, not a discarded ancestor hash set.
    sets.cells = s2_index::cell_ids(covering.cells_, covering.options_, covering.regions_.size() > 1,
                                  invalid, outside && !invalid, false, buffer, distance);
  }
  const auto vertices = request.flags & SEEKDB_PLUGIN_SPATIAL_VERTICES
      ? s2_index::vertex_cell_ids(covering.vertices_, invalid) : std::vector<uint64_t>{};
  seekdb_plugin_spatial_cover_result_v1_t result{};
  result.struct_size = sizeof(result);
  result.flags = (geographic ? SEEKDB_PLUGIN_SPATIAL_RESULT_GEOGRAPHIC : 0) |
                 (geometry.type == 1 ? SEEKDB_PLUGIN_SPATIAL_RESULT_POINT : 0) |
                 (empty ? SEEKDB_PLUGIN_SPATIAL_RESULT_EMPTY : 0) |
                 (outside ? SEEKDB_PLUGIN_SPATIAL_RESULT_OUTSIDE_BOUNDS : 0);
  if (!empty && geographic) {
    const auto rect = s2_index::geographic_mbr(covering.mbr_, false, false, buffer, distance);
    result.xmin = rect.lng_lo().degrees(); result.xmax = rect.lng_hi().degrees();
    result.ymin = rect.lat_lo().degrees(); result.ymax = rect.lat_hi().degrees();
  } else if (!empty) {
    result.xmin = box.min_x; result.xmax = box.max_x;
    result.ymin = box.min_y; result.ymax = box.max_y;
  }
  const bool all_views = request.flags & SEEKDB_PLUGIN_SPATIAL_ALL_VIEWS;
  const auto query_cells = all_views
      ? s2_index::cell_ids(covering.cells_, covering.options_, covering.regions_.size() > 1,
                          invalid, outside && !invalid, true, buffer, distance)
      : std::vector<uint64_t>{};
  const size_t header_size = all_views ? sizeof(seekdb_plugin_spatial_cover_result_v2_t) : sizeof(result);
  const size_t count = sets.cells.size() + sets.ancestors.size() + vertices.size() + query_cells.size();
  if (count > (SEEKDB_PLUGIN_SPATIAL_MAX_BYTES - header_size) / sizeof(uint64_t)) {
    throw std::bad_alloc{};
  }
  result.cell_count = static_cast<uint32_t>(sets.cells.size());
  result.ancestor_count = static_cast<uint32_t>(sets.ancestors.size());
  result.vertex_count = static_cast<uint32_t>(vertices.size());
  std::vector<uint8_t> bytes(header_size + count * sizeof(uint64_t));
  if (all_views) {
    seekdb_plugin_spatial_cover_result_v2_t complete{};
    complete.v1 = result;
    complete.v1.struct_size = sizeof(complete);
    complete.query_cell_count = static_cast<uint32_t>(query_cells.size());
    std::memcpy(bytes.data(), &complete, sizeof(complete));
  } else {
    std::memcpy(bytes.data(), &result, sizeof(result));
  }
  size_t offset = header_size;
  const std::vector<uint64_t> *arrays[] = {&sets.cells, &sets.ancestors, &vertices, &query_cells};
  for (const auto *ids : arrays) {
    if (!ids->empty()) std::memcpy(bytes.data() + offset, ids->data(), ids->size() * sizeof(uint64_t));
    offset += ids->size() * sizeof(uint64_t);
  }
  return bytes;
}
