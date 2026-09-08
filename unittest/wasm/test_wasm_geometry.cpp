// Copyright (c) 2026 OceanBase.
// SPDX-License-Identifier: Apache-2.0
#include "share/geo/ob_s2adapter.h"
#include "lib/resource/achunk_mgr.h"
#include "s2/s2cell_union.h"
#include <cassert>
#include <cmath>
#include <cstdio>
#include <pthread.h>

using namespace oceanbase;
using namespace oceanbase::common;

static void *check_geometry(void *)
{
  for (int face = 0; face < 6; ++face) {
    const S2CellId parent = S2CellId::FromFace(face);
    assert(parent.id() == (uint64_t(2 * face + 1) << 60));
    assert(parent.is_valid() && parent.level() == 0);
    S2CellUnion children;
    std::vector<S2CellId> ids;
    for (int child = 0; child < 4; ++child) {
      const auto id = parent.child(child);
      assert(parent.contains(id) && id.parent() == parent);
      ids.push_back(id);
    }
    children.Init(ids);
    assert(children.num_cells() == 1 && children.cell_id(0) == parent);
  }
  const auto origin = S2LatLng::FromDegrees(0, 0);
  assert(S2CellId(origin).id() == UINT64_C(0x1000000000000001));
  for (int latitude = -80; latitude <= 80; latitude += 20) {
    for (int longitude = -170; longitude <= 170; longitude += 20) {
      const auto point = S2LatLng::FromDegrees(latitude, longitude);
      const S2CellId leaf(point);
      assert(leaf.is_leaf() && S2Cell(leaf).Contains(point.ToPoint()));
      const auto center = S2LatLng(leaf.ToPoint());
      assert(point.GetDistance(center).radians() < 1e-8);
    }
  }
  const auto relation = ObDomainOpType::T_GEO_INTERSECTS;
  ObSpatialMBR dateline(170, -170, -10, 10, relation);
  dateline.is_geog_ = true;
  ObSpatialMBR overlap(175, 179, -5, 5, relation);
  overlap.is_geog_ = true;
  ObSpatialMBR separate(-5, 5, -5, 5, relation);
  separate.is_geog_ = true;
  bool filtered = true;
  assert(dateline.filter(overlap, relation, filtered) == OB_SUCCESS && !filtered);
  assert(dateline.filter(separate, relation, filtered) == OB_SUCCESS && filtered);
  S2LatLngRect rect;
  assert(dateline.generate_latlng_rect(rect) == OB_SUCCESS);
  assert(rect.Contains(S2LatLng::FromDegrees(0, 180)));
  assert(!rect.Contains(origin));
  S2RegionCoverer::Options options;
  options.set_max_cells(24);
  options.set_max_level(12);
  S2RegionCoverer coverer(options);
  const S2CellUnion covering = coverer.GetCovering(rect);
  assert(!covering.empty());
  for (int lat = -9; lat <= 9; lat += 3) {
    for (int lng = 171; lng <= 189; lng += 3) {
      auto point = S2LatLng::FromDegrees(lat, lng > 180 ? lng - 360 : lng).ToPoint();
      assert(covering.Contains(point));
    }
  }
  // Spherical polygon predicates exercise the robust geometry implementation.
  std::vector<S2Point> vertices;
  for (const auto &latlng : {S2LatLng::FromDegrees(-10, -10), S2LatLng::FromDegrees(-10, 10),
                            S2LatLng::FromDegrees(10, 10), S2LatLng::FromDegrees(10, -10)}) {
    vertices.push_back(latlng.ToPoint());
  }
  S2Loop loop(vertices);
  loop.Normalize();
  assert(loop.IsValid() && loop.Contains(origin.ToPoint()));
  assert(!loop.Contains(S2LatLng::FromDegrees(45, 45).ToPoint()));
  return nullptr;
}

int main()
{
  lib::AChunkMgr::instance().set_limit(64 * 1024 * 1024);
  lib::AChunkMgr::instance().set_hard_limit(64 * 1024 * 1024);
  pthread_t workers[2];
  for (auto &worker : workers) assert(pthread_create(&worker, nullptr, check_geometry, nullptr) == 0);
  check_geometry(nullptr);
  for (auto &worker : workers) assert(pthread_join(worker, nullptr) == 0);
  puts("PASS: seekdb spatial MBR filtering, S2 cell IDs, dateline covering and spherical predicates");
}
