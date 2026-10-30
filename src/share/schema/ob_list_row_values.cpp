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
#define USING_LOG_PREFIX SHARE_SCHEMA
#include "share/schema/ob_list_row_values.h"
#include "share/schema/ob_schema_struct.h"
#include "share/datum/typed_datum_compare.h"
#include "common/datum/datum_parameter_codec.h"
namespace oceanbase
{
using namespace common;
namespace share
{
namespace schema
{

bool partition_bound_values_equal(const DatumValue &left, const DatumValue &right)
{
  bool equal = false;
  return datum_values_equal(left, right, equal) == OB_SUCCESS && equal;
}

int copy_partition_bound(const ObRowkey &source, PartitionBoundRow &output, ObIAllocator &owner)
{
  return source.values_view().clone(output, owner);
}
int copy_partition_bound(const ObNewRow &source, PartitionBoundRow &output, ObIAllocator &owner)
{
  // Schema bounds use the physical row, preserving the established provider
  // contract. Clone before the input row owner can be reset or advanced.
  PartitionBoundRow values(source.cells_, source.count_);
  return values.clone(output, owner);
}
int compare_partition_bound_keys(const PartitionBoundRow &left, const PartitionBoundRow &other, int &cmp)
{
  int ret = OB_SUCCESS; cmp = 0;
  if (!left.is_legal() || !other.is_legal()) { ret = OB_INVALID_ARGUMENT; }
  for (int64_t i = 0; OB_SUCC(ret) && cmp == 0 && i < MIN(left.count_, other.count_); ++i) {
    ret = compare_datum_values(left.cells_[i], other.cells_[i], cmp);
  }
  if (OB_SUCC(ret) && cmp == 0) { cmp = left.count_ < other.count_ ? -1 : left.count_ > other.count_ ? 1 : 0; }
  return ret;
}
int compare_partition_bound_rows(const PartitionBoundRow &left, const PartitionBoundRow &other, int &cmp)
{
  int ret = OB_SUCCESS; cmp = 0;
  if (!left.is_valid() || !other.is_valid()) { ret = OB_ERR_UNEXPECTED; }
  for (int64_t i = 0; OB_SUCC(ret) && cmp == 0 && i < MAX(left.get_count(), other.get_count()); ++i) {
    if (i < left.get_count() && i < other.get_count()) { ret = compare_datum_values(left.get_cell(i), other.get_cell(i), cmp); }
    else if (i < left.get_count()) { cmp = left.get_cell(i).is_min_value() ? 0 : 1; }
    else { cmp = other.get_cell(i).is_min_value() ? 0 : -1; }
  }
  return ret;
}
int equal_partition_bound_keys(const PartitionBoundRow &left, const PartitionBoundRow &other, bool &equal)
{
  int ret = OB_SUCCESS; equal = left.count_ == other.count_;
  if (!left.is_legal() || !other.is_legal()) { ret = OB_INVALID_ARGUMENT; }
  for (int64_t i = 0; OB_SUCC(ret) && equal && i < left.count_; ++i) {
    ret = datum_values_equal(left.cells_[i], other.cells_[i], equal);
  }
  return ret;
}
bool equal_partition_bound_rows(const PartitionBoundRow &left, const PartitionBoundRow &other)
{
  bool equal = left.get_count() == other.get_count();
  if (!left.is_legal() || !other.is_legal()) { equal = false; }
  for (int64_t i = 0; equal && i < left.get_count(); ++i) {
    equal = datum_values_equal(left.get_cell(i), other.get_cell(i), equal) == OB_SUCCESS && equal;
  }
  return equal;
}

int PartitionBoundRowSet::find(const PartitionBoundRow &row, uint64_t &hash, int64_t &head, bool &found) const
{
  int ret = OB_SUCCESS; hash = 0; head = -1; found = false;
  for (int64_t i = 0; OB_SUCC(ret) && i < row.count(); ++i) {
    ret = hash_datum_value(row.get_cells()[i], hash, hash);
  }
  if (OB_SUCC(ret)) {
    ret = heads_.get_refactored(hash, head);
    if (ret == OB_HASH_NOT_EXIST) { ret = OB_SUCCESS; head = -1; }
  }
  for (int64_t i = head; OB_SUCC(ret) && !found && i >= 0;) {
    int cmp = 0;
    if (i >= entries_.count()) { ret = OB_ERR_UNEXPECTED; }
    else if (OB_FAIL(compare_partition_bound_keys(row, entries_.at(i).row_, cmp))) {}
    else { found = cmp == 0; i = entries_.at(i).next_; }
  }
  return ret;
}
int PartitionBoundRowSet::set_refactored(const PartitionBoundRow &row)
{
  uint64_t hash = 0; int64_t head = -1; bool found = false;
  int ret = find(row, hash, head, found);
  if (OB_SUCC(ret) && found) { ret = OB_HASH_EXIST; }
  else if (OB_SUCC(ret)) {
    const int64_t index = entries_.count();
    if (OB_FAIL(entries_.push_back(Entry(row, head)))) {}
    else if (OB_FAIL(heads_.set_refactored(hash, index, 1))) { entries_.pop_back(); }
  }
  return ret;
}
int PartitionBoundRowSet::exist_refactored(const PartitionBoundRow &row) const
{
  uint64_t hash = 0; int64_t head = -1; bool found = false;
  const int ret = find(row, hash, head, found);
  return ret != OB_SUCCESS ? ret : found ? OB_HASH_EXIST : OB_HASH_NOT_EXIST;
}

/*
* ObListRowValues
*/
ObListRowValues::ObListRowValues()
  : values_()
{}

ObListRowValues::~ObListRowValues()
{
  reset();
}

ObListRowValues::ObListRowValues(ObIAllocator &allocator)
  : values_(SCHEMA_MALLOC_BLOCK_SIZE, ModulePageAllocator(allocator))
{}

int ObListRowValues::assign(ObIAllocator &allocator, const ObListRowValues &other)
{
  int ret = OB_SUCCESS;
  const int64_t count = other.values_.count();
  if (OB_FAIL(values_.reserve(count))) {
  } else {
    for (int64_t i = 0; OB_SUCC(ret) && i < count; i++) {
      const PartitionBoundRow &row = other.values_.at(i);
      if (OB_FAIL(push_back_with_deep_copy(allocator, row))) {
      }
    } // for
  }
  return ret;
}

int ObListRowValues::push_back_with_deep_copy(ObIAllocator &allocator, const PartitionBoundRow &row)
{
  int ret = OB_SUCCESS;
  PartitionBoundRow tmp_row;
  if (OB_FAIL(row.clone(tmp_row, allocator))) {
  } else if (OB_FAIL(values_.push_back(tmp_row))) {
  }
  return ret;
}

// CAREFUL! this struct cannot add more member to serialization
int ObListRowValues::serialize(char *buf, const int64_t buf_len, int64_t &pos) const
{
  int ret = OB_SUCCESS;
  if (OB_FAIL(serialization::encode_vi64(buf, buf_len, pos, values_.count()))) {
  }
  for (int64_t i = 0; OB_SUCC(ret) && i < values_.count(); i ++) {
    if (OB_FAIL(values_.at(i).serialize_row(buf, buf_len, pos))) {
    }
  }
  return ret;
}

int64_t ObListRowValues::get_serialize_size() const
{
  int64_t len = 0;
  len += serialization::encoded_length_vi64(values_.count());
  for (int64_t i = 0; i < values_.count(); i ++) {
    len += values_.at(i).get_row_serialize_size();
  }
  return len;
}

int64_t ObListRowValues::get_deep_copy_size() const
{
  int64_t list_row_size = values_.get_data_size();
  for (int64_t i = 0; i < values_.count(); i ++) {
    list_row_size += values_.at(i).get_deep_copy_size();
  }
  return list_row_size * 2 - 1;
}

int ObListRowValues::deserialize(ObIAllocator &allocator, const char *buf, const int64_t data_len, int64_t &pos)
{
  int ret = OB_SUCCESS;
  int64_t size = 0, next = pos;
  ObArenaAllocator scratch("ListRowVal");
  ObSEArray<PartitionBoundRow, 2> staged;
  ObSEArray<DatumValue, 4> previous;
  PartitionBoundRow row;
  if (OB_FAIL(serialization::decode_vi64(buf, data_len, next, &size))) {}
  else if (size < 0 || size > data_len) { ret = OB_INVALID_DATA; }
  else if (OB_FAIL(staged.reserve(size))) {}
  if (OB_SUCC(ret)) { ret = previous.prepare_allocate(1024); }
  for (int64_t i = 0; OB_SUCC(ret) && i < size; ++i) {
    PartitionBoundRow owned;
    row.assign(previous.get_data(), previous.count());
    if (OB_FAIL(row.deserialize_row(scratch, buf, data_len, next))) {}
    else if (OB_FAIL(row.clone(owned, allocator))) {}
    else { ret = staged.push_back(owned); }
    for (int64_t j = 0; OB_SUCC(ret) && j < row.count(); ++j) { previous.at(j) = row.get_cells()[j]; }
  }
  if (OB_SUCC(ret) && OB_FAIL(values_.reserve(values_.count() + size))) {}
  for (int64_t i = 0; OB_SUCC(ret) && i < staged.count(); ++i) { ret = values_.push_back(staged.at(i)); }
  if (OB_SUCC(ret)) { pos = next; }
  return ret;
}

struct InnerPartListVectorCmp
{
public:
  InnerPartListVectorCmp() : ret_(OB_SUCCESS) {}
public:
  bool operator()(const PartitionBoundRow &left, const PartitionBoundRow &right)
  {
    bool bool_ret = false;
    int cmp = 0;
    if (OB_SUCCESS != ret_) {
      // failed before
    } else if (OB_SUCCESS != (ret_ = compare_partition_bound_rows(left, right, cmp))) {
      SHARE_SCHEMA_LOG_RET(ERROR, ret_, "l or r is invalid", K(ret_));
    } else {
      bool_ret = (cmp < 0);
    }
    return bool_ret;
  }
  int get_ret() const { return ret_; }
private:
  int ret_;
};

int ObListRowValues::sort_array()
{
  int ret = OB_SUCCESS;
  InnerPartListVectorCmp part_list_vector_op;
  lib::ob_sort(values_.begin(), values_.end(), part_list_vector_op);
  if (OB_FAIL(part_list_vector_op.get_ret())) {
  }
  return ret;
}


} // namespace schema
} // namespace share
} // namespace oceanbase
