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
#include "sql/engine/expr/plugin_sql_context.h"
#include "observer/omt/ob_srs_service.h"
#include <thread>

namespace gis_srs_lookup_test {
using oceanbase::omt::ObSrsCacheSnapShot;
// Real catalog-row extraction and snapshot builder, controlled SQL transport.
class Row : public sqlclient::ObMySQLResult {
public:
  uint64_t srid = 70001000;
  int id_error = OB_SUCCESS, bounds_error = OB_ERR_NULL_VALUE;
  std::string wkt = gis_srs_test::geographic_wkt(), proj4 = "+proj=longlat +datum=WGS84";
  mutable ObArenaAllocator numbers;
  int64_t get_column_count() const override { return 11; }
  int close() override { return OB_SUCCESS; }
  int next() override { return OB_ITER_END; }
  int get_uint(const char *name, uint64_t &v) const override {
    if (std::strcmp(name, "srs_id") == 0) { v = srid; return id_error; }
    if (std::strcmp(name, "organization_coordsys_id") == 0) { v = 4326; return OB_SUCCESS; }
    return OB_ERR_COLUMN_NOT_FOUND;
  }
  int get_varchar(const char *name, ObString &v) const override {
    if (std::strcmp(name, "definition") == 0) v.assign_ptr(wkt.data(), wkt.size());
    else if (std::strcmp(name, "proj4text") == 0) v.assign_ptr(proj4.data(), proj4.size());
    else v = ObString::make_string("fixture");
    return OB_SUCCESS;
  }
  int inner_get_number(const char *, number::ObNumber &v, IAllocator &a) const override {
    return bounds_error == OB_SUCCESS ? v.from("12.5", a) : bounds_error;
  }
  int get_number(const char *, number::ObNumber &v) const override {
    return bounds_error == OB_SUCCESS ? v.from("12.5", numbers) : bounds_error;
  }
#define UNUSED_SRS_GETTER(method, type) \
  int method(int64_t, type &) const override { return OB_ERR_UNEXPECTED; } \
  int method(const char *, type &) const override { return OB_ERR_UNEXPECTED; }
  UNUSED_SRS_GETTER(get_int, int64_t)
  UNUSED_SRS_GETTER(get_datetime, int64_t)
  UNUSED_SRS_GETTER(get_date, int32_t)
  UNUSED_SRS_GETTER(get_time, int64_t)
  UNUSED_SRS_GETTER(get_year, uint8_t)
  UNUSED_SRS_GETTER(get_bool, bool)
  UNUSED_SRS_GETTER(get_float, float)
  UNUSED_SRS_GETTER(get_double, double)
  UNUSED_SRS_GETTER(get_type, ObObjMeta)
#undef UNUSED_SRS_GETTER
  int get_uint(int64_t, uint64_t &) const override { return OB_ERR_UNEXPECTED; }
  int get_varchar(int64_t, ObString &) const override { return OB_ERR_UNEXPECTED; }
  int get_timestamp(int64_t, const ObTimeZoneInfo *, int64_t &) const override { return OB_ERR_UNEXPECTED; }
  int get_timestamp(const char *, const ObTimeZoneInfo *, int64_t &) const override { return OB_ERR_UNEXPECTED; }
  int get_obj(int64_t, ObObj &, const ObTimeZoneInfo *, ObIAllocator *) const override { return OB_ERR_UNEXPECTED; }
  int get_obj(const char *, ObObj &) const override { return OB_ERR_UNEXPECTED; }
  int inner_get_number(int64_t, number::ObNumber &, IAllocator &) const override { return OB_ERR_UNEXPECTED; }
};

static void run(GisCatalogContext &catalog)
{
  ObSrsCacheSnapShot first, second;
  CHECK(first.init() == OB_SUCCESS && second.init() == OB_SUCCESS);
  Row row;
  const ObSrsItem *item = nullptr;
  const auto original = row.wkt;
  const int parsed = first.parse_srs_item(&row, item);
  std::cout << "raw SRS catalog parse: status=" << parsed << std::endl;
  CHECK(parsed == OB_SUCCESS && item && item->get_srid() == row.srid);
  CHECK(first.add_srs_item(row.srid, item) == OB_SUCCESS);
  const SrsDefinition *raw = nullptr;
  CHECK(first.get_srs_definition(row.srid, raw) == OB_SUCCESS && raw && std::isnan(raw->min_x));
  row.wkt.assign(row.wkt.size(), 'x'); row.proj4.assign(row.proj4.size(), 'x');
  CHECK(std::string(raw->definition.ptr(), raw->definition.length()) == original);
  CHECK(std::string(raw->proj4text.ptr(), raw->proj4text.length()) == "+proj=longlat +datum=WGS84");
  row.wkt = original; row.proj4 = "+proj=longlat +datum=WGS84";
  ++row.srid; row.bounds_error = OB_SUCCESS;
  CHECK(first.parse_srs_item(&row, item) == OB_SUCCESS);
  CHECK(first.get_srs_definition(row.srid, raw) == OB_SUCCESS && raw->min_x == 12.5 && raw->max_y == 12.5);
  ++row.srid; row.bounds_error = OB_TIMEOUT; item = nullptr;
  CHECK(first.parse_srs_item(&row, item) == OB_TIMEOUT && !item);
  CHECK(first.get_srs_definition(row.srid, raw) == OB_ERR_SRS_NOT_FOUND && !raw);
  row.bounds_error = OB_ERR_NULL_VALUE; row.id_error = OB_TIMEOUT;
  CHECK(first.parse_srs_item(&row, item) == OB_TIMEOUT && !item);
  CHECK(first.get_srs_definition(row.srid, raw) == OB_ERR_SRS_NOT_FOUND && !raw);
  CHECK(first.add_pg_reserved_srs_item(ObString::make_string(WORLD_MERCATOR_WKT), 999000) == OB_SUCCESS);
  CHECK(first.get_srs_definition(999000, raw) == OB_SUCCESS && raw && !raw->proj4text.empty());
  SrsDefinition changed = *raw;
  changed.definition = ObString::make_string("replacement-generation-definition");
  CHECK(second.add_srs_definition(changed) == OB_SUCCESS);
  CHECK(second.add_srs_definition(changed) != OB_SUCCESS); // No overwrite of published bytes.

  class Snapshot final : public ObISrsSnapshot {
  public:
    ObSrsCacheSnapShot &cache;
    int reads = 0, fault = 0;
    SrsDefinition bad;
    explicit Snapshot(ObSrsCacheSnapShot &c) : cache(c) {}
    void retain() override { cache.retain(); }
    void release() override { cache.release(); }
    int get_srs_item(uint64_t id, const ObSrsItem *&out) override { return cache.get_srs_item(id, out); }
    int get_srs_definition(uint64_t id, const SrsDefinition *&out) override {
      ++reads;
      if (fault == 1 && reads == 2) return OB_TIMEOUT;
      int ret = cache.get_srs_definition(id, out);
      if (ret == OB_SUCCESS && fault == 2) { bad = *out; ++bad.srid; out = &bad; }
      return ret;
    }
  } snapshot(first);
  class Provider final : public ObISrsProvider {
  public:
    ObISrsSnapshot *current;
    ObISrsSnapshot &replacement;
    int calls = 0, failure = OB_SUCCESS;
    Provider(ObISrsSnapshot &a, ObISrsSnapshot &b) : current(&a), replacement(b) {}
    int get_tenant_srs_guard(ObSrsCacheGuard &guard) override {
      ++calls;
      if (failure != OB_SUCCESS) return failure;
      guard.bind(*current); current = &replacement; return OB_SUCCESS;
    }
    int get_srs_bounds(uint64_t, const ObSrsItem *, const ObSrsBoundsItem *&) override { return OB_NOT_SUPPORTED; }
  } provider(snapshot, second);
  auto &execution = catalog.execution;
  auto *saved_provider = execution.get_srs_provider();
  execution.set_srs_provider(&provider);
  ObSQLSessionInfo::ExecCtxSessionRegister registration(*catalog.session, &execution);
  const auto saved_timeout = execution.get_physical_plan_ctx()->get_timeout_timestamp();
  for (int scenario = 0; scenario < 14; ++scenario) {
    provider.current = &snapshot; provider.calls = 0; provider.failure = scenario == 4 ? OB_TIMEOUT : OB_SUCCESS;
    snapshot.reads = 0; snapshot.fault = scenario == 12 ? 1 : scenario == 7 ? 2 : 0;
    execution.set_srs_provider(scenario == 13 ? nullptr : &provider);
    execution.get_physical_plan_ctx()->set_timeout_timestamp(scenario == 8 ? 1 : INT64_MAX);
    PluginSqlContext query(execution);
    seekdb_plugin_execution_context_v2_t context{}; context.v1.struct_size = sizeof(context); query.attach(context);
    CHECK(context.sql_api->struct_size >= sizeof(seekdb_plugin_sql_api_v5_t));
    CHECK(context.sql_api->spi_major == 1 && context.sql_api->spi_minor >= SEEKDB_PLUGIN_SQL_SRS_LOOKUP_MINOR);
    const auto &api = *reinterpret_cast<const seekdb_plugin_sql_api_v5_t *>(context.sql_api);
    CHECK(api.lookup_srs && api.v4.mutate_routine && api.v4.v3.lookup_routine && api.v4.v3.v2.poll_query);
    struct Consumer {
      const seekdb_plugin_sql_api_v5_t &api;
      seekdb_plugin_sql_context_handle_t *context;
      ObSrsCacheSnapShot &snapshot;
      ObExecContext &execution;
      int scenario, calls = 0;
      std::string definition;
      static seekdb_plugin_status_t SEEKDB_PLUGIN_CALL consume(void *ptr,
          const seekdb_plugin_srs_definition_v1_t *rows, uint32_t count) {
        auto &s = *static_cast<Consumer *>(ptr); ++s.calls;
        CHECK(count == 2 && s.snapshot.get_ref_count() == 1);
        for (uint32_t i = 0; i < count; ++i) {
          CHECK(rows[i].struct_size == sizeof(rows[i]) && rows[i].srid == 999000);
          CHECK(rows[i].definition && rows[i].definition_size == std::strlen(WORLD_MERCATOR_WKT));
          CHECK(std::string(rows[i].definition, rows[i].definition_size) == WORLD_MERCATOR_WKT);
          for (auto r : rows[i].reserved) CHECK(r == 0);
        }
        s.definition.assign(rows[0].definition, rows[0].definition_size);
        if (s.scenario == 6) {
          seekdb_plugin_query_status_v1_t status{}; status.struct_size = sizeof(status);
          CHECK(s.api.v4.v3.v2.poll_query(s.context, &status) != SEEKDB_PLUGIN_STATUS_OK);
          CHECK(status.database_error == OB_STATE_NOT_MATCH);
        }
        if (s.scenario == 9) s.execution.get_physical_plan_ctx()->set_timeout_timestamp(1);
        return s.scenario == 5 ? SEEKDB_PLUGIN_STATUS_NO_MEMORY : SEEKDB_PLUGIN_STATUS_OK;
      }
    } consumer{api, context.sql_context, first, execution, scenario, 0, {}};
    uint32_t ids[] = {999000, scenario == 1 ? 80000001u : scenario == 2 ? 0u : 999000u};
    uint8_t unaligned[sizeof(ids) + 1]; std::memcpy(unaligned + 1, ids, sizeof(ids));
    auto *input = reinterpret_cast<const uint32_t *>(unaligned + 1);
    seekdb_plugin_sql_result_v1_t result{}; result.struct_size = scenario == 11 ? 0 : sizeof(result);
    result.returned_rows = 99;
    if (scenario == 10) {
      std::thread wrong([&] {
        CHECK(api.lookup_srs(context.sql_context, input, 2, Consumer::consume, &consumer, &result) == SEEKDB_PLUGIN_STATUS_FAILED_PRECONDITION);
      }); wrong.join();
      CHECK(query.error() == OB_SUCCESS && result.returned_rows == 99 && provider.calls == 0);
    }
    const auto status = api.lookup_srs(context.sql_context, input, scenario == 3 ? 0 : 2,
                                     Consumer::consume, &consumer, &result);
    const int expected = scenario == 0 || scenario == 10 ? OB_SUCCESS : scenario == 1 ? OB_ERR_SRS_NOT_FOUND :
        scenario == 2 || scenario == 3 || scenario == 11 ? OB_INVALID_ARGUMENT :
        scenario == 5 ? OB_CANCELED : scenario == 6 ? OB_STATE_NOT_MATCH : scenario == 7 ? OB_ERR_UNEXPECTED :
        scenario == 13 ? OB_NOT_INIT : OB_TIMEOUT;
    CHECK((status == SEEKDB_PLUGIN_STATUS_OK) == (expected == OB_SUCCESS));
    CHECK(query.error() == expected);
    CHECK(first.get_ref_count() == 0 && second.get_ref_count() == 0);
    CHECK(consumer.calls == ((scenario == 0 || scenario == 5 || scenario == 6 || scenario == 9 || scenario == 10) ? 1 : 0));
    if (scenario != 11) CHECK(result.database_error == expected && result.returned_rows == (expected == OB_SUCCESS ? 2 : 0));
    if (expected == OB_SUCCESS) CHECK(consumer.definition == WORLD_MERCATOR_WKT && provider.calls == 1);
    else {
      const int calls = provider.calls;
      result.struct_size = sizeof(result);
      CHECK(api.lookup_srs(context.sql_context, input, 2, Consumer::consume, &consumer, &result) != SEEKDB_PLUGIN_STATUS_OK);
      CHECK(result.database_error == expected && provider.calls == calls);
    }
  }
  execution.set_srs_provider(saved_provider);
  execution.get_physical_plan_ctx()->set_timeout_timestamp(saved_timeout);
  std::cout << "PASS: raw SRS catalog row ownership, single-snapshot batch SPI, refresh interleaving, sticky errors and thread/deadline fences" << std::endl;
}
} // namespace gis_srs_lookup_test
