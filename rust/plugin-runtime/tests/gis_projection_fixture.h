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
#pragma once
#include "seekdb/plugin/srs_spi.h"

namespace gis_projection_test {
static void exercise(ObPluginLoader &loader)
{
  using gis_spatial_test::geometry;
  const std::string geographic = "+proj=longlat +datum=WGS84";
  const std::string utm = "+proj=utm +zone=31 +datum=WGS84 +units=m";
  constexpr double degree = 0.017453292519943295;
  auto bytes = geometry(1, {3 * degree, 0}, 4326);
  std::string source = geographic, target = utm;
  uint32_t srid = 32631;
  uint8_t unaligned_srid[sizeof(srid) + 1]{};
  struct Sink {
    unsigned calls = 0;
    seekdb_plugin_status_t status = SEEKDB_PLUGIN_STATUS_OK;
    std::vector<uint8_t> bytes;
    static seekdb_plugin_status_t SEEKDB_PLUGIN_CALL emit(seekdb_plugin_host_handle_t *host,
        const seekdb_plugin_execution_result_v1_t *result) {
      auto &s = *reinterpret_cast<Sink *>(host);
      CHECK(result && result->struct_size == sizeof(*result) && !result->is_null);
      CHECK(result->type_id && std::strcmp(result->type_id, "org.seekdb.gis.geometry") == 0);
      CHECK(result->data && result->data_size >= 10);
      ++s.calls; s.bytes.assign(result->data, result->data + result->data_size);
      return s.status;
    }
  } sink;
  seekdb_plugin_execution_context_v1_t context{};
  context.struct_size = sizeof(context); context.host = reinterpret_cast<seekdb_plugin_host_handle_t *>(&sink);
  context.emit_result = Sink::emit;
  seekdb_plugin_execution_value_v1_t args[4]{};
  for (auto &a : args) a.struct_size = sizeof(a);
  args[0].type_id = "org.seekdb.gis.geometry";
  args[1].type_id = args[2].type_id = "core.type.bytes";
  args[3].type_id = "core.type.uint32";
  const auto call = [&](int expected = OB_SUCCESS, seekdb_plugin_status_t status = SEEKDB_PLUGIN_STATUS_OK) {
    sink = {}; sink.status = status;
    args[0].data = bytes.data(); args[0].data_size = bytes.size();
    args[1].data = reinterpret_cast<const uint8_t *>(source.data()); args[1].data_size = source.size();
    args[2].data = reinterpret_cast<const uint8_t *>(target.data()); args[2].data_size = target.size();
    std::memcpy(unaligned_srid + 1, &srid, sizeof(srid));
    args[3].data = unaligned_srid + 1; args[3].data_size = sizeof(srid);
    const int ret = loader.execute_function(SEEKDB_PLUGIN_SRS_TRANSFORM_SERVICE, 1, 0, &context, args, 4);
    if (ret != expected) std::cerr << "projection service: " << ret << " expected " << expected << std::endl;
    CHECK(ret == expected);
    CHECK(sink.calls == ((expected == OB_SUCCESS || status != SEEKDB_PLUGIN_STATUS_OK) ? 1 : 0));
    if (sink.calls) for (int i = 0; i < 4; ++i) CHECK(sink.bytes[i] == uint8_t(srid >> (8 * i)));
  };
  const auto coordinate = [&](unsigned i, size_t offset = 10) {
    double v; CHECK(sink.bytes.size() >= offset + (i + 1) * sizeof(v));
    std::memcpy(&v, sink.bytes.data() + offset + i * sizeof(v), sizeof(v)); return v;
  };
  call(); CHECK(std::abs(coordinate(0) - 500000) < 1e-8 && std::abs(coordinate(1)) < 1e-8);
  auto projected = sink.bytes;
  target = "+proj=utm +zone=31 +datum=WGS84 +units=km";
  call(); CHECK(std::abs(coordinate(0) - 500) < 1e-10);
  target = "+proj=utm +zone=31 +south +datum=WGS84 +units=m";
  call(); CHECK(std::abs(coordinate(0) - 500000) < 1e-8 && coordinate(1) == 10000000);
  bytes = projected; source = utm; target = geographic; srid = 4326;
  call(); CHECK(std::abs(coordinate(0) - 3 * degree) < 1e-14 && std::abs(coordinate(1)) < 1e-14);
  // Definitions, not numeric SRID equality, determine the transformation.
  source = geographic; target = utm; bytes = geometry(1, {3 * degree, 0}, srid);
  call(); CHECK(std::abs(coordinate(0) - 500000) < 1e-8);
  source = "+proj=longlat +datum=WGS84 +pm=paris"; target = geographic;
  bytes = geometry(1, {0, 0});
  call(); CHECK(std::abs(coordinate(0) - (2 + 20.0 / 60 + 14.025 / 3600) * degree) < 1e-14);
  source = geographic; target = utm + " +vunits=km";
  bytes = geometry(1001, {3 * degree, 0, 1234});
  call(); CHECK(std::abs(coordinate(0) - 500000) < 1e-8 && coordinate(2) == 1.234);
  bytes = sink.bytes; source = target; target = geographic;
  call(); CHECK(std::abs(coordinate(0) - 3 * degree) < 1e-14 && coordinate(2) == 1234);
  const std::string shift = "+proj=longlat +a=6378137 +rf=298.257223563 +towgs84=1,2,3,0,0,0,0";
  source = shift;
  bytes = geometry(1001, {0, 0, 0});
  call(); CHECK(sink.bytes.size() == 34 && std::abs(coordinate(0) - std::atan2(2.0, 6378138.0)) < 1e-14);
  CHECK(coordinate(1) > 0 && std::abs(coordinate(2) - 1) < 1e-5);
  bytes = sink.bytes; source = geographic; target = shift;
  call(); CHECK(std::abs(coordinate(0)) < 1e-12 && std::abs(coordinate(1)) < 1e-12 && std::abs(coordinate(2)) < 1e-5);

  // Every vertex is visited, with no partial emission on a failing tail.
  source = geographic; target = utm;
  bytes = geometry(2, {3 * degree, 0, 3 * degree, 0.5}, 4326);
  call(); CHECK(sink.bytes.size() == bytes.size() && std::abs(coordinate(0, 14) - 500000) < 1e-8);
  CHECK(coordinate(3, 14) > 3000000);
  bytes = geometry(2, {3 * degree, 0, 3 * degree, 2}, 4326);
  call(OB_INVALID_ARGUMENT);
  auto collection = geometry(7, {}, 4326);
  gis_spatial_test::append_u32(collection, 2);
  const auto point = geometry(1, {3 * degree, 0}, 4326);
  const auto bad = geometry(1, {3 * degree, 2}, 4326);
  collection.insert(collection.end(), point.begin() + 5, point.end());
  collection.insert(collection.end(), bad.begin() + 5, bad.end());
  bytes = collection; call(OB_INVALID_ARGUMENT);
  bytes = geometry(1, {3 * degree, 0}, 4326);
  call(OB_TIMEOUT, SEEKDB_PLUGIN_STATUS_TIMEOUT);
  call(OB_ALLOCATE_MEMORY_FAILED, SEEKDB_PLUGIN_STATUS_NO_MEMORY);
  for (auto &arg : args) {
    arg.reserved[0] = 1; call(OB_INVALID_ARGUMENT); arg.reserved[0] = 0;
    arg.reserved_bytes[0] = 1; call(OB_INVALID_ARGUMENT); arg.reserved_bytes[0] = 0;
    arg.is_null = 1; call(OB_INVALID_ARGUMENT); arg.is_null = 0;
    --arg.struct_size; call(OB_INVALID_ARGUMENT); ++arg.struct_size;
    auto *type = arg.type_id; arg.type_id = "bad.type"; call(OB_INVALID_ARGUMENT); arg.type_id = type;
  }
  for (const std::string &bad_definition : {std::string(), std::string("+datum=WGS84"),
      std::string("+proj=unknown +datum=WGS84"), std::string("+proj=utm +zone=99 +datum=WGS84"),
      std::string("+proj=longlat +datum=WGS84 +nadgrids=/tmp/grid"),
      std::string("+proj=longlat +datum=NAD27"),
      std::string("+proj=longlat +datum=WGS84 +init=epsg:4326"),
      std::string("+proj=geocent +datum=WGS84"), std::string("+proj=longlat +datum=WGS84 +axis=neu"),
      std::string(SEEKDB_PLUGIN_SRS_MAX_PROJ4_BYTES + 1, 'x'), geographic + std::string(1, '\0')}) {
    source = bad_definition; target = utm; call(OB_INVALID_ARGUMENT);
    source = geographic; target = bad_definition; call(OB_INVALID_ARGUMENT);
  }
  bytes = geometry(7, {}, 4326); gis_spatial_test::append_u32(bytes, 0);
  source = geographic; target = utm; call(); CHECK(sink.bytes.size() == bytes.size());
  target = "bad"; call(OB_INVALID_ARGUMENT); // Empty geometry is not a bypass.
  target = "+proj=longlat +datum=NAD27"; call(OB_INVALID_ARGUMENT);
  std::cout << "PASS: leased general proj4 service: UTM, horizontal/vertical units, prime meridian, 3D datum, tail-failure and ABI fences" << std::endl;
}
} // namespace gis_projection_test
