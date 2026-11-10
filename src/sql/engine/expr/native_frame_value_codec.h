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

#include "common/datum/datum_copy_plan.h"
#include "common/datum/datum_parameter_codec.h"
#include "common/datum/native_typed_datum.h"

namespace oceanbase
{
namespace sql
{

// Query-frame wire value: owned FullType, explicit state/flags, and typed value
// bytes. No Datum object representation, frame header, or process address is
// serialized. The frame protocol supplies its version. Reference spans retain
// their actual width (in particular zero/short Decimal coefficients).
// Version 2 arrays carry each real leaf declaration and semantic value. Version
// 1 is an explicit physical compatibility port, whose arrays remain unprepared.
// Present PL addresses have no native wire representation. Failure publishes
// neither value/type nor pos; partial allocations have the caller bulk lifetime.
class NativeFrameValueCodec
{
public:
  static constexpr int64_t PHYSICAL_ARRAY_VERSION = 1;
  static constexpr int64_t TYPED_ARRAY_VERSION = 2;
  // The enclosing frame/batch version selects this value protocol explicitly.
  static int size(const common::SqlType &type, const common::Datum &value, int64_t &output,
                  int64_t wire_version = TYPED_ARRAY_VERSION)
  { return size_impl(type, value, output, wire_version, 0); }
  static int encode(const common::SqlType &type, const common::Datum &value,
                    char *buf, int64_t length, int64_t &pos,
                    int64_t wire_version = TYPED_ARRAY_VERSION)
  { return encode_impl(type, value, buf, length, pos, wire_version, 0); }
  static int decode(const char *buf, int64_t length, int64_t &pos,
                    common::ObIAllocator &owner, const common::SqlType *&type,
                    common::Datum &value, int64_t wire_version = TYPED_ARRAY_VERSION)
  { return decode_impl(buf, length, pos, owner, type, value, wire_version, 0); }

  // V2 parameter attributes have seven logical fields and no value. Preserve
  // both flag bytes here; the historical ParamFlag/Parameter wire is separate.
  // Failure leaves pos/output unchanged (an encoder may have written bytes).
  static constexpr int64_t metadata_size() { return 30; }
  static int encode_metadata(const common::DatumParameterMetadata &attributes,
                             char *buf, int64_t length, int64_t &pos)
  {
    if (buf == nullptr || pos < 0 || pos > length) { return common::OB_INVALID_ARGUMENT; }
    if (length - pos < metadata_size()) { return common::OB_BUF_NOT_ENOUGH; }
    int64_t cursor = pos;
    const int ret = encode_attributes(attributes, buf, length, cursor);
    if (ret == common::OB_SUCCESS) { pos = cursor; }
    return ret;
  }
  static int decode_metadata(const char *buf, int64_t length, int64_t &pos,
                             common::DatumParameterMetadata &attributes)
  {
    if (buf == nullptr || pos < 0 || pos > length) { return common::OB_INVALID_ARGUMENT; }
    if (length - pos < metadata_size()) { return common::OB_DESERIALIZE_ERROR; }
    int64_t cursor = pos;
    common::DatumParameterMetadata candidate;
    const int ret = decode_attributes(buf, length, cursor, candidate);
    if (ret == common::OB_SUCCESS) { attributes = candidate; pos = cursor; }
    return ret;
  }

private:
  static int size_impl(const common::SqlType &type, const common::Datum &value, int64_t &output,
                       int64_t wire_version, int depth)
  {
    using namespace common;
    if (depth > 64) { return OB_SIZE_OVERFLOW; }
    if (!valid_version(wire_version)) { return OB_NOT_SUPPORTED; }
    if (value.is_inline_extended()) {
      char scratch[16]; Datum wire;
      int ret = value.write_inline_extended_payload(scratch, sizeof(scratch));
      if (ret == OB_SUCCESS) {
        ret = wire.set_reference(scratch, value.inline_extended_payload_size(), Datum::LegacyExtended);
      }
      return ret == OB_SUCCESS ? size_impl(type, wire, output, wire_version, depth) : ret;
    }
    int64_t span = 0;
    int ret = DatumCopyPlan(type).carrier_span_size(value, span);
    int64_t bytes = type.get_serialize_size();
    if (ret != OB_SUCCESS) { return ret; }
    if (bytes < 0 || __builtin_add_overflow(bytes, int64_t(7), &bytes)) { return OB_SIZE_OVERFLOW; }
    if (value.is_present()) {
      if (type.legacy_meta().is_pl_extend_type()) { return OB_NOT_SUPPORTED; }
      if (type.canonical_representation() == DatumRepresentation::Inline64) {
        span = 8;
      } else if (type.legacy_meta().is_ext_sql_array()) {
        const DatumParameterArray *array = get_array(value);
        if (array == nullptr) { return OB_INVALID_ARGUMENT; }
        if (OB_FAIL(array_body_size(*array, span, wire_version, depth))) { return ret; }
        if (__builtin_add_overflow(span, int64_t(14), &span)) { return OB_SIZE_OVERFLOW; }
      }
      if (__builtin_add_overflow(bytes, span, &bytes)) { return OB_SIZE_OVERFLOW; }
    }
    output = bytes;
    return OB_SUCCESS;
  }

