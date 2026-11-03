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
#include "observer/omt/srs_snapshot_lifecycle.h"
#include "common/mysqlclient/ob_isql_result_handler.h"
#include <functional>

namespace gis_srs_lifecycle_test {
static void publication()
{
  struct Snapshot {
    int references = 0, disposals = 0, reads = 0;
    int get_ref_count() { CHECK(disposals == 0); ++reads; return references; }
  } a, b, c;
  struct Retired {
    std::vector<Snapshot *> items;
    bool fail = false, release_during_enqueue = false;
    int push_back(Snapshot *s) {
      if (release_during_enqueue) s->references = 0;
      if (fail) return OB_ALLOCATE_MEMORY_FAILED;
      items.push_back(s); return OB_SUCCESS;
    }
    int64_t size() const { return items.size(); }
    Snapshot *operator[](int64_t i) { return items.at(i); }
    void remove(int64_t i) { items.erase(items.begin() + i); }
  } retired;
  const auto dispose = [&](Snapshot *s) {
    CHECK(s->references == 0 && s->disposals == 0);
    CHECK(std::find(retired.items.begin(), retired.items.end(), s) == retired.items.end());
    ++s->disposals;
  };
  Snapshot *current = &a, *candidate = &b;
  a.references = 1; retired.release_during_enqueue = true;
  CHECK(oceanbase::omt::publish_srs_snapshot(current, candidate, retired, dispose) == OB_SUCCESS);
  CHECK(current == &b && candidate == nullptr && a.reads == 1 && a.disposals == 0 && retired.size() == 1);
  oceanbase::omt::collect_srs_snapshots(retired, dispose);
  CHECK(retired.size() == 0 && a.disposals == 1);
  oceanbase::omt::collect_srs_snapshots(retired, dispose);
  CHECK(a.disposals == 1); // No dangling retirement entry / double disposal.
  b.references = 1; candidate = &c; retired.fail = true; retired.release_during_enqueue = false;
  CHECK(oceanbase::omt::publish_srs_snapshot(current, candidate, retired, dispose) == OB_ALLOCATE_MEMORY_FAILED);
  CHECK(current == &b && candidate == nullptr && b.disposals == 0 && c.disposals == 1 && retired.size() == 0);
  b.references = 0;
  Snapshot d;
  candidate = &d; retired.fail = false;
  CHECK(oceanbase::omt::publish_srs_snapshot(current, candidate, retired, dispose) == OB_SUCCESS);
  CHECK(current == &d && b.disposals == 1 && retired.size() == 0);
  dispose(current); current = nullptr;
  Snapshot e, f;
  current = &e; candidate = &f; e.references = 1;
  CHECK(oceanbase::omt::publish_srs_snapshot(current, candidate, retired, dispose) == OB_SUCCESS);
  oceanbase::omt::collect_srs_snapshots(retired, dispose);
  CHECK(e.disposals == 0 && retired.size() == 1);
  e.references = 0;
  oceanbase::omt::collect_srs_snapshots(retired, dispose);
  CHECK(e.disposals == 1 && retired.size() == 0);
  dispose(current);
  std::cout << "PASS: production SRS publication helper, release/enqueue interleaving, queue OOM ownership and exactly-once reclamation" << std::endl;
}

class Proxy final : public ObMySQLProxy {
public:
  int reads = 0, status = OB_SUCCESS, row_error = OB_SUCCESS;
  int64_t catalog_count = 5152;
  bool empty_rows = false;
  std::string wkt = gis_srs_test::geographic_wkt();
  std::function<void()> during_read;
  int read(ReadResult &result, const char *sql, int32_t) override {
    ++reads;
    CHECK(std::strstr(sql, "FROM oceanbase.__all_spatial_reference_systems") != nullptr);
    if (during_read) during_read();
    if (status != OB_SUCCESS) return status;
    bool count = std::strstr(sql, "count(*)") != nullptr;
    Handler *handler = nullptr;
    return result.create_handler(handler, *this, count);
  }
private:
  class Result final : public gis_srs_lookup_test::Row {
  public:
    Proxy &owner;
    bool count;
    int index = -1;
    Result(Proxy &p, bool c) : owner(p), count(c) { srid = 4326; wkt = p.wkt; }
    int next() override {
      ++index;
      if (!count && owner.empty_rows) return OB_ITER_END;
      if (index == 0) return OB_SUCCESS;
      return count ? OB_ITER_END : owner.row_error == OB_SUCCESS ? OB_ITER_END : owner.row_error;
    }
    int get_int(const char *name, int64_t &out) const override {
      if (!count || std::strcmp(name, "srs_cnt")) return OB_ERR_COLUMN_NOT_FOUND;
      out = owner.catalog_count; return OB_SUCCESS;
    }
  };
  class Handler final : public sqlclient::ObISQLResultHandler {
  public:
    Result row;
    Handler(Proxy &p, bool &c) : row(p, c) {}
    sqlclient::ObMySQLResult *mysql_result() override { return &row; }
  };
};

static std::string definition(const ObSrsCacheGuard &guard)
{
  const SrsDefinition *raw = nullptr;
  CHECK(guard.get_srs_definition(4326, raw) == OB_SUCCESS && raw);
  return std::string(raw->definition.ptr(), raw->definition.length());
}

static void run()
{
  publication();
  oceanbase::omt::ObSrsService service;
  Proxy proxy, unused;
  {
    ObSrsCacheGuard guard;
    CHECK(service.get_tenant_srs_guard(guard) == OB_NOT_INIT && guard.empty());
  }
  CHECK(service.init(proxy) == OB_SUCCESS);
  CHECK(service.init(unused) == OB_INIT_TWICE);
  const auto original = proxy.wkt;
  {
    ObSrsCacheGuard old;
    CHECK(service.get_tenant_srs_guard(old) == OB_SUCCESS && proxy.reads == 2);
    CHECK(definition(old) == original);
    const SrsDefinition *reserved = nullptr;
    CHECK(old.get_srs_definition(999031, reserved) == OB_SUCCESS && reserved && !reserved->proj4text.empty());
    const int reads = proxy.reads;
    CHECK(service.get_tenant_srs_guard(old) == OB_SUCCESS && proxy.reads == reads);
    { ObSrsCacheGuard hit; CHECK(service.get_tenant_srs_guard(hit) == OB_SUCCESS && proxy.reads == reads); }

    service.mark_stale(); proxy.status = OB_TIMEOUT;
    { ObSrsCacheGuard failed; CHECK(service.get_tenant_srs_guard(failed) == OB_TIMEOUT && failed.empty()); }
    const int failure_reads = proxy.reads;
    { ObSrsCacheGuard failed; CHECK(service.get_tenant_srs_guard(failed) == OB_TIMEOUT && failed.empty()); }
    CHECK(proxy.reads == failure_reads + 1 && definition(old) == original);
    proxy.status = OB_SUCCESS;
    proxy.wkt.replace(proxy.wkt.find("WGS 84"), 6, "refreshed");
    {
      ObSrsCacheGuard fresh;
      CHECK(service.get_tenant_srs_guard(fresh) == OB_SUCCESS);
      CHECK(definition(fresh) == proxy.wkt && definition(old) == original);
      // Invalidate on a second thread while refresh holds its acquisition lock.
      service.mark_stale();
      bool invalidated = false;
      proxy.during_read = [&] {
        if (!invalidated) { invalidated = true; std::thread mark([&] { service.mark_stale(); }); mark.join(); }
      };
      { ObSrsCacheGuard marked; CHECK(service.get_tenant_srs_guard(marked) == OB_SUCCESS); }
      CHECK(invalidated);
      proxy.during_read = {};
      const int marked_reads = proxy.reads;
      { ObSrsCacheGuard retry; CHECK(service.get_tenant_srs_guard(retry) == OB_SUCCESS); }
      CHECK(proxy.reads == marked_reads + 2); // Mark during refresh was not lost.
      { ObSrsCacheGuard hit; CHECK(service.get_tenant_srs_guard(hit) == OB_SUCCESS); }
      CHECK(proxy.reads == marked_reads + 2);

      service.mark_stale(); proxy.row_error = OB_TIMEOUT;
      { ObSrsCacheGuard failed; CHECK(service.get_tenant_srs_guard(failed) == OB_TIMEOUT && failed.empty()); }
      CHECK(definition(fresh) == proxy.wkt && definition(old) == original);
      proxy.row_error = OB_SUCCESS;
      { ObSrsCacheGuard retry; CHECK(service.get_tenant_srs_guard(retry) == OB_SUCCESS); }

      service.mark_stale(); proxy.catalog_count = 1;
      { ObSrsCacheGuard failed; CHECK(service.get_tenant_srs_guard(failed) == OB_ERR_SRS_EMPTY && failed.empty()); }
      proxy.catalog_count = 5152;
      { ObSrsCacheGuard retry; CHECK(service.get_tenant_srs_guard(retry) == OB_SUCCESS); }
      service.mark_stale(); proxy.empty_rows = true;
      { ObSrsCacheGuard failed; CHECK(service.get_tenant_srs_guard(failed) == OB_ERR_SRS_EMPTY && failed.empty()); }
      proxy.empty_rows = false;
      { ObSrsCacheGuard retry; CHECK(service.get_tenant_srs_guard(retry) == OB_SUCCESS); }
    }
    CHECK(definition(old) == original); // Old data survives many refreshes/failures.
  }
  service.mark_stale();
  { ObSrsCacheGuard collect; CHECK(service.get_tenant_srs_guard(collect) == OB_SUCCESS); }
  CHECK(unused.reads == 0);
  // Provider shutdown is quiescent: all guards have already been destroyed.
  service.destroy(); service.destroy();
  { ObSrsCacheGuard closed; CHECK(service.get_tenant_srs_guard(closed) == OB_NOT_INIT); }
  CHECK(service.init(proxy) == OB_SUCCESS);
  { ObSrsCacheGuard reopened; CHECK(service.get_tenant_srs_guard(reopened) == OB_SUCCESS); }
  // Destructor performs a final idempotent destroy.
  std::cout << "PASS: actual SRS provider refresh, pinned old/raw/reserved data, retry after read/row/import failures, concurrent invalidation, qualified catalog and destroy/reinit" << std::endl;
}
} // namespace gis_srs_lifecycle_test

