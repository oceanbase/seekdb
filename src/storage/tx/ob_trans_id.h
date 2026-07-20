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
#ifndef OCEANBASE_STORAGE_TX_OB_TRANS_ID_H_
#define OCEANBASE_STORAGE_TX_OB_TRANS_ID_H_
// plain value types extracted from ob_trans_define.h(no upper-layer dependency;for by-value use by share RPC args,
// this header is conf logical L2;serialization implementation remains in the trans module cpp and links into the same library)
#include <stdint.h>
#include "lib/utility/ob_unify_serialize.h"
#include "lib/utility/ob_print_utils.h"
#include "lib/json/ob_yson.h"
#include "lib/ob_define.h"
#include "lib/time/ob_time_utility.h"  // ObMonotonicTs
namespace oceanbase
{
namespace transaction
{
class ObTransID
{
  OB_UNIS_VERSION(1);
public:
  ObTransID() : tx_id_(0) {}
  ObTransID(const int64_t tx_id) : tx_id_(tx_id) {}
  ~ObTransID() { tx_id_ = 0; }
  ObTransID &operator=(const ObTransID &r) {
    if (this != &r) {
      tx_id_ = r.tx_id_;
    }
    return *this;
  }
  ObTransID &operator=(const int64_t &id) {
    tx_id_ = id;
    return *this;
  }
  bool operator<(const ObTransID &id) {
    bool bool_ret = false;
    if (this->compare(id) < 0) {
      bool_ret = true;
    }
    return bool_ret;
  }
  bool operator>(const ObTransID &id) {
    bool bool_ret = false;
    if (this->compare(id) > 0) {
      bool_ret = true;
    }
    return bool_ret;
  }
  int64_t get_id() const { return tx_id_; }
  uint64_t hash() const
  {
    return murmurhash(&tx_id_, sizeof(tx_id_), 0);
  }
  int hash(uint64_t &hash_val) const
  {
    hash_val = hash();
    return OB_SUCCESS;
  }
  bool is_valid() const { return tx_id_ > 0; }
  void reset() { tx_id_ = 0; }
  int compare(const ObTransID& other) const;
  operator int64_t() const { return tx_id_; }
  bool operator==(const ObTransID &other) const
  { return tx_id_ == other.tx_id_; }
  bool operator!=(const ObTransID &other) const
  { return tx_id_ != other.tx_id_; }
  /*  XA  */
  int parse(char *b) {
    UNUSED(b);
    return OB_SUCCESS;
  }
  TO_STRING_AND_YSON(OB_ID(txid), tx_id_);
private:
  int64_t tx_id_;
};

typedef ObMonotonicTs MonotonicTs;

}  // namespace transaction
}  // namespace oceanbase
#endif
