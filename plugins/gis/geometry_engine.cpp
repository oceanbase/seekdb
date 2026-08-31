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

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>
#include <sstream>
#include <string>
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
    if (out == nullptr || data_ == nullptr || data_ + count > end_) return false;
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

static bool read_geometry(Reader &reader, Geometry &geometry, uint32_t srid)
{
  const uint8_t *order = nullptr;
  uint32_t encoded_type = 0;
  if (!reader.bytes(1, &order) || (*order != 0 && *order != 1)) return false;
  reader.set_little_endian(*order == 1);
  if (!reader.u32(&encoded_type)) return false;
  const bool ewkb_z = (encoded_type & UINT32_C(0x80000000)) != 0;
  const bool ewkb_m = (encoded_type & UINT32_C(0x40000000)) != 0;
  const bool ewkb_srid = (encoded_type & UINT32_C(0x20000000)) != 0;
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
    if (!reader.u32(&count) || count < 2 || count > 1000000) return false;
    geometry.points.resize(count);
    for (Point &point : geometry.points) {
      if (!reader.number(&point.x) || !reader.number(&point.y) ||
          (geometry.dimensions == 3 && !reader.number(&point.z))) return false;
    }
  } else if (type == 3) {
    uint32_t ring_count = 0;
    if (!reader.u32(&ring_count) || ring_count > 100000) return false;
    geometry.rings.resize(ring_count);
    for (std::vector<Point> &ring : geometry.rings) {
      uint32_t count = 0;
      if (!reader.u32(&count) || count < 4 || count > 1000000) return false;
      ring.resize(count);
      for (Point &point : ring) {
        if (!reader.number(&point.x) || !reader.number(&point.y) ||
            (geometry.dimensions == 3 && !reader.number(&point.z))) return false;
      }
    }
  } else {
    uint32_t count = 0;
    if (!reader.u32(&count) || count > 100000) return false;
    geometry.children.resize(count);
    for (Geometry &child : geometry.children) {
      if (!read_geometry(reader, child, srid)) return false;
    }
  }
  return true;
}

