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
// Included inside geometry_engine.cpp's private namespace.
// Ordinary and PG-compatible I/O share catalog lookup and ownership, not their
// defaults or optional arguments. Binary EWKB always emits long-lat coordinates.
static bool geometry_from_ewkb(const uint8_t *data, size_t size, Geometry &geometry)
{
  // Port get_header_info_from_ewkb/construct_ewkb_data: EWKB flags belong
  // only to the root. Its Z flag (not an ISO type offset) selects dimensions;
  // children retain ordinary WKB types and cannot introduce SRIDs or flags.
  // The original entry admits little-endian roots only.
  if (!data || size < 5 || data[0] != 1) return false;
  Reader header(data + 1, size - 1);
  uint32_t encoded_type = 0, srid = 0;
  if (!header.u32(&encoded_type) || (encoded_type & UINT32_C(0x40000000))) return false;
  const uint32_t lower_type = encoded_type & UINT32_C(0x0fffffff);
  if (lower_type >= 4000 || lower_type % 1000 < 1 || lower_type % 1000 > 7) return false;
  const bool z = (encoded_type & UINT32_C(0x80000000)) != 0;
  const bool has_srid = (encoded_type & UINT32_C(0x20000000)) != 0;
  if (has_srid && !header.u32(&srid)) return false;
  const size_t payload = has_srid ? 9 : 5;
  std::vector<uint8_t> canonical;
  canonical.reserve(size - payload + 5);
  canonical.push_back(1);
  append_u32(canonical, lower_type % 1000 + (z ? 1000 : 0));
  canonical.insert(canonical.end(), data + payload, data + size);
  Reader reader(canonical.data(), canonical.size());
  if (!read_geometry(reader, geometry, srid, 0, false, z ? -1 : 1) || reader.remaining()) return false;
  const auto valid = [&](auto &&self, const Geometry &g) -> bool {
    if (g.dimensions != geometry.dimensions) return false;
    // Preserve the original 2D WKB check visitor's nonempty polygon/multi
    // admission and bitwise XY closure. Its 3D checker has different rules.
    if (!z) {
      if (g.type == 3 && g.rings.empty()) return false;
      if (g.type >= 4 && g.type <= 6 && g.children.empty()) return false;
      for (const auto &ring : g.rings) {
        if (std::memcmp(&ring.front().x, &ring.back().x, sizeof(double)) ||
            std::memcmp(&ring.front().y, &ring.back().y, sizeof(double))) return false;
      }
    }
    for (const auto &child : g.children) if (!self(self, child)) return false;
    return true;
  };
  return valid(valid, geometry);
}

