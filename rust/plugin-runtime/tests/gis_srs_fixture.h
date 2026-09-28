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

namespace gis_srs_test {
struct Sink {
  unsigned calls = 0;
  seekdb_plugin_status_t status = SEEKDB_PLUGIN_STATUS_OK;
  seekdb_plugin_srs_metadata_v1_t header{};
  std::vector<seekdb_plugin_srs_parameter_v1_t> parameters;
  static seekdb_plugin_status_t SEEKDB_PLUGIN_CALL emit(seekdb_plugin_host_handle_t *host,
      const seekdb_plugin_execution_result_v1_t *result) {
    auto &s = *reinterpret_cast<Sink *>(host);
    CHECK(result && result->struct_size == sizeof(*result) && !result->is_null);
    CHECK(std::strcmp(result->type_id, SEEKDB_PLUGIN_SRS_RESULT_TYPE) == 0 && result->data);
    CHECK(result->data_size >= sizeof(s.header));
    std::memcpy(&s.header, result->data, sizeof(s.header));
    CHECK(s.header.struct_size == sizeof(s.header) && s.header.parameter_count <= SEEKDB_PLUGIN_SRS_MAX_PARAMETERS);
    CHECK(s.header.reserved_word == 0);
    for (auto r : s.header.reserved) CHECK(r == 0);
    CHECK(result->data_size == sizeof(s.header) + s.header.parameter_count * sizeof(seekdb_plugin_srs_parameter_v1_t));
    s.parameters.resize(s.header.parameter_count);
    if (!s.parameters.empty()) std::memcpy(s.parameters.data(), result->data + sizeof(s.header),
                                           s.parameters.size() * sizeof(s.parameters[0]));
    for (const auto &p : s.parameters) CHECK(p.reserved_word == 0);
    ++s.calls;
    return s.status;
  }
};

static std::string geographic_wkt()
{
  return R"(GEOGCS["WGS 84",DATUM["World Geodetic System 1984",SPHEROID["WGS 84",6378137,298.257223563,AUTHORITY["EPSG","7030"]],AUTHORITY["EPSG","6326"]],PRIMEM["Greenwich",0,AUTHORITY["EPSG","8901"]],UNIT["degree",0.017453292519943278,AUTHORITY["EPSG","9122"]],AXIS["Lat",NORTH],AXIS["Lon",EAST],AUTHORITY["EPSG","4326"]])";
}
static void exercise(ObPluginLoader &loader)
{
  const std::string geographic = geographic_wkt();
  std::string wkt = geographic;
  seekdb_plugin_srs_request_v1_t request{}; request.struct_size = sizeof(request); request.srid = 4326;
  std::vector<uint8_t> request_bytes(sizeof(request) + 1);
  seekdb_plugin_execution_value_v1_t args[2]{};
  for (auto &arg : args) arg.struct_size = sizeof(arg);
  args[0].type_id = "core.type.bytes"; args[1].type_id = SEEKDB_PLUGIN_SRS_REQUEST_TYPE;
  args[1].data = request_bytes.data() + 1; args[1].data_size = sizeof(request);
  Sink sink;
  seekdb_plugin_execution_context_v1_t ctx{};
  ctx.struct_size = sizeof(ctx); ctx.host = reinterpret_cast<seekdb_plugin_host_handle_t *>(&sink);
  ctx.emit_result = Sink::emit;
  const auto call = [&](int expected = OB_SUCCESS, seekdb_plugin_status_t status = SEEKDB_PLUGIN_STATUS_OK) {
    sink = {}; sink.status = status;
    std::memcpy(request_bytes.data() + 1, &request, sizeof(request));
    args[0].data = reinterpret_cast<const uint8_t *>(wkt.data()); args[0].data_size = wkt.size();
    CHECK(loader.execute_function(SEEKDB_PLUGIN_SRS_DESCRIBE_SERVICE, 1, 0, &ctx, args, 2) == expected);
    CHECK(sink.calls == ((expected == OB_SUCCESS || status != SEEKDB_PLUGIN_STATUS_OK) ? 1 : 0));
  };
  call();
  CHECK(sink.header.flags == (SEEKDB_PLUGIN_SRS_GEOGRAPHIC | SEEKDB_PLUGIN_SRS_WGS84));
  CHECK(sink.header.srid == 4326 && sink.header.axis0 == 4 && sink.header.axis1 == 1);
  CHECK(sink.header.geographic_axis0 == 4 && sink.header.geographic_axis1 == 1);
  CHECK(sink.header.semi_major == 6378137 && sink.header.inverse_flattening == 298.257223563);
  CHECK(sink.header.linear_unit == 1 && sink.parameters.empty());
  for (double value : sink.header.towgs84) CHECK(std::isnan(value));
  call(OB_TIMEOUT, SEEKDB_PLUGIN_STATUS_TIMEOUT);
  call(OB_ALLOCATE_MEMORY_FAILED, SEEKDB_PLUGIN_STATUS_NO_MEMORY);
  request.srid = 999; call(); CHECK(sink.header.srid == 999 && (sink.header.flags & SEEKDB_PLUGIN_SRS_WGS84));
  // SRID does not override authoritative WKT metadata.
  request.srid = UINT32_MAX; call(OB_INVALID_ARGUMENT); request.srid = 4326;
  request.reserved[3] = 1; call(OB_INVALID_ARGUMENT); request.reserved[3] = 0;
  --request.struct_size; call(OB_INVALID_ARGUMENT); ++request.struct_size;
  --args[1].data_size; call(OB_INVALID_ARGUMENT); ++args[1].data_size;
  args[1].type_id = "core.type.bytes"; call(OB_INVALID_ARGUMENT); args[1].type_id = SEEKDB_PLUGIN_SRS_REQUEST_TYPE;
  args[0].is_null = 1; call(OB_INVALID_ARGUMENT); args[0].is_null = 0;
  args[0].reserved_bytes[0] = 1; call(OB_INVALID_ARGUMENT); args[0].reserved_bytes[0] = 0;
  args[1].reserved[0] = 1; call(OB_INVALID_ARGUMENT); args[1].reserved[0] = 0;
  for (const std::string &bad : {std::string(), std::string(" \t"), geographic + "junk",
      std::string(SEEKDB_PLUGIN_SRS_MAX_WKT_BYTES + 1, ' ')}) { wkt = bad; call(OB_INVALID_ARGUMENT); }
  const std::string parameters = R"(,PARAMETER["lat",0,AUTHORITY["EPSG","8801"]],PARAMETER["lon",-62,AUTHORITY["EPSG","8802"]],PARAMETER["x",400000,AUTHORITY["EPSG","8806"]],PARAMETER["y",0,AUTHORITY["EPSG","8807"]])";
  const auto projected = [&](int method, const std::string &params) {
    return "PROJCS[\"custom\"," + geographic + ",PROJECTION[\"method\",AUTHORITY[\"EPSG\",\"" +
        std::to_string(method) + "\"]]" + params + ",UNIT[\"metre\",1],AXIS[\"E\",EAST],AXIS[\"N\",NORTH]]";
  };
  wkt = projected(9806, parameters); call();
  CHECK(sink.header.projection_method == 9806 && sink.parameters.size() == 4);
  CHECK(sink.header.flags == SEEKDB_PLUGIN_SRS_WGS84 && sink.header.semi_major == 6378137);
  CHECK(sink.header.axis0 == 1 && sink.header.axis1 == 4 && sink.header.geographic_axis0 == 4);
  CHECK(sink.parameters[0].authority_code == 8801 && sink.parameters[1].value == -62);
  wkt = projected(9807, parameters); call(OB_INVALID_ARGUMENT);
  wkt = projected(9807, parameters + R"(,PARAMETER["k",0.9996,AUTHORITY["EPSG","8805"]])"); call();
  CHECK(sink.parameters.size() == 5 && sink.parameters[2].authority_code == 8805 && sink.parameters[2].value == 0.9996);
  wkt = projected(9806, parameters + R"(,PARAMETER["duplicate",42,AUTHORITY["EPSG","8802"]])"); call();
  CHECK(sink.parameters[1].value == 42); // Original last matching authority wins.
  wkt = projected(123456, ""); call();
  CHECK(sink.header.projection_method == 0 && sink.parameters.empty()); // Not a transform-support claim.
  wkt = geographic;
  wkt.insert(wkt.find(",AUTHORITY[\"EPSG\",\"6326\"]"), ",TOWGS84[1,2,3,4,5,6,7]");
  call();
  CHECK(sink.header.flags == (SEEKDB_PLUGIN_SRS_GEOGRAPHIC | SEEKDB_PLUGIN_SRS_HAS_TOWGS84));
  for (int i = 0; i < 7; ++i) CHECK(sink.header.towgs84[i] == i + 1);
  std::cout << "PASS: leased SRS metadata ABI, WGS84/axes/units/parameters, Cassini method and input/callback failures" << std::endl;
}
} // namespace gis_srs_test