static bool decode(const seekdb_plugin_execution_value_v1_t &value, Geometry &geometry)
{
  if (value.struct_size != sizeof(value) || value.is_null || value.data == nullptr ||
      value.data_size < 10 || value.type_id == nullptr ||
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
  return context != nullptr && context->struct_size == sizeof(*context) &&
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

static bool same_point(const Point &left, const Point &right)
{
  return left.x == right.x && left.y == right.y && left.z == right.z;
}

static bool point_in_ring(const Point &point, const std::vector<Point> &ring)
{
  bool inside = false;
  if (ring.size() < 4) return false;
  for (size_t i = 0, j = ring.size() - 1; i < ring.size(); j = i++) {
    const Point &a = ring[i];
    const Point &b = ring[j];
    const bool crosses = ((a.y > point.y) != (b.y > point.y)) &&
        (point.x < (b.x - a.x) * (point.y - a.y) / (b.y - a.y) + a.x);
    if (crosses) inside = !inside;
  }
  return inside;
}

static bool point_in_geometry(const Point &point, const Geometry &geometry)
{
  if (geometry.type == 1) return !geometry.points.empty() && same_point(point, geometry.points[0]);
  if (geometry.type == 3) {
    if (geometry.rings.empty() || !point_in_ring(point, geometry.rings[0])) return false;
    for (size_t i = 1; i < geometry.rings.size(); ++i) {
      if (point_in_ring(point, geometry.rings[i])) return false;
    }
    return true;
  }
  for (const Geometry &child : geometry.children) {
    if (point_in_geometry(point, child)) return true;
  }
  return false;
}

static bool boxes_intersect(const Box &left, const Box &right)
{
  return left.min_x <= right.max_x && right.min_x <= left.max_x &&
         left.min_y <= right.max_y && right.min_y <= left.max_y;
}

static double boxes_distance(const Box &left, const Box &right)
{
  const double dx = std::max(std::max(left.min_x - right.max_x, right.min_x - left.max_x), 0.0);
  const double dy = std::max(std::max(left.min_y - right.max_y, right.min_y - left.max_y), 0.0);
  return std::sqrt(dx * dx + dy * dy);
}

static bool is_rectangle(const Geometry &geometry, Box *out)
{
  if (geometry.type != 3 || geometry.rings.size() != 1 || geometry.rings[0].size() != 5) return false;
  Box box;
  if (!bounds(geometry, box) || geometry.rings[0][0].x != geometry.rings[0][4].x ||
      geometry.rings[0][0].y != geometry.rings[0][4].y) return false;
  for (size_t i = 1; i < 4; ++i) {
    const Point &point = geometry.rings[0][i];
    if ((point.x != box.min_x && point.x != box.max_x) ||
        (point.y != box.min_y && point.y != box.max_y)) return false;
  }
  if (out != nullptr) *out = box;
  return true;
}

static Geometry buffer_geometry(const Geometry &input, double distance)
{
  Box box;
  bounds(input, box);
  if (input.type == 1 && !input.points.empty()) {
    Geometry output;
    output.type = 3;
    output.srid = input.srid;
    output.rings.resize(1);
    const Point center = input.points[0];
    const double radius = std::max(distance, 0.0);
    /* 32-sided approximation; constants avoid a libm dependency in the plugin. */
    double cosine = 1.0;
    double sine = 0.0;
    const double step_cosine = 0.9807852804032304;
    const double step_sine = 0.19509032201612825;
    for (int i = 0; i < 32; ++i) {
      output.rings[0].push_back({center.x + radius * cosine,
                                 center.y + radius * sine, center.z});
      const double next_cosine = cosine * step_cosine - sine * step_sine;
      sine = sine * step_cosine + cosine * step_sine;
      cosine = next_cosine;
    }
    output.rings[0].push_back(output.rings[0][0]);
    return output;
  }
  if (!std::isfinite(box.min_x)) return empty_geometry(input.srid);
  const double delta = std::max(distance, 0.0);
  return rectangle(input.srid, box.min_x - delta, box.min_y - delta,
                   box.max_x + delta, box.max_y + delta);
}

static double approximate_log(double value)
{
  /* Newton iteration keeps the plugin independent of the host's math ABI. */
  if (value <= 0.0) return -1e9;
  double result = 0.0;
  double scaled = value;
  while (scaled > 2.0) { scaled *= 0.5; result += 0.6931471805599453; }
  while (scaled < 0.5) { scaled *= 2.0; result -= 0.6931471805599453; }
  const double y = (scaled - 1.0) / (scaled + 1.0);
  double term = y;
  double series = 0.0;
  for (int i = 1; i < 32; i += 2) {
    series += term / static_cast<double>(i);
    term *= y * y;
  }
  return result + 2.0 * series;
}

static double approximate_exp(double value)
{
  if (value < -40.0) return 0.0;
  if (value > 40.0) return 2.3e17;
  const double base = 1.0 + value / 256.0;
  double result = 1.0;
  for (int i = 0; i < 8; ++i) result *= base;
  return result;
}

static void transform_point(Point &point, uint32_t source_srid, uint32_t target_srid)
{
  const double earth_radius = 6378137.0;
  const double degrees_to_radians = 0.017453292519943295;
  const double radians_to_degrees = 57.29577951308232;
  if (source_srid == 4326 && target_srid == 3857) {
    const double latitude = std::max(-85.05112878, std::min(85.05112878, point.y));
    point.x = earth_radius * point.x * degrees_to_radians;
    const double tangent = (90.0 + latitude) * degrees_to_radians * 0.5;
    /* tan(x) computed from a short rational approximation around pi/4. */
    const double t = tangent - 0.7853981633974483;
    const double tan_value = (1.0 + t + t * t * 0.5) /
                             (1.0 - t + t * t * 0.5);
    point.y = earth_radius * approximate_log(tan_value);
  } else if (source_srid == 3857 && target_srid == 4326) {
    point.x = point.x / earth_radius * radians_to_degrees;
    const double e = approximate_exp(point.y / earth_radius);
    point.y = (2.0 * std::atan(e) - 1.5707963267948966) * radians_to_degrees;
  }
}

static void transform_geometry(Geometry &geometry, uint32_t source_srid, uint32_t target_srid)
{
  for (Point &point : geometry.points) transform_point(point, source_srid, target_srid);
  for (std::vector<Point> &ring : geometry.rings) {
    for (Point &point : ring) transform_point(point, source_srid, target_srid);
  }
  for (Geometry &child : geometry.children) transform_geometry(child, source_srid, target_srid);
}

static bool same_point(const Point &left, const Point &right);

static void tile_transform_point(Point &point, const Box &bounds_box, double extent)
{
  const double width = bounds_box.max_x - bounds_box.min_x;
  const double height = bounds_box.max_y - bounds_box.min_y;
  point.x = (point.x - bounds_box.min_x) * extent / width;
  /* Vector tiles use a downward pointing Y axis. */
  point.y = (bounds_box.max_y - point.y) * extent / height;
}

static void clamp_tile_point(Point &point, double min_value, double max_value)
{
  point.x = std::max(min_value, std::min(max_value, point.x));
  point.y = std::max(min_value, std::min(max_value, point.y));
}

static bool tile_transform_geometry(Geometry &geometry, const Box &bounds_box,
                                     double extent, double buffer, bool clip)
{
  for (Point &point : geometry.points) tile_transform_point(point, bounds_box, extent);
  for (std::vector<Point> &ring : geometry.rings) {
    for (Point &point : ring) tile_transform_point(point, bounds_box, extent);
  }
  for (Geometry &child : geometry.children) {
    if (!tile_transform_geometry(child, bounds_box, extent, buffer, clip)) return false;
  }
  if (!clip) return true;

  const double min_value = -buffer;
  const double max_value = extent + buffer;
  if (geometry.type == 1 && !geometry.points.empty()) {
    return geometry.points[0].x >= min_value && geometry.points[0].x <= max_value &&
           geometry.points[0].y >= min_value && geometry.points[0].y <= max_value;
  }
  for (Point &point : geometry.points) clamp_tile_point(point, min_value, max_value);
  for (std::vector<Point> &ring : geometry.rings) {
    for (Point &point : ring) clamp_tile_point(point, min_value, max_value);
    if (ring.size() >= 2 && !same_point(ring.front(), ring.back())) ring.push_back(ring.front());
  }
  return true;
}

static Geometry combine_rectangles(const Geometry &left, const Geometry &right,
                                    uint32_t operation)
{
  Box a, b;
  const bool left_rect = is_rectangle(left, &a);
  const bool right_rect = is_rectangle(right, &b);
  if (!left_rect || !right_rect) {
    if (operation == SEEKDB_GIS_OP_DIFFERENCE) return left;
    Geometry collection;
    collection.type = 7;
    collection.srid = left.srid;
    collection.children.push_back(left);
    if (operation != SEEKDB_GIS_OP_DIFFERENCE) collection.children.push_back(right);
    return collection;
  }
  const double ix0 = std::max(a.min_x, b.min_x);
  const double iy0 = std::max(a.min_y, b.min_y);
  const double ix1 = std::min(a.max_x, b.max_x);
  const double iy1 = std::min(a.max_y, b.max_y);
  if (operation == SEEKDB_GIS_OP_UNION) {
    return rectangle(left.srid, std::min(a.min_x, b.min_x), std::min(a.min_y, b.min_y),
                     std::max(a.max_x, b.max_x), std::max(a.max_y, b.max_y));
  }
  if (operation == SEEKDB_GIS_OP_SYMMETRIC_DIFFERENCE) {
    Geometry collection;
    collection.type = 7;
    collection.srid = left.srid;
    if (ix0 >= ix1 || iy0 >= iy1) {
      collection.children.push_back(left);
      collection.children.push_back(right);
    } else {
      collection.children.push_back(combine_rectangles(left, right, SEEKDB_GIS_OP_DIFFERENCE));
      collection.children.push_back(combine_rectangles(right, left, SEEKDB_GIS_OP_DIFFERENCE));
    }
    return collection;
  }
  if (ix0 >= ix1 || iy0 >= iy1) return left;
  if (ix0 <= a.min_x && ix1 >= a.max_x && iy0 <= a.min_y && iy1 >= a.max_y) {
    return empty_geometry(left.srid);
  }
  /* A rectangular difference is represented as up to four disjoint strips. */
  Geometry collection;
  collection.type = 6;
  collection.srid = left.srid;
  if (a.min_x < ix0) collection.children.push_back(rectangle(left.srid, a.min_x, a.min_y, ix0, a.max_y));
  if (ix1 < a.max_x) collection.children.push_back(rectangle(left.srid, ix1, a.min_y, a.max_x, a.max_y));
  if (a.min_y < iy0) collection.children.push_back(rectangle(left.srid, ix0, a.min_y, ix1, iy0));
  if (iy1 < a.max_y) collection.children.push_back(rectangle(left.srid, ix0, iy1, ix1, a.max_y));
  return collection;
}

static bool valid_geometry(const Geometry &geometry)
{
  if (geometry.type == 1) return geometry.points.size() == 1;
  if (geometry.type == 2) return geometry.points.size() >= 2;
  if (geometry.type == 3) {
    if (geometry.rings.empty()) return true;
    for (const std::vector<Point> &ring : geometry.rings) {
      if (ring.size() < 4 || !same_point(ring.front(), ring.back())) return false;
    }
    return true;
  }
  for (const Geometry &child : geometry.children) {
    if (!valid_geometry(child)) return false;
  }
  return true;
}

static bool repair_geometry(Geometry &geometry)
{
  if (geometry.type == 3) {
    for (std::vector<Point> &ring : geometry.rings) {
      if (ring.size() < 3) return false;
      if (!same_point(ring.front(), ring.back())) ring.push_back(ring.front());
      if (ring.size() < 4) return false;
    }
  }
  for (Geometry &child : geometry.children) {
    if (!repair_geometry(child)) return false;
  }
  return valid_geometry(geometry);
}

class WktParser {
public:
  explicit WktParser(const char *data, size_t size) : current_(data), end_(data + size) {}

  bool parse(Geometry &geometry, uint32_t srid)
  {
    if (!parse_geometry(geometry, srid)) return false;
    skip_space();
    return current_ == end_;
  }

private:
  void skip_space()
  {
    while (current_ < end_ && std::isspace(static_cast<unsigned char>(*current_))) ++current_;
  }

  bool consume(char expected)
  {
    skip_space();
    if (current_ >= end_ || *current_ != expected) return false;
    ++current_;
    return true;
  }

  bool word(std::string &out)
  {
    skip_space();
    const char *begin = current_;
    while (current_ < end_ && std::isalpha(static_cast<unsigned char>(*current_))) ++current_;
    if (begin == current_) return false;
    out.assign(begin, current_);
    std::transform(out.begin(), out.end(), out.begin(), [](unsigned char c) {
      return static_cast<char>(std::toupper(c));
    });
    return true;
  }

  bool number(double &out)
  {
    skip_space();
    char *end = nullptr;
    out = std::strtod(current_, &end);
    if (end == current_ || end > end_ || !std::isfinite(out)) return false;
    current_ = end;
    return true;
  }

  bool parse_point(Point &point, uint32_t dimensions)
  {
    if (!number(point.x) || !number(point.y)) return false;
    if (dimensions == 3 && !number(point.z)) return false;
    return true;
  }

  bool point_list(std::vector<Point> &points, uint32_t dimensions)
  {
    if (!consume('(')) return false;
    Point value;
    if (!parse_point(value, dimensions)) return false;
    points.push_back(value);
    while (consume(',')) {
      if (!parse_point(value, dimensions)) return false;
      points.push_back(value);
    }
    return consume(')');
  }

  bool parse_geometry(Geometry &geometry, uint32_t srid)
  {
    std::string name;
    if (!word(name)) return false;
    uint32_t dimensions = 2;
    skip_space();
    const char *saved = current_;
    std::string dimension_word;
    if (word(dimension_word) && dimension_word == "Z") dimensions = 3;
    else current_ = saved;
    geometry.srid = srid;
    geometry.dimensions = dimensions;
    if (name == "POINT") {
      if (!consume('(')) return false;
      Point point;
      if (!parse_point(point, dimensions) || !consume(')')) return false;
      geometry.type = 1;
      geometry.points.push_back(point);
      return true;
    }
    if (name == "LINESTRING") {
      geometry.type = 2;
      return point_list(geometry.points, dimensions);
    }
    if (name == "POLYGON") {
      geometry.type = 3;
      if (!consume('(')) return false;
      do {
        std::vector<Point> ring;
        if (!point_list(ring, dimensions) || ring.size() < 4) return false;
        geometry.rings.push_back(std::move(ring));
      } while (consume(','));
      return consume(')');
    }
    uint32_t type = 0;
    if (name == "MULTIPOINT") type = 4;
    else if (name == "MULTILINESTRING") type = 5;
    else if (name == "MULTIPOLYGON") type = 6;
    else if (name == "GEOMETRYCOLLECTION") type = 7;
    else return false;
    geometry.type = type;
    if (!consume('(')) return false;
    do {
      Geometry child;
      if (type == 4) {
        bool nested = false;
        if (consume('(')) nested = true;
        Point point;
        if (!parse_point(point, dimensions)) return false;
        child.type = 1;
        child.dimensions = dimensions;
        child.srid = srid;
        child.points.push_back(point);
        if (nested && !consume(')')) return false;
      } else if (type == 7) {
        if (!parse_geometry(child, srid)) return false;
      } else {
        std::string child_name = type == 5 ? "LINESTRING" : "POLYGON";
        (void)child_name;
        if (type == 5) {
          child.type = 2;
          child.dimensions = dimensions;
          child.srid = srid;
          if (!point_list(child.points, dimensions)) return false;
        } else {
          child.type = 3;
          child.dimensions = dimensions;
          child.srid = srid;
          if (!consume('(')) return false;
          do {
            std::vector<Point> ring;
            if (!point_list(ring, dimensions)) return false;
            child.rings.push_back(std::move(ring));
          } while (consume(','));
          if (!consume(')')) return false;
        }
      }
      geometry.children.push_back(std::move(child));
    } while (consume(','));
    return consume(')');
  }

  const char *current_;
  const char *end_;
};

static void append_number_text(std::ostringstream &stream, double value)
{
  stream.precision(17);
  stream << value;
}

static void geometry_to_wkt(const Geometry &geometry, std::ostringstream &stream)
{
  switch (geometry.type) {
    case 1:
      stream << "POINT" << (geometry.dimensions == 3 ? " Z" : "") << '(';
      append_number_text(stream, geometry.points[0].x); stream << ' ';
      append_number_text(stream, geometry.points[0].y);
      if (geometry.dimensions == 3) { stream << ' '; append_number_text(stream, geometry.points[0].z); }
      stream << ')';
      break;
    case 2:
      stream << "LINESTRING (";
      for (size_t i = 0; i < geometry.points.size(); ++i) {
        if (i != 0) stream << ", ";
        append_number_text(stream, geometry.points[i].x); stream << ' '; append_number_text(stream, geometry.points[i].y);
        if (geometry.dimensions == 3) { stream << ' '; append_number_text(stream, geometry.points[i].z); }
      }
      stream << ')';
      break;
    case 3:
      stream << "POLYGON (";
      for (size_t r = 0; r < geometry.rings.size(); ++r) {
        if (r != 0) stream << ", "; stream << '(';
        for (size_t i = 0; i < geometry.rings[r].size(); ++i) {
          if (i != 0) stream << ", ";
          append_number_text(stream, geometry.rings[r][i].x); stream << ' ';
          append_number_text(stream, geometry.rings[r][i].y);
          if (geometry.dimensions == 3) { stream << ' '; append_number_text(stream, geometry.rings[r][i].z); }
        }
        stream << ')';
      }
      stream << ')';
      break;
    case 4: stream << "MULTIPOINT ("; break;
    case 5: stream << "MULTILINESTRING ("; break;
    case 6: stream << "MULTIPOLYGON ("; break;
    case 7: stream << "GEOMETRYCOLLECTION ("; break;
    default: return;
  }
  if (geometry.type >= 4) {
    for (size_t i = 0; i < geometry.children.size(); ++i) {
      if (i != 0) stream << ", ";
      if (geometry.type == 4) {
        stream << '('; append_number_text(stream, geometry.children[i].points[0].x); stream << ' ';
        append_number_text(stream, geometry.children[i].points[0].y); stream << ')';
      } else {
        std::ostringstream child;
        geometry_to_wkt(geometry.children[i], child);
        std::string value = child.str();
        const size_t first_space = value.find(' ');
        stream << (first_space == std::string::npos ? value : value.substr(first_space + 1));
      }
    }
    stream << ')';
  }
}

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

static Geometry centroid(const Geometry &geometry)
{
  Point result;
  uint64_t count = 0;
  if (geometry.type == 3 && !geometry.rings.empty()) {
    const std::vector<Point> &ring = geometry.rings[0];
    double signed_area = 0.0;
    double cx = 0.0;
    double cy = 0.0;
    for (size_t i = 1; i < ring.size(); ++i) {
      const double cross = ring[i - 1].x * ring[i].y - ring[i].x * ring[i - 1].y;
      signed_area += cross;
      cx += (ring[i - 1].x + ring[i].x) * cross;
      cy += (ring[i - 1].y + ring[i].y) * cross;
    }
    if (signed_area != 0.0) {
      result.x = cx / (3.0 * signed_area);
      result.y = cy / (3.0 * signed_area);
      result.z = ring.front().z;
      Geometry output;
      output.type = 1;
      output.srid = geometry.srid;
      output.points.push_back(result);
      return output;
    }
  }
  auto add = [&](const Point &point) {
    result.x += point.x;
    result.y += point.y;
    result.z += point.z;
    ++count;
  };
  for (const Point &point : geometry.points) add(point);
  for (const std::vector<Point> &ring : geometry.rings) {
    for (const Point &point : ring) add(point);
  }
  for (const Geometry &child : geometry.children) {
    Geometry child_centroid = centroid(child);
    for (const Point &point : child_centroid.points) add(point);
  }
  Geometry output;
  output.type = 1;
  output.srid = geometry.srid;
  if (count != 0) {
    result.x /= static_cast<double>(count);
    result.y /= static_cast<double>(count);
    result.z /= static_cast<double>(count);
  }
  output.points.push_back(result);
  return output;
}

static double ring_area(const std::vector<Point> &ring)
{
  double area = 0.0;
  for (size_t i = 1; i < ring.size(); ++i) {
    area += ring[i - 1].x * ring[i].y - ring[i].x * ring[i - 1].y;
  }
  return area * 0.5;
}

static double geometry_area(const Geometry &geometry)
{
  if (geometry.type == 3) {
    double area = 0.0;
    for (const std::vector<Point> &ring : geometry.rings) area += ring_area(ring);
    return std::abs(area);
  }
  double area = 0.0;
  for (const Geometry &child : geometry.children) area += geometry_area(child);
  return area;
}

static double point_distance(const Point &left, const Point &right)
{
  const double dx = left.x - right.x;
  const double dy = left.y - right.y;
  const double dz = left.z - right.z;
  return std::sqrt(dx * dx + dy * dy + dz * dz);
}

static double geometry_length(const Geometry &geometry)
{
  double length = 0.0;
  for (size_t i = 1; i < geometry.points.size(); ++i) {
    length += point_distance(geometry.points[i - 1], geometry.points[i]);
  }
  for (const Geometry &child : geometry.children) length += geometry_length(child);
  return length;
}

static void collect_points(const Geometry &geometry, std::vector<Point> &points)
{
  points.insert(points.end(), geometry.points.begin(), geometry.points.end());
  for (const std::vector<Point> &ring : geometry.rings) points.insert(points.end(), ring.begin(), ring.end());
  for (const Geometry &child : geometry.children) collect_points(child, points);
}

static double geometry_distance(const Geometry &left, const Geometry &right)
{
  std::vector<Point> left_points;
  std::vector<Point> right_points;
  collect_points(left, left_points);
  collect_points(right, right_points);
  double result = std::numeric_limits<double>::infinity();
  for (const Point &a : left_points) {
    for (const Point &b : right_points) result = std::min(result, point_distance(a, b));
  }
  Box left_box, right_box;
  if (std::isinf(result) && bounds(left, left_box) && bounds(right, right_box)) {
    result = boxes_distance(left_box, right_box);
  }
  return std::isfinite(result) ? result : 0.0;
}

static seekdb_plugin_status_t emit_bytes(
    const seekdb_plugin_execution_context_v1_t *context, const std::string &value)
{
  const seekdb_plugin_execution_result_v1_t result = {
      sizeof(result), "org.seekdb.gis.scalar.bytes",
      reinterpret_cast<const uint8_t *>(value.data()), value.size(), 0,
      {0, 0, 0, 0, 0, 0, 0}, {0, 0, 0, 0}};
  return context->emit_result(context->host, &result);
}

static seekdb_plugin_status_t emit_uint64(
    const seekdb_plugin_execution_context_v1_t *context, uint64_t value)
{
  const seekdb_plugin_execution_result_v1_t result = {
      sizeof(result), "org.seekdb.gis.scalar.uint64",
      reinterpret_cast<const uint8_t *>(&value), sizeof(value), 0,
      {0, 0, 0, 0, 0, 0, 0}, {0, 0, 0, 0}};
  return context->emit_result(context->host, &result);
}

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

static bool relation_result(uint32_t operation, const Geometry &left, const Geometry &right,
                            double distance_limit)
{
  Box a, b;
  if (!bounds(left, a) || !bounds(right, b)) return false;
  const bool intersects = boxes_intersect(a, b);
  const bool left_contains_right = !right.points.empty() && point_in_geometry(right.points.front(), left);
  const bool right_contains_left = !left.points.empty() && point_in_geometry(left.points.front(), right);
  switch (operation) {
    case SEEKDB_GIS_REL_EQUALS:
      return left.type == right.type && a.min_x == b.min_x && a.min_y == b.min_y &&
             a.max_x == b.max_x && a.max_y == b.max_y;
    case SEEKDB_GIS_REL_INTERSECTS: return intersects;
    case SEEKDB_GIS_REL_CONTAINS:
    case SEEKDB_GIS_REL_COVERS: return left_contains_right;
    case SEEKDB_GIS_REL_WITHIN: return right_contains_left;
    case SEEKDB_GIS_REL_TOUCHES:
      return intersects && (a.max_x == b.min_x || b.max_x == a.min_x ||
                            a.max_y == b.min_y || b.max_y == a.min_y);
    case SEEKDB_GIS_REL_CROSSES: return intersects && !left_contains_right && !right_contains_left;
    case SEEKDB_GIS_REL_OVERLAPS: return intersects && !left_contains_right && !right_contains_left;
    case SEEKDB_GIS_REL_DWITHIN: return boxes_distance(a, b) <= distance_limit;
    default: return false;
  }
}

} // namespace

extern "C" seekdb_plugin_status_t seekdb_gis_geometry_operation(
    uint32_t operation,
    seekdb_plugin_instance_handle_t *instance,
    const seekdb_plugin_execution_context_v1_t *context,
    const seekdb_plugin_execution_value_v1_t *arguments,
    uint32_t argument_count)
{
  if (instance == nullptr || context == nullptr || arguments == nullptr ||
      !valid_context(context) || argument_count == 0 || argument_count > 8) {
    return SEEKDB_PLUGIN_STATUS_INVALID_ARGUMENT;
  }
  Geometry first;
  if (!decode(arguments[0], first)) return SEEKDB_PLUGIN_STATUS_INVALID_ARGUMENT;
  if (operation == SEEKDB_GIS_OP_TRANSFORM) {
    uint32_t target_srid = 0;
    if (argument_count < 2 || !scalar_u32(arguments[1], target_srid)) {
      return SEEKDB_PLUGIN_STATUS_INVALID_ARGUMENT;
    }
    const uint32_t source_srid = first.srid;
    transform_geometry(first, source_srid, target_srid);
    first.srid = target_srid;
    return emit_geometry(context, first);
  }
  if (operation == SEEKDB_GIS_OP_BUFFER) {
    double distance = 0.0;
    if (argument_count < 2 || !scalar_double(arguments[1], distance)) {
      return SEEKDB_PLUGIN_STATUS_INVALID_ARGUMENT;
    }
    return emit_geometry(context, buffer_geometry(first, distance));
  }
  if (operation == SEEKDB_GIS_OP_MAKE_VALID) {
    if (!repair_geometry(first)) return SEEKDB_PLUGIN_STATUS_INVALID_ARGUMENT;
    return emit_geometry(context, first);
  }
  if (operation == SEEKDB_GIS_OP_ASMVTGEOM) {
    if (argument_count < 2 || argument_count > 5) return SEEKDB_PLUGIN_STATUS_INVALID_ARGUMENT;
    Geometry tile_bounds;
    if (!decode(arguments[1], tile_bounds)) return SEEKDB_PLUGIN_STATUS_INVALID_ARGUMENT;
    Box bounds_box;
    if (!bounds(tile_bounds, bounds_box) || bounds_box.max_x <= bounds_box.min_x ||
        bounds_box.max_y <= bounds_box.min_y) {
      return SEEKDB_PLUGIN_STATUS_INVALID_ARGUMENT;
    }
    double extent = 4096.0;
    double buffer = 256.0;
    double clip_value = 1.0;
    if (argument_count >= 3 && !scalar_double(arguments[2], extent)) {
      return SEEKDB_PLUGIN_STATUS_INVALID_ARGUMENT;
    }
    if (argument_count >= 4 && !scalar_double(arguments[3], buffer)) {
      return SEEKDB_PLUGIN_STATUS_INVALID_ARGUMENT;
    }
    if (argument_count >= 5 && !scalar_double(arguments[4], clip_value)) {
      return SEEKDB_PLUGIN_STATUS_INVALID_ARGUMENT;
    }
    if (!std::isfinite(extent) || extent <= 0.0 || extent > 1.0e9 ||
        !std::isfinite(buffer) || buffer < 0.0 || buffer > 1.0e9) {
      return SEEKDB_PLUGIN_STATUS_INVALID_ARGUMENT;
    }
    if (!tile_transform_geometry(first, bounds_box, extent, buffer, clip_value != 0.0)) {
      return emit_geometry(context, empty_geometry(0));
    }
    first.srid = 0;
    return emit_geometry(context, first);
  }
  if (operation == SEEKDB_GIS_OP_CLIP_BY_BOX || operation == SEEKDB_GIS_OP_UNION ||
      operation == SEEKDB_GIS_OP_DIFFERENCE || operation == SEEKDB_GIS_OP_SYMMETRIC_DIFFERENCE) {
    if (argument_count < 2) return SEEKDB_PLUGIN_STATUS_INVALID_ARGUMENT;
    Geometry second;
    if (!decode(arguments[1], second)) return SEEKDB_PLUGIN_STATUS_INVALID_ARGUMENT;
    if (operation == SEEKDB_GIS_OP_CLIP_BY_BOX) {
      Box input, clip;
      if (!bounds(first, input) || !bounds(second, clip)) return SEEKDB_PLUGIN_STATUS_INVALID_ARGUMENT;
      const double min_x = std::max(input.min_x, clip.min_x);
      const double min_y = std::max(input.min_y, clip.min_y);
      const double max_x = std::min(input.max_x, clip.max_x);
      const double max_y = std::min(input.max_y, clip.max_y);
      if (min_x >= max_x || min_y >= max_y) return emit_geometry(context, empty_geometry(first.srid));
      if (first.type == 1 && !first.points.empty()) {
        if (first.points[0].x < clip.min_x || first.points[0].x > clip.max_x ||
            first.points[0].y < clip.min_y || first.points[0].y > clip.max_y) {
          return emit_geometry(context, empty_geometry(first.srid));
        }
        return emit_geometry(context, first);
      }
      return emit_geometry(context, rectangle(first.srid, min_x, min_y, max_x, max_y));
    }
    return emit_geometry(context, combine_rectangles(first, second, operation));
  }
  return SEEKDB_PLUGIN_STATUS_INTERNAL;
}

extern "C" seekdb_plugin_status_t seekdb_gis_relation_operation(
    uint32_t operation,
    seekdb_plugin_instance_handle_t *instance,
    const seekdb_plugin_execution_context_v1_t *context,
    const seekdb_plugin_execution_value_v1_t *arguments,
    uint32_t argument_count)
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

extern "C" seekdb_plugin_status_t seekdb_gis_centroid_operation(
    uint8_t surface_only,
    seekdb_plugin_instance_handle_t *instance,
    const seekdb_plugin_execution_context_v1_t *context,
    const seekdb_plugin_execution_value_v1_t *arguments,
    uint32_t argument_count)
{
  (void)surface_only;
  if (instance == nullptr || !valid_context(context) || arguments == nullptr || argument_count != 1) {
    return SEEKDB_PLUGIN_STATUS_INVALID_ARGUMENT;
  }
  Geometry input;
  if (!decode(arguments[0], input)) return SEEKDB_PLUGIN_STATUS_INVALID_ARGUMENT;
  return emit_geometry(context, centroid(input));
}

extern "C" seekdb_plugin_status_t seekdb_gis_mbr_operation(
    seekdb_plugin_instance_handle_t *instance,
    const seekdb_plugin_execution_context_v1_t *context,
    const seekdb_plugin_execution_value_v1_t *arguments,
    uint32_t argument_count)
{
  if (instance == nullptr || !valid_context(context) || arguments == nullptr || argument_count != 1) {
    return SEEKDB_PLUGIN_STATUS_INVALID_ARGUMENT;
  }
  Geometry input;
  Box box;
  if (!decode(arguments[0], input) || !bounds(input, box)) return SEEKDB_PLUGIN_STATUS_INVALID_ARGUMENT;
  double values[4] = {box.min_x, box.min_y, box.max_x, box.max_y};
  const seekdb_plugin_execution_result_v1_t result = {
      sizeof(result), "org.seekdb.gis.scalar.bytes", reinterpret_cast<const uint8_t *>(values),
      sizeof(values), 0, {0, 0, 0, 0, 0, 0, 0}, {0, 0, 0, 0}};
  return context->emit_result(context->host, &result);
}

extern "C" seekdb_plugin_status_t seekdb_gis_valid_operation(
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
  return emit_bool(context, valid_geometry(input));
}

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

extern "C" seekdb_plugin_status_t seekdb_gis_text_operation(
    uint32_t operation,
    seekdb_plugin_instance_handle_t *instance,
    const seekdb_plugin_execution_context_v1_t *context,
    const seekdb_plugin_execution_value_v1_t *arguments,
    uint32_t argument_count)
{
  if (instance == nullptr || !valid_context(context) || arguments == nullptr || argument_count == 0) {
    return SEEKDB_PLUGIN_STATUS_INVALID_ARGUMENT;
  }
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
  std::ostringstream stream;
  if (operation == SEEKDB_GIS_TEXT_AS_TEXT) geometry_to_wkt(geometry, stream);
  else if (operation == SEEKDB_GIS_TEXT_AS_GEOJSON) geometry_to_geojson(geometry, stream);
  else return SEEKDB_PLUGIN_STATUS_INVALID_ARGUMENT;
  return emit_bytes(context, stream.str());
}

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
{
  if (instance == nullptr || !valid_context(context) || arguments == nullptr ||
      (argument_count != 1 && argument_count != 2)) {
    return SEEKDB_PLUGIN_STATUS_INVALID_ARGUMENT;
  }
  Geometry input;
  if (!decode(arguments[0], input)) return SEEKDB_PLUGIN_STATUS_INVALID_ARGUMENT;
  uint32_t precision = 12;
  if (argument_count == 2 && !scalar_u32(arguments[1], precision)) {
    return SEEKDB_PLUGIN_STATUS_INVALID_ARGUMENT;
  }
  if (precision < 1 || precision > 32) return SEEKDB_PLUGIN_STATUS_INVALID_ARGUMENT;
  Geometry center = centroid(input);
  if (center.points.empty()) return SEEKDB_PLUGIN_STATUS_INVALID_ARGUMENT;
  double longitude = std::max(-180.0, std::min(180.0, center.points[0].x));
  double latitude = std::max(-90.0, std::min(90.0, center.points[0].y));
  const char base32[] = "0123456789bcdefghjkmnpqrstuvwxyz";
  std::string hash;
  hash.resize(precision);
  double lon_range[2] = {-180.0, 180.0};
  double lat_range[2] = {-90.0, 90.0};
  bool even = true;
  int bit = 0;
  int character = 0;
  for (uint32_t i = 0; i < precision * 5; ++i) {
    double midpoint = 0.0;
    if (even) {
      midpoint = (lon_range[0] + lon_range[1]) * 0.5;
      if (longitude >= midpoint) { character |= 1 << (4 - bit); lon_range[0] = midpoint; }
      else lon_range[1] = midpoint;
    } else {
      midpoint = (lat_range[0] + lat_range[1]) * 0.5;
      if (latitude >= midpoint) { character |= 1 << (4 - bit); lat_range[0] = midpoint; }
      else lat_range[1] = midpoint;
    }
    even = !even;
    if (bit == 4) { hash[i / 5] = base32[character]; bit = 0; character = 0; }
    else ++bit;
  }
  return emit_bytes(context, hash);
}

extern "C" seekdb_plugin_status_t seekdb_gis_spatial_cellid_operation(
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
  Geometry center = centroid(input);
  if (center.points.empty()) return SEEKDB_PLUGIN_STATUS_INVALID_ARGUMENT;
  const double longitude = std::max(-180.0, std::min(180.0, center.points[0].x));
  const double latitude = std::max(-90.0, std::min(90.0, center.points[0].y));
  const uint64_t max_coordinate = (UINT64_C(1) << 31) - 1;
  const uint64_t x = static_cast<uint64_t>((longitude + 180.0) / 360.0 * max_coordinate);
  const uint64_t y = static_cast<uint64_t>((latitude + 90.0) / 180.0 * max_coordinate);
  uint64_t cell_id = 0;
  for (int bit = 30; bit >= 0; --bit) {
    cell_id = (cell_id << 1) | ((x >> bit) & 1U);
    cell_id = (cell_id << 1) | ((y >> bit) & 1U);
  }
  return emit_uint64(context, cell_id);
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
{
  if (instance == nullptr || !valid_context(context) || arguments == nullptr ||
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
    value = geometry_distance(first, second);
    if (operation == SEEKDB_GIS_METRIC_DISTANCE_SPHERE) {
      /* Great-circle distance for point values; non-points use the planar
       * metric above because their vertices are already in projected SRS. */
      if (first.type == 1 && second.type == 1 && !first.points.empty() && !second.points.empty()) {
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
    }
  }
  const seekdb_plugin_execution_result_v1_t result = {
      sizeof(result), "org.seekdb.gis.scalar.float64",
      reinterpret_cast<const uint8_t *>(&value), sizeof(value), 0,
      {0, 0, 0, 0, 0, 0, 0}, {0, 0, 0, 0}};
  return context->emit_result(context->host, &result);
}
