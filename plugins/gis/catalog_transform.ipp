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
struct CatalogSrs {
  uint32_t srid = 0;
  std::string wkt, proj4;
  seekdb::gis::srs::Metadata metadata;

  bool prepare()
  {
    namespace srs = seekdb::gis::srs;
    srs::CoordinateSystem parsed;
    if (!srs::parse(wkt, parsed) || srs::prepare(srid, parsed, metadata) != srs::PrepareStatus::ok)
      return false;
    if (!metadata.geographic) {
      // Stored projected coordinates are X/Y. Nonstandard projected axis
      // directions need a separately verified normalization policy.
      return std::isfinite(metadata.linear_unit) && metadata.linear_unit > 0 &&
          ((metadata.axis0 == 0 && metadata.axis1 == 0) ||
           (metadata.axis0 == 1 && metadata.axis1 == 4)) && !proj4.empty();
    }
    const auto coordinates = metadata.coordinates();
    const bool axes = ((metadata.axis0 == 1 || metadata.axis0 == 3) &&
                       (metadata.axis1 == 2 || metadata.axis1 == 4)) ||
                      ((metadata.axis1 == 1 || metadata.axis1 == 3) &&
                       (metadata.axis0 == 2 || metadata.axis0 == 4));
    if (!axes || !coordinates.usable() || metadata.semi_major <= 0 ||
        metadata.inverse_flattening < 0 || (!metadata.is_wgs84 && !metadata.has_towgs84()))
      return false;
    // Match the original geographic SRS formatter: meridian offset and axis
    // direction are applied by Coordinates, NOT again by proj4 +pm/+axis.
    std::ostringstream out;
    out.imbue(std::locale::classic());
    out << std::setprecision(std::numeric_limits<double>::max_digits10)
        << "+proj=lonlat +a=" << metadata.semi_major;
    if (metadata.inverse_flattening == 0) out << " +b=" << metadata.semi_major;
    else out << " +rf=" << metadata.inverse_flattening;
    out << " +towgs84=";
    for (unsigned i = 0; i < 7; ++i) {
      const double value = metadata.has_towgs84() ? metadata.towgs84[i] : 0;
      if (!std::isfinite(value)) return false;
      if (i) out << ',';
      out << value;
    }
    out << " +no_defs";
    proj4 = out.str();
    return true;
  }
};

struct CatalogSrsBatch {
  CatalogSrs items[2];
  uint32_t expected_count = 2;
  unsigned calls = 0;
  seekdb_plugin_status_t error = SEEKDB_PLUGIN_STATUS_OK;
  static seekdb_plugin_status_t SEEKDB_PLUGIN_CALL consume(
      void *opaque, const seekdb_plugin_srs_definition_v1_t *rows, uint32_t count)
  {
    auto &self = *static_cast<CatalogSrsBatch *>(opaque);
    // Catch inside the plugin callback: never unwind through the host DSO.
    try {
      if (++self.calls != 1 || rows == nullptr || count != self.expected_count)
        return self.error = SEEKDB_PLUGIN_STATUS_INVALID_ARGUMENT;
      for (unsigned i = 0; i < count; ++i) {
        const auto &r = rows[i];
        if (r.struct_size < sizeof(r) || r.srid != self.items[i].srid ||
            r.definition == nullptr || r.definition_size == 0 ||
            r.definition_size > SEEKDB_PLUGIN_SRS_MAX_WKT_BYTES ||
            r.proj4text_size > SEEKDB_PLUGIN_SRS_MAX_PROJ4_BYTES ||
            (r.proj4text_size && r.proj4text == nullptr))
          return self.error = SEEKDB_PLUGIN_STATUS_INVALID_ARGUMENT;
        self.items[i].wkt.assign(r.definition, r.definition_size);
        if (r.proj4text_size) self.items[i].proj4.assign(r.proj4text, r.proj4text_size);
        if (self.items[i].wkt.find('\0') != std::string::npos ||
            self.items[i].proj4.find('\0') != std::string::npos)
          return self.error = SEEKDB_PLUGIN_STATUS_INVALID_ARGUMENT;
      }
      return SEEKDB_PLUGIN_STATUS_OK;
    } catch (const std::bad_alloc &) { return self.error = SEEKDB_PLUGIN_STATUS_NO_MEMORY;
    } catch (...) { return self.error = SEEKDB_PLUGIN_STATUS_INTERNAL; }
  }
};