  static int encode_impl(const common::SqlType &type, const common::Datum &value,
                    char *buf, int64_t length, int64_t &pos, int64_t wire_version, int depth)
  {
    using namespace common;
    if (value.is_inline_extended()) {
      char scratch[16]; Datum wire;
      int ret = value.write_inline_extended_payload(scratch, sizeof(scratch));
      if (ret == OB_SUCCESS) {
        ret = wire.set_reference(scratch, value.inline_extended_payload_size(), Datum::LegacyExtended);
      }
      return ret == OB_SUCCESS ? encode_impl(type, wire, buf, length, pos, wire_version, depth) : ret;
    }
    int64_t bytes = 0;
    int ret = size_impl(type, value, bytes, wire_version, depth);
    if (ret != OB_SUCCESS) { return ret; }
    if (buf == nullptr || pos < 0 || pos > length) { return OB_INVALID_ARGUMENT; }
    if (bytes > length - pos) { return OB_BUF_NOT_ENOUGH; }
    int64_t cursor = pos;
    if (OB_FAIL(type.serialize(buf, length, cursor))) {
    } else if (OB_FAIL(serialization::encode_i8(buf, length, cursor, static_cast<int8_t>(value.state())))) {
    } else if (OB_FAIL(serialization::encode_i16(buf, length, cursor, value.reference_flags()))) {
    } else if (OB_FAIL(serialization::encode_i32(buf, length, cursor, value.length()))) {
    } else if (!value.is_present()) {
    } else if (type.canonical_representation() == DatumRepresentation::Inline64) {
      uint64_t bits = 0;
      if (type.kind() == ObFloatType || type.kind() == ObUFloatType) {
        const float number = value.get_float();
        uint32_t word = 0;
        MEMCPY(&word, &number, sizeof(word));
        bits = word;
      } else if (type.kind() == ObDoubleType || type.kind() == ObUDoubleType) {
        const double number = value.get_double();
        MEMCPY(&bits, &number, sizeof(bits));
      } else if (unsigned_scalar(type.kind())) {
        bits = value.get_uint();
      } else {
        const int64_t number = value.get_int();
        MEMCPY(&bits, &number, sizeof(bits));
      }
      int64_t word = 0;
      MEMCPY(&word, &bits, sizeof(word));
      ret = serialization::encode_i64(buf, length, cursor, word);
    } else if (type.legacy_meta().is_ext_sql_array()) {
      const DatumParameterArray &array = *get_array(value); // preflight above
      int64_t array_bytes = 0;
      const int32_t transient_size = value.is_ext() ? value.get_extend_size() : 0;
      const uint8_t level = value.is_ext() ? value.get_extend_collation_level() : 0;
      const uint8_t collation = value.is_ext() ? value.get_extend_collation() : 0;
      if (OB_FAIL(array_body_size(array, array_bytes, wire_version, depth))) {
      } else if (OB_FAIL(serialization::encode_i32(buf, length, cursor, transient_size))) {
      } else if (OB_FAIL(serialization::encode_i8(buf, length, cursor, level))) {
      } else if (OB_FAIL(serialization::encode_i8(buf, length, cursor, collation))) {
      } else if (OB_FAIL(serialization::encode_i64(buf, length, cursor, array_bytes))) {
      } else { ret = encode_array_body(array, buf, length, cursor, wire_version, depth); }
    } else if (value.length() > 0) {
      MEMCPY(buf + cursor, value.get_reference(), value.length());
      cursor += value.length();
    }
    if (ret == OB_SUCCESS) { pos = cursor; }
    return ret;
  }

