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

#ifndef SRC_STORAGE_COMPACTION_OB_TRANS_CACHE_H_
#define SRC_STORAGE_COMPACTION_OB_TRANS_CACHE_H_

#include "lib/ob_define.h"
#include "lib/utility/ob_print_utils.h"
#include "lib/hash/ob_hashutils.h"
#include "storage/tx/ob_trans_define.h"
#include "storage/tx/ob_tx_data_define.h"

namespace oceanbase
{

namespace storage
{
class ObTxData;
}

namespace compaction
{
struct ObMergeCachedTransKey {
  ObMergeCachedTransKey()
    : trans_id_(),
      sql_sequence_()
  {}
  ObMergeCachedTransKey(
    transaction::ObTransID trans_id,
    transaction::ObTxSEQ sql_sequence)
    : trans_id_(trans_id),
      sql_sequence_(sql_sequence)
  {}
  ~ObMergeCachedTransKey() {}
  inline bool operator == (const ObMergeCachedTransKey &other) const
  {
    return trans_id_ == other.trans_id_ && sql_sequence_ == other.sql_sequence_;
  }
  inline uint64_t hash() const
  {
    uint64_t hash_value = trans_id_.hash();
    uint64_t seq_hash = sql_sequence_.hash();
    hash_value = murmurhash(&seq_hash, sizeof(seq_hash), hash_value);
    return hash_value;
  }
  inline bool is_valid() const
  {
    return trans_id_.is_valid() && sql_sequence_.is_valid();
  }
  TO_STRING_KV(K_(trans_id), K_(sql_sequence));

  transaction::ObTransID trans_id_;
  transaction::ObTxSEQ sql_sequence_;
};

struct ObMergeCachedTransState {
  ObMergeCachedTransState()
    : key_(),
      trans_version_(INVALID_TRANS_VERSION),
      trans_state_(INT32_MAX),
      can_read_(INVALID_BOOL_VALUE)
  {}
  ObMergeCachedTransState(
    transaction::ObTransID trans_id,
    transaction::ObTxSEQ sql_sequence,
    int64_t trans_version,
    int32_t trans_state,
    int16_t can_read)
    : key_(trans_id, sql_sequence),
      trans_version_(trans_version),
      trans_state_(trans_state),
      can_read_(can_read)
  {}
  virtual ~ObMergeCachedTransState() {}
  inline bool is_valid() const
  {
    return key_.is_valid() && INVALID_TRANS_VERSION != trans_version_ && INT32_MAX != trans_state_ &&
      INVALID_BOOL_VALUE != can_read_;
  }
  TO_STRING_KV(K_(key), K_(trans_state), K_(trans_version), K_(can_read));

  static const int16_t INVALID_BOOL_VALUE = -1;
  static const int64_t INVALID_TRANS_VERSION = -1;
  ObMergeCachedTransKey key_;
  int64_t trans_version_;
  int32_t trans_state_;
  int16_t can_read_; // 0 false; 1 true
};

class ObCachedTransStateMgr {
public:
  ObCachedTransStateMgr(common::ObIAllocator &allocator)
    : is_inited_(false),
      max_cnt_(0),
      allocator_(allocator),
      array_(nullptr)
  {}
  ~ObCachedTransStateMgr() { destroy(); }
  int init(int64_t max_cnt);
  void destroy();
  inline uint64_t cal_idx(const ObMergeCachedTransKey &key) { return key.hash() % max_cnt_; }
  int get_trans_state(const transaction::ObTransID &trans_id, const transaction::ObTxSEQ &sql_seq, ObMergeCachedTransState &trans_state);
  int add_trans_state(
    const transaction::ObTransID &trans_id,
    const transaction::ObTxSEQ &sql_seq,
    const int64_t trans_version,
    const int32_t trans_state,
    const int16_t can_read);
private:
  bool is_inited_;
  int64_t max_cnt_;
  common::ObIAllocator &allocator_;
  ObMergeCachedTransState *array_;
};

} // namespace compaction
} // namespace oceanbase

#endif
