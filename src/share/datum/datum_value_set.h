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

#pragma once
#include "share/datum/typed_datum_compare.h"
#include "lib/hash/ob_hashmap.h"
#include "lib/container/ob_se_array.h"

namespace oceanbase { namespace common {
struct DatumValueHasher
{
  int operator()(const DatumValue &value, uint64_t &hash) const
  { return hash_datum_value(value, 0, hash); }
};

// A statement-local set of typed views. Producers retain payload ownership.
// Hash/equality operations are fallible; an error never means "not equal".
// The hasher parameter also allows callers to use another compatible hash.
template <typename Hasher = DatumValueHasher>
class DatumValueSet
{
public:
  explicit DatumValueSet(const Hasher &hasher = Hasher()) : hasher_(hasher) {}
  DatumValueSet(const DatumValueSet &) = delete;
  DatumValueSet &operator=(const DatumValueSet &) = delete;
  int create(int64_t bucket_count, const lib::ObLabel &bucket_label,
      const lib::ObLabel &node_label)
  { return heads_.create(bucket_count, bucket_label, node_label); }
  int64_t size() const { return entries_.count(); }
  bool created() const { return heads_.created(); }
  int destroy() {
    const int ret = heads_.destroy();
    if (ret == OB_SUCCESS) { entries_.reset(); }
    return ret;
  }
  int insert(const DatumValue &value, bool &inserted) {
    int ret = OB_SUCCESS;
    inserted = false;
    uint64_t hash = 0;
    int64_t head = -1;
    if (!heads_.created()) {
      ret = OB_NOT_INIT;
    } else if (OB_FAIL(hasher_(value, hash))) {
    } else {
      ret = heads_.get_refactored(hash, head);
      if (ret == OB_HASH_NOT_EXIST) { ret = OB_SUCCESS; head = -1; }
      bool found = false;
      for (int64_t index = head; OB_SUCC(ret) && !found && index >= 0;) {
        if (index >= entries_.count()) {
          ret = OB_ERR_UNEXPECTED;
        } else if (OB_FAIL(datum_values_equal(entries_.at(index).value_, value, found))) {
        } else {
          index = entries_.at(index).next_;
        }
      }
      if (OB_SUCC(ret) && !found) {
        const int64_t index = entries_.count();
        if (OB_FAIL(entries_.push_back(Entry(value, head)))) {
        } else if (OB_FAIL(heads_.set_refactored(hash, index, 1))) {
          entries_.pop_back();
        } else {
          inserted = true;
        }
      }
    }
    return ret;
  }
private:
  struct Entry {
    Entry() : value_(), next_(-1) {}
    Entry(const DatumValue &value, int64_t next) : value_(value), next_(next) {}
    DatumValue value_;
    int64_t next_;
  };
  Hasher hasher_;
  hash::ObHashMap<uint64_t, int64_t, hash::NoPthreadDefendMode> heads_;
  ObSEArray<Entry, 16> entries_;
};
} }
