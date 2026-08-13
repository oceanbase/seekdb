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

#ifndef OCEANBASE_SHARE_OB_TABLET_AUTOINCREMENT_SERVICE_H_
#define OCEANBASE_SHARE_OB_TABLET_AUTOINCREMENT_SERVICE_H_

#include "lib/hash/ob_hashmap.h"
#include "lib/hash/ob_link_hashmap.h"
#include "lib/allocator/ob_small_allocator.h"
#include "share/autoincrement/ob_i_tablet_autoincrement_admin.h"
#include "share/ob_tablet_autoincrement_param.h"
#include "share/autoincrement/ob_i_tablet_autoincrement_service.h"

namespace oceanbase
{
namespace share
{
namespace schema
{
class ObSimpleTableSchemaV2;
}

struct ObTabletCacheNode
{
public:
  ObTabletCacheNode() : cache_start_(0), cache_end_(0) {}

  void reset() { cache_start_ = 0; cache_end_ = 0; }
  bool is_valid() { return cache_end_ != 0; }

  TO_STRING_KV(K_(cache_start),
               K_(cache_end));
public:
  uint64_t cache_start_;
  uint64_t cache_end_;
};

class ObTabletAutoincMgr: public common::LinkHashValue<ObTabletAutoincKey>
{
public:
  ObTabletAutoincMgr()
    : mutex_(ObLatchIds::TABLET_AUTO_INCREMENT_MGR_LOCK),
      tablet_id_(),
      next_value_(1),
      last_refresh_ts_(common::ObTimeUtility::current_time()),
      cache_size_(DEFAULT_TABLET_INCREMENT_CACHE_SIZE),
      is_inited_(false)
  {}
  virtual ~ObTabletAutoincMgr()
  {
    destroy();
  }

  int init(const common::ObTabletID &tablet_id, const int64_t cache_size);
  void reset();
  int clear();
  int fetch_interval(const ObTabletAutoincParam &param, ObTabletCacheInterval &interval);
  int fetch_interval_without_cache(const ObTabletAutoincParam &param, ObTabletCacheInterval &interval);
  void destroy() {}

