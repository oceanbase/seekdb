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

#include "common/datum/datum_parameter.h"
#include "lib/container/ob_array_wrap.h"

namespace oceanbase
{
namespace common
{

// SQL batch values may have different types/accuracy even when element_ is
// available. Keep a complete native parameter for every value, not just a Datum
// and one shared type. Both descriptors and payloads belong to the caller arena.
struct DatumParameterArray
{
  DatumParameterArray() : data_(nullptr), count_(0), element_(), source_value_size_(0) {}

  static DatumParameterArray *alloc(ObIAllocator &allocator, int64_t count)
  {
    DatumParameterArray *array = nullptr;
    if (count >= 0 && count <= (INT64_MAX - sizeof(DatumParameterArray)) / sizeof(DatumParameter)) {
      const int64_t bytes = sizeof(DatumParameterArray) + sizeof(DatumParameter) * count;
      void *memory = allocator.alloc(bytes);
      if (memory != nullptr) {
        array = new (memory) DatumParameterArray();
        array->data_ = new (static_cast<char *>(memory) + sizeof(DatumParameterArray)) DatumParameter[count];
        array->count_ = count;
      }
    }
    return array;
  }

  int serialize(char *buf, int64_t length, int64_t &pos) const;
  int64_t get_serialize_size() const;

  // Release only a value owned by clone_parameter() or deep_copy(), never a
  // borrowed descriptor. Cloned arrays are acyclic and have independent leaves.
  static void release_owned(DatumParameter &value, ObIAllocator &allocator)
  {
    if (!value.is_null()) {
      if (value.is_ext_sql_array()) {
        DatumParameterArray *array = value.get_array<DatumParameterArray>();
        release_array(array, allocator);
      }
      allocator.free(const_cast<char *>(value.datum_.ptr_));
    }
    value.reset();
  }

  static void release_array(DatumParameterArray *array, ObIAllocator &allocator)
  {
    if (array != nullptr) {
      for (int64_t i = 0; i < array->count_; ++i) {
        release_owned(array->data_[i], allocator);
        array->data_[i].~DatumParameter();
      }
      array->~DatumParameterArray();
      allocator.free(array);
    }
  }

  // Recursively owns array descriptors and payloads in allocator. Unlike a
  // shallow parameter copy, this is safe after the source arena is reset.
  static int clone_parameter(const DatumParameter &source, DatumParameter &output,
                             ObIAllocator &allocator, int depth = 0)
  {
    int ret = OB_SUCCESS;
    DatumParameter copy;
    if (depth > 64) { ret = OB_SIZE_OVERFLOW; }
    else if (!source.is_ext_sql_array() || source.is_null()) { ret = copy.deep_copy(source, allocator); }
    else {
      const DatumParameterArray *array = source.get_array<DatumParameterArray>();
      DatumParameterArray *owned = nullptr;
      if (array == nullptr || array->count_ < 0 || (array->count_ > 0 && array->data_ == nullptr)) {
        ret = OB_INVALID_ARGUMENT;
      } else if ((owned = alloc(allocator, array->count_)) == nullptr) { ret = OB_ALLOCATE_MEMORY_FAILED; }
      else {
        owned->element_ = array->element_;
        owned->source_value_size_ = array->source_value_size_;
        for (int64_t i = 0; ret == OB_SUCCESS && i < array->count_; ++i) {
          ret = clone_parameter(array->data_[i], owned->data_[i], allocator, depth + 1);
        }
        if (ret == OB_SUCCESS) {
          copy = source;
          ret = copy.alloc_datum_reserved_buff(source.get_type_meta(), source.get_precision(), allocator);
          if (ret == OB_SUCCESS) { copy.set_array_address(owned); }
        }
        if (ret != OB_SUCCESS) { release_array(owned, allocator); }
      }
    }
    if (ret == OB_SUCCESS) { output = copy; }
    return ret;
  }

  typedef ObArrayWrap<DatumParameter> Values;
  TO_STRING_KV("data", Values(data_, count_), K_(count), K_(element));
  DatumParameter *data_;
  int64_t count_;
  ObDataType element_;
  // Runtime provenance only; not part of the array or parameter wire format.
  // Live arrays have size 0, matching a newly created old Ext value. A decoded
  // SQL-array value remembers its outer declared body length, which need not
  // equal the length if this mutable native array is serialized again later.
  int32_t source_value_size_;
};

} // namespace common
} // namespace oceanbase
