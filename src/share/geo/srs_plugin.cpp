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
#include "share/geo/ob_srs_wkt_parser.h"
#include "share/geo/srs_metadata_adapter.h"
#include "share/rc/ob_module_provider.h"
#include "seekdb/plugin/srs_spi.h"
#include "seekdb/geo/srs_projection_parameters.hpp"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <iterator>

#if !SEEKDB_ENABLE_CORE_GIS
namespace oceanbase::common {
namespace {
template <typename T, size_t N> bool zeroes(const T (&v)[N])
{
  return std::all_of(std::begin(v), std::end(v), [](T value) { return value == 0; });
}
struct SrsSink {
  uint32_t srid;
  seekdb_plugin_srs_metadata_v1_t metadata{};
  bool seen = false, invalid = false;
};
seekdb_plugin_status_t SEEKDB_PLUGIN_CALL emit_srs(
    seekdb_plugin_host_handle_t *host, const seekdb_plugin_execution_result_v1_t *result)
{
  if (host == nullptr) return SEEKDB_PLUGIN_STATUS_INVALID_ARGUMENT;
  auto &sink = *reinterpret_cast<SrsSink *>(host);
  const auto fail = [&]() { sink.invalid = true; return SEEKDB_PLUGIN_STATUS_INVALID_ARGUMENT; };
  if (sink.seen || sink.invalid || result == nullptr || result->struct_size != sizeof(*result) ||
      result->is_null || result->type_id == nullptr || std::strcmp(result->type_id, SEEKDB_PLUGIN_SRS_RESULT_TYPE) != 0 ||
      result->data == nullptr || result->data_size < sizeof(sink.metadata) ||
      !zeroes(result->reserved_bytes) || !zeroes(result->reserved)) return fail();
  auto &m = sink.metadata;
  std::memcpy(&m, result->data, sizeof(m));
  if (m.struct_size != sizeof(m) || m.srid != sink.srid || (m.flags & ~7u) != 0 ||
      m.parameter_count > SEEKDB_PLUGIN_SRS_MAX_PARAMETERS || m.reserved_word != 0 || !zeroes(m.reserved) ||
      result->data_size != sizeof(m) + uint64_t(m.parameter_count) * sizeof(seekdb_plugin_srs_parameter_v1_t) ||
      m.axis0 > 5 || m.axis1 > 5 || m.geographic_axis0 < 1 || m.geographic_axis0 > 5 ||
      m.geographic_axis1 < 1 || m.geographic_axis1 > 5 ||
      !std::isfinite(m.semi_major) || !std::isfinite(m.inverse_flattening) ||
      !std::isfinite(m.prime_meridian) || !std::isfinite(m.angular_unit) || std::isnan(m.linear_unit) ||
      bool(m.flags & SEEKDB_PLUGIN_SRS_HAS_TOWGS84) != !std::isnan(m.towgs84[0])) return fail();
  for (double value : m.towgs84) if (std::isinf(value)) return fail();
  const bool geographic = m.flags & SEEKDB_PLUGIN_SRS_GEOGRAPHIC;
  if (geographic && (m.projection_method != 0 || m.parameter_count != 0 ||
      m.axis0 != m.geographic_axis0 || m.axis1 != m.geographic_axis1 || m.linear_unit != 1.0)) return fail();
  if (!geographic && ((m.axis0 == 0) != (m.axis1 == 0))) return fail();
  if (m.flags & SEEKDB_PLUGIN_SRS_WGS84) {
    if (m.geographic_axis0 != 4 || m.geographic_axis1 != 1 || m.semi_major != 6378137.0 ||
        m.inverse_flattening != 298.257223563 || m.prime_meridian != 0.0 ||
        m.angular_unit != 0.017453292519943278) return fail();
    if (m.flags & SEEKDB_PLUGIN_SRS_HAS_TOWGS84)
      for (double value : m.towgs84) if (value != 0.0) return fail();
  }
  const auto *schema = seekdb::geo::srs::find_projection(m.projection_method);
  if ((schema == nullptr && (m.projection_method != 0 || m.parameter_count != 0)) ||
      (schema != nullptr && schema->count != m.parameter_count)) return fail();
  for (uint32_t i = 0; i < m.parameter_count; ++i) {
    seekdb_plugin_srs_parameter_v1_t parameter{};
    std::memcpy(&parameter, result->data + sizeof(m) + i * sizeof(parameter), sizeof(parameter));
    if (parameter.reserved_word != 0 || parameter.authority_code != schema->codes[i] ||
        std::isnan(parameter.value)) return fail();
  }
  sink.seen = true;
  return SEEKDB_PLUGIN_STATUS_OK;
}

// Arena-owned, destructor-independent state. The SRS cache bulk-frees its arena;
// do not put STL allocations, plugin vtables or lease-owning objects here.
class PluginSrs final : public ObSpatialReferenceSystemBase {
public:
  explicit PluginSrs(const seekdb_plugin_srs_metadata_v1_t &metadata) : metadata_(metadata) {}
  ObSrsType srs_type() const override
  { return metadata_.flags & SEEKDB_PLUGIN_SRS_GEOGRAPHIC ? ObSrsType::GEOGRAPHIC_SRS : ObSrsType::PROJECTED_SRS; }
  double prime_meridian() const override { return metadata_.prime_meridian; }
  double linear_unit() const override { return metadata_.linear_unit; }
  double angular_unit() const override { return metadata_.angular_unit; }
  double semi_major_axis() const override
  { return srs_type() == ObSrsType::GEOGRAPHIC_SRS ? metadata_.semi_major : 0.0; }
  double inverse_flattening() const override
  { return srs_type() == ObSrsType::GEOGRAPHIC_SRS ? metadata_.inverse_flattening : 0.0; }
  bool is_wgs84() const override { return metadata_.flags & SEEKDB_PLUGIN_SRS_WGS84; }
  bool has_wgs84_value() const override { return metadata_.flags & SEEKDB_PLUGIN_SRS_HAS_TOWGS84; }
  ObAxisDirection axis_direction(uint8_t index) const override
  { return index == 0 ? static_cast<ObAxisDirection>(metadata_.axis0) :
           index == 1 ? static_cast<ObAxisDirection>(metadata_.axis1) : ObAxisDirection::INIT; }
  int get_proj4_param(ObIAllocator *allocator, ObString &result) const override
  {
    if (srs_type() != ObSrsType::GEOGRAPHIC_SRS) return OB_SUCCESS;
    if (allocator == nullptr) return OB_INVALID_ARGUMENT;
    return build_srs_geographic_proj4(*allocator, metadata_.semi_major, metadata_.inverse_flattening,
                                      is_wgs84(), metadata_.towgs84, result);
  }
  uint32_t get_srid() const override { return metadata_.srid; }
  void set_bounds(double xmin, double ymin, double xmax, double ymax) override
  { bounds_.minX_ = xmin; bounds_.minY_ = ymin; bounds_.maxX_ = xmax; bounds_.maxY_ = ymax; }
  int set_proj4text(ObIAllocator &allocator, const ObString &text) override
  { return deep_copy_ob_string(allocator, text, proj4_); }
  // Legacy no-allocation setter: the cache passes bytes already in its arena.
  void set_proj4text(ObString &text) override { proj4_ = text; }
  const ObSrsBoundsItem *get_bounds() const override { return &bounds_; }
  ObString get_proj4text() override { return proj4_; }
private:
  seekdb_plugin_srs_metadata_v1_t metadata_;
  ObSrsBoundsItem bounds_;
  ObString proj4_;
};
} // namespace

int ObSrsWktParser::parse_srs_wkt(ObIAllocator &allocator, uint64_t srid,
                                 const ObString &wkt, ObSpatialReferenceSystemBase *&out)
{
  if (srid >= UINT32_MAX || wkt.ptr() == nullptr || wkt.length() <= 0 ||
      uint64_t(wkt.length()) > SEEKDB_PLUGIN_SRS_MAX_WKT_BYTES) return OB_INVALID_ARGUMENT;
  if (share::g_mp == nullptr) return OB_NOT_SUPPORTED;
  seekdb_plugin_srs_request_v1_t request{};
  request.struct_size = sizeof(request); request.srid = static_cast<uint32_t>(srid);
  seekdb_plugin_execution_value_v1_t args[2]{};
  for (auto &arg : args) arg.struct_size = sizeof(arg);
  args[0].type_id = "core.type.bytes"; args[0].data = reinterpret_cast<const uint8_t *>(wkt.ptr()); args[0].data_size = wkt.length();
  args[1].type_id = SEEKDB_PLUGIN_SRS_REQUEST_TYPE;
  args[1].data = reinterpret_cast<const uint8_t *>(&request); args[1].data_size = sizeof(request);
  SrsSink sink{static_cast<uint32_t>(srid)};
  seekdb_plugin_execution_context_v1_t context{};
  context.struct_size = sizeof(context); context.host = reinterpret_cast<seekdb_plugin_host_handle_t *>(&sink);
  context.emit_result = emit_srs;
  const int ret = share::g_mp->execute_plugin_function(SEEKDB_PLUGIN_SRS_DESCRIBE_SERVICE, 1, 0, &context, args, 2);
  if (ret != OB_SUCCESS) return ret;
  if (!sink.seen || sink.invalid) return OB_ERR_UNEXPECTED;
  void *memory = allocator.alloc(sizeof(PluginSrs));
  if (memory == nullptr) return OB_ALLOCATE_MEMORY_FAILED;
  out = new(memory) PluginSrs(sink.metadata);
  return OB_SUCCESS;
}
} // namespace oceanbase::common
#endif