  static int decode_impl(const char *buf, int64_t length, int64_t &pos,
                    common::ObIAllocator &owner, const common::SqlType *&type,
                    common::Datum &value, int64_t wire_version, int depth)
  {
    using namespace common;
    if (buf == nullptr || pos < 0 || pos > length) { return OB_INVALID_ARGUMENT; }
    if (depth > 64) { return OB_SIZE_OVERFLOW; }
    if (!valid_version(wire_version)) { return OB_NOT_SUPPORTED; }
    int ret = OB_SUCCESS;
    int64_t cursor = pos;
    const SqlType *candidate_type = nullptr;
    Datum candidate;
    int8_t state = 0;
    int16_t flags = 0;
    int32_t span = 0;
    if (OB_FAIL(SqlType::deserialize(buf, length, cursor, owner, candidate_type))) {
    } else if (length - cursor < 7) {
      ret = OB_DESERIALIZE_ERROR;
    } else if (OB_FAIL(serialization::decode_i8(buf, length, cursor, &state))) {
    } else if (OB_FAIL(serialization::decode_i16(buf, length, cursor, &flags))) {
    } else if (OB_FAIL(serialization::decode_i32(buf, length, cursor, &span))) {
    } else if (state < 0 || state > static_cast<int8_t>(Datum::State::Max)
               || span < 0 || span > (int32_t(1) << 29) - 1) {
      ret = OB_DESERIALIZE_ERROR;
    } else if (state != static_cast<int8_t>(Datum::State::Present)) {
      if (flags != 0 || span != 0) { ret = OB_DESERIALIZE_ERROR; }
      else if (state == static_cast<int8_t>(Datum::State::Nop)) { candidate.set_nop(); }
      else if (state == static_cast<int8_t>(Datum::State::Min)) { candidate.set_min(); }
      else if (state == static_cast<int8_t>(Datum::State::Max)) { candidate.set_max(); }
    } else if (candidate_type->legacy_meta().is_pl_extend_type()) {
      ret = OB_NOT_SUPPORTED;
    } else if (candidate_type->canonical_representation() == DatumRepresentation::Inline64) {
      int64_t word = 0;
      if (span != 0 || flags != 0 || length - cursor < 8) { ret = OB_DESERIALIZE_ERROR; }
      else if (OB_FAIL(serialization::decode_i64(buf, length, cursor, &word))) {
      } else if (candidate_type->kind() == ObFloatType || candidate_type->kind() == ObUFloatType) {
        const uint64_t bits = static_cast<uint64_t>(word);
        const uint32_t low = static_cast<uint32_t>(bits);
        float number = 0;
        MEMCPY(&number, &low, sizeof(number));
        if ((bits >> 32) != 0) { ret = OB_DESERIALIZE_ERROR; }
        else { candidate.set_float(number); }
      } else if (candidate_type->kind() == ObDoubleType || candidate_type->kind() == ObUDoubleType) {
        double number = 0;
        MEMCPY(&number, &word, sizeof(number));
        candidate.set_double(number);
      } else if (unsigned_scalar(candidate_type->kind())) {
        candidate.set_uint(static_cast<uint64_t>(word));
      } else { candidate.set_int(word); }
    } else if (candidate_type->legacy_meta().is_ext_sql_array()) {
      ret = decode_array(buf, length, cursor, owner, *candidate_type, span, flags, candidate, wire_version, depth);
    } else if (candidate_type->canonical_representation() != DatumRepresentation::Reference
               || span > length - cursor) {
      ret = OB_DESERIALIZE_ERROR;
    } else {
      char *payload = span == 0 ? nullptr : static_cast<char *>(owner.alloc(span));
      if (span > 0 && payload == nullptr) { ret = OB_ALLOCATE_MEMORY_FAILED; }
      else {
        if (span > 0) { MEMCPY(payload, buf + cursor, span); }
        cursor += span;
        ret = candidate.set_reference(payload, span, static_cast<uint16_t>(flags));
      }
    }
    int64_t validated_span = 0;
    if (ret == OB_SUCCESS) { ret = DatumCopyPlan(*candidate_type).carrier_span_size(candidate, validated_span); }
    if (ret == OB_SUCCESS) { type = candidate_type; value = candidate; pos = cursor; }
    return ret;
  }

