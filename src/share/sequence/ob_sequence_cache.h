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

#ifndef _OB_SHARE_SEQ_SEQUENCE_CACHE_H_
#define _OB_SHARE_SEQ_SEQUENCE_CACHE_H_

#include "lib/utility/ob_macro_utils.h"
#include "lib/hash/ob_link_hashmap.h"
#include "lib/number/ob_number_v2.h"
#include "share/sequence/ob_sequence_dml_proxy.h"
#include "share/sequence/ob_sequence_option.h"

namespace oceanbase
{
namespace common
{
class ObMySQLProxy;
class ObMySQLTransaction;
}
namespace obrpc
{
  struct ObSeqCleanCacheRes;
}
namespace share
{
namespace schema
{
class ObSchemaGetterGuard;
class ObSequenceSchema;
class ObMultiVersionSchemaService;
}

struct SequenceCacheNode
{
  OB_UNIS_VERSION(1);
public:
  SequenceCacheNode()
      : start_(), end_()
  {}
  int assign(const SequenceCacheNode &other);
  void reset()
  {
  }

  TO_STRING_KV(K_(start),
               K_(end));

  int set_start(const common::number::ObNumber &start)
  {
    return start_.set(start);
  }
  int set_end(const common::number::ObNumber &end)
  {
    return end_.set(end);
  }
  const common::number::ObNumber &start() const { return start_.val(); }
  const common::number::ObNumber &end() const { return end_.val(); }
private:
  ObSequenceValue start_;
  ObSequenceValue end_;
};

// a wrapper class, adaptor for ObLinkHashMap
struct CacheItemKey
{
public:
  CacheItemKey() : tenant_id_(0), key_(0) {}
  CacheItemKey(const uint64_t tenant_id, const uint64_t key) : tenant_id_(tenant_id), key_(key) {}
  ~CacheItemKey() = default;
  bool operator==(const CacheItemKey &other) const
  {
    return tenant_id_ == other.tenant_id_ && other.key_ == key_;
  }

  int compare(const CacheItemKey &other) {
    int ret = tenant_id_ < other.tenant_id_ ? -1 : (tenant_id_ > other.tenant_id_) ? 1 : 0;
    if (0 == ret) {
      ret = key_ < other.key_ ? -1 : (key_ > other.key_) ? 1 : 0;
    }
    return ret;
  }

  uint64_t hash() const
  {
    uint64_t hash_val = 0;
    hash_val = common::murmurhash(&key_, sizeof(key_), hash_val);
    hash_val = common::murmurhash(&tenant_id_, sizeof(tenant_id_), hash_val);
    return hash_val;
  }
  TO_STRING_KV(K_(tenant_id), K_(key));
  uint64_t tenant_id_;
  uint64_t key_;
};

enum SequenceCacheStatus
{
  DELETED,
  INITED
};

struct ObSequenceCacheItem : public common::LinkHashValue<CacheItemKey>
{
public:
  ObSequenceCacheItem()
      : prefetching_(false),
        with_prefetch_node_(false),
        base_on_last_number_(false),
        last_refresh_ts_(INITED),
        alloc_mutex_(common::ObLatchIds::SEQUENCE_VALUE_ALLOC_LOCK),
        fetch_(common::ObLatchIds::SEQUENCE_VALUE_FETCH_LOCK),
        last_number_()
  {}
  int combine_prefetch_node()
  {
    int ret = common::OB_SUCCESS;
    if (OB_LIKELY(with_prefetch_node_)) {
      if (curr_node_.end() != prefetch_node_.start()) {
        // The data of two nodes is not continuous, then use the prefetch data, discard the remaining data of curr_node
        ret = curr_node_.set_start(prefetch_node_.start());
      }
      curr_node_.set_end(prefetch_node_.end());
      // Notify prefetch logic, prefetch node can accept new prefetch data
      with_prefetch_node_ = false;
    }
    return ret;
  }
  int set_last_number(const common::number::ObNumber &num)
  {
    return last_number_.set(num);
  }
  common::number::ObNumber &last_number() { return last_number_.val(); }
  const common::number::ObNumber &last_number() const { return last_number_.val(); }
public:
  SequenceCacheNode curr_node_;
  SequenceCacheNode prefetch_node_;
  // Mark the current item whether it is performing a prefetching operation to avoid concurrent prefetch queries
  bool prefetching_;
  // Mark whether prefetch_node has been filled with a value
  bool with_prefetch_node_;
  // If the calculation process of next-value is: first read last_number, then add increment by
  // But for the first value retrieval, do not add increment by. So use base_on_last_number to mark
  // base_on_last_number_ = false indicates the first value retrieval
  bool base_on_last_number_;
  // Record the last obtained value, used to determine if the next value needs to be incremented by increment_by in cycle mode
  int64_t last_refresh_ts_;
  lib::ObMutex alloc_mutex_;
  lib::ObMutex fetch_;
private:
  ObSequenceValue last_number_;
public:
  TO_STRING_KV(K_(curr_node),
               K_(prefetch_node),
               K_(prefetching),
               K_(with_prefetch_node),
               K_(last_refresh_ts),
               K_(last_number),
               K_(base_on_last_number));
};

class ObSequenceCache
{
public:
  // map sequence_id => sequence cache
  typedef common::ObLinkHashMap<CacheItemKey, ObSequenceCacheItem> NodeMap;
public:
  ObSequenceCache();
  virtual ~ObSequenceCache() = default;
  static ObSequenceCache &get_instance();

  int init(share::schema::ObMultiVersionSchemaService &schema_service,
            common::ObMySQLProxy &sql_proxy);
  int nextval(const share::schema::ObSequenceSchema &schema,
              common::ObIAllocator &allocator, // used for various temporary calculations
              ObSequenceValue &nextval);
  int remove(uint64_t tenant_id, uint64_t sequence_id, obrpc::ObSeqCleanCacheRes &cache_res);

private:
  /* functions */
  int get_item(CacheItemKey &key, ObSequenceCacheItem *&item);

  int del_item(uint64_t tenant_id, CacheItemKey &key, obrpc::ObSeqCleanCacheRes &cache_res);

  int prefetch_sequence_cache(const schema::ObSequenceSchema &schema,
                              ObSequenceCacheItem &cache,
                              ObSequenceCacheItem &old_cache);
  int find_sequence_cache(const schema::ObSequenceSchema &schema,
                          ObSequenceCacheItem &cache);
  int move_next(const schema::ObSequenceSchema &schema,
                ObSequenceCacheItem &cache,
                common::ObIAllocator &allocator,
                ObSequenceValue &nextval);
  int need_refill_cache(const schema::ObSequenceSchema &schema,
                        ObSequenceCacheItem &cache,
                        common::ObIAllocator &allocator,
                        bool &need_refill);
  int refill_sequence_cache(const schema::ObSequenceSchema &schema,
                            common::ObIAllocator &allocator,
                            ObSequenceCacheItem &cache);
  /* variables */
  ObSequenceDMLProxy dml_proxy_;
  bool inited_;
  lib::ObMutex cache_mutex_;
  NodeMap sequence_cache_;
  DISALLOW_COPY_AND_ASSIGN(ObSequenceCache);
};
}
}
#endif /* _OB_SHARE_SEQ_SEQUENCE_CACHE_H_ */
//// end of header file


