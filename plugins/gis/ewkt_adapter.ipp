
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


static seekdb_plugin_status_t geometry_as_ewkt(
    const seekdb_plugin_execution_context_v1_t *context,
    const seekdb_plugin_execution_value_v1_t *arguments, uint32_t count)
{
  if ((count != 1 && count != 2) || arguments[0].struct_size != sizeof(arguments[0]) ||
      (count == 2 && arguments[1].struct_size != sizeof(arguments[1])))
    return SEEKDB_PLUGIN_STATUS_INVALID_ARGUMENT;
  if (arguments[0].is_null || (count == 2 && arguments[1].is_null)) {
    const seekdb_plugin_execution_result_v1_t result = {
        sizeof(result), "core.type.text", nullptr, 0, 1,
        {0, 0, 0, 0, 0, 0, 0}, {0, 0, 0, 0}};
    return context->emit_result(context->host, &result);
  }
  int64_t precision = 15;
  if (count == 2) {
    if (!arguments[1].type_id || std::strcmp(arguments[1].type_id, "core.type.int64") != 0 ||
        !arguments[1].data || arguments[1].data_size != sizeof(precision))
      return SEEKDB_PLUGIN_STATUS_INVALID_ARGUMENT;
    std::memcpy(&precision, arguments[1].data, sizeof(precision));
  }
  Geometry geometry;
  if (!decode(arguments[0], geometry)) return SEEKDB_PLUGIN_STATUS_INVALID_ARGUMENT;
  // Like the original geo_to_ewkt, serialize stored SRID/coordinates without an
  // SRS lookup, axis reversal or geographic-range validation.
  std::string output;
  if (!geometry_to_wkt(geometry, output, precision, true))
    return SEEKDB_PLUGIN_STATUS_INVALID_ARGUMENT;
  return emit_bytes(context, output, "core.type.text");
}
