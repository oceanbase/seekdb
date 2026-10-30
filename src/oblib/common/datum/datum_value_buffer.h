/* Copyright (c) 2025 OceanBase. Licensed under the Apache License, Version 2.0. */
#pragma once

#include "common/datum/datum_value_flat.h"
#include "lib/allocator/ob_allocator.h"

namespace oceanbase { namespace common {

// Reusable native descriptors and small payloads. The explicit owner must
// outlive this buffer and every row borrowing it. Variable/array payloads stay
// borrowed; retaining a complete value requires an explicit owning row clone.
class DatumValueBuffer final
{
public:
  DatumValueBuffer() : owner_(nullptr), values_(nullptr), capacity_(0) {}
  explicit DatumValueBuffer(ObIAllocator &owner) : owner_(&owner), values_(nullptr), capacity_(0) {}
  int init(ObIAllocator *owner)
  {
    if (owner == nullptr) { return OB_INVALID_ARGUMENT; }
    if (owner_ != nullptr) { return OB_INIT_TWICE; }
    owner_ = owner; return OB_SUCCESS;
  }
  ~DatumValueBuffer() { reset(); }
  DatumValueBuffer(const DatumValueBuffer &) = delete;
  DatumValueBuffer &operator=(const DatumValueBuffer &) = delete;

  static int64_t get_deep_copy_size(const DatumValue &value) { return NativeValueFlat::size(value); }
  static int deep_copy(const DatumValue &source, DatumValue &output, char *buffer, int64_t length, int64_t &pos)
  { return NativeValueFlat::copy(source, output, buffer, length, pos); }
  static int relocate(DatumValue &value, char *buffer, int64_t length, int64_t &pos, bool publish = true)
  { return NativeValueFlat::relocate(value, buffer, length, pos, publish); }
  static int make_offsets(DatumValue &value, char *buffer, int64_t length, int64_t &pos,
                           const char *base = nullptr, bool publish = true)
  { return NativeValueFlat::make_offsets(value, buffer, length, pos, base, publish); }
  static int64_t allocation_size(int64_t count)
  {
    return count < 0 || count > INT64_MAX / (sizeof(DatumValue) + SCALAR_BYTES)
        ? INT64_MAX : count * (sizeof(DatumValue) + SCALAR_BYTES);
  }
  // The caller owns this one block directly; no temporary buffer object owns
  // or frees it. This is also the storage used by ob_create_row(s).
  static int allocate(ObIAllocator &owner, int64_t count, DatumValue *&output)
  {
    int ret = OB_SUCCESS;
    const int64_t size = allocation_size(count);
    if (count < 0) { ret = OB_INVALID_ARGUMENT; }
    else if (size == INT64_MAX) { ret = OB_SIZE_OVERFLOW; }
    else if (count == 0) { output = nullptr; }
    else {
      void *memory = owner.alloc(size);
      if (memory == nullptr) { ret = OB_ALLOCATE_MEMORY_FAILED; }
      else { initialize(memory, count, output); }
    }
    return ret;
  }
  static void initialize(void *memory, int64_t count, DatumValue *&output)
  {
    auto *values = new (memory) DatumValue[count];
    for (int64_t i = 0; i < count; ++i) { values[i].datum_.ptr_ = payload(values, count, i); }
    output = values;
  }
  int reserve(int64_t count)
  {
    int ret = OB_SUCCESS;
    if (owner_ == nullptr) { ret = OB_NOT_INIT; }
    else if (count < 0) { ret = OB_INVALID_ARGUMENT; }
    else if (count > capacity_) {
      DatumValue *candidate = nullptr;
      if (OB_FAIL(allocate(*owner_, count, candidate))) {}
      for (int64_t i = 0; OB_SUCC(ret) && i < capacity_; ++i) {
        ret = assign_view(values_[i], candidate, count, i);
      }
      if (OB_SUCC(ret)) {
        owner_->free(values_); values_ = candidate; capacity_ = count;
      } else { owner_->free(candidate); }
    }
    return ret;
  }
  void reset()
  {
    if (values_ != nullptr) { owner_->free(values_); }
    values_ = nullptr; capacity_ = 0;
  }
  DatumValue *data() { return values_; }
  const DatumValue *data() const { return values_; }
  int64_t capacity() const { return capacity_; }
  DatumValue &at(int64_t index) { OB_ASSERT(index >= 0 && index < capacity_); return values_[index]; }
  const DatumValue &at(int64_t index) const { OB_ASSERT(index >= 0 && index < capacity_); return values_[index]; }

