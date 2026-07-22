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

#ifndef OCEANBASE_SHARE_OB_AUTOINCREMENT_SERVICE_H_
#define OCEANBASE_SHARE_OB_AUTOINCREMENT_SERVICE_H_


#include "lib/hash/ob_hashmap.h"
#include "lib/hash/ob_link_hashmap.h"
#include "lib/allocator/ob_small_allocator.h"
#include "common/mysqlclient/ob_mysql_proxy.h"
#include "share/ob_autoincrement_param.h"

namespace oceanbase
{
namespace share
{
namespace schema
{
class ObTableSchema;
}
static const int64_t  FETCH_SEQ_NUM_ONCE = 1000;
static const uint64_t AUTO_INC_DEFAULT_NB_MAX_BITS = 16;                                  // from MySQL
static const uint64_t AUTO_INC_DEFAULT_NB_MAX = (1 << AUTO_INC_DEFAULT_NB_MAX_BITS) - 1;  // from MySQL
static const uint64_t AUTO_INC_DEFAULT_NB_ROWS = 1;                                       // from MySQL


struct CacheNode
{
  CacheNode() : cache_start_(0), cache_end_(0) {}

  void reset() { cache_start_ = 0; cache_end_ = 0; }

  TO_STRING_KV(K_(cache_start),
               K_(cache_end));

  // combine two cache node if they are valid and continuous
  // otherwise use new_node if it is valid
  int combine_cache_node(CacheNode &new_node);

  uint64_t cache_start_; // inclusive
  uint64_t cache_end_; // inclusive!
  //uint64_t cache_count_;
};

struct CacheHandle
{
  CacheHandle()
    : prefetch_start_(0),
      prefetch_end_(0),
      next_value_(0),
      offset_(0),
      increment_(0),
      max_value_(0),
      last_value_to_confirm_(0),
      last_row_dup_flag_(false)

  {}

  TO_STRING_KV(K_(prefetch_start),
               K_(prefetch_end),
               K_(next_value),
               K_(offset),
               K_(increment),
               K_(max_value));

  // CacheHandle represent value acuquision for one query.
  // when a insert stmt has multiple rows,
  // prefetch_start_ represent the first value for first row, prefetch_end_ for the last row
  uint64_t prefetch_start_;
  uint64_t prefetch_end_;
  //uint64_t prefetch_count_;
  uint64_t next_value_;
  uint64_t offset_;
  uint64_t increment_;
  uint64_t max_value_;
  uint64_t last_value_to_confirm_;
  bool     last_row_dup_flag_;

  int next_value(uint64_t &next_value);
  bool in_range(const uint64_t value) const
  { return ((value >= prefetch_start_) && (value <= prefetch_end_)); }

};

struct TableNode: public common::LinkHashValue<AutoincKey>
{
  TableNode()
    : alloc_mutex_(common::ObLatchIds::AUTO_INCREMENT_ALLOC_LOCK),
      table_id_(0),
      next_value_(0),
      local_sync_(0),
      max_value_(0),
      prefetching_(false),
      curr_node_state_is_pending_(false),
      autoinc_version_(OB_INVALID_VERSION)
  {}
  virtual ~TableNode()
  {
    destroy();
  }
  int init(int64_t autoinc_table_part_num);

  TO_STRING_KV(KT_(table_id),
               K_(next_value),
               K_(local_sync),
               K_(max_value),
               K_(curr_node),
               K_(prefetch_node),
               K_(prefetching),
               K_(autoinc_version));

  int alloc_handle(common::ObSmallAllocator &allocator,
                   const uint64_t offset,
                   const uint64_t increment,
                   const uint64_t desired_count,
                   const uint64_t max_value,
                   CacheHandle *&handle,
                   const bool is_retry_alloc = false);

