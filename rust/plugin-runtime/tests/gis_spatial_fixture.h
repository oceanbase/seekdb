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
#pragma once

#include "seekdb/plugin/spatial_index_spi.h"
#include <algorithm>
#include <set>
#include "gis_srs_fixture.h"

namespace gis_spatial_test {
struct FilterSink {
  unsigned calls = 0;
  bool reject = false;
  seekdb_plugin_status_t status = SEEKDB_PLUGIN_STATUS_OK;
};

static seekdb_plugin_status_t SEEKDB_PLUGIN_CALL emit_filter(
    seekdb_plugin_host_handle_t *host, const seekdb_plugin_execution_result_v1_t *result)
{
  auto &sink = *reinterpret_cast<FilterSink *>(host);
  CHECK(result && result->struct_size == sizeof(*result) && !result->is_null);
  CHECK(result->type_id && std::strcmp(result->type_id, "core.type.bool") == 0);
  CHECK(result->data && result->data_size == 1 && result->data[0] <= 1);
  ++sink.calls;
  sink.reject = result->data[0] != 0;
  return sink.status;
}

static void exercise_filter(ObPluginLoader &loader)
{
  seekdb_plugin_spatial_filter_request_v1_t request{};
  request.struct_size = sizeof(request);
  request.operation = SEEKDB_PLUGIN_SPATIAL_FILTER_COVERS;
  request.row_xmin = request.row_ymin = 1;
  request.row_xmax = request.row_ymax = 2;
  request.query_xmax = request.query_ymax = 3;
  std::vector<uint8_t> bytes(sizeof(request) + 1);
  FilterSink sink;
  seekdb_plugin_execution_context_v1_t context{};
  context.struct_size = sizeof(context);
  context.host = reinterpret_cast<seekdb_plugin_host_handle_t *>(&sink);
  context.emit_result = emit_filter;
  seekdb_plugin_execution_value_v1_t argument{};
  argument.struct_size = sizeof(argument);
  argument.type_id = SEEKDB_PLUGIN_SPATIAL_FILTER_REQUEST_TYPE;
  argument.data = bytes.data() + 1;
  argument.data_size = sizeof(request);
  const auto call = [&](int expected = OB_SUCCESS, seekdb_plugin_status_t status = SEEKDB_PLUGIN_STATUS_OK) {
    sink = {}; sink.status = status;
    std::memcpy(bytes.data() + 1, &request, sizeof(request));
    CHECK(loader.execute_function(SEEKDB_PLUGIN_SPATIAL_FILTER_SERVICE, 1, 0,
                                   &context, &argument, 1) == expected);
    CHECK(sink.calls == ((expected == OB_SUCCESS || status != SEEKDB_PLUGIN_STATUS_OK) ? 1 : 0));
  };
  call(); CHECK(!sink.reject);
  request.operation = SEEKDB_PLUGIN_SPATIAL_FILTER_COVERED_BY;
  call(); CHECK(sink.reject);
  request.operation = SEEKDB_PLUGIN_SPATIAL_FILTER_INTERSECTS;
  call(); CHECK(!sink.reject);
  request.query_xmin = 2; request.query_xmax = 3;
  call(); CHECK(!sink.reject); // Boundary contact is retained.
  request.query_xmin = 2.00001;
  call(); CHECK(sink.reject);
  call(OB_TIMEOUT, SEEKDB_PLUGIN_STATUS_TIMEOUT);
  call(OB_ALLOCATE_MEMORY_FAILED, SEEKDB_PLUGIN_STATUS_NO_MEMORY);
  request.flags = 8; call(OB_INVALID_ARGUMENT); request.flags = 0;
  request.operation = 0; call(OB_INVALID_ARGUMENT); request.operation = SEEKDB_PLUGIN_SPATIAL_FILTER_INTERSECTS;
  request.reserved_word = 1; call(OB_INVALID_ARGUMENT); request.reserved_word = 0;
  request.reserved[3] = 1; call(OB_INVALID_ARGUMENT); request.reserved[3] = 0;
  request.struct_size -= 1; call(OB_INVALID_ARGUMENT); request.struct_size += 1;
  argument.data_size -= 1; call(OB_INVALID_ARGUMENT); argument.data_size += 1;
  argument.is_null = 1; call(OB_INVALID_ARGUMENT); argument.is_null = 0;
  argument.type_id = "core.type.bytes"; call(OB_INVALID_ARGUMENT);
  argument.type_id = SEEKDB_PLUGIN_SPATIAL_FILTER_REQUEST_TYPE;
  request.row_xmin = NAN; call(OB_INVALID_ARGUMENT); request.row_xmin = 1;
  request.row_ymin = 3; call(OB_INVALID_ARGUMENT); request.row_ymin = 1;
  request.flags = SEEKDB_PLUGIN_SPATIAL_FILTER_GEOGRAPHIC;
  request.row_xmin = 170; request.row_xmax = -170;
  request.row_ymin = -20; request.row_ymax = 20;
  request.query_xmin = 178; request.query_xmax = -178;
  request.query_ymin = -10; request.query_ymax = 10;
  call(); CHECK(!sink.reject);
  request.operation = SEEKDB_PLUGIN_SPATIAL_FILTER_COVERS; call(); CHECK(sink.reject);
  request.operation = SEEKDB_PLUGIN_SPATIAL_FILTER_COVERED_BY; call(); CHECK(!sink.reject);
  request.row_ymax = 91; call(OB_INVALID_ARGUMENT); request.row_ymax = 20;
  request.row_xmin = -181; call(OB_INVALID_ARGUMENT);
  request.row_xmin = request.row_xmax = request.row_ymin = request.row_ymax = 0;
  request.query_xmin = request.query_xmax = 1e-14;
  request.query_ymin = request.query_ymax = 0;
  request.flags |= SEEKDB_PLUGIN_SPATIAL_FILTER_ROW_POINT | SEEKDB_PLUGIN_SPATIAL_FILTER_QUERY_POINT;
  call(); CHECK(!sink.reject); // Original S2 point ApproxEquals tolerance.
  request.flags &= ~SEEKDB_PLUGIN_SPATIAL_FILTER_GEOGRAPHIC;
  call(); CHECK(sink.reject); // Cartesian path must not acquire that tolerance.
  std::cout << "PASS: leased MBR filter ABI, predicates/date line/point tolerance, malformed requests and callback errors" << std::endl;
}

struct Sink {
  unsigned calls = 0;
  seekdb_plugin_status_t status = SEEKDB_PLUGIN_STATUS_OK;
  std::vector<uint8_t> bytes;
  seekdb_plugin_spatial_cover_result_v1_t header{};
  bool all_views = false;
  std::vector<uint64_t> cells, ancestors, vertices, query_cells;
};

static seekdb_plugin_status_t SEEKDB_PLUGIN_CALL emit(
    seekdb_plugin_host_handle_t *host, const seekdb_plugin_execution_result_v1_t *result)
{
  auto &sink = *reinterpret_cast<Sink *>(host);
  ++sink.calls;
  CHECK(result->struct_size == sizeof(*result) && !result->is_null);
  CHECK(std::strcmp(result->type_id, sink.all_views ? SEEKDB_PLUGIN_SPATIAL_COVER_ALL_RESULT_TYPE :
                                                  SEEKDB_PLUGIN_SPATIAL_COVER_RESULT_TYPE) == 0);
  CHECK(result->data && result->data_size >= sizeof(sink.header));
  CHECK(result->data_size <= SEEKDB_PLUGIN_SPATIAL_MAX_BYTES);
  std::memcpy(&sink.header, result->data, sizeof(sink.header));
  const size_t header_size = sink.all_views ? sizeof(seekdb_plugin_spatial_cover_result_v2_t) : sizeof(sink.header);
  CHECK(sink.header.struct_size == header_size && result->data_size >= header_size);
  if (sink.all_views) {
    seekdb_plugin_spatial_cover_result_v2_t all;
    std::memcpy(&all, result->data, sizeof(all));
    CHECK(all.reserved_word == 0);
    for (auto value : all.reserved) CHECK(value == 0);
    CHECK(all.query_cell_count <= SEEKDB_PLUGIN_SPATIAL_MAX_BYTES / sizeof(uint64_t));
    sink.query_cells.resize(all.query_cell_count);
  }
  CHECK(sink.header.reserved_word == 0);
  for (auto value : sink.header.reserved) CHECK(value == 0);
  const uint64_t count = uint64_t(sink.header.cell_count) + sink.header.ancestor_count + sink.header.vertex_count + sink.query_cells.size();
  CHECK(result->data_size == header_size + count * sizeof(uint64_t));
  sink.cells.resize(sink.header.cell_count);
  sink.ancestors.resize(sink.header.ancestor_count);
  sink.vertices.resize(sink.header.vertex_count);
  size_t offset = header_size;
  for (auto *ids : {&sink.cells, &sink.ancestors, &sink.vertices, &sink.query_cells}) {
    if (!ids->empty()) std::memcpy(ids->data(), result->data + offset, ids->size() * sizeof(uint64_t));
    offset += ids->size() * sizeof(uint64_t);
  }
  sink.bytes.assign(result->data, result->data + result->data_size);
  return sink.status;
}

static void append_u32(std::vector<uint8_t> &bytes, uint32_t value)
{
  for (unsigned i = 0; i < 4; ++i) bytes.push_back(static_cast<uint8_t>(value >> (8 * i)));
}

static std::vector<uint8_t> geometry(uint32_t type, std::initializer_list<double> xy, uint32_t srid = 0)
{
  std::vector<uint8_t> bytes;
  append_u32(bytes, srid);
  bytes.push_back(1); bytes.push_back(1); // version / little endian
  append_u32(bytes, type);
  if (type == 3) append_u32(bytes, 1);
  if (type == 2 || type == 3) append_u32(bytes, xy.size() / 2);
  for (double value : xy) {
    const auto *p = reinterpret_cast<const uint8_t *>(&value);
    bytes.insert(bytes.end(), p, p + sizeof(value));
  }
  return bytes;
}

static void exercise_cells(ObPluginLoader &loader)
{
  struct Capture {
    unsigned calls = 0;
    seekdb_plugin_status_t status = SEEKDB_PLUGIN_STATUS_OK;
    std::vector<seekdb_plugin_spatial_cell_v1_t> cells;
    static seekdb_plugin_status_t SEEKDB_PLUGIN_CALL emit(seekdb_plugin_host_handle_t *host,
        const seekdb_plugin_execution_result_v1_t *result) {
      auto &s = *reinterpret_cast<Capture *>(host);
      CHECK(result && result->struct_size == sizeof(*result) && !result->is_null);
      CHECK(std::strcmp(result->type_id, SEEKDB_PLUGIN_SPATIAL_CELLS_RESULT_TYPE) == 0);
      seekdb_plugin_spatial_cells_result_v1_t header;
      CHECK(result->data_size >= sizeof(header));
      std::memcpy(&header, result->data, sizeof(header));
      CHECK(header.struct_size == sizeof(header));
      for (auto r : header.reserved) CHECK(r == 0);
      CHECK(result->data_size == sizeof(header) + header.cell_count * sizeof(seekdb_plugin_spatial_cell_v1_t));
      s.cells.resize(header.cell_count);
      std::memcpy(s.cells.data(), result->data + sizeof(header), s.cells.size() * sizeof(s.cells[0]));
      ++s.calls;
      return s.status;
    }
  } sink;
  seekdb_plugin_spatial_cells_request_v1_t request{};
  request.struct_size = sizeof(request); request.cell_count = 3;
  uint64_t ids[] = {UINT64_C(0x1000000000000001), UINT64_C(0x1000000000000000), UINT64_MAX};
  std::vector<uint8_t> bytes(1 + sizeof(request) + sizeof(ids));
  seekdb_plugin_execution_value_v1_t arg{};
  arg.struct_size = sizeof(arg); arg.type_id = SEEKDB_PLUGIN_SPATIAL_CELLS_REQUEST_TYPE;
  arg.data = bytes.data() + 1; arg.data_size = bytes.size() - 1;
  seekdb_plugin_execution_context_v1_t context{};
  context.struct_size = sizeof(context);
  context.host = reinterpret_cast<seekdb_plugin_host_handle_t *>(&sink);
  context.emit_result = Capture::emit;
  const auto call = [&](int expected = OB_SUCCESS, seekdb_plugin_status_t status = SEEKDB_PLUGIN_STATUS_OK) {
    sink = {}; sink.status = status;
    std::memcpy(bytes.data() + 1, &request, sizeof(request));
    std::memcpy(bytes.data() + 1 + sizeof(request), ids, sizeof(ids));
    CHECK(loader.execute_function(SEEKDB_PLUGIN_SPATIAL_CELLS_SERVICE, 1, 0, &context, &arg, 1) == expected);
    CHECK(sink.calls == ((expected == OB_SUCCESS || status != SEEKDB_PLUGIN_STATUS_OK) ? 1 : 0));
  };
  call();
  CHECK(sink.cells.size() == 3);
  CHECK(sink.cells[0].range_min == ids[0] && sink.cells[0].range_max == ids[0] && sink.cells[0].ancestor_count == 30);
  CHECK(sink.cells[0].ancestors[29] == ids[1]);
  CHECK(sink.cells[1].range_min == 1 && sink.cells[1].range_max == UINT64_C(0x1fffffffffffffff) && sink.cells[1].ancestor_count == 0);
  CHECK(sink.cells[2].range_min == UINT64_MAX && sink.cells[2].range_max == UINT64_MAX && sink.cells[2].ancestor_count == 0);
  for (const auto &cell : sink.cells) {
    CHECK(cell.reserved_word == 0);
    for (unsigned i = cell.ancestor_count; i < 30; ++i) CHECK(cell.ancestors[i] == 0);
  }
  call(OB_TIMEOUT, SEEKDB_PLUGIN_STATUS_TIMEOUT);
  call(OB_ALLOCATE_MEMORY_FAILED, SEEKDB_PLUGIN_STATUS_NO_MEMORY);
  ids[2] = 0; call(OB_INVALID_ARGUMENT); // Late invalid input must not emit an earlier prefix.
  ids[2] = 2; call(OB_INVALID_ARGUMENT); ids[2] = UINT64_MAX;
  request.cell_count = 0; call(OB_INVALID_ARGUMENT);
  request.cell_count = SEEKDB_PLUGIN_SPATIAL_MAX_CELL_BATCH + 1; call(OB_INVALID_ARGUMENT);
  request.cell_count = 3;
  request.reserved[0] = 1; call(OB_INVALID_ARGUMENT); request.reserved[0] = 0;
  --request.struct_size; call(OB_INVALID_ARGUMENT); ++request.struct_size;
  --arg.data_size; call(OB_INVALID_ARGUMENT); ++arg.data_size;
  arg.is_null = 1; call(OB_INVALID_ARGUMENT); arg.is_null = 0;
  arg.type_id = "core.type.bytes"; call(OB_INVALID_ARGUMENT);
  std::cout << "PASS: leased S2 cell metadata batch, sentinel, invalid IDs and callback failure" << std::endl;
}

static void exercise(ObPluginLoader &loader)
{
  gis_srs_test::exercise(loader);
  exercise_filter(loader);
  exercise_cells(loader);
  seekdb_plugin_spatial_cover_request_v1_t request{};
  request.struct_size = sizeof(request);
  request.xmin = request.ymin = -100;
  request.xmax = request.ymax = 100;
  auto point = geometry(1, {0, 0});
  auto line = geometry(2, {-50, -20, 30, 40});
  auto polygon = geometry(3, {-20, -10, 20, -10, 20, 10, -20, 10, -20, -10});
  seekdb_plugin_execution_value_v1_t arguments[2]{};
  for (auto &argument : arguments) argument.struct_size = sizeof(argument);
  arguments[0].type_id = "org.seekdb.gis.geometry";
  arguments[1].type_id = SEEKDB_PLUGIN_SPATIAL_COVER_REQUEST_TYPE;
  std::vector<uint8_t> request_bytes(sizeof(request) + 1);
  Sink sink;
  seekdb_plugin_execution_context_v1_t context{};
  context.struct_size = sizeof(context);
  context.host = reinterpret_cast<seekdb_plugin_host_handle_t *>(&sink);
  context.emit_result = emit;
  const auto call = [&](const std::vector<uint8_t> &bytes, int expected = OB_SUCCESS,
                        seekdb_plugin_status_t emit_status = SEEKDB_PLUGIN_STATUS_OK) {
    sink = Sink{};
    sink.status = emit_status;
    sink.all_views = (request.flags & SEEKDB_PLUGIN_SPATIAL_ALL_VIEWS) != 0;
    // Deliberately unaligned request: the C ABI must memcpy, not cast it.
    std::memcpy(request_bytes.data() + 1, &request, sizeof(request));
    arguments[0].data = bytes.data(); arguments[0].data_size = bytes.size();
    arguments[1].data = request_bytes.data() + 1; arguments[1].data_size = sizeof(request);
    CHECK(loader.execute_function(SEEKDB_PLUGIN_SPATIAL_COVER_SERVICE, 1, sink.all_views ? 1 : 0,
                                  &context, arguments, 2) == expected);
    if (expected == OB_SUCCESS) CHECK(sink.calls == 1);
    else if (emit_status == SEEKDB_PLUGIN_STATUS_OK) CHECK(sink.calls == 0);
  };
  call(point);
  CHECK(sink.cells == std::vector<uint64_t>{UINT64_C(0x1000000000000001)});
  CHECK(sink.ancestors.empty() && sink.vertices.empty());
  CHECK(sink.header.flags == SEEKDB_PLUGIN_SPATIAL_RESULT_POINT);
  const auto write = sink.bytes;
  request.flags = SEEKDB_PLUGIN_SPATIAL_QUERY | SEEKDB_PLUGIN_SPATIAL_ANCESTORS | SEEKDB_PLUGIN_SPATIAL_VERTICES;
  call(point);
  CHECK(sink.ancestors.size() == 30 && sink.vertices == sink.cells);
  std::set<uint64_t> unique(sink.cells.begin(), sink.cells.end());
  for (auto id : sink.ancestors) CHECK(unique.insert(id).second);
  CHECK(sink.ancestors.back() == UINT64_C(0x1000000000000000));
  for (const auto &shape : {point, line, polygon}) {
    call(shape);
    const auto previous = sink;
    request.flags |= SEEKDB_PLUGIN_SPATIAL_ALL_VIEWS;
    call(shape);
    CHECK(sink.cells == previous.cells && sink.ancestors == previous.ancestors && sink.vertices == previous.vertices);
    std::set<uint64_t> combined(sink.cells.begin(), sink.cells.end());
    combined.insert(sink.ancestors.begin(), sink.ancestors.end());
    CHECK(sink.query_cells.size() == combined.size());
    CHECK(std::set<uint64_t>(sink.query_cells.begin(), sink.query_cells.end()) == combined);
    if (shape == point) {
      CHECK(sink.query_cells.front() == sink.cells.front());
      CHECK(std::equal(sink.ancestors.begin(), sink.ancestors.end(), sink.query_cells.begin() + 1));
    }
    request.flags &= ~SEEKDB_PLUGIN_SPATIAL_ALL_VIEWS;
  }
  call(line);
  CHECK(!sink.cells.empty() && sink.vertices.size() == 2);
  CHECK(sink.header.xmin == -50 && sink.header.xmax == 30 && sink.header.ymin == -20 && sink.header.ymax == 40);
  call(polygon);
  CHECK(!sink.cells.empty() && sink.vertices.size() == 4);
  const auto polygon_cover = sink.bytes;
  call(polygon); CHECK(sink.bytes == polygon_cover);
  request.flags |= SEEKDB_PLUGIN_SPATIAL_QUERY_WINDOW;
  call(polygon);
  CHECK(!sink.cells.empty() && sink.bytes != polygon_cover);
  request.flags &= ~SEEKDB_PLUGIN_SPATIAL_QUERY_WINDOW;
  call(polygon); CHECK(sink.bytes == polygon_cover);

  // Not a centroid/Morton approximation: different regions receive different
  // covering sets; collections retain both components' coverage.
  call(geometry(1, {50, 50}));
  const auto second_cells = sink.cells;
  std::vector<uint8_t> collection = geometry(7, {});
  append_u32(collection, 2);
  const auto second = geometry(1, {50, 50});
  collection.insert(collection.end(), point.begin() + 5, point.end());
  collection.insert(collection.end(), second.begin() + 5, second.end());
  call(collection);
  CHECK(sink.cells.size() == 2 && sink.vertices.size() == 2);
  CHECK(std::find(sink.cells.begin(), sink.cells.end(), second_cells.front()) != sink.cells.end());

  call(geometry(1, {200, 200}));
  CHECK(sink.cells == std::vector<uint64_t>{SEEKDB_PLUGIN_SPATIAL_OUTSIDE_CELL});
  CHECK(sink.ancestors.empty() && sink.vertices == sink.cells);
  CHECK(sink.header.flags & SEEKDB_PLUGIN_SPATIAL_RESULT_OUTSIDE_BOUNDS);
  call(geometry(2, {-200, 0, 200, 0}));
  CHECK(sink.cells.size() > 1 && sink.cells.back() == SEEKDB_PLUGIN_SPATIAL_OUTSIDE_CELL);
  CHECK(sink.header.xmin == -200 && sink.header.xmax == 200); // Original MBR, not clipped MBR.
  call(geometry(3, {-200, -200, 200, -200, 200, 200, -200, 200, -200, -200}));
  CHECK(sink.cells.size() > 1 && sink.cells.back() == SEEKDB_PLUGIN_SPATIAL_OUTSIDE_CELL);
  std::vector<uint8_t> empty = geometry(7, {});
  append_u32(empty, 0);
  call(empty);
  CHECK(sink.cells.empty() && sink.vertices.empty() && sink.ancestors.empty());
  CHECK(sink.header.flags == SEEKDB_PLUGIN_SPATIAL_RESULT_EMPTY);

  request.flags = 0;
  call(point); CHECK(sink.bytes == write); // No state retained by query calls.
  call(point, OB_TIMEOUT, SEEKDB_PLUGIN_STATUS_TIMEOUT); CHECK(sink.calls == 1);
  // Hosts report failures as statuses. Throwing a C++ exception across an
  // independently linked plugin's C callback violates the no-unwind ABI.
  call(point, OB_ALLOCATE_MEMORY_FAILED, SEEKDB_PLUGIN_STATUS_NO_MEMORY); CHECK(sink.calls == 1);
  request.struct_size -= 1; call(point, OB_INVALID_ARGUMENT); request.struct_size += 1;
  request.reserved[0] = 1; call(point, OB_INVALID_ARGUMENT); request.reserved[0] = 0;
  request.flags = 64; call(point, OB_INVALID_ARGUMENT); request.flags = 0;
  request.flags = SEEKDB_PLUGIN_SPATIAL_QUERY_WINDOW; call(point, OB_INVALID_ARGUMENT); request.flags = 0;
  request.srid = 1; call(point, OB_INVALID_ARGUMENT); request.srid = 0;
  request.xmax = request.xmin; call(point, OB_INVALID_ARGUMENT); request.xmax = 100;
  request.buffer_radians = std::numeric_limits<double>::quiet_NaN();
  call(point, OB_INVALID_ARGUMENT); request.buffer_radians = 0;
  request.flags = SEEKDB_PLUGIN_SPATIAL_VERTICES; call(point, OB_INVALID_ARGUMENT);
  request.flags = 0;
  call(geometry(2, {0, 0, 0, 0}), OB_INVALID_ARGUMENT); // S2 invalid input must not abort.
  call(geometry(3, {0, 0, 2, 2, 0, 2, 2, 0, 0, 0}), OB_INVALID_ARGUMENT);
  call(std::vector<uint8_t>(point.begin(), point.end() - 1), OB_INVALID_ARGUMENT);
  auto extra = point; extra.push_back(0); call(extra, OB_INVALID_ARGUMENT);

  request.xmin = request.xmax = request.ymin = request.ymax = 0;
  request.flags = SEEKDB_PLUGIN_SPATIAL_GEOGRAPHIC | SEEKDB_PLUGIN_SPATIAL_QUERY | SEEKDB_PLUGIN_SPATIAL_ANCESTORS;
  request.srid = 4326;
  const auto dateline = geometry(2, {179, 10, -179, 10}, 4326);
  call(dateline);
  CHECK(!sink.cells.empty() && sink.header.xmin > sink.header.xmax);
  CHECK(sink.header.ymax > 10); // Great-circle interior extremum, not vertex-only bounds.
  const auto unbuffered = sink.bytes;
  request.flags |= SEEKDB_PLUGIN_SPATIAL_BUFFER;
  request.buffer_radians = 0.001;
  request.flags |= SEEKDB_PLUGIN_SPATIAL_QUERY_WINDOW;
  call(dateline, OB_INVALID_ARGUMENT);
  request.flags &= ~SEEKDB_PLUGIN_SPATIAL_QUERY_WINDOW;
  call(dateline);
  CHECK(sink.header.ymin < 10);
  const auto buffered = sink.bytes;
  call(dateline); CHECK(sink.bytes == buffered); // Repeated call must not expand again.
  request.flags &= ~SEEKDB_PLUGIN_SPATIAL_BUFFER;
  request.buffer_radians = 0;
  call(dateline); CHECK(sink.bytes == unbuffered);
  call(geometry(1, {0, 91}, 4326), OB_INVALID_ARGUMENT);
  call(geometry(1, {181, 0}, 4326), OB_INVALID_ARGUMENT);
  call(geometry(3, {0, 0, 2, 0, 2, 2, 0, 2, 0, 0}, 4326));
  CHECK(!sink.cells.empty());
  call(geometry(1001, {2, 49, 7}, 4326));
  const auto point_z = sink.bytes;
  call(geometry(1, {2, 49}, 4326)); CHECK(sink.bytes == point_z);
  std::cout << "PASS: leased GIS S2 backend, covering/ancestors/vertices/MBR, bounds retry, geographic buffer, ABI rejection" << std::endl;
}
} // namespace gis_spatial_test
