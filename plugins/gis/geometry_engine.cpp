/*
 * Copyright (c) 2026 OceanBase.
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

#include "geometry_engine.h"
#include "number_format.h"
#include "projection.h"
#include "srs_metadata.h"
#include "seekdb/plugin/sql_spi.h"
#include "seekdb/plugin/srs_spi.h"
#include "seekdb/geo/cartesian_algorithms.hpp"
#include "seekdb/geo/interior_point.hpp"
#include "seekdb/geo/polygon_repair.hpp"
#include "seekdb/geo/box_clip.hpp"
#include "seekdb/geo/tile_grid.hpp"
#include "seekdb/geo/geohash.hpp"
#include "seekdb/geo/axis_order.hpp"
#include "seekdb/geo/pg_coordinate_io.hpp"
#include "seekdb/geo/geographic_box.hpp"
#include "seekdb/geo/s2_covering.hpp"
#include "seekdb/plugin/spatial_index_spi.h"
#include <s2/s2cell.h>
#include <s2/s2loop.h>
#include <s2/s2polygon.h>
#include <s2/s2polyline.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>
#include <iomanip>
#include <locale>
#include <optional>
#include <sstream>
#include <string>
#include <type_traits>
#include <variant>
#include <vector>

namespace {

struct Point {
  double x = 0.0;
  double y = 0.0;
  double z = 0.0;
};

struct Geometry {
  uint32_t type = 0;
  uint32_t dimensions = 2;
  uint32_t srid = 0;
  std::vector<Point> points;
  std::vector<std::vector<Point>> rings;
  std::vector<Geometry> children;
};

#include "cartesian_adapter.ipp"

struct Box {
  double min_x = std::numeric_limits<double>::infinity();
  double min_y = std::numeric_limits<double>::infinity();
  double max_x = -std::numeric_limits<double>::infinity();
  double max_y = -std::numeric_limits<double>::infinity();
};

static uint32_t base_type(uint32_t type)
{
  return type >= 1000 ? type - 1000 : type;
}

static uint32_t dimensions_for(uint32_t type)
{
  return type >= 1000 ? 3 : 2;
}

class Reader {
public:
  Reader(const uint8_t *data, size_t size) : data_(data), end_(data + size), little_endian_(true) {}

  void set_little_endian(bool little_endian) { little_endian_ = little_endian; }

  bool bytes(size_t count, const uint8_t **out)
  {
    if (out == nullptr || data_ == nullptr || count > remaining()) return false;
    *out = data_;
    data_ += count;
    return true;
  }

  bool u32(uint32_t *out)
  {
    const uint8_t *p = nullptr;
    if (out == nullptr || !bytes(4, &p)) return false;
    if (little_endian_) {
      *out = static_cast<uint32_t>(p[0]) |
             (static_cast<uint32_t>(p[1]) << 8) |
             (static_cast<uint32_t>(p[2]) << 16) |
             (static_cast<uint32_t>(p[3]) << 24);
    } else {
      *out = static_cast<uint32_t>(p[3]) |
             (static_cast<uint32_t>(p[2]) << 8) |
             (static_cast<uint32_t>(p[1]) << 16) |
             (static_cast<uint32_t>(p[0]) << 24);
    }
    return true;
  }

  bool number(double *out)
  {
    const uint8_t *p = nullptr;
    if (out == nullptr || !bytes(sizeof(double), &p)) return false;
    if (little_endian_) {
      std::memcpy(out, p, sizeof(double));
    } else {
      uint8_t reversed[sizeof(double)] = {};
      for (size_t i = 0; i < sizeof(double); ++i) reversed[i] = p[sizeof(double) - i - 1];
      std::memcpy(out, reversed, sizeof(double));
    }
    return std::isfinite(*out);
  }

  size_t remaining() const { return static_cast<size_t>(end_ - data_); }

private:
  const uint8_t *data_;
  const uint8_t *end_;
  bool little_endian_;
};

static bool read_geometry(Reader &reader, Geometry &geometry, uint32_t srid, unsigned depth = 0,
                          bool allow_ewkb = true, int required_byte_order = -1)
{
  if (depth > 64) return false;
  const uint8_t *order = nullptr;
  uint32_t encoded_type = 0;
  if (!reader.bytes(1, &order) || (*order != 0 && *order != 1)) return false;
  if (required_byte_order != -1 && *order != required_byte_order) return false;
  reader.set_little_endian(*order == 1);
  if (!reader.u32(&encoded_type)) return false;
  const bool ewkb_z = (encoded_type & UINT32_C(0x80000000)) != 0;
  const bool ewkb_m = (encoded_type & UINT32_C(0x40000000)) != 0;
  const bool ewkb_srid = (encoded_type & UINT32_C(0x20000000)) != 0;
  if (!allow_ewkb && (ewkb_z || ewkb_m || ewkb_srid)) return false;
  if (ewkb_m) return false;
  const uint32_t stripped_type = encoded_type & UINT32_C(0x1fffffff);
  const uint32_t type = base_type(stripped_type);
  if (type < 1 || type > 7) return false;
  geometry.type = type;
  geometry.dimensions = ewkb_z ? 3 : dimensions_for(stripped_type);
  geometry.srid = srid;
  if (ewkb_srid && !reader.u32(&geometry.srid)) return false;

  if (type == 1) {
    Point point;
    if (!reader.number(&point.x) || !reader.number(&point.y) ||
        (geometry.dimensions == 3 && !reader.number(&point.z))) return false;
    geometry.points.push_back(point);
  } else if (type == 2) {
    uint32_t count = 0;
    if (!reader.u32(&count) || count < 2 || count > 1000000 ||
        count > reader.remaining() / (geometry.dimensions * sizeof(double))) return false;
    geometry.points.resize(count);
    for (Point &point : geometry.points) {
      if (!reader.number(&point.x) || !reader.number(&point.y) ||
          (geometry.dimensions == 3 && !reader.number(&point.z))) return false;
    }
  } else if (type == 3) {
    uint32_t ring_count = 0;
    if (!reader.u32(&ring_count) || ring_count > 100000 ||
        ring_count > reader.remaining() / sizeof(uint32_t)) return false;
    geometry.rings.resize(ring_count);
    for (std::vector<Point> &ring : geometry.rings) {
      uint32_t count = 0;
      if (!reader.u32(&count) || count < 4 || count > 1000000 ||
          count > reader.remaining() / (geometry.dimensions * sizeof(double))) return false;
      ring.resize(count);
      for (Point &point : ring) {
        if (!reader.number(&point.x) || !reader.number(&point.y) ||
            (geometry.dimensions == 3 && !reader.number(&point.z))) return false;
      }
    }
  } else {
    uint32_t count = 0;
    if (!reader.u32(&count) || count > 100000 || count > reader.remaining() / 5) return false;
    geometry.children.resize(count);
    for (Geometry &child : geometry.children) {
      if (!read_geometry(reader, child, geometry.srid, depth + 1, allow_ewkb, required_byte_order)) return false;
      if (child.srid != geometry.srid ||
          (type != 7 && (child.type != type - 3 || child.dimensions != geometry.dimensions))) return false;
    }
  }
  return true;
}

static bool decode(const seekdb_plugin_execution_value_v1_t &value, Geometry &geometry)
{
  if (value.struct_size != sizeof(value) || value.is_null || value.data == nullptr ||
      value.data_size < 10 || value.data_size > 16 * 1024 * 1024 || value.type_id == nullptr ||
      std::strcmp(value.type_id, "org.seekdb.gis.geometry") != 0 || value.data[4] != 1) {
    return false;
  }
  uint32_t srid = static_cast<uint32_t>(value.data[0]) |
                  (static_cast<uint32_t>(value.data[1]) << 8) |
                  (static_cast<uint32_t>(value.data[2]) << 16) |
                  (static_cast<uint32_t>(value.data[3]) << 24);
  Reader reader(value.data + 5, static_cast<size_t>(value.data_size - 5));
  if (!read_geometry(reader, geometry, srid) || reader.remaining() != 0) return false;
  return true;
}

static void append_u32(std::vector<uint8_t> &out, uint32_t value)
{
  out.push_back(static_cast<uint8_t>(value));
  out.push_back(static_cast<uint8_t>(value >> 8));
  out.push_back(static_cast<uint8_t>(value >> 16));
  out.push_back(static_cast<uint8_t>(value >> 24));
}

static void append_number(std::vector<uint8_t> &out, double value)
{
  const uint8_t *p = reinterpret_cast<const uint8_t *>(&value);
  out.insert(out.end(), p, p + sizeof(double));
}

static void write_geometry(const Geometry &geometry, std::vector<uint8_t> &out)
{
  out.push_back(1);
  append_u32(out, geometry.type + (geometry.dimensions == 3 ? 1000 : 0));
  if (geometry.type == 1) {
    append_number(out, geometry.points[0].x);
    append_number(out, geometry.points[0].y);
    if (geometry.dimensions == 3) append_number(out, geometry.points[0].z);
  } else if (geometry.type == 2) {
    append_u32(out, static_cast<uint32_t>(geometry.points.size()));
    for (const Point &point : geometry.points) {
      append_number(out, point.x);
      append_number(out, point.y);
      if (geometry.dimensions == 3) append_number(out, point.z);
    }
  } else if (geometry.type == 3) {
    append_u32(out, static_cast<uint32_t>(geometry.rings.size()));
    for (const std::vector<Point> &ring : geometry.rings) {
      append_u32(out, static_cast<uint32_t>(ring.size()));
      for (const Point &point : ring) {
        append_number(out, point.x);
        append_number(out, point.y);
        if (geometry.dimensions == 3) append_number(out, point.z);
      }
    }
  } else {
    append_u32(out, static_cast<uint32_t>(geometry.children.size()));
    for (const Geometry &child : geometry.children) write_geometry(child, out);
  }
}

static bool encode(const Geometry &geometry, std::vector<uint8_t> &out)
{
  out.clear();
  out.reserve(64);
  append_u32(out, geometry.srid);
  out.push_back(1);
  write_geometry(geometry, out);
  return out.size() <= 16 * 1024 * 1024;
}

static bool valid_context(const seekdb_plugin_execution_context_v1_t *context)
{
  return context != nullptr && context->struct_size >= sizeof(*context) &&
         context->emit_result != nullptr;
}

static seekdb_plugin_status_t emit_geometry(
    const seekdb_plugin_execution_context_v1_t *context, const Geometry &geometry)
{
  std::vector<uint8_t> encoded;
  if (!encode(geometry, encoded)) return SEEKDB_PLUGIN_STATUS_NO_MEMORY;
  const seekdb_plugin_execution_result_v1_t result = {
      sizeof(result), "org.seekdb.gis.geometry", encoded.data(), encoded.size(), 0,
      {0, 0, 0, 0, 0, 0, 0}, {0, 0, 0, 0}};
  return context->emit_result(context->host, &result);
}

static seekdb_plugin_status_t emit_bool(
    const seekdb_plugin_execution_context_v1_t *context, bool value)
{
  const uint8_t result_value = value ? 1 : 0;
  const seekdb_plugin_execution_result_v1_t result = {
      sizeof(result), "org.seekdb.gis.scalar.bool", &result_value, sizeof(result_value), 0,
      {0, 0, 0, 0, 0, 0, 0}, {0, 0, 0, 0}};
  return context->emit_result(context->host, &result);
}

static seekdb_plugin_status_t emit_null_geometry(const seekdb_plugin_execution_context_v1_t *context)
{
  const seekdb_plugin_execution_result_v1_t result = {
      sizeof(result), "org.seekdb.gis.geometry", nullptr, 0, 1,
      {0, 0, 0, 0, 0, 0, 0}, {0, 0, 0, 0}};
  return context->emit_result(context->host, &result);
}

static bool scalar_double(const seekdb_plugin_execution_value_v1_t &value, double &out)
{
  if (value.struct_size != sizeof(value) || value.is_null || value.data == nullptr ||
      value.data_size != sizeof(double)) return false;
  std::memcpy(&out, value.data, sizeof(out));
  return std::isfinite(out);
}

static bool scalar_u32(const seekdb_plugin_execution_value_v1_t &value, uint32_t &out)
{
  if (value.struct_size != sizeof(value) || value.is_null || value.data == nullptr ||
      value.data_size != sizeof(uint32_t)) return false;
  std::memcpy(&out, value.data, sizeof(out));
  return true;
}

static bool strategy_value_valid(uint32_t type, double value)
{
  if (!std::isfinite(value) || type < 1 || type > 6) return false;
  if (type == 2 || type == 6) return value == 0;
  return value > 0 && (type == 4 || value <= 65536);
}

static void apply_buffer_strategy(const seekdb_plugin_execution_value_v1_t &argument,
                                  BufferOptions &options)
{
  // Same [uint32 strategy_type][double value] payload as the original
  // ObExprSTBufferStrategy / ObExprSTBuffer::parse_binary_strategy.
  if (argument.struct_size != sizeof(argument) || argument.is_null || argument.data == nullptr ||
      argument.data_size != 12 || argument.type_id == nullptr ||
      (std::strcmp(argument.type_id, "org.seekdb.gis.scalar.bytes") != 0 &&
       std::strcmp(argument.type_id, "core.type.blob") != 0)) throw CartesianInputError{};
  Reader reader(argument.data, argument.data_size);
  uint32_t type = 0;
  double value = 0;
  if (!reader.u32(&type) || !reader.number(&value) || !strategy_value_valid(type, value)) {
    throw CartesianInputError{};
  }
  if (type <= 2) {
    if (options.has_end) throw CartesianInputError{};
    options.has_end = true;
    if (type == 2) options.state |= 2;
    else options.end_count = static_cast<size_t>(value);
  } else if (type <= 4) {
    if (options.has_join) throw CartesianInputError{};
    options.has_join = true;
    if (type == 4) { options.state |= 4; options.miter_limit = value; }
    else options.join_count = static_cast<size_t>(value);
  } else {
    if (options.has_point) throw CartesianInputError{};
    options.has_point = true;
    if (type == 6) options.state |= 1;
    else options.point_count = static_cast<size_t>(value);
  }
}

static void add_point(Box &box, const Point &point)
{
  box.min_x = std::min(box.min_x, point.x);
  box.min_y = std::min(box.min_y, point.y);
  box.max_x = std::max(box.max_x, point.x);
  box.max_y = std::max(box.max_y, point.y);
}

static bool bounds(const Geometry &geometry, Box &box)
{
  for (const Point &point : geometry.points) add_point(box, point);
  for (const std::vector<Point> &ring : geometry.rings) {
    for (const Point &point : ring) add_point(box, point);
  }
  for (const Geometry &child : geometry.children) bounds(child, box);
  return std::isfinite(box.min_x) && std::isfinite(box.min_y) &&
         std::isfinite(box.max_x) && std::isfinite(box.max_y);
}

static Geometry rectangle(uint32_t srid, double min_x, double min_y,
                          double max_x, double max_y)
{
  Geometry geometry;
  geometry.type = 3;
  geometry.dimensions = 2;
  geometry.srid = srid;
  geometry.rings.resize(1);
  geometry.rings[0] = {{min_x, min_y, 0}, {min_x, max_y, 0},
                       {max_x, max_y, 0}, {max_x, min_y, 0},
                       {min_x, min_y, 0}};
  return geometry;
}

static Geometry empty_geometry(uint32_t srid)
{
  Geometry geometry;
  geometry.type = 7;
  geometry.dimensions = 2;
  geometry.srid = srid;
  return geometry;
}

#include "box_clip_adapter.ipp"
#include "mvt_adapter.ipp"
#include "geohash_adapter.ipp"
#include "best_srid_adapter.ipp"
#include "s2_adapter.ipp"

static bool transform_geometry(Geometry &geometry, const seekdb::gis::Projection &projection)
{
  const auto point_transform = [&](Point &point) {
    if (!projection.forward(point.x, point.y, point.z, geometry.dimensions)) return false;
    return std::isfinite(point.x) && std::isfinite(point.y);
  };
  for (Point &point : geometry.points)
    if (!point_transform(point)) return false;
  for (std::vector<Point> &ring : geometry.rings) {
    for (Point &point : ring)
      if (!point_transform(point)) return false;
  }
  for (Geometry &child : geometry.children)
    if (!transform_geometry(child, projection)) return false;
  return true;
}


#include "catalog_transform.ipp"

#include "wkt_parser.ipp"

static void append_number_text(std::ostringstream &stream, double value)
{
  stream.precision(17);
  stream << value;
}

#include "wkt_adapter.ipp"

static void geometry_to_geojson(const Geometry &geometry, std::ostringstream &stream)
{
  const char *name = geometry.type == 1 ? "Point" : geometry.type == 2 ? "LineString" :
      geometry.type == 3 ? "Polygon" : geometry.type == 4 ? "MultiPoint" :
      geometry.type == 5 ? "MultiLineString" : geometry.type == 6 ? "MultiPolygon" :
      "GeometryCollection";
  stream << "{\"type\":\"" << name << "\",\"coordinates\":";
  auto point_json = [&](const Point &point) {
    stream << '['; append_number_text(stream, point.x); stream << ','; append_number_text(stream, point.y);
    if (geometry.dimensions == 3) { stream << ','; append_number_text(stream, point.z); }
    stream << ']';
  };
  if (geometry.type == 1) point_json(geometry.points[0]);
  else if (geometry.type == 2) {
    stream << '['; for (size_t i = 0; i < geometry.points.size(); ++i) { if (i) stream << ','; point_json(geometry.points[i]); } stream << ']';
  } else if (geometry.type == 3) {
    stream << '['; for (size_t r = 0; r < geometry.rings.size(); ++r) { if (r) stream << ','; stream << '['; for (size_t i = 0; i < geometry.rings[r].size(); ++i) { if (i) stream << ','; point_json(geometry.rings[r][i]); } stream << ']'; } stream << ']';
  } else if (geometry.type >= 4 && geometry.type <= 6) {
    stream << '['; for (size_t i = 0; i < geometry.children.size(); ++i) { if (i) stream << ','; if (geometry.type == 4) point_json(geometry.children[i].points[0]); else { Geometry child = geometry.children[i]; std::ostringstream child_json; geometry_to_geojson(child, child_json); const std::string text = child_json.str(); const size_t start = text.find("\":"); const size_t end = text.rfind('}'); stream << (start == std::string::npos ? "[]" : text.substr(start + 2, end - start - 2)); } } stream << ']';
  } else {
    stream << "[]";
  }
  stream << '}';
}

static void collect_points(const Geometry &geometry, std::vector<Point> &points)
{
  points.insert(points.end(), geometry.points.begin(), geometry.points.end());
  for (const std::vector<Point> &ring : geometry.rings) points.insert(points.end(), ring.begin(), ring.end());
  for (const Geometry &child : geometry.children) collect_points(child, points);
}

static seekdb_plugin_status_t emit_bytes(
    const seekdb_plugin_execution_context_v1_t *context, const std::string &value,
    const char *type = "org.seekdb.gis.scalar.bytes")
{
  const seekdb_plugin_execution_result_v1_t result = {
      sizeof(result), type,
      reinterpret_cast<const uint8_t *>(value.data()), value.size(), 0,
      {0, 0, 0, 0, 0, 0, 0}, {0, 0, 0, 0}};
  return context->emit_result(context->host, &result);
}

#include "catalog_geometry_io.ipp"

// These SQL names mirror internal index-column placeholders, not S2 algorithms.
// The index row generator replaces their NULL slots with a *set* of S2 cells
// and the index MBR. A scalar representative point or envelope is not equivalent.
static seekdb_plugin_status_t spatial_index_placeholder(
    seekdb_plugin_instance_handle_t *instance,
    const seekdb_plugin_execution_context_v1_t *context,
    const seekdb_plugin_execution_value_v1_t *arguments,
    uint32_t argument_count, const char *result_type)
try
{
  if (instance == nullptr || !valid_context(context) || arguments == nullptr || argument_count != 1 ||
      arguments[0].struct_size != sizeof(arguments[0]) || arguments[0].type_id == nullptr ||
      std::strcmp(arguments[0].type_id, "org.seekdb.gis.geometry") != 0) {
    return SEEKDB_PLUGIN_STATUS_INVALID_ARGUMENT;
  }
  // Deliberately do not decode/dereference the geometry payload: the original
  // expression returns NULL without inspecting its operand.
  const seekdb_plugin_execution_result_v1_t result = {
      sizeof(result), result_type, nullptr, 0, 1,
      {0, 0, 0, 0, 0, 0, 0}, {0, 0, 0, 0}};
  return context->emit_result(context->host, &result);
}
catch (const std::bad_alloc &) { return SEEKDB_PLUGIN_STATUS_NO_MEMORY; }
catch (...) { return SEEKDB_PLUGIN_STATUS_INTERNAL; }

static seekdb_plugin_status_t emit_int32(
    const seekdb_plugin_execution_context_v1_t *context, int32_t value, bool is_null)
{
  const seekdb_plugin_execution_result_v1_t result = {
      sizeof(result), "org.seekdb.gis.scalar.int32",
      reinterpret_cast<const uint8_t *>(&value), sizeof(value),
      static_cast<uint8_t>(is_null ? 1 : 0),
      {0, 0, 0, 0, 0, 0, 0}, {0, 0, 0, 0}};
  return context->emit_result(context->host, &result);
}

} // namespace

extern "C" seekdb_plugin_status_t seekdb_gis_spatial_cover(
    seekdb_plugin_instance_handle_t *instance,
    const seekdb_plugin_execution_context_v1_t *context,
    const seekdb_plugin_execution_value_v1_t *arguments, uint32_t argument_count)
try {
  if (instance == nullptr || !valid_context(context) || arguments == nullptr || argument_count != 2) {
    return SEEKDB_PLUGIN_STATUS_INVALID_ARGUMENT;
  }
  const auto request = spatial_request(arguments[1]);
  Geometry geometry;
  if (!decode(arguments[0], geometry)) return SEEKDB_PLUGIN_STATUS_INVALID_ARGUMENT;
  const auto bytes = spatial_cover(geometry, request);
  seekdb_plugin_execution_result_v1_t result = {};
  result.struct_size = sizeof(result);
  result.type_id = request.flags & SEEKDB_PLUGIN_SPATIAL_ALL_VIEWS
      ? SEEKDB_PLUGIN_SPATIAL_COVER_ALL_RESULT_TYPE : SEEKDB_PLUGIN_SPATIAL_COVER_RESULT_TYPE;
  result.data = bytes.data();
  result.data_size = bytes.size();
  return context->emit_result(context->host, &result);
}
catch (const CartesianInputError &) { return SEEKDB_PLUGIN_STATUS_INVALID_ARGUMENT; }
catch (const std::bad_alloc &) { return SEEKDB_PLUGIN_STATUS_NO_MEMORY; }
catch (...) { return SEEKDB_PLUGIN_STATUS_INTERNAL; }

extern "C" seekdb_plugin_status_t seekdb_gis_buffer_strategy(
    seekdb_plugin_instance_handle_t *instance,
    const seekdb_plugin_execution_context_v1_t *context,
    const seekdb_plugin_execution_value_v1_t *arguments,
    uint32_t argument_count)
try
{
  if (instance == nullptr || !valid_context(context) || arguments == nullptr ||
      (argument_count != 1 && argument_count != 2)) return SEEKDB_PLUGIN_STATUS_INVALID_ARGUMENT;
  const auto &name = arguments[0];
  if (name.struct_size != sizeof(name) || name.is_null || name.data == nullptr ||
      name.data_size > 12 || name.type_id == nullptr ||
      (std::strcmp(name.type_id, "org.seekdb.gis.scalar.bytes") != 0 &&
       std::strcmp(name.type_id, "core.type.blob") != 0)) return SEEKDB_PLUGIN_STATUS_INVALID_ARGUMENT;
  std::string normalized(reinterpret_cast<const char *>(name.data), name.data_size);
  for (auto &c : normalized) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  const char *names[] = {"", "end_round", "end_flat", "join_round", "join_miter", "point_circle", "point_square"};
  uint32_t type = 0;
  for (uint32_t i = 1; i <= 6; ++i) if (normalized == names[i]) type = i;
  const bool parameterless = type == 2 || type == 6;
  double value = 0;
  if (type == 0 || (parameterless ? argument_count != 1 : argument_count != 2) ||
      (!parameterless && !scalar_double(arguments[1], value)) || !strategy_value_valid(type, value)) {
    return SEEKDB_PLUGIN_STATUS_INVALID_ARGUMENT;
  }
  std::vector<uint8_t> bytes;
  append_u32(bytes, type);
  append_number(bytes, value);
  const seekdb_plugin_execution_result_v1_t result = {
      sizeof(result), "org.seekdb.gis.scalar.bytes", bytes.data(), bytes.size(), 0,
      {0, 0, 0, 0, 0, 0, 0}, {0, 0, 0, 0}};
  return context->emit_result(context->host, &result);
}
catch (const std::bad_alloc &) { return SEEKDB_PLUGIN_STATUS_NO_MEMORY; }
catch (...) { return SEEKDB_PLUGIN_STATUS_INTERNAL; }

extern "C" seekdb_plugin_status_t seekdb_gis_geometry_operation(
    uint32_t operation,
    seekdb_plugin_instance_handle_t *instance,
    const seekdb_plugin_execution_context_v1_t *context,
    const seekdb_plugin_execution_value_v1_t *arguments,
    uint32_t argument_count)
try
{
  if (instance == nullptr || context == nullptr || arguments == nullptr ||
      !valid_context(context) || argument_count == 0 || argument_count > 8) {
    return SEEKDB_PLUGIN_STATUS_INVALID_ARGUMENT;
  }
  if (operation == SEEKDB_GIS_OP_ASMVTGEOM) {
    if (argument_count < 2 || argument_count > 5) return SEEKDB_PLUGIN_STATUS_INVALID_ARGUMENT;
    for (uint32_t i = 0; i < argument_count; ++i) {
      if (arguments[i].struct_size != sizeof(arguments[i])) return SEEKDB_PLUGIN_STATUS_INVALID_ARGUMENT;
    }
    double extent = 4096.0, buffer = 256.0, clip_value = 1.0;
    if ((argument_count >= 3 && !arguments[2].is_null && !scalar_double(arguments[2], extent)) ||
        (argument_count >= 4 && !arguments[3].is_null && !scalar_double(arguments[3], buffer)) ||
        (argument_count >= 5 && !arguments[4].is_null && !scalar_double(arguments[4], clip_value))) {
      return SEEKDB_PLUGIN_STATUS_INVALID_ARGUMENT;
    }
    validate_tile_controls(extent, buffer);
    if (std::trunc(clip_value) != clip_value || clip_value < INT8_MIN || clip_value > INT8_MAX) {
      return SEEKDB_PLUGIN_STATUS_INVALID_ARGUMENT;
    }
    // Original process_input_geometry validates controls even for NULL input.
    if (arguments[0].is_null) return emit_null_geometry(context);
    Geometry input, tile_bounds;
    if (!decode(arguments[0], input) || !decode(arguments[1], tile_bounds)) return SEEKDB_PLUGIN_STATUS_INVALID_ARGUMENT;
    const auto result = as_mvt_geometry(input, tile_bounds, extent, buffer, clip_value != 0);
    return result ? emit_geometry(context, *result) : emit_null_geometry(context);
  }
  Geometry first;
  if (!decode(arguments[0], first)) return SEEKDB_PLUGIN_STATUS_INVALID_ARGUMENT;
  if (operation == SEEKDB_GIS_OP_TRANSFORM) {
    uint32_t target_srid = 0;
    if (argument_count != 2 || !scalar_u32(arguments[1], target_srid)) {
      return SEEKDB_PLUGIN_STATUS_INVALID_ARGUMENT;
    }
    const auto status = catalog_transform(context, first, target_srid);
    if (status != SEEKDB_PLUGIN_STATUS_OK) return status;
    first.srid = target_srid;
    return emit_geometry(context, first);
  }
  if (operation == SEEKDB_GIS_OP_INDEX_BUFFER) {
    double distance = 0.0;
    if (argument_count != 2 || arguments[1].type_id == nullptr ||
        std::strcmp(arguments[1].type_id, "core.type.double") != 0 ||
        !scalar_double(arguments[1], distance)) {
      return SEEKDB_PLUGIN_STATUS_INVALID_ARGUMENT;
    }
    for (uint32_t i = 0; i < argument_count; ++i) {
      for (auto r : arguments[i].reserved_bytes) if (r != 0) return SEEKDB_PLUGIN_STATUS_INVALID_ARGUMENT;
      for (auto r : arguments[i].reserved) if (r != 0) return SEEKDB_PLUGIN_STATUS_INVALID_ARGUMENT;
    }
    // The index caller has resolved the SRS and selected its Cartesian path.
    // Adapt labels only, not coordinates or dimensions, so the existing
    // Cartesian algorithm also serves arbitrary projected SRIDs. This is not
    // the public SQL buffer's SRS admission path.
    const uint32_t srid = first.srid;
    const auto relabel = [](auto &&self, Geometry &geometry, uint32_t id) -> void {
      geometry.srid = id;
      for (auto &child : geometry.children) self(self, child, id);
    };
    relabel(relabel, first, 0);
    BufferOptions options; options.distance = distance;
    Geometry result = buffer_geometry(first, options);
    relabel(relabel, result, srid);
    return emit_geometry(context, result);
  }
  if (operation == SEEKDB_GIS_OP_BUFFER) {
    double distance = 0.0;
    if (argument_count < 2 || !scalar_double(arguments[1], distance)) {
      return SEEKDB_PLUGIN_STATUS_INVALID_ARGUMENT;
    }
    if (argument_count > 5) return SEEKDB_PLUGIN_STATUS_INVALID_ARGUMENT;
    BufferOptions options;
    options.distance = distance;
    for (uint32_t i = 2; i < argument_count; ++i) apply_buffer_strategy(arguments[i], options);
    return emit_geometry(context, buffer_geometry(first, options));
  }
  if (operation == SEEKDB_GIS_OP_MAKE_VALID) {
    if (argument_count != 1) return SEEKDB_PLUGIN_STATUS_INVALID_ARGUMENT;
    return emit_geometry(context, make_valid_geometry(first));
  }
  if (operation == SEEKDB_GIS_OP_CLIP_BY_BOX || operation == SEEKDB_GIS_OP_UNION ||
      operation == SEEKDB_GIS_OP_DIFFERENCE || operation == SEEKDB_GIS_OP_SYMMETRIC_DIFFERENCE) {
    if (argument_count < 2) return SEEKDB_PLUGIN_STATUS_INVALID_ARGUMENT;
    Geometry second;
    if (!decode(arguments[1], second)) return SEEKDB_PLUGIN_STATUS_INVALID_ARGUMENT;
    if (operation == SEEKDB_GIS_OP_CLIP_BY_BOX) {
      if (argument_count != 2) return SEEKDB_PLUGIN_STATUS_INVALID_ARGUMENT;
      const auto clipped = clip_by_box(first, second);
      if (!clipped) {
        const seekdb_plugin_execution_result_v1_t result = {
            sizeof(result), "org.seekdb.gis.geometry", nullptr, 0, 1,
            {0, 0, 0, 0, 0, 0, 0}, {0, 0, 0, 0}};
        return context->emit_result(context->host, &result);
      }
      return emit_geometry(context, *clipped);
    }
    return emit_geometry(context, combine_polygons(first, second, operation));
  }
  return SEEKDB_PLUGIN_STATUS_INTERNAL;
}
catch (const CartesianInputError &) { return SEEKDB_PLUGIN_STATUS_INVALID_ARGUMENT; }
catch (const seekdb::gis::ProjectionInputError &) { return SEEKDB_PLUGIN_STATUS_INVALID_ARGUMENT; }
catch (const std::bad_alloc &) { return SEEKDB_PLUGIN_STATUS_NO_MEMORY; }
catch (...) { return SEEKDB_PLUGIN_STATUS_INTERNAL; }


extern "C" seekdb_plugin_status_t seekdb_gis_srs_transform(
    seekdb_plugin_instance_handle_t *instance,
    const seekdb_plugin_execution_context_v1_t *context,
    const seekdb_plugin_execution_value_v1_t *arguments, uint32_t count)
try {
  if (instance == nullptr || !valid_context(context) || arguments == nullptr || count != 4)
    return SEEKDB_PLUGIN_STATUS_INVALID_ARGUMENT;
  for (uint32_t i = 0; i < count; ++i) {
    const auto &a = arguments[i];
    if (a.struct_size != sizeof(a) || a.is_null || a.type_id == nullptr || a.data == nullptr)
      return SEEKDB_PLUGIN_STATUS_INVALID_ARGUMENT;
    for (auto r : a.reserved_bytes) if (r != 0) return SEEKDB_PLUGIN_STATUS_INVALID_ARGUMENT;
    for (auto r : a.reserved) if (r != 0) return SEEKDB_PLUGIN_STATUS_INVALID_ARGUMENT;
  }
  for (uint32_t i : {1u, 2u}) {
    if (std::strcmp(arguments[i].type_id, "core.type.bytes") != 0 || arguments[i].data_size == 0 ||
        arguments[i].data_size > SEEKDB_PLUGIN_SRS_MAX_PROJ4_BYTES) return SEEKDB_PLUGIN_STATUS_INVALID_ARGUMENT;
  }
  uint32_t target;
  if (std::strcmp(arguments[0].type_id, "org.seekdb.gis.geometry") != 0 ||
      std::strcmp(arguments[3].type_id, "core.type.uint32") != 0 || arguments[3].data_size != sizeof(target))
    return SEEKDB_PLUGIN_STATUS_INVALID_ARGUMENT;
  std::memcpy(&target, arguments[3].data, sizeof(target));
  Geometry geometry;
  if (!decode(arguments[0], geometry)) return SEEKDB_PLUGIN_STATUS_INVALID_ARGUMENT;
  const seekdb::gis::Projection projection(
      std::string(reinterpret_cast<const char *>(arguments[1].data), arguments[1].data_size),
      std::string(reinterpret_cast<const char *>(arguments[2].data), arguments[2].data_size));
  if (!transform_geometry(geometry, projection)) return SEEKDB_PLUGIN_STATUS_INVALID_ARGUMENT;
  geometry.srid = target;
  return emit_geometry(context, geometry);
}
catch (const seekdb::gis::ProjectionInputError &) { return SEEKDB_PLUGIN_STATUS_INVALID_ARGUMENT; }
catch (const std::bad_alloc &) { return SEEKDB_PLUGIN_STATUS_NO_MEMORY; }
catch (...) { return SEEKDB_PLUGIN_STATUS_INTERNAL; }

extern "C" seekdb_plugin_status_t seekdb_gis_relation_operation(
    uint32_t operation,
    seekdb_plugin_instance_handle_t *instance,
    const seekdb_plugin_execution_context_v1_t *context,
    const seekdb_plugin_execution_value_v1_t *arguments,
    uint32_t argument_count)
try
{
  if (instance == nullptr || !valid_context(context) || arguments == nullptr ||
      (operation == SEEKDB_GIS_REL_DWITHIN ? argument_count != 3 : argument_count != 2)) {
    return SEEKDB_PLUGIN_STATUS_INVALID_ARGUMENT;
  }
  Geometry left, right;
  if (!decode(arguments[0], left) || !decode(arguments[1], right)) {
    return SEEKDB_PLUGIN_STATUS_INVALID_ARGUMENT;
  }
  double limit = 0.0;
  if (operation == SEEKDB_GIS_REL_DWITHIN && !scalar_double(arguments[2], limit)) {
    return SEEKDB_PLUGIN_STATUS_INVALID_ARGUMENT;
  }
  return emit_bool(context, relation_result(operation, left, right, limit));
}
catch (const CartesianInputError &) { return SEEKDB_PLUGIN_STATUS_INVALID_ARGUMENT; }
catch (const std::bad_alloc &) { return SEEKDB_PLUGIN_STATUS_NO_MEMORY; }
catch (...) { return SEEKDB_PLUGIN_STATUS_INTERNAL; }


extern "C" seekdb_plugin_status_t seekdb_gis_centroid_operation(
    uint8_t surface_only,
    seekdb_plugin_instance_handle_t *instance,
    const seekdb_plugin_execution_context_v1_t *context,
    const seekdb_plugin_execution_value_v1_t *arguments,
    uint32_t argument_count)
try
{
  if (surface_only > 1) return SEEKDB_PLUGIN_STATUS_INVALID_ARGUMENT;
  if (instance == nullptr || !valid_context(context) || arguments == nullptr || argument_count != 1) {
    return SEEKDB_PLUGIN_STATUS_INVALID_ARGUMENT;
  }
  Geometry input;
  if (!decode(arguments[0], input)) return SEEKDB_PLUGIN_STATUS_INVALID_ARGUMENT;
  const auto result_geometry = surface_only ? point_on_surface(input) : centroid(input);
  if (!surface_only && result_geometry.type == 7) {
    const seekdb_plugin_execution_result_v1_t result = {
        sizeof(result), "org.seekdb.gis.geometry", nullptr, 0, 1,
        {0, 0, 0, 0, 0, 0, 0}, {0, 0, 0, 0}};
    return context->emit_result(context->host, &result);
  }
  return emit_geometry(context, result_geometry);
}
catch (const CartesianInputError &) { return SEEKDB_PLUGIN_STATUS_INVALID_ARGUMENT; }
catch (const std::bad_alloc &) { return SEEKDB_PLUGIN_STATUS_NO_MEMORY; }
catch (...) { return SEEKDB_PLUGIN_STATUS_INTERNAL; }


extern "C" seekdb_plugin_status_t seekdb_gis_mbr_operation(
    seekdb_plugin_instance_handle_t *instance,
    const seekdb_plugin_execution_context_v1_t *context,
    const seekdb_plugin_execution_value_v1_t *arguments,
    uint32_t argument_count)
{
  return spatial_index_placeholder(instance, context, arguments, argument_count,
                                   "org.seekdb.gis.scalar.bytes");
}

extern "C" seekdb_plugin_status_t seekdb_gis_valid_operation(
    seekdb_plugin_instance_handle_t *instance,
    const seekdb_plugin_execution_context_v1_t *context,
    const seekdb_plugin_execution_value_v1_t *arguments,
    uint32_t argument_count)
try
{
  if (instance == nullptr || !valid_context(context) || arguments == nullptr || argument_count != 1) {
    return SEEKDB_PLUGIN_STATUS_INVALID_ARGUMENT;
  }
  Geometry input;
  if (!decode(arguments[0], input)) return SEEKDB_PLUGIN_STATUS_INVALID_ARGUMENT;
  return emit_bool(context, valid_geometry(input));
}
catch (const CartesianInputError &) { return SEEKDB_PLUGIN_STATUS_INVALID_ARGUMENT; }
catch (const std::bad_alloc &) { return SEEKDB_PLUGIN_STATUS_NO_MEMORY; }
catch (...) { return SEEKDB_PLUGIN_STATUS_INTERNAL; }

extern "C" seekdb_plugin_status_t seekdb_gis_geometrytype_operation(
    seekdb_plugin_instance_handle_t *instance,
    const seekdb_plugin_execution_context_v1_t *context,
    const seekdb_plugin_execution_value_v1_t *arguments,
    uint32_t argument_count)
{
  if (instance == nullptr || !valid_context(context) || arguments == nullptr || argument_count != 1) {
    return SEEKDB_PLUGIN_STATUS_INVALID_ARGUMENT;
  }
  Geometry input;
  if (!decode(arguments[0], input)) return SEEKDB_PLUGIN_STATUS_INVALID_ARGUMENT;
  const char *name = nullptr;
  switch (input.type) {
    case 1: name = "POINT"; break;
    case 2: name = "LINESTRING"; break;
    case 3: name = "POLYGON"; break;
    case 4: name = "MULTIPOINT"; break;
    case 5: name = "MULTILINESTRING"; break;
    case 6: name = "MULTIPOLYGON"; break;
    case 7: name = "GEOMETRYCOLLECTION"; break;
    default: return SEEKDB_PLUGIN_STATUS_INVALID_ARGUMENT;
  }
  return emit_bytes(context, name);
}

extern "C" seekdb_plugin_status_t seekdb_gis_collection_operation(
    seekdb_plugin_instance_handle_t *instance,
    const seekdb_plugin_execution_context_v1_t *context,
    const seekdb_plugin_execution_value_v1_t *arguments,
    uint32_t argument_count)
{
  if (instance == nullptr || !valid_context(context) || arguments == nullptr || argument_count != 1) {
    return SEEKDB_PLUGIN_STATUS_INVALID_ARGUMENT;
  }
  Geometry input;
  if (!decode(arguments[0], input)) return SEEKDB_PLUGIN_STATUS_INVALID_ARGUMENT;
  return emit_bool(context, input.type >= 4 && input.type <= 7);
}

extern "C" seekdb_plugin_status_t seekdb_gis_interior_rings_operation(
    seekdb_plugin_instance_handle_t *instance,
    const seekdb_plugin_execution_context_v1_t *context,
    const seekdb_plugin_execution_value_v1_t *arguments,
    uint32_t argument_count)
{
  if (instance == nullptr || !valid_context(context) || arguments == nullptr || argument_count != 1) {
    return SEEKDB_PLUGIN_STATUS_INVALID_ARGUMENT;
  }
  Geometry input;
  if (!decode(arguments[0], input)) return SEEKDB_PLUGIN_STATUS_INVALID_ARGUMENT;
  if (input.type != 3) return emit_int32(context, 0, true);
  const int32_t rings = input.rings.empty() ? 0 : static_cast<int32_t>(input.rings.size() - 1);
  return emit_int32(context, rings, false);
}

#include "ewkt_adapter.ipp"

extern "C" seekdb_plugin_status_t seekdb_gis_text_operation(
    uint32_t operation,
    seekdb_plugin_instance_handle_t *instance,
    const seekdb_plugin_execution_context_v1_t *context,
    const seekdb_plugin_execution_value_v1_t *arguments,
    uint32_t argument_count)
try {
  if (instance == nullptr || !valid_context(context) || arguments == nullptr || argument_count == 0) {
    return SEEKDB_PLUGIN_STATUS_INVALID_ARGUMENT;
  }
  if (operation >= SEEKDB_GIS_TEXT_CATALOG_FROM_TEXT && operation <= SEEKDB_GIS_TEXT_AS_EWKB)
    return catalog_geometry_io(operation, context, arguments, argument_count);
  if (operation == SEEKDB_GIS_TEXT_AS_EWKT)
    return geometry_as_ewkt(context, arguments, argument_count);
  if (operation == SEEKDB_GIS_TEXT_FROM_TEXT) {
    if (argument_count > 2 || arguments[0].struct_size != sizeof(arguments[0]) ||
        arguments[0].is_null || arguments[0].data == nullptr || arguments[0].data_size == 0 ||
        arguments[0].data_size > 1024 * 1024 || arguments[0].type_id == nullptr ||
        std::strcmp(arguments[0].type_id, "org.seekdb.gis.scalar.bytes") != 0) {
      return SEEKDB_PLUGIN_STATUS_INVALID_ARGUMENT;
    }
    uint32_t srid = 0;
    if (argument_count == 2 && !scalar_u32(arguments[1], srid)) return SEEKDB_PLUGIN_STATUS_INVALID_ARGUMENT;
    WktParser parser(reinterpret_cast<const char *>(arguments[0].data),
                     static_cast<size_t>(arguments[0].data_size));
    Geometry geometry;
    if (!parser.parse(geometry, srid)) return SEEKDB_PLUGIN_STATUS_INVALID_ARGUMENT;
    return emit_geometry(context, geometry);
  }
  if (argument_count != 1) return SEEKDB_PLUGIN_STATUS_INVALID_ARGUMENT;
  Geometry geometry;
  if (!decode(arguments[0], geometry)) return SEEKDB_PLUGIN_STATUS_INVALID_ARGUMENT;
  if (operation == SEEKDB_GIS_TEXT_AS_TEXT) {
    std::string text;
    if (!geometry_to_wkt(geometry, text)) return SEEKDB_PLUGIN_STATUS_INVALID_ARGUMENT;
    return emit_bytes(context, text, "core.type.text");
  }
  std::ostringstream stream;
  if (operation == SEEKDB_GIS_TEXT_AS_GEOJSON) geometry_to_geojson(geometry, stream);
  else return SEEKDB_PLUGIN_STATUS_INVALID_ARGUMENT;
  return emit_bytes(context, stream.str(), "core.type.text");
}
catch (const std::bad_alloc &) { return SEEKDB_PLUGIN_STATUS_NO_MEMORY; }
catch (...) { return SEEKDB_PLUGIN_STATUS_INTERNAL; }

extern "C" seekdb_plugin_status_t seekdb_gis_wkb_from_bytes(
    seekdb_plugin_instance_handle_t *instance,
    const seekdb_plugin_execution_context_v1_t *context,
    const seekdb_plugin_execution_value_v1_t *arguments,
    uint32_t argument_count)
{
  if (instance == nullptr || !valid_context(context) || arguments == nullptr ||
      (argument_count != 1 && argument_count != 2) ||
      arguments[0].struct_size != sizeof(arguments[0]) || arguments[0].is_null ||
      arguments[0].data == nullptr || arguments[0].data_size < 9 ||
      arguments[0].data_size > 16 * 1024 * 1024 || arguments[0].type_id == nullptr ||
      std::strcmp(arguments[0].type_id, "org.seekdb.gis.scalar.bytes") != 0) {
    return SEEKDB_PLUGIN_STATUS_INVALID_ARGUMENT;
  }
  uint32_t srid = 0;
  if (argument_count == 2 && !scalar_u32(arguments[1], srid)) {
    return SEEKDB_PLUGIN_STATUS_INVALID_ARGUMENT;
  }
  std::vector<uint8_t> encoded;
  encoded.reserve(static_cast<size_t>(arguments[0].data_size) + 5);
  append_u32(encoded, srid);
  encoded.push_back(1);
  encoded.insert(encoded.end(), arguments[0].data,
                 arguments[0].data + arguments[0].data_size);
  seekdb_plugin_execution_value_v1_t geometry_value = {};
  geometry_value.struct_size = sizeof(geometry_value);
  geometry_value.type_id = "org.seekdb.gis.geometry";
  geometry_value.data = encoded.data();
  geometry_value.data_size = encoded.size();
  Geometry geometry;
  if (!decode(geometry_value, geometry)) return SEEKDB_PLUGIN_STATUS_INVALID_ARGUMENT;
  return emit_geometry(context, geometry);
}

extern "C" seekdb_plugin_status_t seekdb_gis_geohash_operation(
    seekdb_plugin_instance_handle_t *instance,
    const seekdb_plugin_execution_context_v1_t *context,
    const seekdb_plugin_execution_value_v1_t *arguments,
    uint32_t argument_count)
try
{
  if (instance == nullptr || !valid_context(context) || arguments == nullptr ||
      (argument_count != 1 && argument_count != 2) || arguments[0].struct_size != sizeof(arguments[0])) {
    return SEEKDB_PLUGIN_STATUS_INVALID_ARGUMENT;
  }
  std::optional<std::string> hash;
  if (!arguments[0].is_null) {
    Geometry input;
    if (!decode(arguments[0], input)) return SEEKDB_PLUGIN_STATUS_INVALID_ARGUMENT;
    validate_geohash_input(input, input.srid);
    // Original empty/NULL input short-circuits precision evaluation.
    if (!tile_empty(input)) {
      int64_t precision = 0;
      if (argument_count == 2) {
        const auto &value = arguments[1];
        if (value.struct_size != sizeof(value)) return SEEKDB_PLUGIN_STATUS_INVALID_ARGUMENT;
        if (!value.is_null) {
          if (!value.type_id || std::strcmp(value.type_id, "core.type.int64") != 0 ||
              !value.data || value.data_size != sizeof(precision)) return SEEKDB_PLUGIN_STATUS_INVALID_ARGUMENT;
          std::memcpy(&precision, value.data, sizeof(precision));
        }
      }
      hash = geometry_geohash(input, precision);
    }
  }
  if (hash) return emit_bytes(context, *hash);
  const seekdb_plugin_execution_result_v1_t result = {
      sizeof(result), "org.seekdb.gis.scalar.bytes", nullptr, 0, 1,
      {0, 0, 0, 0, 0, 0, 0}, {0, 0, 0, 0}};
  return context->emit_result(context->host, &result);
}
catch (const CartesianInputError &) { return SEEKDB_PLUGIN_STATUS_INVALID_ARGUMENT; }
catch (const std::bad_alloc &) { return SEEKDB_PLUGIN_STATUS_NO_MEMORY; }
catch (...) { return SEEKDB_PLUGIN_STATUS_INTERNAL; }

extern "C" seekdb_plugin_status_t seekdb_gis_best_srid_operation(
    seekdb_plugin_instance_handle_t *instance,
    const seekdb_plugin_execution_context_v1_t *context,
    const seekdb_plugin_execution_value_v1_t *arguments,
    uint32_t argument_count)
try
{
  if (instance == nullptr || !valid_context(context) || arguments == nullptr ||
      (argument_count != 1 && argument_count != 2)) return SEEKDB_PLUGIN_STATUS_INVALID_ARGUMENT;
  for (uint32_t i = 0; i < argument_count; ++i) {
    if (arguments[i].struct_size != sizeof(arguments[i])) return SEEKDB_PLUGIN_STATUS_INVALID_ARGUMENT;
    if (arguments[i].is_null) {
      const seekdb_plugin_execution_result_v1_t result = {
          sizeof(result), "org.seekdb.gis.scalar.int32", nullptr, 0, 1,
          {0, 0, 0, 0, 0, 0, 0}, {0, 0, 0, 0}};
      return context->emit_result(context->host, &result);
    }
  }
  Geometry first, second;
  if (!decode(arguments[0], first) || (argument_count == 2 && !decode(arguments[1], second))) {
    return SEEKDB_PLUGIN_STATUS_INVALID_ARGUMENT;
  }
  const int32_t srid = geometry_best_srid(first, argument_count == 2 ? &second : nullptr);
  const seekdb_plugin_execution_result_v1_t result = {
      sizeof(result), "org.seekdb.gis.scalar.int32", reinterpret_cast<const uint8_t *>(&srid), sizeof(srid), 0,
      {0, 0, 0, 0, 0, 0, 0}, {0, 0, 0, 0}};
  return context->emit_result(context->host, &result);
}
catch (const CartesianInputError &) { return SEEKDB_PLUGIN_STATUS_INVALID_ARGUMENT; }
catch (const std::bad_alloc &) { return SEEKDB_PLUGIN_STATUS_NO_MEMORY; }
catch (...) { return SEEKDB_PLUGIN_STATUS_INTERNAL; }

extern "C" seekdb_plugin_status_t seekdb_gis_spatial_cellid_operation(
    seekdb_plugin_instance_handle_t *instance,
    const seekdb_plugin_execution_context_v1_t *context,
    const seekdb_plugin_execution_value_v1_t *arguments,
    uint32_t argument_count)
{
  return spatial_index_placeholder(instance, context, arguments, argument_count,
                                   "org.seekdb.gis.scalar.uint64");
}

extern "C" seekdb_plugin_status_t seekdb_gis_set_srid_operation(
    seekdb_plugin_instance_handle_t *instance,
    const seekdb_plugin_execution_context_v1_t *context,
    const seekdb_plugin_execution_value_v1_t *arguments,
    uint32_t argument_count)
{
  if (instance == nullptr || !valid_context(context) || arguments == nullptr || argument_count != 2) {
    return SEEKDB_PLUGIN_STATUS_INVALID_ARGUMENT;
  }
  Geometry geometry;
  uint32_t srid = 0;
  if (!decode(arguments[0], geometry) || !scalar_u32(arguments[1], srid)) {
    return SEEKDB_PLUGIN_STATUS_INVALID_ARGUMENT;
  }
  geometry.srid = srid;
  return emit_geometry(context, geometry);
}

extern "C" seekdb_plugin_status_t seekdb_gis_validate_encoded_geometry(
    seekdb_plugin_instance_handle_t *instance,
    const uint8_t *encoded,
    uint64_t encoded_size)
{
  if (instance == nullptr || encoded == nullptr || encoded_size < 10 ||
      encoded_size > 16 * 1024 * 1024) {
    return SEEKDB_PLUGIN_STATUS_INVALID_ARGUMENT;
  }
  seekdb_plugin_execution_value_v1_t value = {};
  value.struct_size = sizeof(value);
  value.type_id = "org.seekdb.gis.geometry";
  value.data = encoded;
  value.data_size = encoded_size;
  Geometry geometry;
  return decode(value, geometry) ? SEEKDB_PLUGIN_STATUS_OK
                                 : SEEKDB_PLUGIN_STATUS_INVALID_ARGUMENT;
}

extern "C" seekdb_plugin_status_t seekdb_gis_metric_operation(
    uint32_t operation,
    seekdb_plugin_instance_handle_t *instance,
    const seekdb_plugin_execution_context_v1_t *context,
    const seekdb_plugin_execution_value_v1_t *arguments,
    uint32_t argument_count)
try
{
  if (operation < SEEKDB_GIS_METRIC_AREA || operation > SEEKDB_GIS_METRIC_DISTANCE_SPHERE ||
      instance == nullptr || !valid_context(context) || arguments == nullptr ||
      ((operation == SEEKDB_GIS_METRIC_DISTANCE || operation == SEEKDB_GIS_METRIC_DISTANCE_SPHERE)
          ? argument_count != 2 : argument_count != 1)) {
    return SEEKDB_PLUGIN_STATUS_INVALID_ARGUMENT;
  }
  Geometry first;
  if (!decode(arguments[0], first)) return SEEKDB_PLUGIN_STATUS_INVALID_ARGUMENT;
  double value = 0.0;
  if (operation == SEEKDB_GIS_METRIC_AREA) value = geometry_area(first);
  else if (operation == SEEKDB_GIS_METRIC_LENGTH) value = geometry_length(first);
  else {
    Geometry second;
    if (!decode(arguments[1], second)) return SEEKDB_PLUGIN_STATUS_INVALID_ARGUMENT;
    if (operation == SEEKDB_GIS_METRIC_DISTANCE) {
      value = geometry_distance(first, second);
    } else if (operation == SEEKDB_GIS_METRIC_DISTANCE_SPHERE) {
      // Do not substitute Cartesian distance for an unsupported spherical call.
      if (first.srid != second.srid || (first.srid != 0 && first.srid != 4326) ||
          first.dimensions != 2 || second.dimensions != 2 ||
          first.type != 1 || second.type != 1 || first.points.empty() || second.points.empty()) {
        return SEEKDB_PLUGIN_STATUS_INVALID_ARGUMENT;
      }
      {
        for (const Geometry *geometry : {&first, &second}) {
          if (geometry->points[0].x < -180.0 || geometry->points[0].x > 180.0 ||
              geometry->points[0].y < -90.0 || geometry->points[0].y > 90.0) {
            return SEEKDB_PLUGIN_STATUS_INVALID_ARGUMENT;
          }
        }
        const double radians = 0.017453292519943295;
        const double lat1 = first.points[0].y * radians;
        const double lat2 = second.points[0].y * radians;
        const double dlat = (second.points[0].y - first.points[0].y) * radians;
        const double dlon = (second.points[0].x - first.points[0].x) * radians;
        const double h = std::sin(dlat * 0.5) * std::sin(dlat * 0.5) +
                         std::cos(lat1) * std::cos(lat2) *
                         std::sin(dlon * 0.5) * std::sin(dlon * 0.5);
        value = 6371008.8 * 2.0 * std::atan2(std::sqrt(h), std::sqrt(std::max(0.0, 1.0 - h)));
      }
    } else return SEEKDB_PLUGIN_STATUS_INVALID_ARGUMENT;
  }
  const seekdb_plugin_execution_result_v1_t result = {
      sizeof(result), "org.seekdb.gis.scalar.float64",
      reinterpret_cast<const uint8_t *>(&value), sizeof(value), 0,
      {0, 0, 0, 0, 0, 0, 0}, {0, 0, 0, 0}};
  return context->emit_result(context->host, &result);
}
catch (const CartesianInputError &) { return SEEKDB_PLUGIN_STATUS_INVALID_ARGUMENT; }
catch (const std::bad_alloc &) { return SEEKDB_PLUGIN_STATUS_NO_MEMORY; }
catch (...) { return SEEKDB_PLUGIN_STATUS_INTERNAL; }
