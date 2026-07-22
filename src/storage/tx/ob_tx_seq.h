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

#ifndef OCEANBASE_TRANSACTION_OB_TX_SEQ_H
#define OCEANBASE_TRANSACTION_OB_TX_SEQ_H

#include "lib/ob_define.h"              // for int64_t
#include "lib/hash_func/murmur_hash.h"  // for murmurhash()
#include "lib/utility/ob_macro_utils.h" // for OB_ASSERT
#include "lib/utility/ob_print_utils.h" // for DECLARE_TO_STRING
#include "share/ob_errno.h"             // for OB_VERSION_NOT_MATCH

namespace oceanbase
{
namespace transaction
{

// Transaction sequence number, split into two parts:
//   part 1: sequence number offset against transaction start
//   part 2: id of parallel write branch
class ObTxSEQ
{
public:
  ObTxSEQ() : raw_val_(0) {}
  explicit ObTxSEQ(int64_t seq, int16_t branch) : raw_val_(CURRENT_FORMAT_MASK)
  {
    OB_ASSERT(seq > 0 && seq <= MAX_SEQ);
    OB_ASSERT(branch >= 0);
    set_branch_(branch);
    set_seq_(seq);
  }
  static const ObTxSEQ &INVL() { static ObTxSEQ v; return v; }
  static const ObTxSEQ &MIN_VAL() { static ObTxSEQ v(1, 0); return v; }
  static const ObTxSEQ &MAX_VAL() { static ObTxSEQ v(MAX_SEQ, MAX_BRANCH); return v; }
  void reset() { raw_val_ = 0; }
  bool is_valid() const { return is_current_format_() && get_seq() > 0; }
  bool is_min() const { return *this == MIN_VAL(); }
  bool is_max() const { return *this == MAX_VAL(); }
  ObTxSEQ clone_with_seq(int64_t seq_abs, int64_t seq_base) const
  {
    ObTxSEQ n = *this;
    const int64_t new_seq = seq_abs - seq_base;
    OB_ASSERT(is_valid());
    OB_ASSERT(new_seq > 0 && new_seq <= MAX_SEQ);
    n.set_seq_(new_seq);
    return n;
  }
  bool operator>(const ObTxSEQ &b) const
  {
    return get_seq() > b.get_seq();
  }
  bool operator>=(const ObTxSEQ &b) const
  {
    return get_seq() >= b.get_seq();
  }
  bool operator<(const ObTxSEQ &b) const
  {
    return b > *this;
  }
  bool operator<=(const ObTxSEQ &b) const
  {
    return b >= *this;
  }
  bool operator==(const ObTxSEQ &b) const
  {
    return raw_val_ == b.raw_val_;
  }
  bool operator!=(const ObTxSEQ &b) const
  {
    return !(*this == b);
  }
  ObTxSEQ &operator++() {
    OB_ASSERT(is_valid() && get_seq() < MAX_SEQ);
    set_seq_(get_seq() + 1);
    return *this;
  }
  ObTxSEQ operator+(int n) const {
    return ObTxSEQ(get_seq() + n, get_branch());
  }
  ObTxSEQ operator-(int n) const {
    return ObTxSEQ(get_seq() - n, get_branch());
  }
  uint64_t hash() const { return murmurhash(&raw_val_, sizeof(raw_val_), 0); }
  // atomic incremental update
  int64_t inc_update(const ObTxSEQ &b) { return common::inc_update(&raw_val_, b.raw_val_); }
  uint64_t cast_to_int() const { return raw_val_; }
  // Rebuild from the raw representation stored in rows, logs, or RPC payloads.
  // This is deliberately not a sequence-number constructor.
  static ObTxSEQ cast_from_int(int64_t raw_val)
  {
    ObTxSEQ seq;
    seq.raw_val_ = raw_val;
    OB_ASSERT(0 == raw_val || seq.is_valid());
    return seq;
  }
  // return sequence number
  int64_t get_seq() const
  {
    return (static_cast<uint64_t>(raw_val_) >> SEQ_SHIFT) & MAX_SEQ;
  }
  int16_t get_branch() const
  {
    return static_cast<int16_t>(static_cast<uint64_t>(raw_val_) & MAX_BRANCH);
  }
  ObTxSEQ &set_branch(int16_t branch)
  {
    OB_ASSERT(is_valid());
    OB_ASSERT(branch >= 0);
    set_branch_(branch);
    return *this;
  }
  // atomic Load/Store
  void atomic_reset() { ATOMIC_SET(&raw_val_, 0); }
  ObTxSEQ atomic_load() const { int64_t v = ATOMIC_LOAD(&raw_val_); ObTxSEQ s; s.raw_val_ = v; return s; }
  void atomic_store(ObTxSEQ seq) { ATOMIC_STORE(&raw_val_, seq.raw_val_); }
  int64_t to_string(char* buf, const int64_t buf_len) const;
  NEED_SERIALIZE_AND_DESERIALIZE;
private:
  static constexpr int64_t MAX_SEQ = (1LL << 47) - 1;
  static constexpr int16_t MAX_BRANCH = (1 << 15) - 1;
  static constexpr int64_t CURRENT_FORMAT_MASK = 1LL << 62;
  static constexpr int64_t SEQ_SHIFT = 15;
  static constexpr uint64_t BRANCH_MASK = (1ULL << SEQ_SHIFT) - 1;
  static constexpr uint64_t SEQ_MASK = static_cast<uint64_t>(MAX_SEQ) << SEQ_SHIFT;
  bool is_current_format_() const
  {
    return raw_val_ > 0 && 0 != (raw_val_ & CURRENT_FORMAT_MASK);
  }
  void set_seq_(int64_t seq)
  {
    raw_val_ = static_cast<int64_t>((static_cast<uint64_t>(raw_val_) & ~SEQ_MASK)
        | (static_cast<uint64_t>(seq) << SEQ_SHIFT));
  }
  void set_branch_(int16_t branch)
  {
    raw_val_ = static_cast<int64_t>((static_cast<uint64_t>(raw_val_) & ~BRANCH_MASK)
        | static_cast<uint16_t>(branch));
  }
  int64_t raw_val_;
};
static_assert(sizeof(ObTxSEQ) == sizeof(int64_t), "ObTxSEQ should sizeof(int64_t)");

inline int64_t ObTxSEQ::to_string(char* buf, const int64_t buf_len) const
{
  int64_t pos = 0;
  if (raw_val_ == INT64_MAX) {
    BUF_PRINTF("MAX");
  } else if (is_valid()) {
    J_OBJ_START();
    J_KV("branch", get_branch(), "seq", get_seq());
    J_OBJ_END();
  } else {
    BUF_PRINTF("%lu", raw_val_);
  }
  return pos;
}

inline OB_DEF_SERIALIZE_SIMPLE(ObTxSEQ)
{
  return serialization::encode_vi64(buf, buf_len, pos, raw_val_);
}

inline OB_DEF_SERIALIZE_SIZE_SIMPLE(ObTxSEQ)
{
  return serialization::encoded_length_vi64(raw_val_);
}

inline OB_DEF_DESERIALIZE_SIMPLE(ObTxSEQ)
{
  int ret = serialization::decode_vi64(buf, data_len, pos, &raw_val_);
  if (OB_SUCC(ret) && 0 != raw_val_ && !is_valid()) {
    ret = OB_VERSION_NOT_MATCH;
  }
  return ret;
}

}
}
#endif
