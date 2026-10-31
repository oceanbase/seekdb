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

namespace oceanbase
{
namespace sql
{

// Query-frame wire value: owned FullType, explicit state/flags, and typed value
// bytes. No Datum object representation, frame header, or process address is
// serialized. The frame protocol supplies its version. Reference spans retain
// their actual width (in particular zero/short Decimal coefficients).
// SQL-array wrappers use the existing recursive domain codec; PL addresses
// have no wire representation. Failure publishes neither value/type nor pos.
class NativeFrameValueCodec
{
public:
  static int size(const common::SqlType &type, const common::Datum &value, int64_t &output)
  {
    using namespace common;
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
        if (OB_FAIL(DatumParameterCodec::array_size(*array, span))) { return ret; }
        if (__builtin_add_overflow(span, int64_t(14), &span)) { return OB_SIZE_OVERFLOW; }
      }
      if (__builtin_add_overflow(bytes, span, &bytes)) { return OB_SIZE_OVERFLOW; }
    }
    output = bytes;
    return OB_SUCCESS;
  }

  static int encode(const common::SqlType &type, const common::Datum &value,
                    char *buf, int64_t length, int64_t &pos)
  {
    using namespace common;
    int64_t bytes = 0;
    int ret = size(type, value, bytes);
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
      if (OB_FAIL(DatumParameterCodec::array_size(array, array_bytes))) {
      } else if (OB_FAIL(serialization::encode_i32(buf, length, cursor, transient_size))) {
      } else if (OB_FAIL(serialization::encode_i8(buf, length, cursor, level))) {
      } else if (OB_FAIL(serialization::encode_i8(buf, length, cursor, collation))) {
      } else if (OB_FAIL(serialization::encode_i64(buf, length, cursor, array_bytes))) {
      } else { ret = DatumParameterCodec::encode_array(array, buf, length, cursor); }
    } else if (value.length() > 0) {
      MEMCPY(buf + cursor, value.get_reference(), value.length());
      cursor += value.length();
    }
    if (ret == OB_SUCCESS) { pos = cursor; }
    return ret;
  }

  static int decode(const char *buf, int64_t length, int64_t &pos,
                    common::ObIAllocator &owner, const common::SqlType *&type,
                    common::Datum &value)
  {
    using namespace common;
    if (buf == nullptr || pos < 0 || pos > length) { return OB_INVALID_ARGUMENT; }
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
      ret = decode_array(buf, length, cursor, owner, *candidate_type, span, flags, candidate);
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

private:
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

  static int decode_array(const char *buf, int64_t length, int64_t &pos,
                          common::ObIAllocator &owner, const common::SqlType &type,
                          int32_t span, int16_t flags, common::Datum &value)
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
      if (OB_FAIL(DatumParameterCodec::decode_array(buf, end, pos, owner, array))) {
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
