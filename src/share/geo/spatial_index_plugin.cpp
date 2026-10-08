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
#include "share/geo/ob_s2adapter.h"
#include "share/geo/ob_srs_info.h"
#include "share/geo/ob_geo_utils.h"
#include "share/rc/ob_module_provider.h"
#include "seekdb/geo/spatial_mbr.hpp"
#include "seekdb/plugin/spatial_index_spi.h"
#include <algorithm>
#include <cstring>
#include <iterator>
#include <vector>
#include <new>

#if !SEEKDB_ENABLE_CORE_GIS
namespace oceanbase::common {
struct SpatialIndexState {
  seekdb_plugin_spatial_cover_result_v2_t header{};
  std::vector<uint64_t> cells, ancestors, vertices, query_cells;
};
namespace {
template <typename T, size_t N> bool zeroes(const T (&values)[N])
{
  return std::all_of(std::begin(values), std::end(values), [](T v) { return v == 0; });
}

bool valid_result(const seekdb_plugin_execution_result_v1_t *r, const char *type)
{
  return r != nullptr && r->struct_size == sizeof(*r) && r->is_null == 0 &&
      r->type_id != nullptr && std::strcmp(r->type_id, type) == 0 && r->data != nullptr &&
      r->data_size <= SEEKDB_PLUGIN_SPATIAL_MAX_BYTES && zeroes(r->reserved_bytes) && zeroes(r->reserved);
}

struct CoverSink {
  SpatialIndexState state;
  bool geographic = false;
  bool seen = false;
  bool invalid = false;
  int failure = OB_SUCCESS;
};

seekdb_plugin_status_t SEEKDB_PLUGIN_CALL emit_cover(
    seekdb_plugin_host_handle_t *host, const seekdb_plugin_execution_result_v1_t *result)
{
  if (host == nullptr) return SEEKDB_PLUGIN_STATUS_INVALID_ARGUMENT;
  auto &sink = *reinterpret_cast<CoverSink *>(host);
  auto fail = [&]() { sink.invalid = true; return SEEKDB_PLUGIN_STATUS_INVALID_ARGUMENT; };
  if (sink.seen || sink.invalid || !valid_result(result, SEEKDB_PLUGIN_SPATIAL_COVER_ALL_RESULT_TYPE) ||
      result->data_size < sizeof(sink.state.header)) return fail();
  auto &h = sink.state.header;
  std::memcpy(&h, result->data, sizeof(h));
  const auto &v = h.v1;
  const uint64_t count = uint64_t(v.cell_count) + v.ancestor_count + v.vertex_count + h.query_cell_count;
  const bool empty = (v.flags & SEEKDB_PLUGIN_SPATIAL_RESULT_EMPTY) != 0;
  if (v.struct_size != sizeof(h) || (v.flags & ~15u) != 0 || v.reserved_word != 0 ||
      h.reserved_word != 0 || !zeroes(v.reserved) || !zeroes(h.reserved) ||
      bool(v.flags & SEEKDB_PLUGIN_SPATIAL_RESULT_GEOGRAPHIC) != sink.geographic ||
      result->data_size != sizeof(h) + count * sizeof(uint64_t) ||
      uint64_t(h.query_cell_count) != uint64_t(v.cell_count) + v.ancestor_count) return fail();
  if (empty) {
    if (count != 0 || (v.flags & (SEEKDB_PLUGIN_SPATIAL_RESULT_POINT |
        SEEKDB_PLUGIN_SPATIAL_RESULT_OUTSIDE_BOUNDS)) != 0 ||
        v.xmin != 0 || v.xmax != 0 || v.ymin != 0 || v.ymax != 0) return fail();
  } else if (v.cell_count == 0 || !std::isfinite(v.xmin) || !std::isfinite(v.xmax) ||
      !std::isfinite(v.ymin) || !std::isfinite(v.ymax) || v.ymin > v.ymax ||
      (!sink.geographic && v.xmin > v.xmax) ||
      (sink.geographic && (v.xmin < -180 || v.xmin > 180 || v.xmax < -180 || v.xmax > 180 ||
                          v.ymin < -90 || v.ymax > 90))) return fail();
  try {
    const uint8_t *ptr = result->data + sizeof(h);
    auto copy = [&](std::vector<uint64_t> &out, uint32_t n, bool parents) {
      out.resize(n);
      if (n != 0) std::memcpy(out.data(), ptr, size_t(n) * sizeof(uint64_t));
      ptr += size_t(n) * sizeof(uint64_t);
      return std::all_of(out.begin(), out.end(), [&](uint64_t id) {
        return id != 0 && (id != UINT64_MAX || (!parents &&
            (v.flags & SEEKDB_PLUGIN_SPATIAL_RESULT_OUTSIDE_BOUNDS) != 0));
      });
    };
    if (!copy(sink.state.cells, v.cell_count, false) ||
        !copy(sink.state.ancestors, v.ancestor_count, true) ||
        !copy(sink.state.vertices, v.vertex_count, false) ||
        !copy(sink.state.query_cells, h.query_cell_count, false)) return fail();
    sink.seen = true;
    return SEEKDB_PLUGIN_STATUS_OK;
  } catch (const std::bad_alloc &) {
    sink.failure = OB_ALLOCATE_MEMORY_FAILED;
    sink.invalid = true;
    return SEEKDB_PLUGIN_STATUS_NO_MEMORY;
  } catch (...) {
    sink.failure = OB_ERR_UNEXPECTED;
    sink.invalid = true;
    return SEEKDB_PLUGIN_STATUS_INTERNAL;
  }
}

struct CellSink {
  uint64_t requested;
  seekdb_plugin_spatial_cell_v1_t cell{};
  bool seen = false;
  bool invalid = false;
};

seekdb_plugin_status_t SEEKDB_PLUGIN_CALL emit_cell(
    seekdb_plugin_host_handle_t *host, const seekdb_plugin_execution_result_v1_t *result)
{
  if (host == nullptr) return SEEKDB_PLUGIN_STATUS_INVALID_ARGUMENT;
  auto &sink = *reinterpret_cast<CellSink *>(host);
  auto fail = [&]() { sink.invalid = true; return SEEKDB_PLUGIN_STATUS_INVALID_ARGUMENT; };
  seekdb_plugin_spatial_cells_result_v1_t h{};
  if (sink.seen || sink.invalid || !valid_result(result, SEEKDB_PLUGIN_SPATIAL_CELLS_RESULT_TYPE) ||
      result->data_size != sizeof(h) + sizeof(sink.cell)) return fail();
  std::memcpy(&h, result->data, sizeof(h));
  std::memcpy(&sink.cell, result->data + sizeof(h), sizeof(sink.cell));
  const auto &c = sink.cell;
  if (h.struct_size != sizeof(h) || h.cell_count != 1 || !zeroes(h.reserved) ||
      c.cell_id != sink.requested || c.cell_id == 0 || c.range_min == 0 ||
      c.range_min > c.cell_id || c.range_max < c.cell_id || c.ancestor_count > 30 ||
      c.reserved_word != 0 || (c.cell_id == UINT64_MAX &&
      (c.range_min != UINT64_MAX || c.range_max != UINT64_MAX || c.ancestor_count != 0))) return fail();
  for (uint32_t i = 0; i < 30; ++i) {
    if (i >= c.ancestor_count) { if (c.ancestors[i] != 0) return fail(); }
    else if (c.ancestors[i] == 0 || c.ancestors[i] == UINT64_MAX || c.ancestors[i] == c.cell_id ||
        std::find(c.ancestors, c.ancestors + i, c.ancestors[i]) != c.ancestors + i) return fail();
  }
  sink.seen = true;
  return SEEKDB_PLUGIN_STATUS_OK;
}

int cell_metadata(uint64_t id, seekdb_plugin_spatial_cell_v1_t &out)
{
  if (id == 0) return OB_INVALID_ARGUMENT;
  if (share::g_mp == nullptr) return OB_NOT_SUPPORTED;
  seekdb_plugin_spatial_cells_request_v1_t request{};
  request.struct_size = sizeof(request);
  request.cell_count = 1;
  uint8_t bytes[sizeof(request) + sizeof(id)];
  std::memcpy(bytes, &request, sizeof(request));
  std::memcpy(bytes + sizeof(request), &id, sizeof(id));
  seekdb_plugin_execution_value_v1_t argument{};
  argument.struct_size = sizeof(argument);
  argument.type_id = SEEKDB_PLUGIN_SPATIAL_CELLS_REQUEST_TYPE;
  argument.data = bytes;
  argument.data_size = sizeof(bytes);
  CellSink sink{id};
  seekdb_plugin_execution_context_v1_t context{};
  context.struct_size = sizeof(context);
  context.host = reinterpret_cast<seekdb_plugin_host_handle_t *>(&sink);
  context.emit_result = emit_cell;
  const int ret = share::g_mp->execute_plugin_function(
      SEEKDB_PLUGIN_SPATIAL_CELLS_SERVICE, 1, 0, &context, &argument, 1);
  if (ret != OB_SUCCESS) return ret;
  if (!sink.seen || sink.invalid) return OB_ERR_UNEXPECTED;
  out = sink.cell;
  return OB_SUCCESS;
}

int append_cells(ObS2Cellids &out, const uint64_t *data, size_t count)
{
  const int64_t original = out.size();
  if (count > size_t(INT32_MAX - original)) return OB_SIZE_OVERFLOW;
  int ret = count <= size_t(out.remain()) ? OB_SUCCESS : out.reserve(original + count);
  for (size_t i = 0; ret == OB_SUCCESS && i < count; ++i) ret = out.push_back(data[i]);
  if (ret != OB_SUCCESS && out.size() > original) out.remove(original + out.begin(), out.end());
  return ret;
}

struct SpatialFilterSink {
  bool seen = false;
  bool invalid = false;
  bool reject = true;
};

seekdb_plugin_status_t SEEKDB_PLUGIN_CALL emit_spatial_filter(
    seekdb_plugin_host_handle_t *host, const seekdb_plugin_execution_result_v1_t *result)
{
  if (host == nullptr) return SEEKDB_PLUGIN_STATUS_INVALID_ARGUMENT;
  auto &sink = *reinterpret_cast<SpatialFilterSink *>(host);
  if (sink.seen || sink.invalid || result == nullptr || result->struct_size != sizeof(*result) ||
      result->is_null != 0 || result->type_id == nullptr ||
      std::strcmp(result->type_id, "core.type.bool") != 0 || result->data == nullptr ||
      result->data_size != 1 || result->data[0] > 1 ||
      std::any_of(std::begin(result->reserved_bytes), std::end(result->reserved_bytes),
                  [](uint8_t v) { return v != 0; }) ||
      std::any_of(std::begin(result->reserved), std::end(result->reserved),
                  [](uint64_t v) { return v != 0; })) {
    sink.invalid = true;
    return SEEKDB_PLUGIN_STATUS_INVALID_ARGUMENT;
  }
  sink.seen = true;
  sink.reject = result->data[0] == 1;
  return SEEKDB_PLUGIN_STATUS_OK;
}
} // namespace

ObS2Adapter::ObS2Adapter(ObIAllocator *allocator, bool geographic, bool query)
    : allocator_(allocator), is_geog_(geographic), query_window_(query), need_buffer_(false), distance_(0) {}

ObS2Adapter::ObS2Adapter(ObIAllocator *allocator, bool geographic, double distance)
    : allocator_(allocator), is_geog_(geographic), query_window_(false), need_buffer_(true), distance_(distance) {}

ObS2Adapter::~ObS2Adapter() = default;

int64_t ObS2Adapter::init(const ObString &swkb, const ObSrsBoundsItem *bound)
{
  if (state_) return OB_INIT_TWICE;
  if (allocator_ == nullptr || swkb.ptr() == nullptr || swkb.length() < 9 ||
      uint64_t(swkb.length()) > SEEKDB_PLUGIN_SPATIAL_MAX_BYTES || (!is_geog_ && bound == nullptr))
    return OB_INVALID_ARGUMENT;
  if (share::g_mp == nullptr) return OB_NOT_SUPPORTED;
  seekdb_plugin_spatial_cover_request_v1_t request{};
  request.struct_size = sizeof(request);
  request.flags = SEEKDB_PLUGIN_SPATIAL_QUERY | SEEKDB_PLUGIN_SPATIAL_ANCESTORS |
      SEEKDB_PLUGIN_SPATIAL_VERTICES | SEEKDB_PLUGIN_SPATIAL_ALL_VIEWS |
      (is_geog_ ? SEEKDB_PLUGIN_SPATIAL_GEOGRAPHIC : 0) |
      (query_window_ ? SEEKDB_PLUGIN_SPATIAL_QUERY_WINDOW : 0) |
      (need_buffer_ ? SEEKDB_PLUGIN_SPATIAL_BUFFER : 0);
  const auto *bytes = reinterpret_cast<const uint8_t *>(swkb.ptr());
  for (unsigned i = 0; i < 4; ++i) request.srid |= uint32_t(bytes[i]) << (8 * i);
  if (!is_geog_) {
    request.xmin = bound->minX_; request.xmax = bound->maxX_;
    request.ymin = bound->minY_; request.ymax = bound->maxY_;
  }
  request.buffer_radians = distance_;
  seekdb_plugin_execution_value_v1_t arguments[2]{};
  for (auto &argument : arguments) argument.struct_size = sizeof(argument);
  arguments[0].type_id = "org.seekdb.gis.geometry";
  arguments[0].data = bytes;
  arguments[0].data_size = swkb.length();
  arguments[1].type_id = SEEKDB_PLUGIN_SPATIAL_COVER_REQUEST_TYPE;
  arguments[1].data = reinterpret_cast<const uint8_t *>(&request);
  arguments[1].data_size = sizeof(request);
  CoverSink sink;
  sink.geographic = is_geog_;
  seekdb_plugin_execution_context_v1_t context{};
  context.struct_size = sizeof(context);
  context.host = reinterpret_cast<seekdb_plugin_host_handle_t *>(&sink);
  context.emit_result = emit_cover;
  const int ret = share::g_mp->execute_plugin_function(SEEKDB_PLUGIN_SPATIAL_COVER_SERVICE,
      1, SEEKDB_PLUGIN_SPATIAL_ALL_VIEWS_MINOR, &context, arguments, 2);
  if (sink.failure != OB_SUCCESS) return sink.failure;
  if (ret != OB_SUCCESS) return ret;
  if (!sink.seen || sink.invalid) return OB_ERR_UNEXPECTED;
  state_.reset(new (std::nothrow) SpatialIndexState(std::move(sink.state)));
  return state_ ? OB_SUCCESS : OB_ALLOCATE_MEMORY_FAILED;
}

int64_t ObS2Adapter::get_cellids(ObS2Cellids &out, bool query)
{
  if (!state_) return OB_NOT_INIT;
  const auto &cells = query ? state_->query_cells : state_->cells;
  return append_cells(out, cells.data(), cells.size());
}

int64_t ObS2Adapter::get_cellids_and_unrepeated_ancestors(ObS2Cellids &out, ObS2Cellids &parents)
{
  if (!state_) return OB_NOT_INIT;
  if (&out == &parents) return OB_INVALID_ARGUMENT;
  const int64_t original = out.size();
  int ret = append_cells(out, state_->cells.data(), state_->cells.size());
  if (ret == OB_SUCCESS) ret = append_cells(parents, state_->ancestors.data(), state_->ancestors.size());
  if (ret != OB_SUCCESS && out.size() > original) out.remove(out.begin() + original, out.end());
  return ret;
}

int64_t ObS2Adapter::get_inner_cover_cellids(ObS2Cellids &out)
{
  if (!state_) return OB_NOT_INIT;
  return append_cells(out, state_->vertices.data(), state_->vertices.size());
}

int64_t ObS2Adapter::get_ancestors(uint64_t id, ObS2Cellids &out)
{
  seekdb_plugin_spatial_cell_v1_t cell{};
  const int ret = cell_metadata(id, cell);
  return ret == OB_SUCCESS ? append_cells(out, cell.ancestors, cell.ancestor_count) : ret;
}

int64_t ObS2Adapter::get_child_of_cellid(uint64_t id, uint64_t &start, uint64_t &end)
{
  seekdb_plugin_spatial_cell_v1_t cell{};
  const int ret = cell_metadata(id, cell);
  if (ret == OB_SUCCESS) { start = cell.range_min; end = cell.range_max; }
  return ret;
}

int64_t ObS2Adapter::get_mbr(ObSpatialMBR &mbr)
{
  if (!state_) return OB_NOT_INIT;
  const auto &v = state_->header.v1;
  const bool empty = (v.flags & SEEKDB_PLUGIN_SPATIAL_RESULT_EMPTY) != 0;
  mbr.x_min_ = empty ? NAN : v.xmin; mbr.x_max_ = empty ? NAN : v.xmax;
  mbr.y_min_ = empty ? NAN : v.ymin; mbr.y_max_ = empty ? NAN : v.ymax;
  mbr.is_geog_ = is_geog_;
  mbr.is_point_ = (v.flags & SEEKDB_PLUGIN_SPATIAL_RESULT_POINT) != 0;
  return OB_SUCCESS;
}

int ObGeoTypeUtil::get_cellid_mbr_from_geom(const ObString &wkb,
    const ObSrsItem *srs, const ObSrsBoundsItem *bounds, ObS2Cellids &cells, ObString &mbr)
{
  // Same S2 covering and storage layout as the legacy backfill helper. The
  // adapter owns a validated plugin result; the core only assembles outputs.
  ObArenaAllocator allocator(lib::ObLabel("GisIndexRebuild"));
  ObS2Adapter adapter(&allocator, srs != nullptr && srs->is_geographical_srs());
  ObSpatialMBR box;
  char encoded[OB_DEFAULT_MBR_SIZE];
  int64_t length = 0;
  const int64_t original = cells.size();
  int ret = OB_SUCCESS;
  if (OB_FAIL(adapter.init(wkb, bounds))) {
  } else if (OB_FAIL(adapter.get_mbr(box))) {
  } else if (!box.is_empty() && OB_ISNULL(mbr.ptr())) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
  } else if (!box.is_empty() && OB_FAIL(box.to_char(encoded, length))) {
  } else if (OB_FAIL(adapter.get_cellids(cells, false))) {
  } else if (box.is_empty() != (cells.size() == original)) {
    ret = OB_ERR_GIS_INVALID_DATA;
  }
  if (OB_SUCC(ret)) {
    // The API requires a caller-owned OB_DEFAULT_MBR_SIZE buffer, just like
    // the original helper. Do not alter its bytes/length on any failure.
    if (length != 0) MEMCPY(mbr.ptr(), encoded, length);
    mbr.assign_ptr(mbr.ptr(), length);
  } else if (cells.size() > original) {
    cells.remove(cells.begin() + original, cells.end());
  }
  return ret;
}

