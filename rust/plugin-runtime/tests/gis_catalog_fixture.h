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
#include "seekdb/plugin/sql_spi.h"
#include <cstring>

// Exact definitions from tools/default_srs_data_mysql.sql. The transport below
// is controlled; production SQL tests put these records in the real host cache.
namespace gis_catalog_test {
struct Record { uint32_t srid; const char *wkt; const char *proj4; };
inline const Record records[] = {
  {3857, R"wkt(PROJCS["WGS 84 / Pseudo-Mercator",GEOGCS["WGS 84",DATUM["World Geodetic System 1984",SPHEROID["WGS 84",6378137,298.257223563,AUTHORITY["EPSG","7030"]],AUTHORITY["EPSG","6326"]],PRIMEM["Greenwich",0,AUTHORITY["EPSG","8901"]],UNIT["degree",0.017453292519943278,AUTHORITY["EPSG","9122"]],AXIS["Lat",NORTH],AXIS["Lon",EAST],AUTHORITY["EPSG","4326"]],PROJECTION["Popular Visualisation Pseudo Mercator",AUTHORITY["EPSG","1024"]],PARAMETER["Latitude of natural origin",0,AUTHORITY["EPSG","8801"]],PARAMETER["Longitude of natural origin",0,AUTHORITY["EPSG","8802"]],PARAMETER["False easting",0,AUTHORITY["EPSG","8806"]],PARAMETER["False northing",0,AUTHORITY["EPSG","8807"]],UNIT["metre",1,AUTHORITY["EPSG","9001"]],AXIS["X",EAST],AXIS["Y",NORTH],AUTHORITY["EPSG","3857"]])wkt", "+proj=merc +a=6378137 +b=6378137 +lat_ts=0.0 +lon_0=0.0 +x_0=0.0 +y_0=0 +k=1.0 +units=m +nadgrids=@null +wktext +no_defs"},
  {4326, R"wkt(GEOGCS["WGS 84",DATUM["World Geodetic System 1984",SPHEROID["WGS 84",6378137,298.257223563,AUTHORITY["EPSG","7030"]],AUTHORITY["EPSG","6326"]],PRIMEM["Greenwich",0,AUTHORITY["EPSG","8901"]],UNIT["degree",0.017453292519943278,AUTHORITY["EPSG","9122"]],AXIS["Lat",NORTH],AXIS["Lon",EAST],AUTHORITY["EPSG","4326"]])wkt", "+proj=longlat +datum=WGS84 +no_defs"},
  {32631, R"wkt(PROJCS["WGS 84 / UTM zone 31N",GEOGCS["WGS 84",DATUM["World Geodetic System 1984",SPHEROID["WGS 84",6378137,298.257223563,AUTHORITY["EPSG","7030"]],AUTHORITY["EPSG","6326"]],PRIMEM["Greenwich",0,AUTHORITY["EPSG","8901"]],UNIT["degree",0.017453292519943278,AUTHORITY["EPSG","9122"]],AXIS["Lat",NORTH],AXIS["Lon",EAST],AUTHORITY["EPSG","4326"]],PROJECTION["Transverse Mercator",AUTHORITY["EPSG","9807"]],PARAMETER["Latitude of natural origin",0,AUTHORITY["EPSG","8801"]],PARAMETER["Longitude of natural origin",3,AUTHORITY["EPSG","8802"]],PARAMETER["Scale factor at natural origin",0.9996,AUTHORITY["EPSG","8805"]],PARAMETER["False easting",500000,AUTHORITY["EPSG","8806"]],PARAMETER["False northing",0,AUTHORITY["EPSG","8807"]],UNIT["metre",1,AUTHORITY["EPSG","9001"]],AXIS["E",EAST],AXIS["N",NORTH],AUTHORITY["EPSG","32631"]])wkt", "+proj=utm +zone=31 +datum=WGS84 +units=m +no_defs"},
};
struct Transport {
  unsigned calls = 0, polls = 0;
  bool fail = false;
  static seekdb_plugin_status_t SEEKDB_PLUGIN_CALL lookup(
      seekdb_plugin_sql_context_handle_t *opaque, const uint32_t *ids, uint32_t count,
      seekdb_plugin_srs_consume_v1_fn consume, void *consumer, seekdb_plugin_sql_result_v1_t *result)
  {
    auto &self = *reinterpret_cast<Transport *>(opaque); ++self.calls;
    CHECK(count == 2);
    if (self.fail) return SEEKDB_PLUGIN_STATUS_TIMEOUT;
    seekdb_plugin_srs_definition_v1_t rows[2]{};
    for (unsigned i = 0; i < 2; ++i) {
      for (const auto &r : records) if (r.srid == ids[i]) {
        rows[i].struct_size = sizeof(rows[i]); rows[i].srid = r.srid;
        rows[i].definition = r.wkt; rows[i].definition_size = std::strlen(r.wkt);
        rows[i].proj4text = r.proj4; rows[i].proj4text_size = std::strlen(r.proj4);
      }
      if (!rows[i].definition) return SEEKDB_PLUGIN_STATUS_INVALID_ARGUMENT;
    }
    result->returned_rows = count;
    return consume(consumer, rows, count);
  }
  static seekdb_plugin_status_t SEEKDB_PLUGIN_CALL poll(
      seekdb_plugin_sql_context_handle_t *opaque, seekdb_plugin_query_status_v1_t *)
  { ++reinterpret_cast<Transport *>(opaque)->polls; return SEEKDB_PLUGIN_STATUS_OK; }
  seekdb_plugin_sql_api_v5_t api{};
  Transport() {
    api.v4.v3.v2.v1.struct_size = sizeof(api);
    api.v4.v3.v2.v1.spi_major = SEEKDB_PLUGIN_SQL_SPI_MAJOR;
    api.v4.v3.v2.v1.spi_minor = SEEKDB_PLUGIN_SQL_SRS_LOOKUP_MINOR;
    api.lookup_srs = lookup; api.v4.v3.v2.poll_query = poll;
  }
  void attach(seekdb_plugin_execution_context_v2_t &context) {
    context.v1.struct_size = sizeof(context);
    context.sql_api = &api.v4.v3.v2.v1;
    context.sql_context = reinterpret_cast<seekdb_plugin_sql_context_handle_t *>(this);
  }
};
}

