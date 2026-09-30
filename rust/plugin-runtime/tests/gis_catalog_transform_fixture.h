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
// Actual SQL-bound transform service, controlled catalog transport. Unlike the
// raw proj4 service these inputs/outputs carry stored X/Y in SRS angular units.
namespace gis_catalog_transform_test {
static void exercise(ObPluginLoader &loader)
{
  struct Entry { uint32_t id; std::string wkt, proj4; };
  struct Host {
    std::vector<Entry> entries;
    unsigned lookups = 0, polls = 0, cancel_at = 0;
    int fault = 0;
    static seekdb_plugin_status_t SEEKDB_PLUGIN_CALL lookup(
        seekdb_plugin_sql_context_handle_t *opaque, const uint32_t *ids, uint32_t count,
        seekdb_plugin_srs_consume_v1_fn consume, void *consumer, seekdb_plugin_sql_result_v1_t *out) {
      auto &s = *reinterpret_cast<Host *>(opaque); ++s.lookups;
      CHECK(count == 1 || count == 2);
      if (s.fault == 1) return SEEKDB_PLUGIN_STATUS_TIMEOUT;
      if (s.fault == 2) return SEEKDB_PLUGIN_STATUS_OK; // Missing callback.
      seekdb_plugin_srs_definition_v1_t rows[2]{};
      for (unsigned i = 0; i < count; ++i) {
        for (const auto &e : s.entries) if (e.id == ids[i]) {
          auto &r = rows[i]; r.struct_size = sizeof(r); r.srid = e.id;
          r.definition = e.wkt.data(); r.definition_size = e.wkt.size();
          r.proj4text = e.proj4.data(); r.proj4text_size = e.proj4.size();
        }
        if (!rows[i].definition) return SEEKDB_PLUGIN_STATUS_INVALID_ARGUMENT;
      }
      if (s.fault == 3) ++rows[count - 1].srid;
      if (s.fault == 4) rows[0].definition_size = SEEKDB_PLUGIN_SRS_MAX_WKT_BYTES + 1;
      auto result = consume(consumer, rows, count);
      if (s.fault == 5) result = consume(consumer, rows, count);
      out->returned_rows = count;
      return result;
    }
    static seekdb_plugin_status_t SEEKDB_PLUGIN_CALL poll(
        seekdb_plugin_sql_context_handle_t *opaque, seekdb_plugin_query_status_v1_t *) {
      auto &s = *reinterpret_cast<Host *>(opaque);
      return ++s.polls == s.cancel_at ? SEEKDB_PLUGIN_STATUS_TIMEOUT : SEEKDB_PLUGIN_STATUS_OK;
    }
  } host;
  for (const auto &r : gis_catalog_test::records) host.entries.push_back({r.srid, r.wkt, r.proj4});
  const std::string local = R"(GEOGCS["local",DATUM["local",SPHEROID["WGS",6378137,298.257223563],TOWGS84[0,0,0,0,0,0,0]],PRIMEM["local",10],UNIT["grad",0.015707963267948966],AXIS["Lat",SOUTH],AXIS["Lon",WEST]])";
  host.entries.push_back({70000001, local, ""});
  host.entries.push_back({70000003, R"(GEOGCS["shift",DATUM["shift",SPHEROID["WGS",6378137,298.257223563],TOWGS84[1,2,3,0,0,0,0]],PRIMEM["Greenwich",0],UNIT["degree",0.017453292519943278],AXIS["Lat",NORTH],AXIS["Lon",EAST]])", ""});
  host.entries.push_back({70000004, R"(GEOGCS["unknown datum",DATUM["unknown",SPHEROID["WGS",6378137,298.257223563]],PRIMEM["Greenwich",0],UNIT["degree",0.017453292519943278],AXIS["Lat",NORTH],AXIS["Lon",EAST]])", ""});
  for (const auto &r : gis_catalog_test::records) if (r.srid == 32631) {
    std::string no_datum(r.proj4);
    no_datum.replace(no_datum.find("+datum=WGS84"), 12, "+ellps=WGS84");
    host.entries.push_back({70000005, r.wkt, no_datum});
    std::string wkt(r.wkt), proj4(r.proj4);
    wkt.replace(wkt.find("UNIT[\"metre\",1,"), 15, "UNIT[\"kilometre\",1000,");
    proj4.replace(proj4.find("+units=m"), 8, "+units=km");
    host.entries.push_back({70000002, wkt, proj4});
  }
  seekdb_plugin_sql_api_v5_t api{};
  api.v4.v3.v2.v1.struct_size = sizeof(api);
  api.v4.v3.v2.v1.spi_major = 1; api.v4.v3.v2.v1.spi_minor = SEEKDB_PLUGIN_SQL_SRS_LOOKUP_MINOR;
  api.lookup_srs = Host::lookup; api.v4.v3.v2.poll_query = Host::poll;
  seekdb_plugin_execution_context_v2_t context{};
  context.v1.struct_size = sizeof(context); context.v1.emit_result = emit_geometry;
  context.sql_context = reinterpret_cast<seekdb_plugin_sql_context_handle_t *>(&host);
  context.sql_api = &api.v4.v3.v2.v1;
  const char *types[] = {"org.seekdb.gis.geometry", "org.seekdb.gis.scalar.uint32"};
  seekdb_plugin_sql_binding_v1_t binding{};
  CHECK(loader.resolve_native_function("org.seekdb.gis", "org.seekdb.gis.function.st_transform",
                                       types, 2, binding) == OB_SUCCESS);
  std::vector<uint8_t> input;
  uint32_t target = 4326;
  GeometrySink sink;
  const auto call = [&](int expected = OB_SUCCESS) {
    sink = {}; host.lookups = host.polls = 0;
    context.v1.host = reinterpret_cast<seekdb_plugin_host_handle_t *>(&sink);
    seekdb_plugin_execution_value_v1_t args[2]{};
    for (unsigned i = 0; i < 2; ++i) { args[i].struct_size = sizeof(args[i]); args[i].type_id = types[i]; }
    args[0].data = input.data(); args[0].data_size = input.size();
    args[1].data = reinterpret_cast<const uint8_t *>(&target); args[1].data_size = sizeof(target);
    const int ret = loader.execute_bound_function(binding, &context.v1, args, 2);
    if (ret != expected) std::cerr << "catalog transform " << ret << " expected " << expected << std::endl;
    CHECK(ret == expected && sink.calls == (expected == OB_SUCCESS ? 1 : 0));
  };
  const auto value = [&](unsigned index) {
    double result; CHECK(sink.bytes.size() >= 10 + (index + 1) * sizeof(result));
    std::memcpy(&result, sink.bytes.data() + 10 + index * sizeof(result), sizeof(result)); return result;
  };
  input = gis_spatial_test::geometry(1, {1, 2}, 70000001); call();
  CHECK(host.lookups == 1 && host.polls == 2);
  CHECK(std::abs(value(0) - 8.1) < 1e-12 && std::abs(value(1) + 1.8) < 1e-12);
  input = sink.bytes; target = 70000001; call();
  CHECK(std::abs(value(0) - 1) < 1e-12 && std::abs(value(1) - 2) < 1e-12);
  input = gis_spatial_test::geometry(1, {3, 0}, 4326); target = 70000002; call();
  CHECK(std::abs(value(0) - 500) < 1e-9 && std::abs(value(1)) < 1e-9);
  input = sink.bytes; target = 4326; call();
  CHECK(std::abs(value(0) - 3) < 1e-12);
  input = gis_spatial_test::geometry(1001, {0, 0, 0}, 70000003); target = 4326; call();
  CHECK(std::abs(value(0) - std::atan2(2.0, 6378138.0) / 0.017453292519943278) < 1e-12);
  CHECK(value(1) > 0 && std::abs(value(2) - 1) < 1e-5);
  input = sink.bytes; target = 70000003; call();
  CHECK(std::abs(value(0)) < 1e-10 && std::abs(value(1)) < 1e-10 && std::abs(value(2)) < 1e-5);
  input = gis_spatial_test::geometry(1, {0, 0}, 70000004); target = 4326; call(OB_INVALID_ARGUMENT);
  input = gis_spatial_test::geometry(1, {500000, 0}, 70000005); call(OB_INVALID_ARGUMENT);
  input = gis_spatial_test::geometry(1, {3, 0}, 4326);
  for (int fault = 1; fault <= 5; ++fault) {
    host.fault = fault;
    call(fault == 1 ? OB_TIMEOUT : fault == 2 ? OB_STATE_NOT_MATCH : OB_INVALID_ARGUMENT);
  }
  host.fault = 0;
  host.cancel_at = 1; call(OB_TIMEOUT);
  host.cancel_at = 2; call(OB_TIMEOUT); // Tail poll: still no emitted geometry.
  host.cancel_at = 0;
  context.v1.struct_size = sizeof(context.v1); call(OB_STATE_NOT_MATCH);
  context.v1.struct_size = sizeof(context);
  api.v4.v3.v2.v1.struct_size = sizeof(seekdb_plugin_sql_api_v4_t); call(OB_STATE_NOT_MATCH);
  api.v4.v3.v2.v1.struct_size = sizeof(api);
  api.v4.v3.v2.poll_query = nullptr; call(OB_STATE_NOT_MATCH);
  api.v4.v3.v2.poll_query = Host::poll;
  input = gis_spatial_test::geometry(2, {}, 4326); target = 32631;
  const uint32_t count = 2048;
  for (unsigned i = 0; i < 4; ++i) input[10 + i] = uint8_t(count >> (8 * i));
  const double vertex[] = {3, 0};
  const auto *bytes = reinterpret_cast<const uint8_t *>(vertex);
  for (uint32_t i = 0; i < count; ++i) input.insert(input.end(), bytes, bytes + sizeof(vertex));
  host.cancel_at = 2; call(OB_TIMEOUT); CHECK(host.polls == 2);
  host.cancel_at = 0; call(); CHECK(host.polls == 4 && sink.bytes.size() == input.size());
  input = gis_spatial_test::geometry(1, {3, 0}, 99999); target = 99999; call(OB_INVALID_ARGUMENT);
  input = gis_spatial_test::geometry(7, {}, 99999); gis_spatial_test::append_u32(input, 0); call(OB_INVALID_ARGUMENT);
  std::cout << "PASS: catalog-bound transform, axes/signs, grad/prime meridian, projected kilometres, missing/duplicate callbacks and cancellation" << std::endl;
  {
    // These are independent I/O controls, not only a round trip that could
    // conceal two compensating axis mistakes. Transport remains controlled.
    host.cancel_at = 0;
    struct Output {
      unsigned calls = 0;
      std::string bytes;
      static seekdb_plugin_status_t SEEKDB_PLUGIN_CALL emit(seekdb_plugin_host_handle_t *h,
          const seekdb_plugin_execution_result_v1_t *r) {
        auto &s = *reinterpret_cast<Output *>(h);
        if (!r || !r->data || r->is_null) return SEEKDB_PLUGIN_STATUS_INVALID_ARGUMENT;
        try { s.bytes.assign(reinterpret_cast<const char *>(r->data), r->data_size); }
        catch (...) { return SEEKDB_PLUGIN_STATUS_NO_MEMORY; }
        ++s.calls;
        return SEEKDB_PLUGIN_STATUS_OK;
      }
    } output;
    context.v1.host = reinterpret_cast<seekdb_plugin_host_handle_t *>(&output);
    context.v1.emit_result = Output::emit;
    uint32_t srid = 4326;
    std::string text = "POINT(49 2)", option;
    const auto io = [&](const char *name, std::string data, bool constructor, bool with_option,
                        int expected = OB_SUCCESS) {
      output = {}; host.lookups = host.polls = 0;
      const char *types[] = {constructor ? "org.seekdb.gis.scalar.bytes" : "org.seekdb.gis.geometry",
          constructor ? "org.seekdb.gis.scalar.uint32" : "org.seekdb.gis.scalar.bytes",
          "org.seekdb.gis.scalar.bytes"};
      const uint32_t count = (constructor ? 2 : 1) + with_option;
      seekdb_plugin_sql_binding_v1_t bound{};
      CHECK(loader.resolve_native_function("org.seekdb.gis", name, types, count, bound) == OB_SUCCESS);
      seekdb_plugin_execution_value_v1_t args[3]{};
      for (unsigned i = 0; i < count; ++i) { args[i].struct_size = sizeof(args[i]); args[i].type_id = types[i]; }
      args[0].data = reinterpret_cast<const uint8_t *>(data.data()); args[0].data_size = data.size();
      if (constructor) { args[1].data = reinterpret_cast<const uint8_t *>(&srid); args[1].data_size = sizeof(srid); }
      if (with_option) { args[count - 1].data = reinterpret_cast<const uint8_t *>(option.data()); args[count - 1].data_size = option.size(); }
      const int ret = loader.execute_bound_function(bound, &context.v1, args, count);
      if (ret != expected) std::cerr << "GIS I/O " << name << " got " << ret << " expected " << expected << std::endl;
      CHECK(ret == expected && output.calls == (expected == OB_SUCCESS ? 1 : 0));
      return output.bytes;
    };
    const char *from = "org.seekdb.gis.function.st_geomfromtext";
    auto geometry = io(from, text, true, false);
    double x = 0, y = 0;
    std::memcpy(&x, geometry.data() + 10, sizeof(x)); std::memcpy(&y, geometry.data() + 18, sizeof(y));
    CHECK(x == 2 && y == 49 && host.lookups == 1);
    CHECK(io("org.seekdb.gis.function.st_astext", geometry, false, false) == text);
    option = "AXIS-ORDER = LONG-LAT";
    CHECK(io("org.seekdb.gis.function.st_astext", geometry, false, true) == "POINT(2 49)");
    const auto wkb = io("org.seekdb.gis.function.st_aswkb", geometry, false, true);
    CHECK(io("org.seekdb.gis.function.st_geomfromwkb", wkb, true, true) == geometry);
    auto ewkb = wkb; ewkb[4] |= 0x20; // EWKB SRID flag is not ordinary WKB.
    ewkb.insert(5, reinterpret_cast<const char *>(&srid), sizeof(srid));
    io("org.seekdb.gis.function.st_geomfromwkb", ewkb, true, true, OB_INVALID_ARGUMENT);
    srid = 70000004; // A local datum without towgs84 is valid for I/O.
    geometry = io(from, text, true, false);
    CHECK(io("org.seekdb.gis.function.st_astext", geometry, false, false) == text);
    srid = 70000001; // grad + south/west + prime meridian: I/O only swaps axes.
    geometry = io(from, "POINT(2 1)", true, false);
    CHECK(io("org.seekdb.gis.function.st_astext", geometry, false, true) == "POINT(1 2)");
    io(from, "POINT(101 1)", true, false, OB_INVALID_ARGUMENT);
    srid = 4326;
    for (const auto bad : {"bad=long-lat", "axis-order=", "axis-order=bad", "axis-order=long-lat extra"}) {
      option = bad; io(from, text, true, true, OB_INVALID_ARGUMENT);
    }
    option = std::string("axis-order=long-lat\0bad", 23);
    io(from, text, true, true, OB_INVALID_ARGUMENT);
    for (int fault = 1; fault <= 5; ++fault) {
      host.fault = fault;
      io(from, text, true, false, fault == 1 ? OB_TIMEOUT : OB_INVALID_ARGUMENT);
    }
    host.fault = 0;
    for (unsigned cancel : {1u, 2u}) { host.cancel_at = cancel; io(from, text, true, false, OB_TIMEOUT); }
    text = "LINESTRING(";
    for (unsigned i = 0; i < 2048; ++i) { if (i) text += ','; text += "49 2"; }
    text += ')'; host.cancel_at = 2; io(from, text, true, false, OB_TIMEOUT);
    CHECK(host.polls == 2);
    host.cancel_at = 0; io(from, text, true, false); CHECK(host.polls == 4);
    context.v1.struct_size = sizeof(context.v1); io(from, text, true, false, OB_STATE_NOT_MATCH);
    context.v1.struct_size = sizeof(context);
    srid = 99999; io(from, "GEOMETRYCOLLECTION EMPTY", true, false, OB_INVALID_ARGUMENT);
    CHECK(host.lookups == 1);
    srid = 0;
    std::string nested;
    for (unsigned i = 0; i < 66; ++i) nested += "GEOMETRYCOLLECTION(";
    nested += "POINT(1 2)"; nested.append(66, ')');
    io(from, nested, true, false, OB_INVALID_ARGUMENT);
    std::cout << "PASS: catalog I/O default/explicit axes, WKB controls, local datum, angular units, bad callbacks, unknown empty SRID and cancellation without partial output" << std::endl;
    const auto pg_io = [&](const char *name, std::string data, bool geometry_input,
                           const char *option_text = nullptr, bool null_option = false,
                           int expected = OB_SUCCESS) {
      output = {}; host.lookups = host.polls = 0;
      const char *types[] = {geometry_input ? "org.seekdb.gis.geometry" : "org.seekdb.gis.scalar.bytes",
                            "org.seekdb.gis.scalar.bytes"};
      const uint32_t count = 1 + (option_text != nullptr || null_option);
      seekdb_plugin_sql_binding_v1_t bound{};
      CHECK(loader.resolve_native_function("org.seekdb.gis", name, types, count, bound) == OB_SUCCESS);
      seekdb_plugin_execution_value_v1_t args[2]{};
      for (unsigned i = 0; i < count; ++i) { args[i].struct_size = sizeof(args[i]); args[i].type_id = types[i]; }
      args[0].data = reinterpret_cast<const uint8_t *>(data.data()); args[0].data_size = data.size();
      args[1].is_null = null_option;
      if (option_text) { args[1].data = reinterpret_cast<const uint8_t *>(option_text); args[1].data_size = std::strlen(option_text); }
      const int ret = loader.execute_bound_function(bound, &context.v1, args, count);
      if (ret != expected) std::cerr << "PG I/O " << name << " got " << ret << " expected " << expected << std::endl;
      CHECK(ret == expected && output.calls == (expected == OB_SUCCESS ? 1 : 0));
      return output.bytes;
    };
    const char *ewkt = "org.seekdb.gis.function.st_geomfromewkt";
    const char *from_ewkb = "org.seekdb.gis.function.st_geomfromewkb";
    const char *as_ewkb = "org.seekdb.gis.function.st_asewkb";
    const char *geog = "org.seekdb.gis.function.st_geogfromtext";
    const auto xy = [&](const std::string &g, double ex, double ey) {
      double x, y; CHECK(g.size() >= 26);
      std::memcpy(&x, g.data() + 10, 8); std::memcpy(&y, g.data() + 18, 8);
      CHECK(x == ex && y == ey);
    };
    geometry = pg_io(ewkt, " SRID=4326;POINT(2 49)", false); xy(geometry, 2, 49);
    const char *as_ewkt = "org.seekdb.gis.function.st_asewkt";
    CHECK(pg_io(as_ewkt, geometry, true) == "SRID=4326;POINT(2 49)");
    CHECK(host.lookups == 0);
    auto unknown_srid = geometry;
    const uint32_t unknown_id = 99999;
    std::memcpy(unknown_srid.data(), &unknown_id, sizeof(unknown_id));
    CHECK(pg_io(as_ewkt, unknown_srid, true) == "SRID=99999;POINT(2 49)");
    CHECK(host.lookups == 0);
    const uint32_t null_id = UINT32_MAX;
    std::memcpy(unknown_srid.data(), &null_id, sizeof(null_id));
    CHECK(pg_io(as_ewkt, unknown_srid, true) == "SRID=NULL;POINT(2 49)");
    CHECK(host.lookups == 0);
    const auto binary = pg_io(as_ewkb, geometry, true);
    CHECK(binary.size() == 25 && uint8_t(binary[4]) == 0x20 && uint8_t(binary[5]) == 0xe6 && uint8_t(binary[6]) == 0x10);
    CHECK(pg_io(from_ewkb, binary, false) == geometry);
    CHECK(pg_io(from_ewkb, binary, false, nullptr, true) == geometry);
    CHECK(pg_io(from_ewkb, binary, false, "axis-order=srid-defined") == geometry);
    auto swapped = pg_io(from_ewkb, binary, false, "axis-order=lat-long"); xy(swapped, 49, 2);
    auto big = binary; big[0] = 0;
    pg_io(from_ewkb, big, false, nullptr, false, OB_INVALID_ARGUMENT);
    auto measured = binary; measured[4] |= 0x40;
    pg_io(from_ewkb, measured, false, nullptr, false, OB_INVALID_ARGUMENT);
    pg_io(geog, "SRID=3857;POINT(2 49)", false, nullptr, false, OB_INVALID_ARGUMENT);
    pg_io(ewkt, "SRID=4326;POINT(2 100)", false, nullptr, false, OB_INVALID_ARGUMENT);
    xy(pg_io(geog, "POINT(190 100)", false), -170, 80);
    xy(pg_io(geog, "SRID=0;POINT(-190 -100)", false), 170, -80);
    xy(pg_io(geog, "POINT(-180 0)", false), -180, 0);
    xy(pg_io(geog, "POINT Z(-180 0 123)", false), 180, 0);
    xy(pg_io(geog, "POINT(180.00000000001 90.00000000001)", false), 180, 90);
    pg_io(ewkt, "SRID=4294967296;POINT(0 0)", false, nullptr, false, OB_INVALID_ARGUMENT);
    pg_io(ewkt, "SRID=99999;GEOMETRYCOLLECTION EMPTY", false, nullptr, false, OB_INVALID_ARGUMENT);
    CHECK(host.lookups == 1);
    const auto empty = pg_io(ewkt, "SRID=4326;GEOMETRYCOLLECTION EMPTY", false);
    CHECK(pg_io(from_ewkb, pg_io(as_ewkb, empty, true), false) == empty);
    for (unsigned cancel : {1u, 2u}) {
      host.cancel_at = cancel; pg_io(ewkt, "SRID=4326;POINT(2 49)", false, nullptr, false, OB_TIMEOUT);
      pg_io(as_ewkb, geometry, true, nullptr, false, OB_TIMEOUT);
    }
    host.cancel_at = 0;
    std::cout << "PASS: PG EWKT SRID, EWKB root flags/optional NULL, geography default/folding, original 2D/3D boundary policy and cancellation" << std::endl;
  }
}
}