  // Copies at most 16 payload bytes into the reserved scalar slot; larger
  // bytes and a copied SQL-array address do not gain an owning lifetime.
  static int assign_view(const DatumValue &source, DatumValue *values, int64_t capacity, int64_t index)
  {
    int ret = OB_SUCCESS;
    if (values == nullptr || index < 0 || index >= capacity) { ret = OB_INVALID_ARGUMENT; }
    else if (!source.is_null() && source.datum_.len_ > 0 && source.datum_.ptr_ == nullptr) { ret = OB_INVALID_ARGUMENT; }
    else {
      DatumValue copy = source;
      if (source.is_null()) { copy.datum_.ptr_ = payload(values, capacity, index); }
      else if (source.datum_.len_ <= SCALAR_BYTES) {
        if (source.datum_.len_ > 0) { MEMMOVE(payload(values, capacity, index), source.datum_.ptr_, source.datum_.len_); }
        copy.datum_.ptr_ = payload(values, capacity, index);
      }
      values[index] = copy;
    }
    return ret;
  }
  int assign_view(int64_t index, const DatumValue &source)
  { return assign_view(source, values_, capacity_, index); }
  static int set_null(DatumValue *values, int64_t capacity, int64_t index)
  {
    if (values == nullptr || index < 0 || index >= capacity) { return OB_INVALID_ARGUMENT; }
    values[index].set_null(); values[index].datum_.ptr_ = payload(values, capacity, index); return OB_SUCCESS;
  }
  int set_null(int64_t index) { return set_null(values_, capacity_, index); }
#define DATUM_BUFFER_SCALAR(name, type, storage_type, metadata_setter) \
  static int name(DatumValue *values, int64_t capacity, int64_t index, type value) \
  { return scalar(values, capacity, index, static_cast<storage_type>(value), &SqlTypeMeta::metadata_setter); } \
  int name(int64_t index, type value) { return name(values_, capacity_, index, value); }
  DATUM_BUFFER_SCALAR(set_int, int64_t, int64_t, set_int)
  DATUM_BUFFER_SCALAR(set_uint64, uint64_t, uint64_t, set_uint64)
  DATUM_BUFFER_SCALAR(set_uint32, uint32_t, uint64_t, set_uint32)
  DATUM_BUFFER_SCALAR(set_tinyint, int8_t, int64_t, set_tinyint)
  DATUM_BUFFER_SCALAR(set_bool, bool, int64_t, set_tinyint)
  DATUM_BUFFER_SCALAR(set_float, float, float, set_float)
  DATUM_BUFFER_SCALAR(set_double, double, double, set_double)
  DATUM_BUFFER_SCALAR(set_datetime, int64_t, int64_t, set_datetime)
  DATUM_BUFFER_SCALAR(set_timestamp, int64_t, int64_t, set_timestamp)
  DATUM_BUFFER_SCALAR(set_time, int64_t, int64_t, set_time)
  DATUM_BUFFER_SCALAR(set_date, int32_t, int32_t, set_date)
  DATUM_BUFFER_SCALAR(set_year, uint8_t, uint8_t, set_year)
#undef DATUM_BUFFER_SCALAR
  static int set_string_view(DatumValue *values, int64_t capacity, int64_t index,
                             SqlValueType type, const ObString &bytes)
  {
    if (values == nullptr || index < 0 || index >= capacity) { return OB_INVALID_ARGUMENT; }
    DatumValue value = values[index];
    value.meta_.set_type(type); value.meta_.set_collation_level(CS_LEVEL_IMPLICIT); value.datum_.set_string(bytes);
    return assign_view(value, values, capacity, index);
  }
  int set_string_view(int64_t index, SqlValueType type, const ObString &bytes)
  { return set_string_view(values_, capacity_, index, type, bytes); }
  static int set_varchar_view(DatumValue *values, int64_t capacity, int64_t index, const ObString &bytes)
  { return set_string_view(values, capacity, index, ObVarcharType, bytes); }
  int set_varchar_view(int64_t index, const ObString &bytes)
  { return set_varchar_view(values_, capacity_, index, bytes); }
  static int set_lob_view(DatumValue *values, int64_t capacity, int64_t index,
                          SqlValueType type, const ObString &bytes)
  {
    if (values == nullptr || index < 0 || index >= capacity) { return OB_INVALID_ARGUMENT; }
    DatumValue value = values[index];
    value.meta_.set_type(type); value.meta_.set_inrow();
    if (type != ObCollectionSQLType) { value.meta_.set_collation_level(CS_LEVEL_IMPLICIT); }
    value.datum_.set_string(bytes); return assign_view(value, values, capacity, index);
  }
  int set_lob_view(int64_t index, SqlValueType type, const ObString &bytes)
  { return set_lob_view(values_, capacity_, index, type, bytes); }

private:
  static char *payload(DatumValue *values, int64_t capacity, int64_t index)
  { return reinterpret_cast<char *>(values + capacity) + SCALAR_BYTES * index; }
  template<typename T> static int scalar(DatumValue *values, int64_t capacity,
      int64_t index, T value, void (SqlTypeMeta::*set_meta)())
  {
    static_assert(sizeof(T) <= SCALAR_BYTES, "Reserved native scalar payload");
    if (values == nullptr || index < 0 || index >= capacity) { return OB_INVALID_ARGUMENT; }
    char *bytes = payload(values, capacity, index); MEMCPY(bytes, &value, sizeof(value));
    values[index].datum_.set_string(bytes, sizeof(value)); (values[index].meta_.*set_meta)(); return OB_SUCCESS;
  }
  static constexpr int64_t SCALAR_BYTES = 16;
  ObIAllocator *owner_;
  DatumValue *values_;
  int64_t capacity_;
};
} }