static seekdb_plugin_status_t catalog_transform(const seekdb_plugin_execution_context_v1_t *context,
                                                Geometry &geometry, uint32_t target)
{
  if (geometry.srid == UINT32_MAX || target == UINT32_MAX) return SEEKDB_PLUGIN_STATUS_INVALID_ARGUMENT;
  if (geometry.srid == 0 || target == 0)
    return geometry.srid == target ? SEEKDB_PLUGIN_STATUS_OK : SEEKDB_PLUGIN_STATUS_INVALID_ARGUMENT;
  if (context->struct_size < sizeof(seekdb_plugin_execution_context_v2_t))
    return SEEKDB_PLUGIN_STATUS_FAILED_PRECONDITION;
  const auto &sql = *reinterpret_cast<const seekdb_plugin_execution_context_v2_t *>(context);
  if (sql.sql_context == nullptr || sql.sql_api == nullptr ||
      sql.sql_api->struct_size < sizeof(seekdb_plugin_sql_api_v5_t) ||
      sql.sql_api->spi_major != SEEKDB_PLUGIN_SQL_SPI_MAJOR ||
      sql.sql_api->spi_minor < SEEKDB_PLUGIN_SQL_SRS_LOOKUP_MINOR)
    return SEEKDB_PLUGIN_STATUS_FAILED_PRECONDITION;
  const auto &api = *reinterpret_cast<const seekdb_plugin_sql_api_v5_t *>(sql.sql_api);
  if (api.lookup_srs == nullptr || api.v4.v3.v2.poll_query == nullptr)
    return SEEKDB_PLUGIN_STATUS_FAILED_PRECONDITION;
  CatalogSrsBatch batch;
  const uint32_t ids[] = {geometry.srid, target};
  batch.items[0].srid = ids[0]; batch.items[1].srid = ids[1];
  seekdb_plugin_sql_result_v1_t result{}; result.struct_size = sizeof(result);
  auto status = api.lookup_srs(sql.sql_context, ids, 2, CatalogSrsBatch::consume, &batch, &result);
  if (batch.error != SEEKDB_PLUGIN_STATUS_OK) return batch.error;
  if (status != SEEKDB_PLUGIN_STATUS_OK) return status;
  if (batch.calls != 1 || result.database_error != 0 || result.returned_rows != 2)
    return SEEKDB_PLUGIN_STATUS_FAILED_PRECONDITION;
  auto &source = batch.items[0]; auto &destination = batch.items[1];
  if (!source.prepare() || !destination.prepare()) return SEEKDB_PLUGIN_STATUS_INVALID_ARGUMENT;
  // Boost otherwise silently skips datum conversion if either side is unknown.
  const seekdb::gis::Projection projection(source.proj4, destination.proj4, true);
  const auto poll = [&]() {
    seekdb_plugin_query_status_v1_t query{}; query.struct_size = sizeof(query);
    return api.v4.v3.v2.poll_query(sql.sql_context, &query);
  };
  status = poll();
  if (status != SEEKDB_PLUGIN_STATUS_OK) return status;
  size_t vertices = 0;
  const auto transform = [&](auto &&self, Geometry &g) -> bool {
    const auto point = [&](Point &p) {
      if ((++vertices & 1023u) == 0 && (status = poll()) != SEEKDB_PLUGIN_STATUS_OK) return false;
      if (source.metadata.geographic) {
        const auto c = source.metadata.coordinates();
        const double longitude = p.x * c.angular_unit, latitude = p.y * c.angular_unit;
        constexpr double pi = 3.14159265358979323846;
        if (longitude <= -pi || longitude > pi || latitude < -pi / 2 || latitude > pi / 2)
          return false;
        // Internal WKB is always longitude/X then latitude/Y, even when WKT
        // declares latitude-first. No axis-order swap belongs here.
        if (!c.longitude_to_radians(p.x, p.x) || !c.latitude_to_radians(p.y, p.y)) return false;
      }
      if (ids[0] != ids[1] && !projection.forward(p.x, p.y, p.z, g.dimensions)) return false;
      if (destination.metadata.geographic) {
        const auto c = destination.metadata.coordinates();
        if (!c.longitude_from_radians(p.x, p.x) || !c.latitude_from_radians(p.y, p.y)) return false;
      }
      return std::isfinite(p.x) && std::isfinite(p.y);
    };
    for (auto &p : g.points) if (!point(p)) return false;
    for (auto &ring : g.rings) for (auto &p : ring) if (!point(p)) return false;
    for (auto &child : g.children) if (!self(self, child)) return false;
    g.srid = target;
    return true;
  };
  if (!transform(transform, geometry))
    return status == SEEKDB_PLUGIN_STATUS_OK ? SEEKDB_PLUGIN_STATUS_INVALID_ARGUMENT : status;
  return poll(); // No result is emitted if cancellation arrived during the tail.
}