  TO_STRING_KV(K_(tablet_id),
               K_(next_value),
               K_(last_refresh_ts),
               K_(cache_size),
               K_(curr_node),
               K_(prefetch_node),
               K_(is_inited));
private:
  int set_interval(const ObTabletAutoincParam &param, ObTabletCacheInterval &interval);
  int fetch_new_range(const ObTabletAutoincParam &param,
                      const common::ObTabletID &tablet_id,
                      ObTabletCacheNode &node);
  bool prefetch_condition()
  {
    return !prefetch_node_.is_valid() &&
        (next_value_ - curr_node_.cache_start_) * PREFETCH_THRESHOLD > curr_node_.cache_end_ - curr_node_.cache_start_;
  }
  bool is_retryable(int ret)
  {
    return OB_NOT_MASTER == ret || OB_NOT_INIT == ret || OB_TIMEOUT == ret || OB_EAGAIN == ret || OB_LS_NOT_EXIST == ret || OB_SERVER_RUNTIME_NOT_READY == ret || OB_LS_LOCATION_NOT_EXIST == ret;
  }
  bool is_block_renew_location(int ret)
  {
    return OB_LOCATION_LEADER_NOT_EXIST == ret || OB_LS_LOCATION_LEADER_NOT_EXIST == ret || OB_NO_READABLE_REPLICA == ret
      || OB_NOT_MASTER == ret || OB_RS_NOT_MASTER == ret || OB_RS_SHUTDOWN == ret || OB_PARTITION_NOT_EXIST == ret || OB_LOCATION_NOT_EXIST == ret
      || OB_PARTITION_IS_STOPPED == ret || OB_SERVER_IS_INIT == ret || OB_SERVER_IS_STOPPING == ret || OB_SERVER_RUNTIME_NOT_READY == ret
      || OB_TRANS_RPC_TIMEOUT == ret || OB_TRANS_STMT_NEED_RETRY == ret
      || OB_LS_NOT_EXIST == ret || OB_TABLET_NOT_EXIST == ret || OB_LS_LOCATION_NOT_EXIST == ret || OB_PARTITION_IS_BLOCKED == ret || OB_MAPPING_BETWEEN_TABLET_AND_LS_NOT_EXIST == ret
      || OB_GET_LOCATION_TIME_OUT == ret;
  }
private:
  static const int64_t PREFETCH_THRESHOLD = 4;
  static const int64_t RETRY_INTERVAL = 100 * 1000L; // 100ms
  lib::ObMutex mutex_;
  common::ObTabletID tablet_id_;
  uint64_t next_value_;
  int64_t  last_refresh_ts_; // use this to determine active tablet
  int64_t cache_size_;
  ObTabletCacheNode curr_node_;
  ObTabletCacheNode prefetch_node_;
  bool is_inited_;
};

class ObTabletAutoincMgrAllocHandle
{
public:
  typedef LinkHashNode<ObTabletAutoincKey> TabletAutoincNode;
  typedef ObTabletAutoincMgr TabletAutoincMgr;
  static ObTabletAutoincMgr* alloc_value() { return op_reclaim_alloc(TabletAutoincMgr); }
  static void free_value(ObTabletAutoincMgr* val) { op_reclaim_free(val); val = nullptr; }
  static TabletAutoincNode* alloc_node(ObTabletAutoincMgr* val) { UNUSED(val); return op_reclaim_alloc(TabletAutoincNode); }
  static void free_node(TabletAutoincNode* node) { op_reclaim_free(node); node = nullptr; }
};

class ObTabletAutoincrementService
    : public ObITabletAutoincrementService,
      public ObITabletAutoincrementAdmin
{
public:
  static ObTabletAutoincrementService &get_instance();
  static const int64_t DEFAULT_CACHE_SIZE = 10000;
  static const int64_t LOB_CACHE_SIZE = 100000;
  int init();
  void destroy();
  int get_tablet_cache_interval(ObTabletCacheInterval &interval);
  int get_autoinc_seq(const common::ObTabletID &tablet_id, uint64_t &autoinc_seq, const int64_t cache_size=ObTabletAutoincrementService::DEFAULT_CACHE_SIZE);
  int next_value(
      const common::ObTabletID &tablet_id,
      uint64_t &value) override
  {
    return get_autoinc_seq(tablet_id, value);
  }
  int copy_sequences_for_fork(
      const common::ObIArray<common::ObTabletID> &source_tablet_ids,
      const common::ObIArray<common::ObTabletID> &destination_tablet_ids,
      common::ObMySQLTransaction &trans) override;
  int collect_table_cache_invalidation(
      schema::ObSchemaGetterGuard &schema_guard,
      const schema::ObTableSchema &table_schema,
      common::ObIArray<common::ObTabletID> &cache_tablet_ids) override;
  int collect_table_cache_invalidation(
      schema::ObSchemaGuardWrapper &schema_guard,
      const schema::ObTableSchema &table_schema,
      common::ObIArray<common::ObTabletID> &cache_tablet_ids) override;
  int collect_database_cache_invalidation(
      const schema::ObDatabaseSchema &database_schema,
      common::ObIArray<common::ObTabletID> &cache_tablet_ids) override;
  int invalidate_caches(
      const common::ObIArray<common::ObTabletID> &cache_tablet_ids) override;
  int read_migration_sequences(
      const common::ObIArray<ObTabletAutoincSeqCopyParam> &request_params,
      common::ObIArray<ObTabletAutoincSeqCopyParam> &result_params) override;
  int write_migration_sequences(
      const common::ObIArray<ObTabletAutoincSeqCopyParam> &request_params,
      common::ObIArray<ObTabletAutoincSeqCopyParam> &result_params) override;
  int clear_tablet_autoinc_seq_cache(const common::ObIArray<common::ObTabletID> &tablet_ids, const int64_t abs_timeout_us);
private:
  int collect_single_table_cache_invalidation_(
      const schema::ObSimpleTableSchemaV2 &table_schema,
      common::ObIArray<common::ObTabletID> &cache_tablet_ids);
  int acquire_mgr(const common::ObTabletID &tablet_id, const int64_t init_cache_size, ObTabletAutoincMgr *&autoinc_mgr);
  void release_mgr(ObTabletAutoincMgr *autoinc_mgr);

  ObTabletAutoincrementService();
  ~ObTabletAutoincrementService();

private:
  typedef common::ObLinkHashMap<ObTabletAutoincKey, ObTabletAutoincMgr, ObTabletAutoincMgrAllocHandle> TabletAutoincMgrMap;
  const static int INIT_NODE_MUTEX_NUM = 1009L;
  bool is_inited_;
  common::ObSmallAllocator node_allocator_;
  TabletAutoincMgrMap tablet_autoinc_mgr_map_;
  lib::ObMutex init_node_mutexs_[INIT_NODE_MUTEX_NUM];
};


} // end namespace share
} // end namespace oceanbase
#endif