  static bool valid_version(int64_t version)
  { return version == PHYSICAL_ARRAY_VERSION || version == TYPED_ARRAY_VERSION; }

  static bool unsigned_scalar(common::SqlValueType kind)
  {
    using namespace common;
    return (kind >= ObUTinyIntType && kind <= ObUInt64Type) || kind == ObYearType
        || kind == ObBitType || kind == ObEnumType || kind == ObSetType;
  }

  static const common::DatumParameterArray *get_array(const common::Datum &value)
  {
    using namespace common;
    int64_t address = 0;
    if (value.has_ext_value()) { address = value.get_ext(); }
    else if (value.reference_flags() == 0 && value.length() == sizeof(address)
             && value.get_reference() != nullptr) { MEMCPY(&address, value.get_reference(), sizeof(address)); }
    return reinterpret_cast<const DatumParameterArray *>(address);
  }

  // New typed-array metadata is field-wise and fixed-width (30 bytes). It
  // carries both logical flag bytes, including runtime flags, only in v2;
  // the old Parameter/ParamFlag wire remains untouched.
  static int encode_attributes(const common::DatumParameterMetadata &attributes,
                               char *buf, int64_t length, int64_t &pos)
  {
    using namespace common;
    int ret = OB_SUCCESS;
    int64_t accuracy = 0;
    const uint64_t bits = attributes.accuracy_.get_accuracy();
    MEMCPY(&accuracy, &bits, sizeof(accuracy));
    if (length - pos < metadata_size()) { return OB_BUF_NOT_ENOUGH; }
    if (OB_FAIL(serialization::encode(buf, length, pos, attributes.type_meta_))) {
    } else if (OB_FAIL(serialization::encode_i64(buf, length, pos, accuracy))) {
    } else if (OB_FAIL(serialization::encode_i32(buf, length, pos, attributes.res_flags_))) {
    } else if (OB_FAIL(serialization::encode_i32(buf, length, pos, attributes.raw_text_pos_))) {
    } else if (OB_FAIL(serialization::encode_i32(buf, length, pos, attributes.raw_text_len_))) {
    } else if (OB_FAIL(serialization::encode(buf, length, pos, attributes.param_meta_))) {
    } else if (OB_FAIL(serialization::encode_i8(buf, length, pos, attributes.flag_.flag_))) {
    } else { ret = serialization::encode_i8(buf, length, pos, attributes.flag_.extend_flag_); }
    return ret;
  }

