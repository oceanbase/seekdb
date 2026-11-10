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

#include "common/datum/datum.h"
#include "common/datum/datum_parameter_array.h"

namespace oceanbase
{
namespace common
{
class SqlType;

// A value and its complete, immutable declaration. This descriptor owns
// neither object: a retaining boundary must copy both into its actual owner.
// It is deliberately distinct from the physical DatumValue/DatumParameter ABI.
struct NativeTypedDatum
{
  NativeTypedDatum() : datum_(), type_(nullptr) {}
  Datum datum_;
  const SqlType *type_;
  TO_STRING_KV(K_(datum), KP_(type));
};

static_assert(sizeof(NativeTypedDatum) == 24, "Native typed values are 24 bytes");
static_assert(std::is_standard_layout<NativeTypedDatum>::value, "Stable typed value layout");
static_assert(std::is_trivially_copyable<NativeTypedDatum>::value, "Copying borrows its owner");

namespace native_parameter_detail
{
inline int array_view(const NativeTypedDatum &value, const DatumParameterArray *&output)
{
  if (value.type_ == nullptr || !value.type_->legacy_meta().is_ext_sql_array()
      || !value.datum_.is_present()) { return OB_INVALID_ARGUMENT; }
  alignas(8) char scratch[64];
  ObDatum legacy;
  int ret = DatumCopyPlan(*value.type_).legacy_view(value.datum_, legacy, scratch, sizeof(scratch));
  int64_t address = 0;
  if (ret == OB_SUCCESS) {
    if (legacy.has_ext_value()) { address = legacy.get_ext(); }
    else if (legacy.ptr_ == nullptr || legacy.len_ != sizeof(address)) { ret = OB_INVALID_ARGUMENT; }
    else { MEMCPY(&address, legacy.ptr_, sizeof(address)); }
  }
  if (ret == OB_SUCCESS) {
    const auto *array = reinterpret_cast<const DatumParameterArray *>(address);
    if (array == nullptr || array->count_ < 0 || (array->count_ > 0 && array->data_ == nullptr)) {
      ret = OB_INVALID_ARGUMENT;
    } else { output = array; }
  }
  return ret;
}

inline int validate_graph(const NativeTypedDatum &value, int depth)
{
  if (depth > 64) { return OB_SIZE_OVERFLOW; }
  if (value.type_ == nullptr) { return OB_NOT_INIT; }
  int64_t span = 0;
  int ret = DatumCopyPlan(*value.type_).carrier_span_size(value.datum_, span);
  if (ret == OB_SUCCESS && value.datum_.is_present() && value.type_->legacy_meta().is_ext_sql_array()) {
    const DatumParameterArray *array = nullptr;
    ret = array_view(value, array);
    for (int64_t i = 0; ret == OB_SUCCESS && i < array->count_; ++i) {
      NativeTypedDatum leaf;
      if (OB_FAIL(array->get_native_element(i, leaf.datum_, leaf.type_))) {
      } else { ret = validate_graph(leaf, depth + 1); }
    }
  }
  return ret;
}

// This concrete SQL-array graph uses one physical-native value per cell plus
// its own real declaration. No outer element type supplies a missing leaf.
// Partial allocations have the existing SqlType::clone_to bulk-owner lifetime.
inline int clone_array(const DatumParameterArray &source, ObIAllocator &bulk_owner,
                       DatumParameterArray *&output, int depth)
{
  if (depth > 64) { return OB_SIZE_OVERFLOW; }
  if (source.count_ < 0 || (source.count_ > 0 && source.data_ == nullptr)) { return OB_INVALID_ARGUMENT; }
  DatumParameterArray *candidate = DatumParameterArray::alloc(bulk_owner, source.count_);
  int ret = candidate == nullptr ? OB_ALLOCATE_MEMORY_FAILED : OB_SUCCESS;
  if (ret == OB_SUCCESS) {
    candidate->element_ = source.element_;
    candidate->source_value_size_ = source.source_value_size_;
  }
  for (int64_t i = 0; ret == OB_SUCCESS && i < source.count_; ++i) {
    NativeTypedDatum semantic;
    const SqlType *owned_type = nullptr;
    DatumParameter physical;
    if (OB_FAIL(source.get_native_element(i, semantic.datum_, semantic.type_))) {
    } else if (OB_FAIL(semantic.type_->clone_to(bulk_owner, owned_type))) {
    } else if (source.data_[i].is_ext_sql_array() && !source.data_[i].is_null()) {
      const DatumParameterArray *nested = nullptr;
      DatumParameterArray *owned = nullptr;
      if (OB_FAIL(array_view(semantic, nested))) {
      } else if (OB_FAIL(clone_array(*nested, bulk_owner, owned, depth + 1))) {
      } else {
        physical = source.data_[i];
        ret = physical.set_array_address(owned, bulk_owner);
      }
    } else { ret = physical.deep_copy(source.data_[i], bulk_owner); }
    if (ret == OB_SUCCESS) {
      candidate->data_[i] = physical;
      ret = candidate->set_element_type_borrowed(i, *owned_type, bulk_owner);
    }
  }
  if (ret == OB_SUCCESS) { output = candidate; }
  return ret;
}
} // namespace native_parameter_detail

// Retain a parameter value and declaration in a bulk-lifetime owner. Ordinary
// reference bytes and SQL-array graphs are owned; PL/Cursor/LOB pointees keep
// their original same-domain borrow. This is not NativeSqlValueOwner deep retain.
// On failure output is unchanged and unpublished allocations stay in bulk_owner.
inline int clone_native_parameter_value(const NativeTypedDatum &source,
    ObIAllocator &bulk_owner, NativeTypedDatum &output)
{
  int ret = native_parameter_detail::validate_graph(source, 0);
  NativeTypedDatum candidate;
  if (ret == OB_SUCCESS) { ret = source.type_->clone_to(bulk_owner, candidate.type_); }
  if (ret == OB_SUCCESS && source.datum_.is_present() && source.type_->legacy_meta().is_ext_sql_array()) {
    const DatumParameterArray *array = nullptr;
    DatumParameterArray *owned = nullptr;
    if (OB_FAIL(native_parameter_detail::array_view(source, array))) {
    } else if (OB_FAIL(native_parameter_detail::clone_array(*array, bulk_owner, owned, 0))) {
    } else if (source.datum_.is_inline_extended()) {
      candidate.datum_.set_inline_extended(reinterpret_cast<int64_t>(owned),
          source.datum_.get_extend_type(), source.datum_.get_extend_size(),
          source.datum_.get_extend_collation_level(), source.datum_.get_extend_collation(),
          source.datum_.inline_extended_payload_size() == DATUM_LEGACY_EXTEND_SIZE);
    } else {
      // Preserve the prepared carrier's supported 8B/15B/16B encoding, replacing
      // only the graph address. The preceding CopyPlan validation bounds it.
      const int64_t length = source.datum_.length();
      const int64_t offset = source.datum_.has_ext_value() && length == DATUM_LEGACY_EXTEND_SIZE ? 8 : 0;
      void *bytes = bulk_owner.alloc(length);
      if (bytes == nullptr) { ret = OB_ALLOCATE_MEMORY_FAILED; }
      else {
        MEMCPY(bytes, source.datum_.get_reference(), length);
        const int64_t address = reinterpret_cast<int64_t>(owned);
        MEMCPY(static_cast<char *>(bytes) + offset, &address, sizeof(address));
        ret = candidate.datum_.set_reference(bytes, length, source.datum_.reference_flags());
      }
    }
  } else if (ret == OB_SUCCESS && source.datum_.is_present()
      && source.type_->legacy_meta().is_pl_extend_type()) {
    // Validation above still uses the complete declaration. This native
    // retaining boundary copies only the carrier, borrowing the original PL
    // resource; an explicitly borrowed old descriptor need not be allocated.
    candidate.datum_.set_inline_extended(source.datum_.get_ext(),
        source.datum_.get_extend_type(), source.datum_.get_extend_size(),
        source.datum_.get_extend_collation_level(), source.datum_.get_extend_collation(),
        source.datum_.is_inline_extended()
            ? source.datum_.inline_extended_payload_size() == DATUM_LEGACY_EXTEND_SIZE
            : source.datum_.length() == DATUM_LEGACY_EXTEND_SIZE);
  } else if (ret == OB_SUCCESS) {
    const DatumCopyPlan plan(*source.type_);
    int64_t bytes = 0;
    if (OB_FAIL(plan.carrier_span_size(source.datum_, bytes))) {
    } else {
      char *buffer = bytes == 0 ? nullptr : static_cast<char *>(bulk_owner.alloc(bytes));
      if (bytes > 0 && buffer == nullptr) { ret = OB_ALLOCATE_MEMORY_FAILED; }
      else {
        int64_t position = 0;
        ret = plan.copy_carrier_span_borrowing_resources(source.datum_, candidate.datum_, buffer, bytes, position);
      }
    }
  }
  if (ret == OB_SUCCESS) { output = candidate; }
  return ret;
}

// Synchronous prepared-type export. Scalars/normalized Decimal coefficients
// borrow caller scratch; ordinary references borrow source bytes. A physical
// wire adapter must keep its own historical current metadata alongside this
// view, and native array wire must additionally encode every bound leaf Type.
inline int export_native_parameter_value(const NativeTypedDatum &source,
    ObDatum &output, void *caller_scratch, int64_t capacity)
{
  if (capacity < 0) { return OB_INVALID_ARGUMENT; }
  if (capacity < 64) { return OB_BUF_NOT_ENOUGH; }
  if (caller_scratch == nullptr) { return OB_INVALID_ARGUMENT; }
  int ret = native_parameter_detail::validate_graph(source, 0);
  if (ret == OB_SUCCESS) {
    ret = DatumCopyPlan(*source.type_).legacy_view(source.datum_, output, caller_scratch, 64);
  }
  return ret;
}

} // namespace common
} // namespace oceanbase