  bool prefetch_condition()
  {
    return false;
  }
  void destroy()
  {
  }
  lib::ObMutex alloc_mutex_;
  uint64_t table_id_;
  uint64_t next_value_;
  // The persisted high watermark already observed by this process. Values less
  // than or equal to local_sync_ do not need to be persisted again.
  uint64_t local_sync_;
  // Type-specific upper bound used when exposing the logical next value.
  uint64_t max_value_;
  CacheNode curr_node_;
  CacheNode prefetch_node_;
  bool prefetching_;
  // we are not sure if curr_node is avaliable.
  // it will become avaliable again after fetch a new node
  // and combine them together.
  // ref: 
  bool curr_node_state_is_pending_;
  int64_t  autoinc_version_;
};

// atomic update if greater than origin value
template<typename T>
inline void atomic_update(T &v, T new_v)
{
  while (true) {
    T cur_v = v;
    if (new_v <= cur_v) {
      break;
    } else if (ATOMIC_BCAS(&v, cur_v, new_v)) {
      break;
    }
  }
}

class ObAutoIncInnerTableProxy
{
public:
  ObAutoIncInnerTableProxy() : mysql_proxy_(nullptr) {}
  ~ObAutoIncInnerTableProxy() {}
  int init(common::ObMySQLProxy *mysql_proxy)
  {
    mysql_proxy_ = mysql_proxy;
    return common::OB_SUCCESS;
  }

  void reset()
  {
    mysql_proxy_ = NULL;
  }

public:
  int next_autoinc_value(const AutoincKey &key,
                         const uint64_t offset,
                         const uint64_t increment,
                         const uint64_t base_value,
                         const uint64_t max_value,
                         const uint64_t desired_count,
                         const int64_t &inner_autoinc_version,
                         uint64_t &start_inclusive,
                         uint64_t &end_inclusive,
                         uint64_t &sync_value );

  int get_autoinc_value(const AutoincKey &key, const int64_t &autoinc_version, uint64_t &seq_value, uint64_t &sync_value);

  int get_autoinc_value_in_batch(const common::ObIArray<AutoincKey> &keys,
                                 common::hash::ObHashMap<AutoincKey, uint64_t> &seq_values);

  int sync_autoinc_value(const AutoincKey &key,
                         const uint64_t insert_value,
                         const uint64_t max_value,
                         const int64_t autoinc_version,
                         uint64_t &seq_value,
                         uint64_t &sync_value);
  int read_and_push_inner_table(const AutoincKey &key,
                                const uint64_t max_value,
                                const uint64_t cache_end,
                                const int64_t autoinc_version,
                                bool &is_valid,
                                uint64_t &new_end);
private:
  int check_inner_autoinc_version(const int64_t &request_autoinc_version,
                                  const int64_t &inner_autoinc_version,
                                  const AutoincKey &key);
private:
  common::ObMySQLProxy *mysql_proxy_;
};

class ObInnerTableAutoincrementStore
{
public:
  ObInnerTableAutoincrementStore() {}
  ~ObInnerTableAutoincrementStore() = default;

  int init(common::ObMySQLProxy *mysql_proxy)
  {
    return inner_table_proxy_.init(mysql_proxy);
  }

  int get_value(
      const AutoincKey &key,
      const uint64_t offset,
      const uint64_t increment,
      const uint64_t max_value,
      const uint64_t table_auto_increment,
      const uint64_t desired_count,
      const uint64_t cache_size,
      const int64_t &autoinc_version,
      uint64_t &sync_value,
      uint64_t &start_inclusive,
      uint64_t &end_inclusive);

  int get_sequence_value(const AutoincKey &key,
                         const int64_t &autoinc_version,
                         uint64_t &sequence_value);

  int get_auto_increment_values(
      const common::ObIArray<AutoincKey> &autoinc_keys,
      const common::ObIArray<int64_t> &autoinc_versions,
      common::hash::ObHashMap<AutoincKey, uint64_t> &seq_values);

