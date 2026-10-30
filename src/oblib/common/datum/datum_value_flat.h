/* Copyright (c) 2025 OceanBase. Licensed under the Apache License, Version 2.0. */
#pragma once
#include "common/datum/datum_parameter_array.h"
namespace oceanbase { namespace common {
// Flat, explicit-buffer ownership shared by ordinary rows and chunk stores.
// Layout is deterministic: rounded-eight-byte spans; arrays allocate their
// descriptor/parameters, recursively clone children, then store the address.
class NativeValueFlat final
{
public:
  static int64_t span(int64_t bytes)
  { return bytes < 0 || bytes > INT64_MAX - 7 ? INT64_MAX : (bytes + 7) & ~int64_t(7); }
  static bool add_size(int64_t &total, int64_t bytes)
  {
    if (bytes < 0 || bytes > INT64_MAX - total) { total = INT64_MAX; return false; }
    total += bytes; return true;
  }
  static int64_t size(const DatumValue &value, int depth = 0)
  {
    if (depth > 64) { return INT64_MAX; }
    if (value.is_null()) { return 0; }
    if (value.datum_.len_ > 0 && value.datum_.ptr_ == nullptr) { return INT64_MAX; }
    int64_t bytes = 0;
    if (value.is_ext_sql_array()) {
      if (value.datum_.ptr_ == nullptr || value.datum_.len_ != sizeof(int64_t)) { return INT64_MAX; }
      const auto *array = reinterpret_cast<const DatumParameterArray *>(value.get_ext());
      if (array == nullptr || array->count_ < 0
          || array->count_ > (INT64_MAX - sizeof(DatumParameterArray)) / sizeof(DatumParameter)
          || (array->count_ > 0 && array->data_ == nullptr)) { return INT64_MAX; }
      bytes = span(sizeof(DatumParameterArray) + sizeof(DatumParameter) * array->count_);
      for (int64_t i = 0; bytes != INT64_MAX && i < array->count_; ++i) {
        add_size(bytes, size(array->data_[i].value_view(), depth + 1));
      }
      add_size(bytes, span(sizeof(int64_t)));
    } else { bytes = span(MAX(int64_t(1), value.datum_.get_deep_copy_size())); }
    return bytes;
  }
  class BufferOwner final : public ObIAllocator
  {
  public:
    BufferOwner(char *buffer, int64_t length, int64_t &pos) : buffer_(buffer), length_(length), pos_(pos) {}
    void *alloc(int64_t bytes) override
    {
      const int64_t count = span(bytes);
      if (buffer_ == nullptr || pos_ < 0 || pos_ > length_ || count > length_ - pos_) { return nullptr; }
      char *value = buffer_ + pos_; pos_ += count;
      if (count > bytes) { MEMSET(value + bytes, 0, count - bytes); }
      return value;
    }
    void *alloc(int64_t bytes, const ObMemAttr &) override { return alloc(bytes); }
    void free(void *) override {}
  private:
    char *buffer_; int64_t length_; int64_t &pos_;
  };
  static int take_span(char *buffer, int64_t length, int64_t &pos, int64_t bytes, char *&output)
  {
    const int64_t count = span(bytes);
    if (buffer == nullptr || pos < 0 || pos > length || count > length - pos) { return OB_SIZE_OVERFLOW; }
    output = buffer + pos; pos += count; return OB_SUCCESS;
  }
  static int copy(const DatumValue &source, DatumValue &output, char *buffer,
                   int64_t length, int64_t &pos)
  {
    if (buffer == nullptr || pos < 0 || pos > length) { return OB_INVALID_ARGUMENT; }
    const int64_t bytes = size(source);
    if (bytes == INT64_MAX || bytes > length - pos) { return OB_SIZE_OVERFLOW; }
    int64_t next = pos; BufferOwner owner(buffer, length, next); DatumParameter input, copy;
    input.assign_value(source);
    const int ret = DatumParameterArray::clone_parameter(input, copy, owner);
    if (ret == OB_SUCCESS) { output = copy.value_view(); pos = next; }
    return ret;
  }
  // Caller may first validate with publish=false and then publish a complete
  // row. Traversal never follows old pointers, even in nested array headers.
  static int relocate(DatumValue &value, char *buffer, int64_t length, int64_t &pos,
                       bool publish = true)
  {
    int64_t next = pos;
    const int ret = walk(value, buffer, length, next, publish, nullptr, 0);
    if (ret == OB_SUCCESS) { pos = next; }
    return ret;
  }
  // Rewrites every descriptor and nested array address as an offset from base.
  // Restore uses the same traversal, not pointer arithmetic on stale offsets.
  static int make_offsets(DatumValue &value, char *buffer, int64_t length, int64_t &pos,
                           const char *base = nullptr, bool publish = true)
  {
    int64_t next = pos;
    const int ret = walk(value, buffer, length, next, publish, base == nullptr ? buffer : base, 0);
    if (ret == OB_SUCCESS) { pos = next; }
    return ret;
  }
private:
  static char *pointer(char *address, const char *base)
  { return base == nullptr ? address : reinterpret_cast<char *>(reinterpret_cast<uintptr_t>(address) - reinterpret_cast<uintptr_t>(base)); }
  static int walk(DatumValue &value, char *buffer, int64_t length, int64_t &pos,
                    bool publish, const char *offset_base, int depth)
  {
    int ret = OB_SUCCESS;
    if (depth > 64) { return OB_SIZE_OVERFLOW; }
    if (buffer == nullptr || pos < 0 || pos > length) { return OB_SIZE_OVERFLOW; }
    if (value.is_null()) { if (publish) { value.datum_.ptr_ = nullptr; } return ret; }
    char *memory = nullptr;
    if (value.is_ext_sql_array()) {
      if (value.datum_.len_ != sizeof(int64_t)) { return OB_INVALID_DATA; }
      if (length - pos < sizeof(DatumParameterArray)) { return OB_SIZE_OVERFLOW; }
      auto *array = reinterpret_cast<DatumParameterArray *>(buffer + pos);
      if (array->count_ < 0 || array->count_ > (INT64_MAX - sizeof(*array)) / sizeof(DatumParameter)) { return OB_INVALID_DATA; }
      const int64_t bytes = sizeof(*array) + sizeof(DatumParameter) * array->count_;
      if (OB_FAIL(take_span(buffer, length, pos, bytes, memory))) { return ret; }
      auto *data = reinterpret_cast<DatumParameter *>(memory + sizeof(*array));
      for (int64_t i = 0; OB_SUCC(ret) && i < array->count_; ++i) {
        DatumValue child = data[i].value_view();
        if (OB_FAIL(walk(child, buffer, length, pos, publish, offset_base, depth + 1))) {}
        else if (publish) { data[i].datum_ = child.datum_; }
      }
      if (OB_SUCC(ret) && OB_FAIL(take_span(buffer, length, pos, sizeof(int64_t), memory))) {}
      else if (OB_SUCC(ret) && publish) {
        array->data_ = reinterpret_cast<DatumParameter *>(pointer(reinterpret_cast<char *>(data), offset_base));
        const int64_t address = reinterpret_cast<int64_t>(pointer(reinterpret_cast<char *>(array), offset_base));
        MEMCPY(memory, &address, sizeof(address)); value.datum_.ptr_ = pointer(memory, offset_base);
      }
    } else if (OB_FAIL(take_span(buffer, length, pos, MAX(int64_t(1), int64_t(value.datum_.len_)), memory))) {}
    else if (publish) { value.datum_.ptr_ = pointer(memory, offset_base); }
    return ret;
  }
};
} }