  static int decode_attributes(const char *buf, int64_t length, int64_t &pos,
                               common::DatumParameterMetadata &attributes)
  {
    using namespace common;
    if (length - pos < metadata_size()) { return OB_DESERIALIZE_ERROR; }
    int ret = OB_SUCCESS;
    int64_t accuracy = 0;
    int32_t flags = 0;
    int8_t flag = 0, extended_flag = 0;
    if (OB_FAIL(serialization::decode(buf, length, pos, attributes.type_meta_))) {
    } else if (OB_FAIL(serialization::decode_i64(buf, length, pos, &accuracy))) {
    } else if (OB_FAIL(serialization::decode_i32(buf, length, pos, &flags))) {
    } else if (OB_FAIL(serialization::decode_i32(buf, length, pos, &attributes.raw_text_pos_))) {
    } else if (OB_FAIL(serialization::decode_i32(buf, length, pos, &attributes.raw_text_len_))) {
    } else if (OB_FAIL(serialization::decode(buf, length, pos, attributes.param_meta_))) {
    } else if (OB_FAIL(serialization::decode_i8(buf, length, pos, &flag))) {
    } else if (OB_FAIL(serialization::decode_i8(buf, length, pos, &extended_flag))) {
    } else {
      attributes.accuracy_.set_accuracy(static_cast<uint64_t>(accuracy));
      attributes.res_flags_ = static_cast<uint32_t>(flags);
      attributes.flag_.flag_ = static_cast<uint8_t>(flag);
      attributes.flag_.extend_flag_ = static_cast<uint8_t>(extended_flag);
    }
    return ret;
  }

  // Array element_ is historical physical metadata, not a substitute for any
  // leaf's FullType. V2 preserves all of its actual fields explicitly. V1
  // continues to use the established ObDataType codec in the physical port.
  static constexpr int64_t ARRAY_ELEMENT_METADATA_SIZE = 18;
  static int encode_element_metadata(const common::ObDataType &element,
                                      char *buf, int64_t length, int64_t &pos)
  {
    using namespace common;
    if (length - pos < ARRAY_ELEMENT_METADATA_SIZE) { return OB_BUF_NOT_ENOUGH; }
    int ret = OB_SUCCESS;
    int64_t accuracy = 0;
    const uint64_t bits = element.accuracy_.get_accuracy();
    MEMCPY(&accuracy, &bits, sizeof(accuracy));
    if (OB_FAIL(serialization::encode(buf, length, pos, element.meta_))) {
    } else if (OB_FAIL(serialization::encode_i64(buf, length, pos, accuracy))) {
    } else if (OB_FAIL(serialization::encode_i32(buf, length, pos, static_cast<int32_t>(element.charset_)))) {
    } else if (OB_FAIL(serialization::encode_i8(buf, length, pos, element.is_binary_collation_ ? 1 : 0))) {
    } else { ret = serialization::encode_i8(buf, length, pos, element.is_zero_fill_ ? 1 : 0); }
    return ret;
  }

  static int decode_element_metadata(const char *buf, int64_t length, int64_t &pos,
                                      common::ObDataType &element)
  {
    using namespace common;
    if (length - pos < ARRAY_ELEMENT_METADATA_SIZE) { return OB_DESERIALIZE_ERROR; }
    int ret = OB_SUCCESS;
    int64_t accuracy = 0;
    int32_t charset = 0;
    int8_t binary = 0, zero_fill = 0;
    if (OB_FAIL(serialization::decode(buf, length, pos, element.meta_))) {
    } else if (OB_FAIL(serialization::decode_i64(buf, length, pos, &accuracy))) {
    } else if (OB_FAIL(serialization::decode_i32(buf, length, pos, &charset))) {
    } else if (OB_FAIL(serialization::decode_i8(buf, length, pos, &binary))) {
    } else if (OB_FAIL(serialization::decode_i8(buf, length, pos, &zero_fill))) {
    } else if ((binary != 0 && binary != 1) || (zero_fill != 0 && zero_fill != 1)) {
      ret = OB_DESERIALIZE_ERROR;
    } else {
      element.accuracy_.set_accuracy(static_cast<uint64_t>(accuracy));
      element.charset_ = static_cast<ObCharsetType>(charset);
      element.is_binary_collation_ = binary != 0;
      element.is_zero_fill_ = zero_fill != 0;
    }
    return ret;
  }

