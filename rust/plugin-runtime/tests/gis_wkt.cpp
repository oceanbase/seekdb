
/*
 * Copyright (c) 2025 OceanBase.
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

#include <cstdint>
#include <cstring>
#include <iostream>
#include <memory>
#include <string>
#include <utility>
#include <vector>
#include "share/geo/ob_geo_to_wkt_visitor.h"
#include "share/geo/ob_geo_3d.h"
#include "share/plugin/ob_plugin_loader.h"
#include "seekdb/plugin/sql_spi.h"
#include "legacy_wkt_parser.h"
#include "legacy_ewkb_header.h"
#include "share/geo/ob_geo_wkb_check_visitor.h"

using namespace oceanbase::common;
using namespace oceanbase::share::plugin;
#define CHECK(condition) do { if (!(condition)) { std::cerr << __LINE__ << ": " << #condition << std::endl; std::abort(); } } while (false)
#include "native_activation_fixture.h"

struct Shape {
  Shape(uint32_t type, uint32_t dimensions, std::vector<double> points = {},
        std::vector<Shape> children = {})
      : type(type), dimensions(dimensions), points(std::move(points)), children(std::move(children)) {}
  uint32_t type = 1, dimensions = 2;
  std::vector<double> points;
  std::vector<Shape> children;
};
static void u32(std::string &out, uint32_t value)
{ for (unsigned i = 0; i < 4; ++i) out += static_cast<char>(value >> (8 * i)); }
static void append_double(std::string &out, double value)
{
  uint64_t bits; std::memcpy(&bits, &value, sizeof(bits));
  for (unsigned i = 0; i < 8; ++i) out += static_cast<char>(bits >> (8 * i));
}
static void wkb(const Shape &shape, std::string &out, bool ring = false)
{
  if (!ring) { out += char(1); u32(out, shape.type + (shape.dimensions == 3 ? 1000 : 0)); }
  if (shape.type <= 2) {
    if (shape.type == 2) u32(out, shape.points.size() / shape.dimensions);
    for (double value : shape.points) append_double(out, value);
  } else {
    u32(out, shape.children.size());
    for (const auto &child : shape.children) wkb(child, out, shape.type == 3);
  }
}
static std::string encoded(const Shape &shape, uint32_t srid)
{
  // Execution SPI geometry envelope v1, not the kernel's SWKB version marker.
  std::string result; u32(result, srid); result += char(1); wkb(shape, result); return result;
}
static int original(const Shape &shape, const std::string &bytes, bool ewkt,
                    int64_t precision, uint32_t srid, std::string &result)
{
  ObArenaAllocator arena;
  ObString data(bytes.size() - 5, bytes.data() + 5), text;
  if (shape.dimensions == 3) {
    ObGeometry3D geo(srid); geo.set_data(data);
    const int ret = geo.to_wkt(arena, text, ewkt ? srid : 0, -1);
    if (ret == OB_SUCCESS) result.assign(text.ptr(), text.length());
    return ret;
  }
  std::unique_ptr<ObGeometry> geo;
  switch (shape.type) {
    case 1: geo.reset(new ObIWkbGeomPoint(srid)); break;
    case 2: geo.reset(new ObIWkbGeomLineString(srid)); break;
    case 3: geo.reset(new ObIWkbGeomPolygon(srid)); break;
    case 4: geo.reset(new ObIWkbGeomMultiPoint(srid)); break;
    case 5: geo.reset(new ObIWkbGeomMultiLineString(srid)); break;
    case 6: geo.reset(new ObIWkbGeomMultiPolygon(srid)); break;
    case 7: geo.reset(new ObIWkbGeomCollection(srid)); break;
    default: return OB_INVALID_ARGUMENT;
  }
  geo->set_data(data);
  ObGeoToWktVisitor visitor(&arena);
  int ret = ewkt ? visitor.init(srid, precision == 0 ? 25 : precision) : OB_SUCCESS;
  if (ret == OB_SUCCESS) ret = geo->do_visit(visitor);
  if (ret == OB_SUCCESS) { visitor.get_wkt(text); result.assign(text.ptr(), text.length()); }
  return ret;
}
static int original_ewkb(std::string input, std::string &output)
{
  ObString bytes(input.size(), input.data()), canonical;
  ObGeoWkbHeader header;
  int ret = LegacyEwkbHeader::get_header_info_from_ewkb(bytes, header);
  if (ret != OB_SUCCESS) return ret;
  if (header.bo_ != ObGeoWkbByteOrder::LittleEndian) return OB_INVALID_ARGUMENT;
  if ((ret = LegacyEwkbHeader::construct_ewkb_data(bytes, canonical)) != OB_SUCCESS) return ret;
  const uint32_t type = static_cast<uint32_t>(header.type_);
  if (type >= 1001 && type <= 1007) {
    ObGeometry3D geo(header.srid_); geo.set_data(canonical);
    ret = geo.check_wkb_valid();
  } else {
    std::unique_ptr<ObGeometry> geo;
    switch (type) {
      case 1: geo.reset(new ObIWkbGeomPoint(header.srid_)); break;
      case 2: geo.reset(new ObIWkbGeomLineString(header.srid_)); break;
      case 3: geo.reset(new ObIWkbGeomPolygon(header.srid_)); break;
      case 4: geo.reset(new ObIWkbGeomMultiPoint(header.srid_)); break;
      case 5: geo.reset(new ObIWkbGeomMultiLineString(header.srid_)); break;
      case 6: geo.reset(new ObIWkbGeomMultiPolygon(header.srid_)); break;
      case 7: geo.reset(new ObIWkbGeomCollection(header.srid_)); break;
      default: return OB_INVALID_ARGUMENT;
    }
    geo->set_data(canonical);
    ObGeoWkbCheckVisitor visitor(canonical, header.bo_);
    ret = geo->do_visit(visitor);
    if (ret == OB_SUCCESS && static_cast<ObIWkbGeometry *>(geo.get())->length() != canonical.length())
      ret = OB_INVALID_ARGUMENT;
  }
  if (ret == OB_SUCCESS) {
    u32(output, header.srid_); output += char(1);
    output.append(canonical.ptr(), canonical.length());
  }
  return ret;
}

struct Sink {
  std::string text;
  unsigned calls = 0;
  static seekdb_plugin_status_t SEEKDB_PLUGIN_CALL emit(
      seekdb_plugin_host_handle_t *host, const seekdb_plugin_execution_result_v1_t *result)
  {
    auto &sink = *reinterpret_cast<Sink *>(host);
    if (!result || result->is_null || !result->data) return SEEKDB_PLUGIN_STATUS_INVALID_ARGUMENT;
    try { sink.text.assign(reinterpret_cast<const char *>(result->data), result->data_size); }
    catch (...) { return SEEKDB_PLUGIN_STATUS_NO_MEMORY; }
    ++sink.calls;
    return SEEKDB_PLUGIN_STATUS_OK;
  }
};
int main(int argc, char **argv)
{
  CHECK(argc == 2);
  OB_LOGGER.set_file_name("gis_wkt.log", true);
  OB_LOGGER.set_enable_async_log(false); OB_LOGGER.set_log_level("ERROR");
  CHECK(ObCharset::init_charset() == OB_SUCCESS);
  native_activation_test::Observation observation; observation.gis = true;
  auto guard = std::make_shared<native_activation_test::TestGuard>(observation);
  ObPluginLoader loader;
  const std::string path(argv[1]); const auto slash = path.rfind('/');
  CHECK(slash != std::string::npos);
  CHECK(loader.init(path.substr(0, slash),
      std::make_shared<native_activation_test::TestVerifier>(false, true, false),
      guard, guard, observation.registry) == OB_SUCCESS);
  CHECK(loader.load(path.substr(slash + 1)) == OB_SUCCESS);
  unsigned cases = 0, failures = 0;
  const auto check = [&](const Shape &shape, bool ewkt, int64_t precision, uint32_t srid) {
    const std::string bytes = encoded(shape, srid);
    std::string expected;
    const int reference = original(shape, bytes, ewkt, precision, srid, expected);
    Sink sink;
    seekdb_plugin_execution_context_v2_t context{};
    context.v1.struct_size = sizeof(context); context.v1.emit_result = Sink::emit;
    context.v1.host = reinterpret_cast<seekdb_plugin_host_handle_t *>(&sink);
    const char *types[] = {"org.seekdb.gis.geometry", "core.type.int64"};
    const uint32_t count = ewkt ? 2 : 1;
    seekdb_plugin_execution_value_v1_t arguments[2]{};
    for (unsigned i = 0; i < count; ++i) { arguments[i].struct_size = sizeof(arguments[i]); arguments[i].type_id = types[i]; }
    arguments[0].data = reinterpret_cast<const uint8_t *>(bytes.data()); arguments[0].data_size = bytes.size();
    arguments[1].data = reinterpret_cast<const uint8_t *>(&precision); arguments[1].data_size = sizeof(precision);
    seekdb_plugin_sql_binding_v1_t binding{};
    CHECK(loader.resolve_native_function("org.seekdb.gis",
        ewkt ? "org.seekdb.gis.function.st_asewkt" : "org.seekdb.gis.function.st_astext",
        types, count, binding) == OB_SUCCESS);
    const int actual = loader.execute_bound_function(binding, &context.v1, arguments, count);
    ++cases;
    if ((reference == OB_SUCCESS) != (actual == OB_SUCCESS) ||
        (reference == OB_SUCCESS && (sink.calls != 1 || sink.text != expected)) ||
        (actual != OB_SUCCESS && sink.calls != 0)) {
      if (++failures <= 20) std::cerr << "type=" << shape.type << " dims=" << shape.dimensions
          << " ewkt=" << ewkt << " precision=" << precision << " reference=" << reference
          << " actual=" << actual << "\nexpected: " << expected << "\nactual:   " << sink.text << std::endl;
    }
  };
  for (uint32_t dimensions : {2u, 3u}) {
    Shape point{1, dimensions, {1.2345678901234567, -1.2345678901234567e-14}};
    if (dimensions == 3) point.points.push_back(9.876543210987654);
    Shape line{2, dimensions, point.points};
    line.points.insert(line.points.end(), point.points.begin(), point.points.end());
    Shape ring{2, dimensions, {0,0}}, polygon{3, dimensions};
    if (dimensions == 3) ring.points.push_back(1);
    for (auto xy : {std::pair<double,double>{4,0}, {4,3}, {0,3}, {0,0}}) {
      ring.points.push_back(xy.first); ring.points.push_back(xy.second);
      if (dimensions == 3) ring.points.push_back(1);
    }
    polygon.children = {ring};
    Shape multi_point{4, dimensions, {}, {point, point}};
    Shape multi_line{5, dimensions, {}, {line, line}};
    Shape multi_polygon{6, dimensions, {}, {polygon, polygon}};
    Shape empty{7, dimensions}, collection{7, dimensions, {}, {point, multi_point, line, multi_line, polygon, multi_polygon, empty}};
    Shape nested{7, dimensions, {}, {collection, point, empty}};
    Shape other_point{1, dimensions == 2 ? 3u : 2u, {1, 2}};
    if (other_point.dimensions == 3) other_point.points.push_back(3);
    Shape mixed{7, dimensions, {}, {other_point}};
    check(mixed, false, -1, 0);
    check(mixed, true, 15, 0);
    for (const auto &shape : {point, line, polygon, multi_point, multi_line, multi_polygon, empty, collection, nested}) {
      check(shape, false, -1, 0);
      for (int64_t precision : {-1,0,1,2,15,24,25}) {
        for (uint32_t srid : {0u, 4326u, UINT32_MAX}) check(shape, true, precision, srid);
      }
    }
    for (unsigned size = 2; size <= 90; ++size) {
      line.points.clear();
      for (unsigned i = 0; i < size; ++i) line.points.insert(line.points.end(), point.points.begin(), point.points.end());
      for (int64_t precision : {-1,0,15,25}) check(line, true, precision, 0);
      check(line, false, -1, 0);
    }
  }

  unsigned parsed_cases = 0, parsed_failures = 0;
  const auto parse_check = [&](const std::string &input) {
    ObArenaAllocator arena;
    LegacyWktParser reference(arena, ObString(input.size(), input.data()));
    int expected_status = reference.inner_parse();
    if (expected_status == OB_SUCCESS && !reference.is_wkt_end()) expected_status = OB_ERR_PARSER_SYNTAX;
    const ObString expected = reference.wkb_buf_.string();
    Sink sink;
    seekdb_plugin_execution_context_v2_t context{};
    context.v1.struct_size = sizeof(context); context.v1.emit_result = Sink::emit;
    context.v1.host = reinterpret_cast<seekdb_plugin_host_handle_t *>(&sink);
    const char *types[] = {"org.seekdb.gis.scalar.bytes"};
    seekdb_plugin_execution_value_v1_t argument{};
    argument.struct_size = sizeof(argument); argument.type_id = types[0];
    argument.data = reinterpret_cast<const uint8_t *>(input.data()); argument.data_size = input.size();
    for (const char *name : {"org.seekdb.gis.function.st_geomfromtext",
                            "org.seekdb.gis.function.st_geomfromewkt"}) {
      sink = {};
      seekdb_plugin_sql_binding_v1_t binding{};
      CHECK(loader.resolve_native_function("org.seekdb.gis", name, types, 1, binding) == OB_SUCCESS);
      const int actual = loader.execute_bound_function(binding, &context.v1, &argument, 1);
      ++parsed_cases;
      if ((expected_status == OB_SUCCESS) != (actual == OB_SUCCESS) ||
          (expected_status == OB_SUCCESS &&
           (sink.calls != 1 || sink.text.size() != static_cast<size_t>(expected.length()) + 5 ||
            sink.text.substr(0, 5) != std::string("\0\0\0\0\1", 5) ||
            sink.text.substr(5) != std::string(expected.ptr(), expected.length()))) ||
          (actual != OB_SUCCESS && sink.calls != 0)) {
        if (++parsed_failures <= 20) std::cerr << "parse " << name << " input=" << input
            << " reference=" << expected_status << " actual=" << actual
            << " expected_bytes=" << expected.length() << " actual_bytes=" << sink.text.size() << std::endl;
      }
    }
  };
  for (const char *input : {
      "POINT(1 2)",
      "POINT(1 2 3)",
      "pointz(1 2 3)",
      "POINT Z(1 2 3)",
      "POINTZ Z(1 2 3)",
      "POINT Z(1 2)",
      "POINT(1 2 3 4)",
      "POINT(0.1 -0)",
      "POINT(+.1 -.2)",
      "POINT(1-2)",
      "POINT(0x1p2 3)",
      "POINT(1e309 2)",
      "POINT(1e-999 2)",
      "POINT(NaN 1)",
      "POINT(-nan 1)",
      "POINT(INF 2)",
      "POINT(1e 2)",
      "POINT(1.e2 .3)",
      "LINESTRING(0 0,1 1)",
      "LINESTRING(0 0)",
      "LINESTRING(0 0 1,1 1 2)",
      "LINESTRINGZ(0 0 1,1 1 2)",
      "LINESTRING(0 0,1 1 2)",
      "POLYGON((0 0,1 0,1 1,0 0))",
      "POLYGON((0 0,1 0,1 1,0 1))",
      "POLYGON((0 0,1 0,0 0))",
      "POLYGON((-0 0,1 0,1 1,0 0))",
      "POLYGON Z((0 0 1,1 0 2,1 1 3,0 0 4))",
      "MULTIPOINT(1 2,3 4)",
      "MULTIPOINT((1 2),(3 4))",
      "MULTIPOINT(1 2,(3 4))",
      "MULTIPOINT((1 2),3 4)",
      "MULTIPOINT(1 2 3,4 5 6)",
      "MULTILINESTRING((0 0,1 1),(2 2,3 3))",
      "MULTILINESTRING((0 0))",
      "MULTILINESTRINGZ((0 0 1,1 1 2))",
      "MULTIPOLYGON(((0 0,1 0,1 1,0 0)))",
      "MULTIPOLYGON(((0 0,1 0,1 1,0 1)))",
      "MULTIPOLYGON(((0 0,1 0,0 0)))",
      "GEOMETRYCOLLECTION EMPTY",
      "GEOMETRYCOLLECTION()",
      "GEOMETRYCOLLECTION Z EMPTY",
      "GEOMETRYCOLLECTIONZ()",
      "GEOMCOLLECTION EMPTY",
      "GEOMETRYCOLLECTION(POINT(1 2 3),POINT(4 5 6))",
      "GEOMETRYCOLLECTION(POINT(1 2),POINT(3 4 5))",
      "GEOMETRYCOLLECTION Z(POINT(1 2 3),GEOMETRYCOLLECTION EMPTY)",
      "GEOMETRYCOLLECTION(GEOMETRYCOLLECTION EMPTY,POINT(1 2 3))",
      "GEOMETRYCOLLECTION(GEOMETRYCOLLECTION(POINT(1 2 3)),POINT(4 5 6))",
      "POINT EMPTY",
      "LINESTRING EMPTY",
      "POLYGON EMPTY",
      "MULTIPOINT EMPTY",
      "MULTILINESTRING EMPTY",
      "MULTIPOLYGON EMPTY",
      "POINT M(1 2 3)",
      "POINT ZM(1 2 3 4)",
      "POINT(1 2) trailing",
      " POINT\t(1\n2) ",
      "GEOMETRYCOLLECTION(POINT(1 2),)",
      "POLYGON((0 0,1 0,1 1,0 0),())"
  }) parse_check(input);
  for (int points = 2; points < 50; ++points) {
    std::string line = "LINESTRING(";
    for (int i = 0; i < points; ++i) {
      if (i) line += ',';
      line += std::to_string(i) + " " + std::to_string(-i) + " " + std::to_string(i * 3);
    }
    line += ')';
    parse_check(line);
  }
  std::cout << "WKT original-parser differential: " << parsed_cases << " cases, "
            << parsed_failures << " failures" << std::endl;
  failures += parsed_failures;
  unsigned binary_cases = 0, binary_failures = 0;
  const auto binary_check = [&](const std::string &input) {
    std::string expected;
    const int reference = original_ewkb(input, expected);
    Sink sink;
    seekdb_plugin_execution_context_v2_t context{};
    context.v1.struct_size = sizeof(context); context.v1.emit_result = Sink::emit;
    context.v1.host = reinterpret_cast<seekdb_plugin_host_handle_t *>(&sink);
    const char *types[] = {"org.seekdb.gis.scalar.bytes"};
    seekdb_plugin_execution_value_v1_t argument{};
    argument.struct_size = sizeof(argument); argument.type_id = types[0];
    argument.data = reinterpret_cast<const uint8_t *>(input.data()); argument.data_size = input.size();
    seekdb_plugin_sql_binding_v1_t binding{};
    CHECK(loader.resolve_native_function("org.seekdb.gis", "org.seekdb.gis.function.st_geomfromewkb",
        types, 1, binding) == OB_SUCCESS);
    const int actual = loader.execute_bound_function(binding, &context.v1, &argument, 1);
    ++binary_cases;
    if ((reference == OB_SUCCESS) != (actual == OB_SUCCESS) ||
        (reference == OB_SUCCESS && (sink.calls != 1 || sink.text != expected)) ||
        (actual != OB_SUCCESS && sink.calls != 0)) {
      if (++binary_failures <= 30) {
        std::cerr << "EWKB case=" << binary_cases << " reference=" << reference << " actual=" << actual << " bytes=";
        for (unsigned char c : input) std::cerr << std::hex << unsigned(c) << ':';
        std::cerr << std::dec << std::endl;
      }
    }
  };
  const auto replace_type = [](std::string bytes, uint32_t type) {
    for (unsigned i = 0; i < 4; ++i) bytes[1 + i] = static_cast<char>(type >> (8 * i));
    return bytes;
  };
  for (uint32_t dimensions : {2u, 3u}) {
    Shape point{1, dimensions, {1, 2}};
    if (dimensions == 3) point.points.push_back(3);
    Shape line{2, dimensions, point.points};
    line.points.insert(line.points.end(), point.points.begin(), point.points.end());
    Shape ring = line;
    ring.points.insert(ring.points.end(), line.points.begin(), line.points.end());
    Shape polygon{3, dimensions, {}, {ring}};
    for (const auto &shape : {point, line, polygon, Shape{4, dimensions, {}, {point}},
                             Shape{5, dimensions, {}, {line}}, Shape{6, dimensions, {}, {polygon}},
                             Shape{7, dimensions, {}, {point, line, polygon}}, Shape{7, dimensions}}) {
      std::string canonical; wkb(shape, canonical);
      for (uint32_t offset : {0u, 1000u, 2000u, 3000u, 4000u}) {
        for (uint32_t flags : {0u, UINT32_C(0x80000000), UINT32_C(0x40000000),
                               UINT32_C(0x20000000), UINT32_C(0xa0000000), UINT32_C(0x10000000)}) {
          std::string input = replace_type(canonical, (shape.type + offset) | flags);
          if (flags & UINT32_C(0x20000000)) input.insert(5, 4, char(0));
          binary_check(input);
        }
      }
      std::string valid = replace_type(canonical, shape.type | (dimensions == 3 ? UINT32_C(0x80000000) : 0));
      for (size_t size = 0; size < valid.size(); ++size) binary_check(valid.substr(0, size));
      binary_check(valid + char(0));
    }
    // Child headers are WKB, not recursive EWKB envelopes. A root Z flag
    // does not authorize embedded child SRIDs or child EWKB Z flags.
    std::string child; wkb(point, child);
    for (uint32_t child_flags : {UINT32_C(0x80000000), UINT32_C(0x20000000), UINT32_C(0xa0000000)}) {
      std::string input(1, char(1)); u32(input, 7 | (dimensions == 3 ? UINT32_C(0x80000000) : 0));
      u32(input, 1);
      std::string nested = replace_type(child, 1 | child_flags);
      if (child_flags & UINT32_C(0x20000000)) nested.insert(5, 4, char(0));
      input += nested;
      binary_check(input);
    }
    const auto shape_check = [&](const Shape &shape) {
      std::string bytes; wkb(shape, bytes);
      binary_check(replace_type(bytes, shape.type | (dimensions == 3 ? UINT32_C(0x80000000) : 0)));
    };
    // Empty types and ring admission differ between the legacy 2D and 3D
    // validators. Check those differences rather than imposing WKT rules.
    for (uint32_t type : {2u, 3u, 4u, 5u, 6u, 7u}) shape_check(Shape{type, dimensions});
    shape_check(Shape{2, dimensions, point.points});
    Shape open_ring{2, dimensions, {0, 0}};
    if (dimensions == 3) open_ring.points.push_back(1);
    for (auto xy : {std::pair<double, double>{1, 0}, {1, 1}, {0, 1}}) {
      open_ring.points.push_back(xy.first); open_ring.points.push_back(xy.second);
      if (dimensions == 3) open_ring.points.push_back(2);
    }
    shape_check(Shape{3, dimensions, {}, {open_ring}});
    open_ring.points[3 * dimensions + 1] = 0;
    shape_check(Shape{3, dimensions, {}, {open_ring}}); // Z closure is not required.
    open_ring.points[3 * dimensions] = -0.0;
    shape_check(Shape{3, dimensions, {}, {open_ring}}); // Bitwise XY closure in 2D.
    Shape other{1, dimensions == 2 ? 3u : 2u, {1, 2}};
    if (other.dimensions == 3) other.points.push_back(3);
    shape_check(Shape{7, dimensions, {}, {other}});
    std::string big_root; wkb(point, big_root); big_root[0] = 0;
    binary_check(big_root);
    if (dimensions == 2) {
      // A numerically equivalent big-endian child is forbidden by the
      // original 2D visitor's uniform byte-order rule.
      std::string input(1, char(1)); u32(input, 7); u32(input, 1);
      std::string big_child(1, char(0));
      big_child.append("\0\0\0\1", 4);
      for (double value : {1.0, 2.0}) {
        std::string scalar; append_double(scalar, value);
        for (auto i = scalar.rbegin(); i != scalar.rend(); ++i) big_child += *i;
      }
      binary_check(input + big_child);
    }
  }
  std::cout << "EWKB original-header/validator differential: " << binary_cases << " cases, "
            << binary_failures << " failures" << std::endl;
  failures += binary_failures;
  CHECK(loader.shutdown_for_process_exit(1000000) == OB_SUCCESS);
  std::cout << "WKT original-visitor differential: " << cases << " cases, " << failures << " failures" << std::endl;
  return failures ? 1 : 0;
}
