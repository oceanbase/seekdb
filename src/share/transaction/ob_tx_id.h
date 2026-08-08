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

#ifndef OCEANBASE_SHARE_TRANSACTION_OB_TX_ID_H_
#define OCEANBASE_SHARE_TRANSACTION_OB_TX_ID_H_

#include <stdint.h>
#include "lib/json/ob_yson.h"
#include "lib/ob_define.h"
#include "lib/time/ob_time_utility.h"
#include "lib/utility/ob_print_utils.h"
#include "lib/utility/ob_unify_serialize.h"

namespace oceanbase
{
namespace transaction
{

// Stable transaction identifier shared by protocol and data-plane modules.
class ObTransID
{
  OB_UNIS_VERSION(1);
public:
  ObTransID() : tx_id_(0) {}
  ObTransID(const int64_t tx_id) : tx_id_(tx_id) {}
  ~ObTransID() { tx_id_ = 0; }
  ObTransID &operator=(const ObTransID &r)
  {
    if (this != &r) {
      tx_id_ = r.tx_id_;
    }
    return *this;
  }
  ObTransID &operator=(const int64_t &id)
  {
    tx_id_ = id;
    return *this;
  }
  bool operator<(const ObTransID &id) { return compare(id) < 0; }
  bool operator>(const ObTransID &id) { return compare(id) > 0; }
  int64_t get_id() const { return tx_id_; }
  uint64_t hash() const { return murmurhash(&tx_id_, sizeof(tx_id_), 0); }
  int hash(uint64_t &hash_val) const
  {
    hash_val = hash();
    return OB_SUCCESS;
  }
  bool is_valid() const { return tx_id_ > 0; }
  void reset() { tx_id_ = 0; }
  int compare(const ObTransID &other) const;
  operator int64_t() const { return tx_id_; }
  bool operator==(const ObTransID &other) const { return tx_id_ == other.tx_id_; }
  bool operator!=(const ObTransID &other) const { return tx_id_ != other.tx_id_; }
  TO_STRING_AND_YSON(OB_ID(txid), tx_id_);
private:
  int64_t tx_id_;
};

using MonotonicTs = ObMonotonicTs;

} // namespace transaction
} // namespace oceanbase

#endif // OCEANBASE_SHARE_TRANSACTION_OB_TX_ID_H_
