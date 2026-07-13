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

#ifndef _OB_SHARE_OB_GLOBAL_AUTOINC_SERVICE_H_
#define _OB_SHARE_OB_GLOBAL_AUTOINC_SERVICE_H_

#include "lib/hash/ob_hashmap.h"
#include "lib/hash/ob_link_hashmap.h"
#include "lib/lock/ob_spin_lock.h"
#include "common/mysqlclient/ob_mysql_proxy.h"
#include "share/ob_autoincrement_param.h"
#include "share/ob_autoincrement_service.h"
#include "share/ob_gais_msg.h"

namespace oceanbase
{
namespace share
{
struct ObGAISNextAutoIncValReq;
struct ObGAISAutoIncKeyArg;
struct ObGAISPushAutoIncValReq;
struct ObAutoIncCacheNode
{
public:
  ObAutoIncCacheNode() : start_(0), end_(0), sync_value_(0), autoinc_version_(OB_INVALID_VERSION) {}
  int init(const uint64_t start,
           const uint64_t end,
           const uint64_t sync_value,
           const int64_t autoinc_version);
  inline bool is_valid() const
  {
    return start_ > 0 && end_ >= start_ && sync_value_ <= start_;
  }
  inline bool need_fetch_next_node(const uint64_t base_value,
                                   const uint64_t desired_cnt,
                                   const uint64_t max_value) const;
  inline bool need_sync(const uint64_t new_sync_value) const
  {
    return new_sync_value > sync_value_ && new_sync_value > end_;
  }
  int with_new_start(const uint64_t new_start);
  int with_new_end(const uint64_t new_end);
  void reset() {
    start_ = 0;
    end_ = 0;
    sync_value_ = 0;
    autoinc_version_ = OB_INVALID_VERSION;
  }
  TO_STRING_KV(K_(start), K_(end), K_(sync_value), K_(autoinc_version));

  uint64_t start_; // next auto_increment value can be used
  uint64_t end_;   // last available value in the cache(included)
  uint64_t sync_value_;
  int64_t autoinc_version_;
};

class ObGlobalAutoIncService
{
public:
  ObGlobalAutoIncService() : is_inited_(false) {}
  virtual ~ObGlobalAutoIncService() {}

  const static int MUTEX_NUM = 1024;
  const static int INIT_HASHMAP_SIZE = 1000;
  int init(common::ObMySQLProxy *mysql_proxy);
  static int mtl_init(ObGlobalAutoIncService *&gais);
  void destroy();
  int clear();

  /*
   * This method handles the request for getting next (batch) auto-increment value.
   * If the cache can satisfy the request, use the auto-increment in the cache to return,
   * otherwise, need to require auto-increment from inner table and fill it in the cache,
   * and then consume the auto-increment in the cache.
   */
  int handle_next_autoinc_request(const ObGAISNextAutoIncValReq &request,
                                  obcall::ObGAISNextValResult &result);

  /*
   * This method handles the request for getting current auto-increment value. If it exists
   * in the cache, it is taken from the cache, otherwise it is taken from inner table.
   * Note: the cache will not be updated during this method.
   */
  int handle_curr_autoinc_request(const ObGAISAutoIncKeyArg &request,
                                  obcall::ObGAISCurrValResult &result);

  /*
   * This method handles the request for push local sync value to global. If the local sync
   * is smaller than the sync value in the cache, no update is required. Otherwise,
   * both the cache and inner table need to be updated. Then returns the latest sync value.
   */
  int handle_push_autoinc_request(const ObGAISPushAutoIncValReq &request,
                                  uint64_t &sync_value);

  int handle_clear_autoinc_cache_request(const ObGAISAutoIncKeyArg &request);
    /*
   * This method handles the request for getting next (batch) sequence value.
   * If the cache can satisfy the request, use the sequence in the cache to return,
   * otherwise, need to require sequence from inner table and fill it in the cache,
   * and then consume the sequence in the cache.
   */
  int handle_next_sequence_request(const ObGAISNextSequenceValReq &request,
                                  obcall::ObGAISNextSequenceValResult &result);

  TO_STRING_KV(K_(is_inited), K(autoinc_map_.size()));

private:
  int fetch_next_node_(const ObGAISNextAutoIncValReq &request, ObAutoIncCacheNode &node);
  int read_value_from_inner_table_(const share::AutoincKey &key,
                                   const int64_t &inner_autoinc_version,
                                   uint64_t &sequence_val,
                                   uint64_t &sync_val);
  int sync_value_to_inner_table_(const ObGAISPushAutoIncValReq &request,
                                 ObAutoIncCacheNode &node,
                                 uint64_t &sync_value);
  static uint64_t calc_next_cache_boundary(const uint64_t insert_value,
                                           const uint64_t cache_size,
                                           const uint64_t max_value)
  {
    uint64_t next_cache_boundary = 0;
    if (max_value < cache_size || insert_value > max_value - cache_size) {
      next_cache_boundary = max_value;
    } else {
      next_cache_boundary = insert_value + cache_size;
    }
    return next_cache_boundary;
  }
private:
  bool is_inited_;
  share::ObAutoIncInnerTableProxy inner_table_proxy_;
  common::hash::ObHashMap<uint64_t, ObAutoIncCacheNode> autoinc_map_; // table_id -> node
  lib::ObMutex op_mutex_[MUTEX_NUM];
};

} // share
} // oceanbase

#endif // _OB_SHARE_OB_GLOBAL_AUTOINC_SERVICE_H_