int ObGeoTypeUtil::get_buffered_geo(ObArenaAllocator *allocator, const ObString &wkb,
    double distance, const ObSrsItem *srs, ObString &result)
{
  uint32_t srid = 0;
  int ret = OB_SUCCESS;
  if (allocator == nullptr || !std::isfinite(distance)) return OB_INVALID_ARGUMENT;
  if (OB_FAIL(get_srid_from_wkb(wkb, srid))) return ret;
  if ((srid != 0 && (srs == nullptr || srs->get_srid() != srid)) ||
      (srs != nullptr && srs->is_geographical_srs())) return OB_INVALID_ARGUMENT;
  if (share::g_mp == nullptr) return OB_NOT_SUPPORTED;
  struct Sink {
    ObArenaAllocator &allocator;
    uint32_t srid;
    ObString bytes;
    bool seen = false, invalid = false;
    int failure = OB_SUCCESS;
    static seekdb_plugin_status_t SEEKDB_PLUGIN_CALL emit(seekdb_plugin_host_handle_t *host,
        const seekdb_plugin_execution_result_v1_t *value) {
      if (host == nullptr) return SEEKDB_PLUGIN_STATUS_INVALID_ARGUMENT;
      auto &sink = *reinterpret_cast<Sink *>(host);
      uint32_t actual_srid = 0;
      if (sink.seen || sink.invalid || !valid_result(value, "org.seekdb.gis.geometry") ||
          ObGeoTypeUtil::get_srid_from_wkb(ObString(value->data_size,
              reinterpret_cast<const char *>(value->data)), actual_srid) != OB_SUCCESS ||
          actual_srid != sink.srid) {
        sink.invalid = true;
        return SEEKDB_PLUGIN_STATUS_INVALID_ARGUMENT;
      }
      sink.seen = true;
      sink.failure = ob_write_string(sink.allocator, ObString(value->data_size,
          reinterpret_cast<const char *>(value->data)), sink.bytes);
      return sink.failure == OB_SUCCESS ? SEEKDB_PLUGIN_STATUS_OK : SEEKDB_PLUGIN_STATUS_NO_MEMORY;
    }
  } sink{*allocator, srid, {}};
  seekdb_plugin_execution_value_v1_t args[2]{};
  for (auto &arg : args) arg.struct_size = sizeof(arg);
  args[0].type_id = "org.seekdb.gis.geometry";
  args[0].data = reinterpret_cast<const uint8_t *>(wkb.ptr()); args[0].data_size = wkb.length();
  args[1].type_id = "core.type.double";
  args[1].data = reinterpret_cast<const uint8_t *>(&distance); args[1].data_size = sizeof(distance);
  seekdb_plugin_execution_context_v1_t context{};
  context.struct_size = sizeof(context); context.host = reinterpret_cast<seekdb_plugin_host_handle_t *>(&sink);
  context.emit_result = Sink::emit;
  ret = share::g_mp->execute_plugin_function(SEEKDB_PLUGIN_SPATIAL_BUFFER_SERVICE, 1, 0, &context, args, 2);
  if (sink.failure != OB_SUCCESS) ret = sink.failure;
  if (ret == OB_SUCCESS && (!sink.seen || sink.invalid)) ret = OB_ERR_UNEXPECTED;
  if (ret == OB_SUCCESS) result = sink.bytes;
  return ret;
}

