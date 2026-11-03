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
#include "geometry_engine.h"
#include "seekdb/plugin/spatial_index_spi.h"
#include "seekdb/geo/s2_mbr.hpp"
#include "seekdb/geo/s2_covering.hpp"
#include <algorithm>
#include <cstring>
#include <iterator>
#include <new>

extern "C" seekdb_plugin_status_t seekdb_gis_spatial_filter(
    seekdb_plugin_instance_handle_t *instance,
    const seekdb_plugin_execution_context_v1_t *context,
    const seekdb_plugin_execution_value_v1_t *arguments, uint32_t argument_count)
try {
  seekdb_plugin_spatial_filter_request_v1_t request{};
  if (instance == nullptr || context == nullptr || context->struct_size < sizeof(*context) ||
      context->emit_result == nullptr || arguments == nullptr || argument_count != 1 ||
      arguments[0].struct_size != sizeof(arguments[0]) || arguments[0].is_null ||
      arguments[0].data == nullptr || arguments[0].data_size != sizeof(request) ||
      arguments[0].type_id == nullptr ||
      std::strcmp(arguments[0].type_id, SEEKDB_PLUGIN_SPATIAL_FILTER_REQUEST_TYPE) != 0) {
    return SEEKDB_PLUGIN_STATUS_INVALID_ARGUMENT;
  }
  std::memcpy(&request, arguments[0].data, sizeof(request));
  if (request.struct_size != sizeof(request) || (request.flags & ~7u) != 0 || request.reserved_word != 0 ||
      std::any_of(std::begin(request.reserved), std::end(request.reserved), [](uint64_t v) { return v != 0; })) {
    return SEEKDB_PLUGIN_STATUS_INVALID_ARGUMENT;
  }
  namespace mbr = seekdb::geo::index_mbr;
  mbr::Relation relation;
  switch (request.operation) {
    case SEEKDB_PLUGIN_SPATIAL_FILTER_COVERS: relation = mbr::Relation::covers; break;
    case SEEKDB_PLUGIN_SPATIAL_FILTER_INTERSECTS: relation = mbr::Relation::intersects; break;
    case SEEKDB_PLUGIN_SPATIAL_FILTER_COVERED_BY: relation = mbr::Relation::covered_by; break;
    default: return SEEKDB_PLUGIN_STATUS_INVALID_ARGUMENT;
  }
  bool reject = false;
  if (!mbr::filter({request.row_xmin, request.row_xmax, request.row_ymin, request.row_ymax},
                   {request.query_xmin, request.query_xmax, request.query_ymin, request.query_ymax},
                   request.flags & SEEKDB_PLUGIN_SPATIAL_FILTER_GEOGRAPHIC,
                   request.flags & SEEKDB_PLUGIN_SPATIAL_FILTER_ROW_POINT,
                   request.flags & SEEKDB_PLUGIN_SPATIAL_FILTER_QUERY_POINT, relation, reject)) {
    return SEEKDB_PLUGIN_STATUS_INVALID_ARGUMENT;
  }
  const uint8_t value = reject ? 1 : 0;
  seekdb_plugin_execution_result_v1_t result{};
  result.struct_size = sizeof(result);
  result.type_id = "core.type.bool";
  result.data = &value;
  result.data_size = sizeof(value);
  return context->emit_result(context->host, &result);
}
catch (const std::bad_alloc &) { return SEEKDB_PLUGIN_STATUS_NO_MEMORY; }
catch (...) { return SEEKDB_PLUGIN_STATUS_INTERNAL; }

extern "C" seekdb_plugin_status_t seekdb_gis_spatial_cells(
    seekdb_plugin_instance_handle_t *instance,
    const seekdb_plugin_execution_context_v1_t *context,
    const seekdb_plugin_execution_value_v1_t *arguments, uint32_t argument_count)
try {
  seekdb_plugin_spatial_cells_request_v1_t request{};
  if (instance == nullptr || context == nullptr || context->struct_size < sizeof(*context) ||
      context->emit_result == nullptr || arguments == nullptr || argument_count != 1 ||
      arguments[0].struct_size != sizeof(arguments[0]) || arguments[0].is_null ||
      arguments[0].data == nullptr || arguments[0].data_size < sizeof(request) ||
      arguments[0].type_id == nullptr ||
      std::strcmp(arguments[0].type_id, SEEKDB_PLUGIN_SPATIAL_CELLS_REQUEST_TYPE) != 0) {
    return SEEKDB_PLUGIN_STATUS_INVALID_ARGUMENT;
  }
  std::memcpy(&request, arguments[0].data, sizeof(request));
  if (request.struct_size != sizeof(request) || request.cell_count == 0 ||
      request.cell_count > SEEKDB_PLUGIN_SPATIAL_MAX_CELL_BATCH ||
      arguments[0].data_size != sizeof(request) + uint64_t(request.cell_count) * sizeof(uint64_t) ||
      std::any_of(std::begin(request.reserved), std::end(request.reserved), [](uint64_t v) { return v != 0; })) {
    return SEEKDB_PLUGIN_STATUS_INVALID_ARGUMENT;
  }
  seekdb_plugin_spatial_cells_result_v1_t header{};
  header.struct_size = sizeof(header);
  header.cell_count = request.cell_count;
  std::vector<uint8_t> bytes(sizeof(header) + header.cell_count * sizeof(seekdb_plugin_spatial_cell_v1_t));
  std::memcpy(bytes.data(), &header, sizeof(header));
  for (uint32_t i = 0; i < request.cell_count; ++i) {
    uint64_t cell = 0;
    std::memcpy(&cell, arguments[0].data + sizeof(request) + i * sizeof(cell), sizeof(cell));
    seekdb::geo::s2_index::CellInfo info;
    if (!seekdb::geo::s2_index::cell_info(cell, info)) return SEEKDB_PLUGIN_STATUS_INVALID_ARGUMENT;
    seekdb_plugin_spatial_cell_v1_t item{};
    item.cell_id = info.cell;
    item.range_min = info.range_min; item.range_max = info.range_max;
    item.ancestor_count = info.ancestor_count;
    std::copy(info.ancestors.begin(), info.ancestors.end(), item.ancestors);
    std::memcpy(bytes.data() + sizeof(header) + i * sizeof(item), &item, sizeof(item));
  }
  seekdb_plugin_execution_result_v1_t result{};
  result.struct_size = sizeof(result);
  result.type_id = SEEKDB_PLUGIN_SPATIAL_CELLS_RESULT_TYPE;
  result.data = bytes.data(); result.data_size = bytes.size();
  return context->emit_result(context->host, &result);
}
catch (const std::bad_alloc &) { return SEEKDB_PLUGIN_STATUS_NO_MEMORY; }
catch (...) { return SEEKDB_PLUGIN_STATUS_INTERNAL; }
