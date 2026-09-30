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
#include "geometry_engine.h"
#include "srs_metadata.h"
#include "seekdb/plugin/srs_spi.h"
#include <algorithm>
#include <cstring>
#include <iterator>
#include <new>

extern "C" seekdb_plugin_status_t seekdb_gis_srs_describe(
    seekdb_plugin_instance_handle_t *instance,
    const seekdb_plugin_execution_context_v1_t *context,
    const seekdb_plugin_execution_value_v1_t *arguments, uint32_t count)
try {
  seekdb_plugin_srs_request_v1_t request{};
  if (instance == nullptr || context == nullptr || context->struct_size < sizeof(*context) ||
      context->emit_result == nullptr || arguments == nullptr || count != 2)
    return SEEKDB_PLUGIN_STATUS_INVALID_ARGUMENT;
  for (uint32_t i = 0; i < 2; ++i) {
    const auto &arg = arguments[i];
    if (arg.struct_size != sizeof(arg) || arg.is_null || arg.data == nullptr || arg.type_id == nullptr ||
        std::any_of(std::begin(arg.reserved_bytes), std::end(arg.reserved_bytes), [](uint8_t v) { return v != 0; }) ||
        std::any_of(std::begin(arg.reserved), std::end(arg.reserved), [](uint64_t v) { return v != 0; }))
      return SEEKDB_PLUGIN_STATUS_INVALID_ARGUMENT;
  }
  if (std::strcmp(arguments[0].type_id, "core.type.bytes") != 0 || arguments[0].data_size == 0 ||
      arguments[0].data_size > SEEKDB_PLUGIN_SRS_MAX_WKT_BYTES ||
      std::strcmp(arguments[1].type_id, SEEKDB_PLUGIN_SRS_REQUEST_TYPE) != 0 ||
      arguments[1].data_size != sizeof(request)) return SEEKDB_PLUGIN_STATUS_INVALID_ARGUMENT;
  std::memcpy(&request, arguments[1].data, sizeof(request));
  if (request.struct_size != sizeof(request) || request.srid == UINT32_MAX ||
      std::any_of(std::begin(request.reserved), std::end(request.reserved), [](uint64_t v) { return v != 0; }))
    return SEEKDB_PLUGIN_STATUS_INVALID_ARGUMENT;
  namespace srs = seekdb::gis::srs;
  srs::CoordinateSystem parsed;
  srs::Metadata metadata;
  if (!srs::parse(std::string_view(reinterpret_cast<const char *>(arguments[0].data), arguments[0].data_size), parsed) ||
      srs::prepare(request.srid, parsed, metadata) != srs::PrepareStatus::ok)
    return SEEKDB_PLUGIN_STATUS_INVALID_ARGUMENT;
  seekdb_plugin_srs_metadata_v1_t header{};
  header.struct_size = sizeof(header); header.srid = metadata.srid;
  header.flags = (metadata.geographic ? SEEKDB_PLUGIN_SRS_GEOGRAPHIC : 0) |
      (metadata.is_wgs84 ? SEEKDB_PLUGIN_SRS_WGS84 : 0) |
      (metadata.has_towgs84() ? SEEKDB_PLUGIN_SRS_HAS_TOWGS84 : 0);
  header.projection_method = metadata.projection_method;
  header.axis0 = metadata.axis0; header.axis1 = metadata.axis1;
  header.geographic_axis0 = metadata.geographic_axis0; header.geographic_axis1 = metadata.geographic_axis1;
  header.semi_major = metadata.semi_major; header.inverse_flattening = metadata.inverse_flattening;
  header.prime_meridian = metadata.prime_meridian; header.angular_unit = metadata.angular_unit;
  header.linear_unit = metadata.linear_unit;
  std::copy(metadata.towgs84.begin(), metadata.towgs84.end(), header.towgs84);
  if (metadata.parameters.size() > SEEKDB_PLUGIN_SRS_MAX_PARAMETERS) return SEEKDB_PLUGIN_STATUS_INTERNAL;
  header.parameter_count = static_cast<uint32_t>(metadata.parameters.size());
  std::vector<uint8_t> bytes(sizeof(header) + header.parameter_count * sizeof(seekdb_plugin_srs_parameter_v1_t));
  std::memcpy(bytes.data(), &header, sizeof(header));
  for (size_t i = 0; i < metadata.parameters.size(); ++i) {
    seekdb_plugin_srs_parameter_v1_t parameter{};
    parameter.authority_code = metadata.parameters[i].first; parameter.value = metadata.parameters[i].second;
    std::memcpy(bytes.data() + sizeof(header) + i * sizeof(parameter), &parameter, sizeof(parameter));
  }
  seekdb_plugin_execution_result_v1_t result{};
  result.struct_size = sizeof(result); result.type_id = SEEKDB_PLUGIN_SRS_RESULT_TYPE;
  result.data = bytes.data(); result.data_size = bytes.size();
  return context->emit_result(context->host, &result);
}
catch (const std::bad_alloc &) { return SEEKDB_PLUGIN_STATUS_NO_MEMORY; }
catch (...) { return SEEKDB_PLUGIN_STATUS_INTERNAL; }