static seekdb_plugin_status_t catalog_geometry_io(uint32_t operation,
    const seekdb_plugin_execution_context_v1_t *context,
    const seekdb_plugin_execution_value_v1_t *arguments, uint32_t count)
{
  namespace srs = seekdb::gis::srs;
  namespace geo_srs = seekdb::geo::srs;
  const bool ewkt = operation == SEEKDB_GIS_TEXT_FROM_EWKT;
  const bool ewkb = operation == SEEKDB_GIS_TEXT_FROM_EWKB;
  const bool geography = operation == SEEKDB_GIS_TEXT_GEOGRAPHY;
  const bool as_ewkb = operation == SEEKDB_GIS_TEXT_AS_EWKB;
  const bool pg = ewkt || ewkb || geography || as_ewkb;
  const bool input = operation == SEEKDB_GIS_TEXT_CATALOG_FROM_TEXT ||
                     operation == SEEKDB_GIS_TEXT_CATALOG_FROM_WKB || ewkt || ewkb || geography;
  const uint32_t maximum = pg ? (ewkb ? 2 : 1) : input ? 3 : 2;
  if (count == 0 || count > maximum) return SEEKDB_PLUGIN_STATUS_INVALID_ARGUMENT;
  for (uint32_t i = 0; i < count; ++i)
    if (arguments[i].struct_size != sizeof(arguments[i]) || (arguments[i].is_null && !ewkb))
      return SEEKDB_PLUGIN_STATUS_INVALID_ARGUMENT;
  const auto bytes = [](const seekdb_plugin_execution_value_v1_t &a) {
    return a.type_id && std::strcmp(a.type_id, "org.seekdb.gis.scalar.bytes") == 0 &&
           (a.data || !a.data_size) && a.data_size <= 16 * 1024 * 1024;
  };
  geo_srs::AxisOrder order = pg ? geo_srs::AxisOrder::long_lat : geo_srs::AxisOrder::srid_defined;
  const uint32_t option = ewkb ? 1 : input ? 2 : 1;
  if (count > option && !arguments[option].is_null) {
    const auto &a = arguments[option];
    if (!bytes(a) || !geo_srs::parse_axis_order(
          std::string_view(a.data ? reinterpret_cast<const char *>(a.data) : "", a.data_size), order))
      return SEEKDB_PLUGIN_STATUS_INVALID_ARGUMENT;
  }
  if (arguments[0].is_null) return emit_null_geometry(context);
  Geometry geometry;
  if (input) {
    uint32_t srid = 0;
    if ((!pg && count > 1 && !scalar_u32(arguments[1], srid)) || !bytes(arguments[0]) ||
        !arguments[0].data_size) return SEEKDB_PLUGIN_STATUS_INVALID_ARGUMENT;
    if (operation == SEEKDB_GIS_TEXT_CATALOG_FROM_TEXT || ewkt || geography) {
      std::string_view text(reinterpret_cast<const char *>(arguments[0].data), arguments[0].data_size);
      if (pg) {
        const auto delimiter = text.find(';');
        if (delimiter != std::string_view::npos) {
          if (!seekdb::geo::pg::parse_srid_prefix(text.substr(0, delimiter), srid))
            return SEEKDB_PLUGIN_STATUS_INVALID_ARGUMENT;
          text.remove_prefix(delimiter + 1);
        }
        if (geography && !srid) srid = 4326;
      }
      WktParser parser(text.data(), text.size());
      if (!parser.parse(geometry, srid)) return SEEKDB_PLUGIN_STATUS_INVALID_ARGUMENT;
    } else if (ewkb) {
      if (!geometry_from_ewkb(arguments[0].data, arguments[0].data_size, geometry))
        return SEEKDB_PLUGIN_STATUS_INVALID_ARGUMENT;
    } else {
      Reader reader(arguments[0].data, arguments[0].data_size);
      if (!read_geometry(reader, geometry, srid, 0, false) || reader.remaining()) return SEEKDB_PLUGIN_STATUS_INVALID_ARGUMENT;
    }
  } else if (!decode(arguments[0], geometry)) return SEEKDB_PLUGIN_STATUS_INVALID_ARGUMENT;
  if (geometry.srid == UINT32_MAX) return SEEKDB_PLUGIN_STATUS_INVALID_ARGUMENT;
  srs::Metadata metadata;
  const seekdb_plugin_sql_api_v5_t *api = nullptr;
  seekdb_plugin_sql_context_handle_t *sql_context = nullptr;
  if (geometry.srid) {
    if (context->struct_size < sizeof(seekdb_plugin_execution_context_v2_t))
      return SEEKDB_PLUGIN_STATUS_FAILED_PRECONDITION;
    const auto &sql = *reinterpret_cast<const seekdb_plugin_execution_context_v2_t *>(context);
    if (!sql.sql_api || !sql.sql_context || sql.sql_api->struct_size < sizeof(*api) ||
        sql.sql_api->spi_major != SEEKDB_PLUGIN_SQL_SPI_MAJOR ||
        sql.sql_api->spi_minor < SEEKDB_PLUGIN_SQL_SRS_LOOKUP_MINOR)
      return SEEKDB_PLUGIN_STATUS_FAILED_PRECONDITION;
    api = reinterpret_cast<const seekdb_plugin_sql_api_v5_t *>(sql.sql_api);
    sql_context = sql.sql_context;
    if (!api->lookup_srs || !api->v4.v3.v2.poll_query) return SEEKDB_PLUGIN_STATUS_FAILED_PRECONDITION;
    CatalogSrsBatch batch; batch.expected_count = 1; batch.items[0].srid = geometry.srid;
    seekdb_plugin_sql_result_v1_t result{}; result.struct_size = sizeof(result);
    const auto status = api->lookup_srs(sql_context, &geometry.srid, 1,
                                      CatalogSrsBatch::consume, &batch, &result);
    if (batch.error != SEEKDB_PLUGIN_STATUS_OK) return batch.error;
    if (status != SEEKDB_PLUGIN_STATUS_OK) return status;
    if (batch.calls != 1 || result.database_error || result.returned_rows != 1)
      return SEEKDB_PLUGIN_STATUS_INVALID_ARGUMENT;
    srs::CoordinateSystem parsed;
    if (!srs::parse(batch.items[0].wkt, parsed) ||
        srs::prepare(geometry.srid, parsed, metadata) != srs::PrepareStatus::ok)
      return SEEKDB_PLUGIN_STATUS_INVALID_ARGUMENT;
    // I/O does not transform datums; unlike ST_Transform, no towgs84 is needed.
    if (metadata.geographic && (!std::isfinite(metadata.angular_unit) || metadata.angular_unit <= 0))
      return SEEKDB_PLUGIN_STATUS_INVALID_ARGUMENT;
  }
  const auto poll = [&]() -> seekdb_plugin_status_t {
    if (!api) return SEEKDB_PLUGIN_STATUS_OK;
    seekdb_plugin_query_status_v1_t query{}; query.struct_size = sizeof(query);
    return api->v4.v3.v2.poll_query(sql_context, &query);
  };
  auto status = poll();
  if (status != SEEKDB_PLUGIN_STATUS_OK) return status;
  const bool geographic = geometry.srid && metadata.geographic;
  if (geography && !geographic) return SEEKDB_PLUGIN_STATUS_INVALID_ARGUMENT;
  const bool reverse = (geographic || ewkb) && (order == geo_srs::AxisOrder::lat_long ||
      (!pg && order == geo_srs::AxisOrder::srid_defined && metadata.coordinates().latitude_first()));
  size_t vertices = 0;
  const auto visit = [&](auto &&self, Geometry &g) -> bool {
    if (g.srid != geometry.srid) return false; // No mixed-SRID child escapes validation.
    const auto point = [&](Point &p) {
      if ((++vertices & 1023u) == 0 && (status = poll()) != SEEKDB_PLUGIN_STATUS_OK) return false;
      if (input && reverse) std::swap(p.x, p.y);
      if (geography) {
        // The original 2D visitor normalizes only out-of-range pairs; the 3D
        // visitor always normalizes. Retain its degree-based folding policy.
        if (g.dimensions == 3 || p.x < -180 || p.x > 180 || p.y < -90 || p.y > 90) {
          p.x = seekdb::geo::pg::normalize_longitude(p.x);
          p.y = seekdb::geo::pg::normalize_latitude(p.y);
        }
      } else if (geographic) {
        constexpr double pi = 3.14159265358979323846;
        const double x = p.x * metadata.angular_unit, y = p.y * metadata.angular_unit;
        if (!std::isfinite(x) || !std::isfinite(y) || x <= -pi || x > pi || y < -pi / 2 || y > pi / 2)
          return false;
      }
      if (!input && reverse) std::swap(p.x, p.y);
      return true;
    };
    for (auto &p : g.points) if (!point(p)) return false;
    for (auto &ring : g.rings) for (auto &p : ring) if (!point(p)) return false;
    for (auto &child : g.children) if (!self(self, child)) return false;
    return true;
  };
  if (!visit(visit, geometry))
    return status == SEEKDB_PLUGIN_STATUS_OK ? SEEKDB_PLUGIN_STATUS_INVALID_ARGUMENT : status;
  status = poll();
  if (status != SEEKDB_PLUGIN_STATUS_OK) return status;
  if (input) return emit_geometry(context, geometry);
  if (operation == SEEKDB_GIS_TEXT_CATALOG_AS_TEXT) {
    std::string text;
    if (!geometry_to_wkt(geometry, text)) return SEEKDB_PLUGIN_STATUS_INVALID_ARGUMENT;
    return emit_bytes(context, text, "core.type.text");
  }
  std::vector<uint8_t> wkb;
  write_geometry(geometry, wkb);
  if (as_ewkb) {
    // Reuse the original envelope conversion: only the root header gains
    // EWKB flags/SRID; child WKB remains the original canonical representation.
    uint32_t type = geometry.type | (geometry.dimensions == 3 ? UINT32_C(0x80000000) : 0);
    if (geometry.srid) type |= UINT32_C(0x20000000);
    for (unsigned i = 0; i < 4; ++i) wkb[1 + i] = uint8_t(type >> (8 * i));
    if (geometry.srid) {
      std::vector<uint8_t> id; append_u32(id, geometry.srid);
      wkb.insert(wkb.begin() + 5, id.begin(), id.end());
    }
  }
  const seekdb_plugin_execution_result_v1_t result = {
      sizeof(result), "core.type.blob", wkb.data(), wkb.size(), 0, {}, {}};
  return context->emit_result(context->host, &result);
}