  int sync_value(
      const AutoincKey &key,
      const uint64_t max_value,
      const uint64_t value,
      const int64_t &autoinc_version,
      const int64_t cache_size,
      uint64_t &stored_sync_value);

private:
  ObAutoIncInnerTableProxy inner_table_proxy_;
};

class ObAutoincrementService
{
public:
  static const int64_t DEFAULT_TABLE_NODE_NUM = 1024;
//  static const int64_t BATCH_FETCH_COUNT = 1024;
  typedef common::ObLinkHashMap<AutoincKey, TableNode> NodeMap;
public:
  ObAutoincrementService();
  ~ObAutoincrementService();
  static ObAutoincrementService &get_instance();
  int init(common::ObMySQLProxy *mysql_proxy);
  int get_handle(AutoincParam &param, CacheHandle *&handle);
  void release_handle(CacheHandle *&handle);

  int sync_insert_value(AutoincParam &param);

  int sync_insert_value_local(AutoincParam &param);

  int sync_auto_increment(const schema::ObTableSchema &table_schema,
                          const uint64_t sync_value);
  int clear_autoinc_cache(const uint64_t table_id,
                          const uint64_t column_id);

  int get_sequence_value(const uint64_t table_id,
                         const uint64_t column_id,
                         const int64_t autoinc_version,
                         uint64_t &seq_value);

  int get_sequence_values(const common::ObIArray<AutoincKey> &autoinc_keys,
                          const common::ObIArray<int64_t> &autoinc_versions,
                          common::hash::ObHashMap<AutoincKey, uint64_t> &seq_values);
  int reinit_autoinc_row(const uint64_t &table_id,
                         const uint64_t &column_id,
                         const int64_t &autoinc_version,
                         common::ObMySQLTransaction &trans);
  int lock_autoinc_row(const uint64_t &table_id,
                       const uint64_t &column_id,
                       common::ObMySQLTransaction &trans);
  int reset_autoinc_row(const uint64_t &table_id,
                        const uint64_t &column_id,
                        const int64_t &autoinc_version,
                        common::ObMySQLTransaction &trans);
  // for alter table autoinc to recognize old autoincrement value in inner table
  int try_lock_autoinc_row(const uint64_t &table_id,
                           const uint64_t &column_id,
                           const int64_t &autoinc_version,
                           bool &need_update_inner_table,
                           common::ObMySQLTransaction &trans);

  static int calculate_idempotent_autoinc_val_for_ddl(AutoincParam *autoinc_param,
                                                      const int64_t table_all_slice_count,
                                                      const int64_t table_level_slice_idx,
                                                      const int64_t slice_row_idx,
                                                      const int64_t autoinc_range_interval,
                                                      uint64_t &autoinc_value);
  static int calc_next_value(const uint64_t last_next_value,
                             const uint64_t offset,
                             const uint64_t increment,
                             uint64_t &new_next_value);
  static int calc_prev_value(const uint64_t last_next_value,
                             const uint64_t offset,
                             const uint64_t increment,
                             uint64_t &prev_value);
  static uint64_t get_max_value(const common::ObObjType type);

private:
  int sync_insert_value_to_store(AutoincParam &param, CacheHandle *&cache_handle,
                                 const uint64_t value_to_sync);

private:
  int get_local_sequence_value_(const AutoincKey &key,
                                const int64_t autoinc_version,
                                uint64_t &seq_value,
                                bool &found);
  int get_table_node(const AutoincParam &param, TableNode *&table_node);
  int fetch_table_node(const AutoincParam &param,
                       TableNode *table_node,
                       const bool fetch_prefetch = false);
  int refresh_local_sync_value(const uint64_t table_id,
                               const uint64_t column_id,
                               const uint64_t sync_value);

  int alloc_autoinc_try_lock(lib::ObMutex &alloc_mutex);

private:
  common::ObSmallAllocator node_allocator_;
  common::ObSmallAllocator handle_allocator_;
  ObInnerTableAutoincrementStore autoinc_store_;
  lib::ObMutex             map_mutex_;
  //common::hash::ObHashMap<AutoincKey, TableNode*> node_map_;
  NodeMap node_map_;
  const static int INIT_NODE_MUTEX_NUM = 1024;
  common::ObLatch init_node_mutex_[INIT_NODE_MUTEX_NUM];
};
}//end namespace share
}//end namespace oceanbase
#endif
