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

// SQL array cells retain their historical physical value and metadata. A real
// typed producer may additionally bind one complete declaration per cell; that
// directory contains no values and is absent from the historical wire format.
struct DatumParameterArray
{
  DatumParameterArray()
    : data_(nullptr), count_(0), element_(), source_value_size_(0), element_types_(nullptr) {}

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

  const SqlType *get_element_type(int64_t index) const
  {
    return index >= 0 && index < count_ && element_types_ != nullptr
        ? element_types_[index] : nullptr;
  }

  // The declaration is supplied by the actual producer, not recovered from
  // element_ or physical metadata. Its lifetime remains with that producer.
  // The directory has bulk lifetime; generic physical release does not own it.
  int set_element_type_borrowed(int64_t index, const SqlType &type,
                                ObIAllocator &bulk_metadata_owner)
  {
    int ret = check_index(index);
    Datum semantic;
    if (ret == OB_SUCCESS) { ret = import_element(data_[index], type, semantic); }
    if (ret == OB_SUCCESS) { ret = prepare_type_directory(bulk_metadata_owner); }
    if (ret == OB_SUCCESS) { element_types_[index] = &type; }
    return ret;
  }

  // The returned reference bytes still belong to this physical cell. Inline
  // values are copied into value; no view into a local scalar scratch escapes.
  int get_native_element(int64_t index, Datum &value, const SqlType *&type) const
  {
    int ret = check_index(index);
    const SqlType *bound = get_element_type(index);
    Datum candidate;
    if (ret == OB_SUCCESS && bound == nullptr) { ret = OB_NOT_INIT; }
    if (ret == OB_SUCCESS) { ret = import_element(data_[index], *bound, candidate); }
    if (ret == OB_SUCCESS) { value = candidate; type = bound; }
    return ret;
  }

  // Own only the physical span and type-pointer directory. The supplied Type
  // and resource/array pointee must already outlive this array. For a retaining
  // boundary, clone_native_parameter_value owns the complete typed graph.
  int set_native_element_borrowed_type(int64_t index, const Datum &value,
      const SqlType &type, const DatumParameterMetadata &attributes,
      ObIAllocator &physical_and_metadata_owner)
  {
    int ret = check_index(index);
    alignas(8) char scratch[64];
    ObDatum legacy;
    int64_t array_address = 0;
    DatumParameter candidate;
    Datum checked;
    if (ret == OB_SUCCESS) {
      ret = DatumCopyPlan(type).legacy_view(value, legacy, scratch, sizeof(scratch));
    }
    if (ret == OB_SUCCESS && !legacy.is_null() && type.legacy_meta().is_ext_sql_array()
        && attributes.type_meta_.is_ext_sql_array() && legacy.has_ext_value()) {
      // The physical array domain has an 8B address wrapper even when a
      // semantic producer uses the supported extended carrier encoding.
      array_address = legacy.get_ext();
      legacy.set_string(reinterpret_cast<const char *>(&array_address), sizeof(array_address));
    }
    if (ret == OB_SUCCESS) {
      ret = candidate.assign_legacy_owned(legacy, attributes.type_meta_, physical_and_metadata_owner);
    }
    if (ret == OB_SUCCESS) {
      candidate.set_metadata(attributes);
      ret = import_element(candidate, type, checked);
    }
    if (ret == OB_SUCCESS) { ret = prepare_type_directory(physical_and_metadata_owner); }
    if (ret == OB_SUCCESS) { data_[index] = candidate; element_types_[index] = &type; }
    return ret;
  }

  // Release only a value owned by clone_parameter() or deep_copy(), never a
  // borrowed descriptor. Cloned arrays are acyclic and have independent leaves.
  static void release_owned(DatumParameter &value, ObIAllocator &allocator)
  {
    if (!value.is_null()) {
      if (value.is_ext_sql_array()) {
        DatumParameterArray *array = value.get_array<DatumParameterArray>();
        release_array(array, allocator);
      }
      if (value.datum_.is_present() && !value.datum_.is_inline_extended()
          && physical_reference(value.get_type_meta())) {
        allocator.free(const_cast<void *>(value.datum_.get_reference()));
      }
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

  // Recursively owns physical descriptors and payloads in allocator. Runtime
  // Type provenance is deliberately not carried into this legacy-domain copy;
  // use the typed cloner when declarations must survive source-owner reset.
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
          ret = copy.set_array_address(owned, allocator);
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
private:
  int check_index(int64_t index) const
  { return index < 0 || index >= count_ || data_ == nullptr ? OB_INVALID_ARGUMENT : OB_SUCCESS; }

  static bool physical_reference(const SqlTypeMeta &meta)
  {
    const SqlValueDatumMapType mapping = ObDatum::get_sql_value_datum_map_type(meta.get_type());
    return meta.is_unknown() || meta.is_ext_sql_array()
        || (mapping != OBJ_DATUM_8BYTE_DATA && mapping != OBJ_DATUM_4BYTE_DATA
            && mapping != OBJ_DATUM_1BYTE_DATA);
  }

  static int import_element(const DatumParameter &physical, const SqlType &type, Datum &value)
  {
    alignas(8) char scratch[64];
    ObDatum legacy;
    int ret = physical.export_legacy(legacy, scratch, sizeof(scratch));
    if (ret == OB_SUCCESS && !legacy.is_null() && !legacy.is_min()
        && !legacy.is_max() && !legacy.is_nop()) {
      const SqlTypeMeta &meta = physical.get_type_meta();
      // This is a representation binding, never a cast between different
      // current kinds/subtypes/scales. A NULL or special state has no such
      // payload, but still requires its real declaration at the typed boundary.
      if (meta.get_type() != type.kind()
          || (meta.is_ext() && meta.get_extend_type() != type.legacy_meta().get_extend_type())
          || (meta.is_decimal_int() && meta.get_scale() != type.legacy_meta().get_scale())) {
        ret = OB_INVALID_ARGUMENT;
      }
    }
    if (ret == OB_SUCCESS) { ret = DatumCopyPlan(type).import_legacy(legacy, value); }
    if (ret == OB_SUCCESS && value.is_present() && physical.datum_.is_inline_extended()) {
      // The borrowed legacy view above uses this function's scratch. Metadata
      // validation did not cast the value, so publish its self-contained bits.
      value = physical.datum_;
    }
    return ret;
  }

  int prepare_type_directory(ObIAllocator &bulk_owner)
  {
    int ret = OB_SUCCESS;
    if (element_types_ == nullptr) {
      if (count_ < 0 || count_ > INT64_MAX / static_cast<int64_t>(sizeof(const SqlType *))) {
        ret = OB_SIZE_OVERFLOW;
      } else if (count_ > 0) {
        auto **types = static_cast<const SqlType **>(bulk_owner.alloc(count_ * sizeof(const SqlType *)));
        if (types == nullptr) { ret = OB_ALLOCATE_MEMORY_FAILED; }
        else {
          for (int64_t i = 0; i < count_; ++i) { types[i] = nullptr; }
          element_types_ = types;
        }
      }
    }
    return ret;
  }

  // Optional borrowed immutable declarations, with a bulk-owned pointer
  // directory. Generic alloc/decode/clone leave this null and unprepared.
  const SqlType **element_types_;
};

} // namespace common
} // namespace oceanbase