  static int array_body_size(const common::DatumParameterArray &array, int64_t &output,
                             int64_t version, int depth)
  {
    using namespace common;
    if (version == PHYSICAL_ARRAY_VERSION) { return DatumParameterCodec::array_size(array, output); }
    if (array.count_ < 0 || (array.count_ > 0 && array.data_ == nullptr)) { return OB_INVALID_ARGUMENT; }
    int64_t bytes = ARRAY_ELEMENT_METADATA_SIZE + 12;
    int ret = OB_SUCCESS;
    for (int64_t i = 0; ret == OB_SUCCESS && i < array.count_; ++i) {
      NativeTypedDatum leaf;
      int64_t leaf_bytes = 0;
      if (OB_FAIL(array.get_native_element(i, leaf.datum_, leaf.type_))) {
      } else if (OB_FAIL(size_impl(*leaf.type_, leaf.datum_, leaf_bytes, version, depth + 1))) {
      } else if (__builtin_add_overflow(leaf_bytes, metadata_size(), &leaf_bytes)
                 || __builtin_add_overflow(bytes, leaf_bytes, &bytes)) { ret = OB_SIZE_OVERFLOW; }
    }
    if (ret == OB_SUCCESS) { output = bytes; }
    return ret;
  }

  static int encode_array_body(const common::DatumParameterArray &array,
                               char *buf, int64_t length, int64_t &pos, int64_t version, int depth)
  {
    using namespace common;
    if (version == PHYSICAL_ARRAY_VERSION) { return DatumParameterCodec::encode_array(array, buf, length, pos); }
    int ret = OB_SUCCESS;
    if (OB_FAIL(encode_element_metadata(array.element_, buf, length, pos))) {
    } else if (OB_FAIL(serialization::encode_i64(buf, length, pos, array.count_))) {
    } else { ret = serialization::encode_i32(buf, length, pos, array.source_value_size_); }
    for (int64_t i = 0; ret == OB_SUCCESS && i < array.count_; ++i) {
      NativeTypedDatum leaf;
      if (OB_FAIL(array.get_native_element(i, leaf.datum_, leaf.type_))) {
      } else if (OB_FAIL(encode_metadata(array.data_[i].get_metadata(), buf, length, pos))) {
      } else { ret = encode_impl(*leaf.type_, leaf.datum_, buf, length, pos, version, depth + 1); }
    }
    return ret;
  }

  // Decoded reference spans already belong to owner. Keep those bytes directly;
  // only small semantic Decimal needs an owned physical coefficient. This
  // avoids a second reference allocation retained in the same decode arena.
  static int bind_decoded_element(common::DatumParameterArray &array, int64_t index,
      const common::NativeTypedDatum &leaf, const common::DatumParameterMetadata &attributes,
      common::ObIAllocator &owner)
  {
    using namespace common;
    if (leaf.datum_.is_present() && leaf.type_->kind() == ObDecimalIntType
        && leaf.type_->canonical_representation() == DatumRepresentation::Inline64) {
      return array.set_native_element_borrowed_type(index, leaf.datum_, *leaf.type_, attributes, owner);
    }
    Datum physical = leaf.datum_;
    int ret = OB_SUCCESS;
    if (leaf.datum_.is_present() && leaf.type_->legacy_meta().is_ext_sql_array()
        && leaf.datum_.has_ext_value()) {
      const int64_t offset = leaf.datum_.length() == DATUM_LEGACY_EXTEND_SIZE ? 8 : 0;
      ret = physical.set_reference(static_cast<const char *>(leaf.datum_.get_reference()) + offset, sizeof(int64_t));
    }
    DatumParameter cell;
    if (ret == OB_SUCCESS) { ret = cell.assign_native_borrowed(physical, attributes.type_meta_); }
    if (ret == OB_SUCCESS) {
      cell.set_metadata(attributes);
      array.data_[index] = cell; // array is not published until the complete body succeeds
      ret = array.set_element_type_borrowed(index, *leaf.type_, owner);
    }
    return ret;
  }