int ObSpatialMBR::to_char(char *buf, int64_t &buf_len) const
{
  // Existing API requires an OB_DEFAULT_MBR_SIZE buffer; buf_len is output,
  // not capacity. This keeps storage encoding separate from the plugin SPI.
  size_t size = 0;
  const bool ok = seekdb::geo::index_mbr::encode({x_min_, x_max_, y_min_, y_max_},
      is_point_, buf, OB_DEFAULT_MBR_SIZE, size);
  buf_len = size;
  return ok ? OB_SUCCESS : OB_INVALID_ARGUMENT;
}

int ObSpatialMBR::from_string(ObString &bytes, ObDomainOpType type, ObSpatialMBR &out, bool point)
{
  seekdb::geo::index_mbr::Box box;
  if (bytes.length() < 0 || !seekdb::geo::index_mbr::decode(bytes.ptr(), bytes.length(), point, box)) {
    return OB_INVALID_ARGUMENT;
  }
  out = ObSpatialMBR(box.xmin, box.xmax, box.ymin, box.ymax, type);
  out.is_point_ = point;
  return OB_SUCCESS;
}

int ObSpatialMBR::filter(const ObSpatialMBR &other, ObDomainOpType type, bool &pass_through) const
{
  // Historical name: true means reject/skip, not retain. Never publish a
  // partial result from a failed/malformed/duplicate plugin callback.
  pass_through = true;
  if (is_geog_ != other.is_geog_) return OB_INVALID_ARGUMENT;
  seekdb_plugin_spatial_filter_request_v1_t request{};
  request.struct_size = sizeof(request);
  switch (type) {
    case ObDomainOpType::T_GEO_COVERS: request.operation = SEEKDB_PLUGIN_SPATIAL_FILTER_COVERS; break;
    case ObDomainOpType::T_GEO_DWITHIN:
    case ObDomainOpType::T_GEO_INTERSECTS: request.operation = SEEKDB_PLUGIN_SPATIAL_FILTER_INTERSECTS; break;
    case ObDomainOpType::T_GEO_COVEREDBY: request.operation = SEEKDB_PLUGIN_SPATIAL_FILTER_COVERED_BY; break;
    case ObDomainOpType::T_GEO_DFULLYWITHIN: return OB_NOT_SUPPORTED;
    default: return OB_INVALID_ARGUMENT;
  }
  if (share::g_mp == nullptr) return OB_NOT_SUPPORTED;
  request.flags = (is_geog_ ? SEEKDB_PLUGIN_SPATIAL_FILTER_GEOGRAPHIC : 0) |
                  (is_point_ ? SEEKDB_PLUGIN_SPATIAL_FILTER_ROW_POINT : 0) |
                  (other.is_point_ ? SEEKDB_PLUGIN_SPATIAL_FILTER_QUERY_POINT : 0);
  request.row_xmin = x_min_; request.row_xmax = x_max_;
  request.row_ymin = y_min_; request.row_ymax = y_max_;
  request.query_xmin = other.x_min_; request.query_xmax = other.x_max_;
  request.query_ymin = other.y_min_; request.query_ymax = other.y_max_;
  seekdb_plugin_execution_value_v1_t argument{};
  argument.struct_size = sizeof(argument);
  argument.type_id = SEEKDB_PLUGIN_SPATIAL_FILTER_REQUEST_TYPE;
  argument.data = reinterpret_cast<const uint8_t *>(&request);
  argument.data_size = sizeof(request);
  SpatialFilterSink sink;
  seekdb_plugin_execution_context_v1_t context{};
  context.struct_size = sizeof(context);
  context.host = reinterpret_cast<seekdb_plugin_host_handle_t *>(&sink);
  context.emit_result = emit_spatial_filter;
  const int ret = share::g_mp->execute_plugin_function(
      SEEKDB_PLUGIN_SPATIAL_FILTER_SERVICE, 1, 0, &context, &argument, 1);
  if (ret != OB_SUCCESS) return ret;
  if (!sink.seen || sink.invalid) return OB_ERR_UNEXPECTED;
  pass_through = sink.reject;
  return OB_SUCCESS;
}
} // namespace oceanbase::common
#endif
