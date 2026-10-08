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
#include "../../../plugins/gis/srs_parser.h"
#include "../../../plugins/gis/srs_metadata.h"
#include "seekdb/geo/srs_projection_parameters.hpp"
#ifdef SEEKDB_TEST_CORE_SRS
// Compile the actual legacy adapter against the same grammar and compare all
// fields, without replacing the core-GIS-off runtime's intentionally closed SRS
// service. The factory implementation is compiled here; geometry-independent
// core-off wrappers still come from the production archives.
#include "../../../plugins/gis/srs_parser.cpp"
#include "../../../plugins/gis/srs_metadata.cpp"
#define ObSrsWktParser TestSrsWktParser
#include "../../../src/share/geo/ob_srs_wkt_parser.cpp"
#include "../../../src/share/geo/ob_srs_info.cpp"
#undef ObSrsWktParser
#endif
#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <set>
#include <cerrno>

namespace srs = seekdb::gis::srs;
#define CHECK(x) do { if (!(x)) { std::cerr << __LINE__ << ": " << #x << std::endl; std::abort(); } } while (false)

#ifdef SEEKDB_TEST_CORE_SRS
using namespace oceanbase::common;
static void same(const ObString &a, const std::string &b)
{ CHECK(size_t(a.length()) == b.size() && (b.empty() || std::memcmp(a.ptr(), b.data(), b.size()) == 0)); }
static void same(double a, double b) { CHECK((std::isnan(a) && std::isnan(b)) || a == b); }
static void same(bool a, bool b) { CHECK(a == b); }
static void same(ObAxisDirection a, srs::Direction b) { CHECK(int(a) == int(b)); }
static void same(const ObRsAuthority &a, const srs::Authority &b)
{ same(a.is_valid, b.is_valid); same(a.org_name, b.org_name); same(a.org_code, b.org_code); }
static void same(const ObSpheroid &a, const srs::Spheroid &b)
{ same(a.name,b.name); same(a.semi_major_axis,b.semi_major_axis); same(a.inverse_flattening,b.inverse_flattening); same(a.authority,b.authority); }
static void same(const ObTowgs84 &a, const srs::Towgs84 &b)
{ same(a.is_valid,b.is_valid); for (int i=0;i<7;++i) same(a.value[i],b.value[i]); }
static void same(const ObRsDatum &a, const srs::Datum &b)
{ same(a.name,b.name); same(a.spheroid,b.spheroid); same(a.towgs84,b.towgs84); same(a.authority,b.authority); }
static void same(const ObPrimem &a, const srs::PrimeMeridian &b)
{ same(a.name,b.name); same(a.longtitude,b.longtitude); same(a.authority,b.authority); }
static void same(const ObRsUnit &a, const srs::Unit &b)
{ same(a.type,b.type); same(a.conversion_factor,b.conversion_factor); same(a.authority,b.authority); }
static void same(const ObRsAxis &a, const srs::Axis &b)
{ same(a.name,b.name); same(a.direction,b.direction); }
static void same(const ObRsAxisPair &a, const srs::AxisPair &b)
{ same(a.x,b.x); same(a.y,b.y); }
static void same(const ObGeographicRs &a, const srs::Geographic &b)
{ same(a.rs_name,b.rs_name); same(a.datum_info,b.datum_info); same(a.primem,b.primem); same(a.unit,b.unit); same(a.axis,b.axis); same(a.authority,b.authority); }
static void same(const ObProjection &a, const srs::Projection &b)
{ same(a.name,b.name); same(a.authority,b.authority); }
static void same(const ObProjectionPram &a, const srs::Parameter &b)
{ same(a.name,b.name); same(a.value,b.value); same(a.authority,b.authority); }
static void same(const ObProjectionRs &a, const srs::Projected &b)
{
  same(a.rs_name,b.rs_name); same(a.projected_rs,b.projected_rs); same(a.projection,b.projection);
  same(a.unit,b.unit); same(a.axis,b.axis); same(a.authority,b.authority);
  CHECK(size_t(a.proj_params.vals.size()) == b.proj_params.size());
  for (int i=0;i<a.proj_params.vals.size();++i) same(a.proj_params.vals[i],b.proj_params[i]);
}
static void compare_core(const std::string &wkt, const srs::CoordinateSystem &plugin)
{
  ObArenaAllocator arena;
  const ObString input(wkt.size(), wkt.data());
  if (const auto *geog = boost::get<srs::Geographic>(&plugin)) {
    ObGeographicRs core;
    CHECK(TestSrsWktParser::parse_geog_srs_wkt(arena, input, core) == OB_SUCCESS);
    same(core, *geog);
  } else {
    ObProjectionRs core;
    CHECK(TestSrsWktParser::parse_proj_srs_wkt(arena, input, core) == OB_SUCCESS);
    same(core, boost::get<srs::Projected>(plugin));
  }
}
static void compare_factory(const std::string &wkt, const srs::Metadata &metadata)
{
  ObArenaAllocator backing;
  ObFIFOAllocator arena;
  CHECK(arena.init(&backing, OB_MALLOC_NORMAL_BLOCK_SIZE, ObMemAttr("SrsTest")) == OB_SUCCESS);
  ObSpatialReferenceSystemBase *info = nullptr;
  CHECK(TestSrsWktParser::parse_srs_wkt(arena, metadata.srid, ObString(wkt.size(), wkt.data()), info) == OB_SUCCESS);
  ObSrsItem item(info);
  CHECK(info->get_srid() == metadata.srid && item.is_geographical_srs() == metadata.geographic);
  same(info->angular_unit(), metadata.angular_unit); same(info->linear_unit(), metadata.linear_unit);
  same(info->prime_meridian(), metadata.prime_meridian);
  CHECK(int(info->axis_direction(0)) == metadata.axis0 && int(info->axis_direction(1)) == metadata.axis1);
  CHECK(info->is_wgs84() == metadata.is_wgs84 && info->has_wgs84_value() == metadata.has_towgs84());
  if (!metadata.geographic) CHECK(static_cast<ObProjectedSrs *>(info)->get_projection_type() ==
                                 static_cast<ObProjectionType>(metadata.projection_method));
  same(item.semi_minor_axis(), seekdb::geo::srs::semi_minor_axis(metadata.geographic, metadata.semi_major, metadata.inverse_flattening));
  const auto coordinates = metadata.coordinates();
  CHECK(item.is_lat_long_order() == coordinates.latitude_first());
  CHECK(item.is_latitude_north() == coordinates.north() && item.is_longtitude_east() == coordinates.east());
  for (double value : {-90.0, -1.25, 0.0, 2.337, 180.0}) {
    double core = 99, plugin_value = 99;
    CHECK((item.latitude_convert_to_radians(value, core) == OB_SUCCESS) == coordinates.latitude_to_radians(value, plugin_value));
    same(core, plugin_value);
    CHECK((item.latitude_convert_from_radians(value, core) == OB_SUCCESS) == coordinates.latitude_from_radians(value, plugin_value));
    same(core, plugin_value);
    CHECK((item.longtitude_convert_to_radians(value, core) == OB_SUCCESS) == coordinates.longitude_to_radians(value, plugin_value));
    same(core, plugin_value);
    CHECK((item.longtitude_convert_from_radians(value, core) == OB_SUCCESS) == coordinates.longitude_from_radians(value, plugin_value));
    same(core, plugin_value);
  }
  info->~ObSpatialReferenceSystemBase();
  arena.free(info);
}
#endif

