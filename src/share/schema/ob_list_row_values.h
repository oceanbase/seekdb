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
#ifndef OB_SHARE_SCHEMA_OB_LIST_ROW_VALUES_H_
#define OB_SHARE_SCHEMA_OB_LIST_ROW_VALUES_H_
#include "common/datum/partition_bound_row.h"
#include "lib/container/ob_se_array.h"
#include "lib/hash/ob_hashmap.h"
namespace oceanbase
{
namespace common
{
class ObIAllocator;
class ObRowkey;
struct ObNewRow;
}
namespace share
{
namespace schema
{

// One-way legacy ingress, used only while upstream row producers migrate.
int copy_partition_bound(const common::ObRowkey &source, common::PartitionBoundRow &output, common::ObIAllocator &owner);
int copy_partition_bound(const common::ObNewRow &source, common::PartitionBoundRow &output, common::ObIAllocator &owner);
int compare_partition_bound_keys(const common::PartitionBoundRow &left, const common::PartitionBoundRow &right, int &cmp);
int compare_partition_bound_rows(const common::PartitionBoundRow &left, const common::PartitionBoundRow &right, int &cmp);
int equal_partition_bound_keys(const common::PartitionBoundRow &left, const common::PartitionBoundRow &right, bool &equal);
bool equal_partition_bound_rows(const common::PartitionBoundRow &left, const common::PartitionBoundRow &right);
bool partition_bound_values_equal(const common::DatumValue &left, const common::DatumValue &right);
inline bool partition_bound_keys_equal(const common::PartitionBoundRow &left, const common::PartitionBoundRow &right)
{ int cmp = 0; return compare_partition_bound_keys(left, right, cmp) == common::OB_SUCCESS && cmp == 0; }
inline int partition_bound_key_compare(const common::PartitionBoundRow &left, const common::PartitionBoundRow &right)
{ int cmp = 0; (void)compare_partition_bound_keys(left, right, cmp); return cmp; }


// Fallible native row set; the schema retains every borrowed bound payload.
class PartitionBoundRowSet
{
public:
  int create(int64_t count, const lib::ObLabel &bucket, const lib::ObLabel &node)
  { return heads_.create(count, bucket, node); }
  int set_refactored(const common::PartitionBoundRow &row);
  int exist_refactored(const common::PartitionBoundRow &row) const;
private:
  struct Entry {
    Entry() : row_(), next_(-1) {}
    Entry(const common::PartitionBoundRow &row, int64_t next) : row_(row), next_(next) {}
    common::PartitionBoundRow row_;
    int64_t next_;
  };
  int find(const common::PartitionBoundRow &row, uint64_t &hash, int64_t &head, bool &found) const;
  common::hash::ObHashMap<uint64_t, int64_t, common::hash::NoPthreadDefendMode> heads_;
  common::ObSEArray<Entry, 16> entries_;
};

struct ObListRowValues
{
public:
  ObListRowValues();
  explicit ObListRowValues(common::ObIAllocator &allocator);
  ~ObListRowValues();
  void reset() { values_.reset(); }
  int64_t count() const { return values_.count(); }
  int push_back(const common::PartitionBoundRow &row) { return values_.push_back(row); }
  common::PartitionBoundRow &at(const int64_t idx) { return values_.at(idx); }
  const common::PartitionBoundRow &at(const int64_t idx) const { return values_.at(idx); }
  const common::ObIArray<common::PartitionBoundRow>& get_values() const { return values_; }
  int assign(ObIAllocator &allocator, const ObListRowValues &other);
  int serialize(char *buf, const int64_t buf_len, int64_t &pos) const;
  int deserialize(common::ObIAllocator &allocator, const char *buf, const int64_t data_len, int64_t &pos);
  int64_t get_serialize_size() const;
  int64_t get_deep_copy_size() const;
  bool operator ==(const ObListRowValues &other) const = delete;
  int sort_array();
  TO_STRING_KV(K_(values));
public:
  int push_back_with_deep_copy(ObIAllocator &allocator, const common::PartitionBoundRow &row);

  // CAREFUL! this struct cannot add more member in serialization
  common::ObSEArray<common::PartitionBoundRow, 2> values_;
private:
  DISALLOW_COPY_AND_ASSIGN(ObListRowValues);
};

} // namespace schema
} // namespace share
} // namespace oceanbase

#endif // OB_SHARE_SCHEMA_OB_LIST_ROW_VALUES_H_