  static int decode_array_body(const char *buf, int64_t length, int64_t &pos,
      common::ObIAllocator &owner, common::DatumParameterArray *&output, int64_t version, int depth)
  {
    using namespace common;
    if (version == PHYSICAL_ARRAY_VERSION) { return DatumParameterCodec::decode_array(buf, length, pos, owner, output); }
    if (pos >= length) { return OB_DESERIALIZE_ERROR; }
    int ret = OB_SUCCESS;
    ObDataType element;
    int64_t count = 0;
    int32_t source_value_size = 0;
    DatumParameterArray *array = nullptr;
    if (OB_FAIL(decode_element_metadata(buf, length, pos, element))) {
    } else if (length - pos < 12) { ret = OB_DESERIALIZE_ERROR; }
    else if (OB_FAIL(serialization::decode_i64(buf, length, pos, &count))) {
    } else if (OB_FAIL(serialization::decode_i32(buf, length, pos, &source_value_size))) {
    } else if (count < 0 || count > (length - pos) / (metadata_size() + 7)) {
      ret = OB_DESERIALIZE_ERROR;
    } else if ((array = DatumParameterArray::alloc(owner, count)) == nullptr) { ret = OB_ALLOCATE_MEMORY_FAILED; }
    else { array->element_ = element; array->source_value_size_ = source_value_size; }
    for (int64_t i = 0; ret == OB_SUCCESS && i < count; ++i) {
      NativeTypedDatum leaf;
      DatumParameterMetadata attributes;
      if (OB_FAIL(decode_metadata(buf, length, pos, attributes))) {
      } else if (OB_FAIL(decode_impl(buf, length, pos, owner, leaf.type_, leaf.datum_, version, depth + 1))) {
      } else { ret = bind_decoded_element(*array, i, leaf, attributes, owner); }
    }
    if (ret == OB_SUCCESS) { output = array; }
    return ret;
  }

  static int decode_array(const char *buf, int64_t length, int64_t &pos,
                          common::ObIAllocator &owner, const common::SqlType &type,
                          int32_t span, int16_t flags, common::Datum &value,
                          int64_t wire_version, int depth)
  {
    using namespace common;
    if (!((flags == 0 && span == 8) || (flags == Datum::LegacyExtended && (span == 15 || span == 16)))
        || length - pos < 14) { return OB_DESERIALIZE_ERROR; }
    int ret = OB_SUCCESS;
    int32_t transient_size = 0;
    int8_t level = 0, collation = 0;
    int64_t array_bytes = 0;
    DatumParameterArray *array = nullptr;
    if (OB_FAIL(serialization::decode_i32(buf, length, pos, &transient_size))) {
    } else if (OB_FAIL(serialization::decode_i8(buf, length, pos, &level))) {
    } else if (OB_FAIL(serialization::decode_i8(buf, length, pos, &collation))) {
    } else if (OB_FAIL(serialization::decode_i64(buf, length, pos, &array_bytes))) {
    } else if (array_bytes < 0 || array_bytes > length - pos
               || (flags == 0 && (transient_size != 0 || level != 0 || collation != 0))) {
      ret = OB_DESERIALIZE_ERROR;
    } else {
      const int64_t end = pos + array_bytes;
      if (OB_FAIL(decode_array_body(buf, end, pos, owner, array, wire_version, depth))) {
      } else if (pos != end) { ret = OB_DESERIALIZE_ERROR; }
      else {
        char *wrapper = static_cast<char *>(owner.alloc(span));
        if (wrapper == nullptr) { ret = OB_ALLOCATE_MEMORY_FAILED; }
        else {
          const int64_t address = reinterpret_cast<int64_t>(array);
          if (span == 8) { MEMCPY(wrapper, &address, sizeof(address)); }
          else if (span == 15) {
            MEMCPY(wrapper, &address, sizeof(address));
            MEMCPY(wrapper + 8, &transient_size, sizeof(transient_size));
            wrapper[12] = type.legacy_meta().get_extend_type(); wrapper[13] = level; wrapper[14] = collation;
          } else {
            wrapper[0] = ObExtendType; wrapper[1] = level; wrapper[2] = collation;
            wrapper[3] = type.legacy_meta().get_extend_type();
            MEMCPY(wrapper + 4, &transient_size, sizeof(transient_size));
            MEMCPY(wrapper + 8, &address, sizeof(address));
          }
          ret = value.set_reference(wrapper, span, flags);
        }
      }
    }
    return ret;
  }
};

} // namespace sql
} // namespace oceanbase
