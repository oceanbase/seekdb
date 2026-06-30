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

#ifndef OCEANBASE_TENANT_SRS_H_
#define OCEANBASE_TENANT_SRS_H_

#include "share/ob_define.h"
#include "share/rc/ob_module_provider.h"
#include "share/rc/ob_tenant_base.h"
#include "common/mysqlclient/ob_mysql_proxy.h"
#include "lib/hash/ob_pointer_hashmap.h"
#include "lib/container/ob_vector.h"
#include "lib/allocator/page_arena.h"
#include "share/geo/ob_srs_wkt_parser.h"
#include "share/geo/ob_srs_info.h"
#include "lib/lock/ob_mutex.h"

namespace oceanbase
{

namespace common
{

namespace sqlclient
{
class ObMySQLResult;
}
}

namespace omt
{

class ObSrsCacheSnapShot
{
public:
  static const uint32_t SRS_ITEM_BUCKET_NUM = 6144;
  explicit ObSrsCacheSnapShot()
    : allocator_("SrsSnapShot", OB_MALLOC_NORMAL_BLOCK_SIZE), ref_count_(0) {}
  virtual ~ObSrsCacheSnapShot() { srs_item_map_.destroy(); }
  int init() { return srs_item_map_.create(SRS_ITEM_BUCKET_NUM, "SrsSnapShot", "SrsSnapShot"); }
  int add_srs_item(uint64_t srid, const common::ObSrsItem* srs_item) { return srs_item_map_.set_refactored(srid, srs_item); }
  int get_srs_item(uint64_t srid, const common::ObSrsItem *&srs_item);
  void dec_ref_count() { ATOMIC_DEC(&ref_count_); }
  void inc_ref_count() { ATOMIC_INC(&ref_count_); }
  int64_t get_ref_count() { return ATOMIC_LOAD64(&ref_count_); }
  int64_t get_srs_count() { return srs_item_map_.size(); }
  int parse_srs_item(common::sqlclient::ObMySQLResult *result, const common::ObSrsItem *&srs_item);
  int add_pg_reserved_srs_item(const common::ObString &pg_wkt, const uint32_t srs_id);

private:
  common::ObArenaAllocator allocator_;
  volatile int64_t ref_count_;
  common::hash::ObHashMap<uint64_t, const common::ObSrsItem*> srs_item_map_;

  int extract_bounds_numberic(common::sqlclient::ObMySQLResult *result, const char *field_name, double &value);

  DISALLOW_COPY_AND_ASSIGN(ObSrsCacheSnapShot);
};

class ObSrsCacheGuard
{
public:
  explicit ObSrsCacheGuard() : srs_cache_(nullptr) {}
  virtual ~ObSrsCacheGuard();
  int get_srs_item(uint64_t srs_id, const common::ObSrsItem *&srs_item);
  void set_srs_snapshot(ObSrsCacheSnapShot *srs_cache) { srs_cache_ =  srs_cache; }
  inline bool empty() { return srs_cache_ == nullptr; }
private:
  ObSrsCacheSnapShot *srs_cache_;
};


class ObTenantSrs
{
public:
  static const int64_t DEFAULT_PAGE_SIZE = 8192L;
  static const uint32_t USER_SRID_MIN = 70000000;
  static const uint32_t USER_SRID_MAX = 2000000000;
  static const uint32_t MAX_WKT_LEN = 4096;

  explicit ObTenantSrs()
    : alloc_("TenantSrs"), sql_proxy_(nullptr), inited_(false),
      last_sys_snapshot_(nullptr),
      srs_old_snapshots_(&mode_arena_, common::ObModIds::OB_MODULE_PAGE_ALLOCATOR),
      srs_stale_(true), infinite_plane_() {}
  virtual ~ObTenantSrs() {};
  int init();
  int get_tenant_srs_guard(ObSrsCacheGuard &srs_guard);
  int get_srs_bounds(uint64_t srid, const ObSrsItem *srs_item, const ObSrsBoundsItem *&bounds_item);
  static int mtl_init(ObTenantSrs* &tenant_srs);
  int start();
  void stop();
  void wait();
  void destroy();
  void mark_stale() { ATOMIC_STORE(&srs_stale_, true); }

private:
  typedef common::PageArena<ObSrsCacheSnapShot*, common::ModulePageAllocator> ObCGeoModuleArena;
  typedef common::ObVector<ObSrsCacheSnapShot*, ObCGeoModuleArena> ObSrsSnapshotVector;

  int fetch_all_srs(ObSrsCacheSnapShot *&srs_snapshot);
  int refresh_sys_srs();
  int generate_pg_reserved_srs(ObSrsCacheSnapShot *&srs_snapshot);
  void recycle_old_snapshots();

  common::ObFIFOAllocator allocator_;
  common::ObArenaAllocator alloc_;
  common::ObMySQLProxy *sql_proxy_;
  bool inited_;
  common::ModulePageAllocator page_allocator_;
  ObCGeoModuleArena mode_arena_;
  ObSrsCacheSnapShot* last_sys_snapshot_;
  ObSrsSnapshotVector srs_old_snapshots_;
  bool srs_stale_;
  lib::ObMutex srs_load_lock_;
  common::ObSrsBoundsItem infinite_plane_;
  DISALLOW_COPY_AND_ASSIGN(ObTenantSrs);
};

#define OTSRS_MGR (share::g_mp->tenant_srs())

}  // namespace omt
}  // namespace oceanbase

#endif
