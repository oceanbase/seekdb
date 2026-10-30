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

#define USING_LOG_PREFIX COMMON

#include "common/row/ob_row.h"
#include "common/datum/datum_parameter_codec.h"
#include "common/datum/datum_parameter_array.h"
#include "common/datum/datum_key_ops.h"
#include "lib/allocator/ob_allocator.h"

namespace oceanbase
{
namespace common
{

DEFINE_SERIALIZE(ObColumnInfo)
{
  int ret = OB_SUCCESS;
  int32_t cs_type_int = static_cast<int32_t>(cs_type_);
  OB_UNIS_ENCODE(index_);
  OB_UNIS_ENCODE(cs_type_int);
  return ret;
}

DEFINE_DESERIALIZE(ObColumnInfo)
{
  int ret = OB_SUCCESS;
  int32_t cs_type_int = 0;
  OB_UNIS_DECODE(index_);
  OB_UNIS_DECODE(cs_type_int);
  cs_type_ = static_cast<ObCollationType>(cs_type_int);
  return ret;
}

DEFINE_GET_SERIALIZE_SIZE(ObColumnInfo)
{
  int64_t len = 0;
  int32_t cs_type_int = static_cast<int32_t>(cs_type_);
  OB_UNIS_ADD_LEN(index_);
  OB_UNIS_ADD_LEN(cs_type_int);   // for cs_type_.
  return len;
}

namespace {
int64_t flat_span(int64_t bytes)
{ return bytes < 0 || bytes > INT64_MAX - 7 ? INT64_MAX : (bytes + 7) & ~int64_t(7); }
bool add_size(int64_t &total, int64_t bytes)
{
  if (bytes < 0 || bytes > INT64_MAX - total) { total = INT64_MAX; return false; }
  total += bytes; return true;
}
bool row_shape(const ObNewRow &row, bool projected)
{
  bool valid = row.count_ >= 0 && (row.count_ == 0 || row.cells_ != nullptr)
      && row.projector_size_ >= 0 && (row.projector_size_ == 0 || row.projector_ != nullptr);
  if (projected) {
    for (int64_t i = 0; valid && i < row.projector_size_; ++i) {
      valid = row.projector_[i] >= 0 && row.projector_[i] < row.count_;
    }
  }
  return valid;
}
int64_t row_memory_size(const ObNewRow &row, bool projected)
{
  if (!row_shape(row, projected)) { return INT64_MAX; }
  const int64_t count = projected ? row.get_count() : row.count_;
  if (count > INT64_MAX / sizeof(DatumValue)) { return INT64_MAX; }
  int64_t bytes = flat_span(count * sizeof(DatumValue));
  for (int64_t i = 0; bytes != INT64_MAX && i < count; ++i) {
    add_size(bytes, DatumValueBuffer::get_deep_copy_size(projected ? row.get_cell(i) : row.cells_[i]));
  }
  if (!projected && bytes != INT64_MAX && row.projector_size_ > 0) {
    if (row.projector_size_ > INT64_MAX / sizeof(int32_t)) { return INT64_MAX; }
    add_size(bytes, flat_span(row.projector_size_ * sizeof(int32_t)));
  }
  return bytes;
}
using FlatRowOwner = NativeValueFlat::BufferOwner;
int copy_flat_row(const ObNewRow &source, ObNewRow &output, char *buffer,
                  int64_t length, int64_t &pos, bool projected)
{
  int ret = OB_SUCCESS;
  const int64_t bytes = row_memory_size(source, projected);
  int64_t next = pos;
  if (buffer == nullptr || pos < 0 || pos > length) { ret = OB_INVALID_ARGUMENT; }
  else if (bytes == INT64_MAX || bytes > length - pos) { ret = OB_SIZE_OVERFLOW; }
  else {
    FlatRowOwner owner(buffer, length, next);
    ObNewRow candidate;
    candidate.count_ = projected ? source.get_count() : source.count_;
    void *descriptors = owner.alloc(sizeof(DatumValue) * candidate.count_);
    if (descriptors == nullptr) { ret = OB_SIZE_OVERFLOW; }
    else { candidate.cells_ = new (descriptors) DatumValue[candidate.count_]; }
    for (int64_t i = 0; OB_SUCC(ret) && i < candidate.count_; ++i) {
      const DatumValue &value = projected ? source.get_cell(i) : source.cells_[i];
      ret = DatumValueBuffer::deep_copy(value, candidate.cells_[i], buffer, length, next);
    }
    if (OB_SUCC(ret) && !projected && source.projector_size_ > 0) {
      candidate.projector_ = static_cast<int32_t *>(owner.alloc(sizeof(int32_t) * source.projector_size_));
      if (candidate.projector_ == nullptr) { ret = OB_SIZE_OVERFLOW; }
      else {
        candidate.projector_size_ = source.projector_size_;
        MEMCPY(candidate.projector_, source.projector_, sizeof(int32_t) * source.projector_size_);
      }
    }
    if (OB_SUCC(ret)) { output = candidate; pos = next; }
  }
  return ret;
}
int take_span(char *buffer, int64_t length, int64_t &pos, int64_t bytes, char *&output)
{
  const int64_t span = flat_span(bytes);
  if (pos < 0 || pos > length || span > length - pos) { return OB_SIZE_OVERFLOW; }
  output = buffer + pos; pos += span; return OB_SUCCESS;
}
} // namespace

void ObNewRow::reset()
{ cells_ = nullptr; count_ = 0; projector_size_ = 0; projector_ = nullptr; }
int64_t ObNewRow::get_deep_copy_size() const { return row_memory_size(*this, false); }
int64_t ObNewRow::get_projected_deep_copy_size() const { return row_memory_size(*this, true); }
bool ObNewRow::operator==(const ObNewRow &other) const
{
  if (get_count() != other.get_count()) { return false; }
  bool equal = true;
  for (int64_t i = 0; equal && i < get_count(); ++i) {
    const DatumValue &left = get_cell(i), &right = other.get_cell(i);
    // Ordinary row equality follows the old null-safe EQ contract. Ext/Ext
    // equality compares the stored value, including NOP and array addresses;
    // key ordering deliberately rejects those non-bound Ext values.
    int ret = OB_SUCCESS;
    if (left.is_ext() && right.is_ext()) { equal = left.get_ext() == right.get_ext(); }
    else { ret = sql_equal_key_value(left, right, equal); }
    if (ret != OB_SUCCESS) {
      COMMON_LOG_RET(ERROR, ret, "native row values cannot compare", K(i));
      right_to_die_or_duty_to_live(); equal = false;
    }
  }
  return equal;
}
int ObNewRow::deep_copy(const ObNewRow &source, char *buffer, int64_t length, int64_t &pos)
{ return copy_flat_row(source, *this, buffer, length, pos, false); }
int ObNewRow::deep_copy_projected(const ObNewRow &source, char *buffer, int64_t length, int64_t &pos)
{ return copy_flat_row(source, *this, buffer, length, pos, true); }
int ObNewRow::clone(ObIAllocator &owner, ObNewRow &output) const
{
  const int64_t size = get_deep_copy_size();
  if (size == INT64_MAX) { return OB_SIZE_OVERFLOW; }
  if (size == 0) { ObNewRow empty; empty.count_ = count_; output = empty; return OB_SUCCESS; }
  char *memory = static_cast<char *>(owner.alloc(size));
  if (memory == nullptr) { return OB_ALLOCATE_MEMORY_FAILED; }
  ObNewRow candidate; int64_t pos = 0;
  const int ret = candidate.deep_copy(*this, memory, size, pos);
  if (ret == OB_SUCCESS) { output = candidate; } else { owner.free(memory); }
  return ret;
}
int ObNewRow::construct(char *buffer, int64_t length, int64_t &pos, ObNewRow *&output)
{
  if (buffer == nullptr || pos < 0 || pos > length) { return OB_INVALID_ARGUMENT; }
  if (length - pos < sizeof(ObNewRow)) { return OB_SIZE_OVERFLOW; }
  auto *header = reinterpret_cast<ObNewRow *>(buffer + pos);
  if (header->count_ < 0 || header->projector_size_ < 0) { return OB_INVALID_DATA; }
  if (header->count_ > INT64_MAX / sizeof(DatumValue)
      || header->projector_size_ > INT64_MAX / sizeof(int32_t)) { return OB_SIZE_OVERFLOW; }
  int64_t end = pos;
  ObNewRow candidate = *header;
  // Verify the complete body without following or rewriting stale pointers.
  // A second pass publishes relocation only after every span is in bounds.
  for (int pass = 0; pass < 2; ++pass) {
    int64_t next = pos + sizeof(ObNewRow); char *memory = nullptr;
    int ret = take_span(buffer, length, next, sizeof(DatumValue) * header->count_, memory);
    if (ret != OB_SUCCESS) { return ret; }
    auto *values = reinterpret_cast<DatumValue *>(memory);
    for (int64_t i = 0; ret == OB_SUCCESS && i < header->count_; ++i) {
      ret = DatumValueBuffer::relocate(values[i], buffer, length, next, pass == 1);
    }
    if (ret != OB_SUCCESS) { return ret; }
    candidate.cells_ = values; candidate.projector_ = nullptr;
    if (header->projector_size_ > 0) {
      if (OB_SUCCESS != (ret = take_span(buffer, length, next, sizeof(int32_t) * header->projector_size_, memory))) { return ret; }
      candidate.projector_ = reinterpret_cast<int32_t *>(memory);
    }
    end = next;
  }
  *header = candidate; output = header; pos = end; return OB_SUCCESS;
}
int ObNewRow::serialize(char *buffer, int64_t length, int64_t &pos) const
{
  int ret = serialization::encode_vi64(buffer, length, pos, count_);
  for (int64_t i = 0; ret == OB_SUCCESS && i < count_; ++i) {
    ret = DatumParameterCodec::encode_value(cells_[i].datum_, cells_[i].meta_, buffer, length, pos);
  }
  if (ret == OB_SUCCESS) { ret = serialization::encode_vi64(buffer, length, pos, projector_size_); }
  for (int64_t i = 0; ret == OB_SUCCESS && i < projector_size_; ++i) {
    ret = serialization::encode_vi32(buffer, length, pos, projector_[i]);
  }
  return ret;
}
int64_t ObNewRow::get_serialize_size() const
{
  int64_t size = serialization::encoded_length_vi64(count_)
      + serialization::encoded_length_vi64(projector_size_);
  for (int64_t i = 0; i < count_; ++i) {
    int64_t bytes = 0;
    if (DatumParameterCodec::value_size(cells_[i].datum_, cells_[i].meta_, bytes) != OB_SUCCESS) { return 0; }
    if (!add_size(size, bytes)) { return 0; }
  }
  for (int64_t i = 0; i < projector_size_; ++i) {
    if (!add_size(size, serialization::encoded_length_vi32(projector_[i]))) { return 0; }
  }
  return size;
}
int ObNewRow::deserialize(ObIAllocator &owner, const char *buffer, int64_t length, int64_t &pos)
{
  int ret = OB_SUCCESS;
  int64_t next = pos, count = 0, projector_size = 0;
  ObArenaAllocator scratch("RowDecode");
  DatumValue *values = nullptr; int32_t *projector = nullptr;
  if (pos < 0 || pos > length || (next < length && buffer == nullptr)) { ret = OB_INVALID_ARGUMENT; }
  else if (next < length && OB_FAIL(serialization::decode_vi64(buffer, length, next, &count))) {}
  else if (count < 0) { ret = OB_INVALID_DATA; }
  else if (count > count_) { ret = OB_ERR_SYS; }
  else if (count > 0 && cells_ == nullptr) { ret = OB_NOT_INIT; }
  else if (count > INT64_MAX / sizeof(DatumValue)) { ret = OB_SIZE_OVERFLOW; }
  else if (count > 0) {
    void *memory = scratch.alloc(sizeof(DatumValue) * count);
    if (memory == nullptr) { ret = OB_ALLOCATE_MEMORY_FAILED; }
    else { values = new (memory) DatumValue[count]; }
  }
  for (int64_t i = 0; OB_SUCC(ret) && i < count; ++i) {
    values[i] = cells_[i];
    if (next < length) {
      ret = DatumParameterCodec::decode_unis_value(buffer, length, next, scratch, values[i].datum_, values[i].meta_);
    }
  }
  if (OB_SUCC(ret) && next < length) { ret = serialization::decode_vi64(buffer, length, next, &projector_size); }
  if (OB_SUCC(ret) && projector_size < 0) { ret = OB_INVALID_DATA; }
  else if (OB_SUCC(ret) && projector_size > projector_size_) { ret = OB_ERR_SYS; }
  else if (OB_SUCC(ret) && projector_size > 0 && projector_ == nullptr) { ret = OB_NOT_INIT; }
  else if (OB_SUCC(ret) && projector_size > INT64_MAX / sizeof(int32_t)) { ret = OB_SIZE_OVERFLOW; }
  else if (OB_SUCC(ret) && projector_size > 0) {
    projector = static_cast<int32_t *>(scratch.alloc(sizeof(int32_t) * projector_size));
    if (projector == nullptr) { ret = OB_ALLOCATE_MEMORY_FAILED; }
  }
  for (int64_t i = 0; OB_SUCC(ret) && i < projector_size; ++i) {
    projector[i] = projector_[i];
    if (next < length) { ret = serialization::decode_vi32(buffer, length, next, projector + i); }
  }
  if (OB_SUCC(ret)) {
    ObNewRow staged(values, count), owned;
    staged.projector_ = projector; staged.projector_size_ = projector_size;
    if (OB_FAIL(staged.clone(owner, owned))) {}
    else {
      for (int64_t i = 0; i < count; ++i) { cells_[i] = owned.cells_[i]; }
      for (int64_t i = 0; i < projector_size; ++i) { projector_[i] = owned.projector_[i]; }
      count_ = count; projector_size_ = projector_size; pos = next;
    }
  }
  return ret;
}
} // namespace common
} // namespace oceanbase