static std::string definition(const std::string &line)
{
  const auto values = line.find(" VALUES (");
  CHECK(values != std::string::npos);
  size_t field = 0;
  std::string value;
  bool quote = false;
  for (size_t i = values + 9; i < line.size(); ++i) {
    const char c = line[i];
    if (quote && c == '\\') {
      CHECK(i + 1 < line.size());
      const char escaped = line[++i];
      switch (escaped) {
        case 'n': value += '\n'; break;
        case 'r': value += '\r'; break;
        case 't': value += '\t'; break;
        case '0': value += '\0'; break;
        case 'b': value += '\b'; break;
        case 'Z': value += char(26); break;
        default: value += escaped; break;
      }
    } else if (c == '\'') {
      if (quote && i + 1 < line.size() && line[i + 1] == '\'') { value += c; ++i; }
      else quote = !quote;
    } else if (c == ',' && !quote) {
      if (field == 5) return value.substr(value.find_first_not_of(" \t"));
      ++field; value.clear();
    } else value += c;
  }
  CHECK(false);
  return {};
}

int main(int argc, char **argv)
{
  CHECK(argc == 2);
  // Golden digest of the pre-extraction factory EPSG -> ordered registration
  // lists. Protect all 39 entries, including lists not exercised by a query.
  uint64_t digest = UINT64_C(14695981039346656037);
  const auto mix = [&](uint32_t value) {
    for (unsigned byte = 0; byte < 4; ++byte) {
      digest ^= (value >> (8 * byte)) & 255;
      digest *= UINT64_C(1099511628211);
    }
  };
  for (const auto &entry : seekdb::geo::srs::projection_parameters) {
    mix(entry.method); mix(entry.count);
    CHECK(seekdb::geo::srs::find_projection(entry.method) == &entry);
    for (size_t i = 0; i < entry.count; ++i) mix(entry.codes[i]);
  }
  CHECK(digest == UINT64_C(0x19ba1e63cbc2f8ab));
  CHECK(seekdb::geo::srs::find_projection(123456) == nullptr);
  for (const std::string number : {"4326", " +04326tail", "-4326", "0x4326", "2147483648",
      "9223372036854775807", "-9223372036854775808", "9223372036854775808",
      "-9223372036854775809", "1844674407370955161600", "", "+", "  x"}) {
    int value = 9;
    const int status = srs::authority_code(number, value);
#ifdef SEEKDB_TEST_CORE_SRS
    int error = 0;
    const int original = ObCharset::strntoll(number.data(), number.size(), 10, &error);
    CHECK(status == error);
    if (status == 0) CHECK(value == original);
#else
    CHECK(status == 0 || status == EDOM || status == ERANGE);
#endif
  }
#ifdef SEEKDB_TEST_CORE_SRS
  CHECK(ObCharset::init_charset() == OB_SUCCESS);
  for (unsigned prefix = 0; prefix < 256; ++prefix) {
    const std::string number = std::string(1, char(prefix)) + "4326suffix";
    int value = 9, error = 0;
    const int status = srs::authority_code(number, value);
    const int original = ObCharset::strntoll(number.data(), number.size(), 10, &error);
    CHECK(status == error);
    if (status == 0) CHECK(value == original);
  }
  struct FailingFifo final : ObFIFOAllocator {
    void *alloc(int64_t) override { return nullptr; }
    void *alloc(int64_t, const ObMemAttr &) override { return nullptr; }
    void free(void *) override {}
  } failing_fifo;
  ObTransverseMercatorSrs unallocated(&failing_fifo);
  CHECK(unallocated.register_proj_params() == OB_ALLOCATE_MEMORY_FAILED);
#endif
  const std::string geographic = R"(GEOGCS["test space",DATUM["datum",SPHEROID["sphere",6371000,0]],PRIMEM["Paris",2.33722917],UNIT["grad",0.015707963267948967],AXIS["longitude",WEST],AXIS["latitude",SOUTH]])";
  const std::string projected = "PROJCS[\"projection\"," + geographic +
      R"(,PROJECTION["Transverse Mercator",AUTHORITY["EPSG","9807"]],PARAMETER["scale",0.9996,AUTHORITY["EPSG","8805"]],UNIT["foot",0.3048]])";
  srs::CoordinateSystem output;
  CHECK(srs::parse(geographic, output));
  srs::Metadata meta;
  CHECK(srs::prepare(42, output, meta) == srs::PrepareStatus::ok && meta.geographic && meta.srid == 42);
  double longitude = 0, latitude = 0;
  CHECK(meta.coordinates().longitude_to_radians(10, longitude));
  CHECK(longitude == (-10 + 2.33722917) * 0.015707963267948967);
  CHECK(meta.coordinates().latitude_to_radians(10, latitude) && latitude == -10 * 0.015707963267948967);
  CHECK(seekdb::geo::srs::semi_minor_axis(true, 6371000, 0) == 6371000);
  CHECK(srs::prepare(UINT32_MAX, output, meta) == srs::PrepareStatus::invalid_value && meta.srid == 42);
  CHECK(srs::prepare(UINT64_C(0x10000002a), output, meta) == srs::PrepareStatus::invalid_value && meta.srid == 42);
  const auto &g = boost::get<srs::Geographic>(output);
  CHECK(g.rs_name == "test space" && g.datum_info.spheroid.semi_major_axis == 6371000);
  CHECK(g.datum_info.spheroid.inverse_flattening == 0 && g.primem.longtitude == 2.33722917);
  CHECK(g.unit.conversion_factor == 0.015707963267948967);
  CHECK(g.axis.x.direction == srs::Direction::WEST && g.axis.y.direction == srs::Direction::SOUTH);
  CHECK(!g.authority.is_valid && !g.datum_info.towgs84.is_valid);
  CHECK(srs::parse(projected, output));
  CHECK(srs::prepare(43, output, meta) == srs::PrepareStatus::missing_parameter && meta.srid == 42);
#ifdef SEEKDB_TEST_CORE_SRS
  compare_core(projected, output);
  struct FailingAllocator final : ObIAllocator {
    void *alloc(int64_t) override { return nullptr; }
    void *alloc(int64_t, const ObMemAttr &) override { return nullptr; }
    void free(void *) override {}
  } fail;
  ObGeographicRs failed;
  CHECK(TestSrsWktParser::parse_geog_srs_wkt(fail, ObString(geographic.size(), geographic.data()), failed) == OB_ALLOCATE_MEMORY_FAILED);
  CHECK(failed.rs_name.empty());
  ObArenaAllocator arena;
  const char *sphere_wkt = R"(GEOGCS["sphere",DATUM["sphere",SPHEROID["sphere",6371000,0],TOWGS84[0,0,0,0,0,0,0]],PRIMEM["Greenwich",0],UNIT["degree",0.017453292519943278],AXIS["Lat",NORTH],AXIS["Lon",EAST]])";
  ObSpatialReferenceSystemBase *sphere = nullptr;
  CHECK(TestSrsWktParser::parse_srs_wkt(arena, 70000001, ObString::make_string(sphere_wkt), sphere) == OB_SUCCESS);
  ObString sphere_proj4;
  CHECK(sphere->get_proj4_param(&arena, sphere_proj4) == OB_SUCCESS);
  const std::string sphere_text(sphere_proj4.ptr(), sphere_proj4.length());
  const auto minor = sphere_text.find(" +b=");
  CHECK(minor != std::string::npos && std::stod(sphere_text.substr(minor + 4)) == 6371000);
  CHECK(sphere->get_proj4_param(nullptr, sphere_proj4) == OB_INVALID_ARGUMENT);
  for (const char *bad : {"", " ", "\t\r\n", "GEOGCS[", "GEOGCS[]"})
    CHECK(TestSrsWktParser::parse_geog_srs_wkt(arena, ObString::make_string(bad), failed) == OB_ERR_PARSER_SYNTAX);
#endif
  const auto &p = boost::get<srs::Projected>(output);
  CHECK(p.projection.authority.is_valid && p.projection.authority.org_code == "9807");
  CHECK(p.proj_params.size() == 1 && p.proj_params[0].value == 0.9996);
  CHECK(p.unit.conversion_factor == 0.3048 && p.axis.x.direction == srs::Direction::INIT &&
        p.axis.y.direction == srs::Direction::INIT && !p.authority.is_valid);
  for (const std::string &bad : {std::string(), std::string(" \t\r\n"), geographic + " junk",
      geographic + "]", geographic.substr(0, geographic.size() - 1), std::string("GEOGCS[]"),
      std::string("GEOGCS[") + std::string(10000, '[')}) {
    CHECK(!srs::parse(bad, output));
    CHECK(boost::get<srs::Projected>(output).rs_name == "projection");
  }
  auto round = projected;
  std::replace(round.begin(), round.end(), '[', '(');
  std::replace(round.begin(), round.end(), ']', ')');
  CHECK(srs::parse("  " + round + "\n\t", output));
  auto lowercase = geographic;
  // Keywords/directions are case-insensitive; quoted values remain unchanged.
  for (const std::string key : {"GEOGCS", "DATUM", "SPHEROID", "PRIMEM", "UNIT", "AXIS", "WEST", "SOUTH"}) {
    size_t pos = 0;
    while ((pos = lowercase.find(key, pos)) != std::string::npos) {
      for (size_t i = 0; i < key.size(); ++i) lowercase[pos + i] += 'a' - 'A';
      pos += key.size();
    }
  }
  CHECK(srs::parse(lowercase, output));
  auto owned = boost::get<srs::Geographic>(output);
  std::fill(lowercase.begin(), lowercase.end(), 'x');
  CHECK(owned.rs_name == "test space");
  auto transform = geographic;
  const auto pos = transform.find("]],PRIMEM");
  CHECK(pos != std::string::npos);
  transform.insert(pos + 1, ",TOWGS84[1,2,3,4,5,6,7]");
  CHECK(srs::parse(transform, output));
  CHECK(boost::get<srs::Geographic>(output).datum_info.towgs84.is_valid);
  for (int i = 0; i < 7; ++i) CHECK(boost::get<srs::Geographic>(output).datum_info.towgs84.value[i] == i + 1);

  std::ifstream file(argv[1]); CHECK(file.good());
  std::string line;
  size_t count = 0, geog_count = 0, projected_count = 0;
  std::set<std::string> methods;
  while (std::getline(file, line)) {
    if (line.find("REPLACE INTO ") != 0) continue;
    const auto wkt = definition(line);
    if (!srs::parse(wkt, output)) { std::cerr << wkt << std::endl; CHECK(false); }
    CHECK(srs::prepare(42, output, meta) == srs::PrepareStatus::ok);
    ++count;
#ifdef SEEKDB_TEST_CORE_SRS
    compare_core(wkt, output);
    compare_factory(wkt, meta);
#endif
    if (const auto *parsed = boost::get<srs::Geographic>(&output)) {
      ++geog_count;
      CHECK(!parsed->rs_name.empty() && parsed->datum_info.spheroid.semi_major_axis > 0);
    } else {
      const auto &projected_rs = boost::get<srs::Projected>(output);
      ++projected_count;
      CHECK(projected_rs.unit.conversion_factor > 0 && !projected_rs.projection.name.empty());
      methods.insert(projected_rs.projection.authority.org_code);
    }
  }
  CHECK(count > 5000 && geog_count > 100 && projected_count > 100 && methods.size() > 20);
  std::cout << "PASS: original WKT grammar with plugin-owned records: " << count << " catalog definitions, "
            << geog_count << " geographic, " << projected_count << " projected, " << methods.size()
            << " projection authorities; ownership/defaults/brackets/failure isolation" << std::endl;
}
