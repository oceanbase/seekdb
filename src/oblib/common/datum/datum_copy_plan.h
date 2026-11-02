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

#include "common/datum/datum_core.h"
#include "common/datum/datum.h"
#include "common/datum/sql_type.h"
#include "lib/utility/ob_print_utils.h"

namespace oceanbase
{
namespace common
{

// A caller prepares this once from its existing column/parameter metadata and
// can reuse it for values with that metadata. Legacy-to-legacy byte copying
// retains its old cost and validation contract. Native transitions explicitly
// validate physical shape and select one canonical representation from the
// complete type; they do not perform SQL casts or enforce value constraints.
//
// The plan owns no payload. Copying an extended address or a locator copies
// its bytes only, not the object/resource behind it. Recursive SQL-array and
// PL/resource ownership remains the responsibility of their dedicated cloners.
class DatumCopyPlan
{
public:
  using ResourceCloner = int (*)(const SqlType &, const Datum &, ObIAllocator &, Datum &, void *);
  DatumCopyPlan(const SqlTypeMeta &meta, const ObAccuracy &accuracy)
    : meta_(meta), accuracy_(accuracy), type_(nullptr) {}

  // type must remain stable for the lifetime of this prepared plan.
  explicit DatumCopyPlan(const SqlType &type)
    : meta_(type.legacy_meta()), accuracy_(type.accuracy()), type_(&type) {}

  const SqlTypeMeta &get_type_meta() const { return meta_; }
  const ObAccuracy &get_accuracy() const { return accuracy_; }

  // Log only the copied metadata and binding presence, never borrowed trees.
  TO_STRING_KV(K_(meta), K_(accuracy), "has_full_type", type_ != nullptr);

  // This reports external payload bytes, excluding the Datum descriptor and
  // any allocator padding. Unknown Decimal precision stays unknown: its
  // existing coefficient width is preserved instead of inferred or expanded.
  int payload_copy_size(const ObDatum &source, int64_t &size) const
  {
    int ret = OB_SUCCESS;
    if (source.is_null()) {
      size = 0;
    } else if (source.len_ > 0 && source.ptr_ == nullptr) {
      ret = OB_INVALID_ARGUMENT;
    } else {
      size = source.len_;
    }
    return ret;
  }

  // Failure leaves destination unchanged, including when it aliases source.
  int deep_copy(const ObDatum &source, ObDatum &destination, ObIAllocator &allocator) const
  {
    int ret = OB_SUCCESS;
    int64_t size = 0;
    ObDatum copy = source;
    if (OB_FAIL(payload_copy_size(source, size))) {
    } else if (!source.is_null()) {
      char *bytes = static_cast<char *>(allocator.alloc(size > 0 ? size : 1));
      if (bytes == nullptr) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
      } else {
        if (size > 0) { MEMCPY(bytes, source.ptr_, size); }
        copy.ptr_ = bytes;
      }
    }
    if (OB_SUCC(ret)) { destination = copy; }
    return ret;
  }

  // The buffer is owned by the caller. Capacity is checked before bytes or
  // descriptors are published; only a successful copy advances position.
  int deep_copy(const ObDatum &source, ObDatum &destination,
                char *buffer, int64_t capacity, int64_t &position) const
  {
    int ret = OB_SUCCESS;
    int64_t size = 0;
    ObDatum copy = source;
    if (position < 0 || capacity < position) {
      ret = OB_INVALID_ARGUMENT;
    } else if (OB_FAIL(payload_copy_size(source, size))) {
    } else if (size > capacity - position) {
      ret = OB_BUF_NOT_ENOUGH;
    } else if (!source.is_null()) {
      if (buffer == nullptr) {
        ret = OB_INVALID_ARGUMENT;
      } else {
        if (size > 0) { MEMMOVE(buffer + position, source.ptr_, size); }
        copy.ptr_ = buffer + position;
      }
    }
    if (OB_SUCC(ret)) {
      destination = copy;
      position += size;
    }
    return ret;
  }

  // Scalars are loaded with memcpy: persisted row payloads need not be aligned.
  // Reference imports borrow bytes. Neither a locator nor an extended address
  // acquires ownership of its pointee through this boundary.
  int import_legacy(const ObDatum &source, Datum &destination) const
  {
    int ret = OB_SUCCESS;
    Datum candidate;
    if (source.is_null()) {
      candidate.set_null();
    } else if (source.len_ > 0 && source.ptr_ == nullptr) {
      ret = OB_INVALID_ARGUMENT;
    } else if (source.is_ext()) {
      int64_t marker = 0;
      if (OB_FAIL(extended_value(source, marker))) {
      } else if (source.is_extended_null()) {
        candidate.set_null();
      } else if (marker == DATUM_MIN_EXT_VALUE) {
        candidate.set_min();
      } else if (marker == DATUM_MAX_EXT_VALUE) {
        candidate.set_max();
      } else if (marker == ObActionFlag::OP_NOP) {
        candidate.set_nop();
      } else {
        ret = import_present(source, candidate);
      }
    } else {
      ret = import_present(source, candidate);
    }
    if (OB_SUCC(ret)) { destination = candidate; }
    return ret;
  }

  // Exactly the same semantic/state/flag/shape preflight as export_legacy().
  // A caller may validate every column before publishing a complete stored row.
  int legacy_payload_size(const Datum &source, int64_t &size) const
  {
    ObDatum prepared;
    char scratch[64];
    bool reference = false;
    const int ret = prepare_legacy(source, prepared, scratch, reference, true);
    if (ret == OB_SUCCESS) { size = prepared.is_null() ? 0 : prepared.len_; }
    return ret;
  }

  // A temporary old ABI view, never an owning copy. Ordinary reference spans
  // borrow source bytes; inline/state/normalized coefficients borrow scratch.
  // Requiring a complete scratch buffer makes its lifetime explicit at every
  // call site. No pointer to a callee-local temporary can escape this method.
  int legacy_view(const Datum &source, ObDatum &destination, void *scratch, uint32_t capacity) const
  {
    if (capacity < 64) { return OB_BUF_NOT_ENOUGH; }
    if (scratch == nullptr) { return OB_INVALID_ARGUMENT; }
    ObDatum candidate;
    bool reference = false;
    const int ret = prepare_legacy(source, candidate, static_cast<char *>(scratch), reference, true);
    if (ret == OB_SUCCESS) { destination = candidate; }
    return ret;
  }

  int payload_copy_size(const Datum &source, int64_t &size) const
  {
    ObDatum prepared;
    char scratch[64];
    bool reference = false;
    int ret = prepare_legacy(source, prepared, scratch, reference, false);
    if (ret == OB_SUCCESS && source.is_present() && needs_resource_clone()) { ret = OB_NOT_SUPPORTED; }
    if (ret == OB_SUCCESS) { size = reference ? source.length() : 0; }
    return ret;
  }

  // Carrier bytes only. For resource values these are the address/locator
  // wrapper, not the retained object. Only callers that keep the existing
  // pointee owner alive may use this with copy_carrier_span_borrowing_resources.
  int carrier_span_size(const Datum &source, int64_t &size) const
  {
    ObDatum prepared;
    char scratch[64];
    bool reference = false;
    const int ret = prepare_legacy(source, prepared, scratch, reference, false);
    if (ret == OB_SUCCESS) { size = reference ? source.length() : 0; }
    return ret;
  }

  int get_decimal(const Datum &source, ObDatum &coefficient_view, void *scratch, uint32_t capacity) const
  {
    return meta_.get_type() != ObDecimalIntType ? OB_INVALID_ARGUMENT
        : legacy_view(source, coefficient_view, scratch, capacity);
  }

  // The established byte equality of two values in this prepared domain.
  // This is distinct from SQL equality (for example, signed zero/NaN bits).
  int binary_equal(const Datum &left, const Datum &right, bool &equal) const
  {
    alignas(8) char left_scratch[64], right_scratch[64];
    ObDatum left_view, right_view;
    int ret = legacy_view(left, left_view, left_scratch, sizeof(left_scratch));
    if (ret == OB_SUCCESS) { ret = legacy_view(right, right_view, right_scratch, sizeof(right_scratch)); }
    if (ret == OB_SUCCESS) {
      equal = left_view.pack_ == right_view.pack_ && left_view.len_ == 0
          ? true : ObDatum::binary_equal(left_view, right_view);
    }
    return ret;
  }

  // Copy and representation normalization only, not a scale conversion or SQL
  // precision cast. Wide native values keep the source's actual legal width.
  int set_decimal(const ObDecimalInt *coefficient, int32_t bytes, Datum &destination,
                  void *buffer, uint32_t capacity) const
  {
    if (meta_.get_type() != ObDecimalIntType || !decimal_width(bytes)
        || (bytes > 0 && coefficient == nullptr)) { return OB_INVALID_ARGUMENT; }
    ObDatum source;
    source.ptr_ = reinterpret_cast<const char *>(coefficient); source.pack_ = bytes;
    Datum candidate;
    int ret = import_legacy(source, candidate);
    SqlType temporary;
    const SqlType *type = nullptr;
    if (ret == OB_SUCCESS) { ret = resolve_type(temporary, type); }
    if (ret == OB_SUCCESS && type->canonical_representation() == DatumRepresentation::Reference) {
      if (static_cast<uint32_t>(bytes) > capacity) { ret = OB_BUF_NOT_ENOUGH; }
      else if (bytes > 0 && buffer == nullptr) { ret = OB_INVALID_ARGUMENT; }
      else {
        if (bytes > 0) { MEMMOVE(buffer, coefficient, bytes); }
        ret = candidate.set_reference(bytes > 0 ? buffer : nullptr, bytes);
      }
    }
    if (ret == OB_SUCCESS) { destination = candidate; }
    return ret;
  }

  int deep_copy_resource(const Datum &source, Datum &destination, ObIAllocator &allocator,
                         ResourceCloner cloner, void *context) const
  {
    if (!source.is_present() || !needs_resource_clone()) { return deep_copy(source, destination, allocator); }
    if (type_ == nullptr || cloner == nullptr) { return OB_INVALID_ARGUMENT; }
    ObDatum prepared;
    char scratch[64];
    bool reference = false;
    Datum candidate;
    int ret = prepare_legacy(source, prepared, scratch, reference, false);
    if (ret == OB_SUCCESS) { ret = cloner(*type_, source, allocator, candidate, context); }
    if (ret == OB_SUCCESS) { ret = prepare_legacy(candidate, prepared, scratch, reference, false); }
    if (ret == OB_SUCCESS) { destination = candidate; }
    return ret;
  }

  // Read-only view of source's actual bytes, valid only until source is
  // modified, moved or destroyed (or referenced bytes are reused). No scratch
  // address escapes: representations needing a wrapper/normalization cannot
  // be borrowed. The caller must not write through legacy scalar setters.
  int borrow_legacy(const Datum &source, ObDatum &destination) const
  {
    ObDatum candidate;
    char scratch[64];
    bool reference = false;
    // Without coefficient materialization this performs the same state,
    // flags, scalar narrowing and reference-shape preflight as export_legacy.
    // A referenced Decimal requiring that final normalization is rejected below.
    int ret = prepare_legacy(source, candidate, scratch, reference, false);
    if (OB_FAIL(ret)) {
    } else if (source.is_null()) {
    } else if (!source.is_present() || source.is_inline_extended()) {
      ret = OB_NOT_SUPPORTED;
    } else if (reference) {
      if (meta_.get_type() == ObDecimalIntType) {
        const bool known_precision = type_ != nullptr ? type_->has_precision()
            : accuracy_.get_precision() != PRECISION_UNKNOWN_YET;
        const ObPrecision precision = type_ != nullptr ? type_->precision() : accuracy_.get_precision();
        if (known_precision && source.length() != coefficient_width(precision)) {
          ret = OB_NOT_SUPPORTED;
        }
      }
      if (OB_SUCC(ret)) { candidate.ptr_ = static_cast<const char *>(source.get_reference()); }
    } else {
      const SqlValueType kind = meta_.get_type();
      if (kind == ObFloatType || kind == ObUFloatType) {
        candidate.ptr_ = reinterpret_cast<const char *>(&source.payload_.f32_);
      } else if (kind == ObDoubleType || kind == ObUDoubleType) {
        candidate.ptr_ = reinterpret_cast<const char *>(&source.payload_.f64_);
      } else {
        candidate.ptr_ = (kind >= ObUTinyIntType && kind <= ObUInt64Type)
            || kind == ObYearType || kind == ObBitType || kind == ObEnumType || kind == ObSetType
            ? reinterpret_cast<const char *>(&source.payload_.u64_)
            : reinterpret_cast<const char *>(&source.payload_.i64_);
#if __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
        candidate.ptr_ += sizeof(uint64_t) - candidate.len_;
#endif
      }
    }
    if (OB_SUCC(ret)) { destination = candidate; }
    return ret;
  }

  // Explicit mutable boundary for an owner of signed BIGINT inline values.
  // The prepared type and producer must establish that i64_ is the active
  // member (e.g. Datum::set_int); Datum has no representation/member tag.
  // Shape checks below cannot distinguish an empty reference from an integer.
  // Until source moves or dies, the view permits only same-representation
  // signed 8-byte writes such as ObDatum::set_int. It does not authorize a
  // different scalar/reference setter or changes to the owner's metadata.
  // No allocation or scratch pointer escapes; failure preserves destination.
  int borrow_mutable_signed_int(Datum &source, ObDatum &destination) const
  {
    int ret = OB_SUCCESS;
    if (type_ == nullptr || type_->kind() != ObIntType
        || type_->form() != SqlTypeForm::Scalar
        || type_->time_semantics() != SqlTimeSemantics::Unspecified
        || type_->canonical_representation() != DatumRepresentation::Inline64) {
      ret = OB_NOT_SUPPORTED;
    } else if (!source.is_present() || source.length() != 0
        || source.reference_flags() != Datum::NoReferenceFlags
        || reinterpret_cast<uintptr_t>(&source.payload_.i64_) % alignof(Datum) != 0) {
      ret = OB_INVALID_ARGUMENT;
    } else {
      ObDatum candidate;
      candidate.ptr_ = reinterpret_cast<const char *>(&source.payload_.i64_);
      candidate.pack_ = sizeof(int64_t);
      destination = candidate;
    }
    return ret;
  }

  // Validation, scalar narrowing and capacity checks finish before any write.
  // Failure preserves destination, cursor and every byte of the caller buffer.
  int export_legacy(const Datum &source, ObDatum &destination, char *buffer,
                    int64_t capacity, int64_t &position) const
  {
    int ret = OB_SUCCESS;
    ObDatum prepared;
    char scratch[64];
    bool reference = false;
    if (position < 0 || capacity < position) {
      ret = OB_INVALID_ARGUMENT;
    } else if (OB_FAIL(prepare_legacy(source, prepared, scratch, reference, true))) {
    } else {
      const int64_t size = prepared.is_null() ? 0 : prepared.len_;
      if (size > capacity - position) {
        ret = OB_BUF_NOT_ENOUGH;
      } else if (size > 0 && buffer == nullptr) {
        ret = OB_INVALID_ARGUMENT;
      } else {
        if (!prepared.is_null()) {
          if (size > 0) { MEMMOVE(buffer + position, prepared.ptr_, size); }
          prepared.ptr_ = buffer == nullptr ? nullptr : buffer + position;
        }
        destination = prepared;
        position += size;
      }
    }
    return ret;
  }

  // Ordinary scalar copies allocate no bytes. Referenced payloads are copied;
  // PL/SQL arrays and resources must continue to use dedicated owning cloners.
  int deep_copy(const Datum &source, Datum &destination, ObIAllocator &allocator) const
  {
    int ret = OB_SUCCESS;
    ObDatum prepared;
    char scratch[64];
    bool reference = false;
    Datum candidate = source;
    if (OB_FAIL(prepare_legacy(source, prepared, scratch, reference, false))) {
    } else if (source.is_present() && needs_resource_clone()) {
      ret = OB_NOT_SUPPORTED;
    } else if (reference) {
      char *bytes = nullptr;
      if (source.length() > 0 && (bytes = static_cast<char *>(allocator.alloc(source.length()))) == nullptr) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
      } else {
        if (source.length() > 0) { MEMCPY(bytes, source.get_reference(), source.length()); }
        ret = candidate.set_reference(bytes, source.length(), source.reference_flags());
      }
    }
    if (OB_SUCC(ret)) { destination = candidate; }
    return ret;
  }

  int deep_copy(const Datum &source, Datum &destination, char *buffer,
                int64_t capacity, int64_t &position) const
  { return copy_native_span(source, destination, buffer, capacity, position, false); }

  // Same-scope carrier copy: resource pointees remain borrowed from their
  // existing owner. This is intentionally distinct from an owning deep_copy.
  // Complete preflight precedes every write; overlap/self-copy is supported.
  int copy_carrier_span_borrowing_resources(const Datum &source, Datum &destination,
                                            char *buffer, int64_t capacity, int64_t &position) const
  { return copy_native_span(source, destination, buffer, capacity, position, true); }

private:
  int copy_native_span(const Datum &source, Datum &destination, char *buffer,
                       int64_t capacity, int64_t &position, bool borrow_resources) const
  {
    int ret = OB_SUCCESS;
    ObDatum prepared;
    char scratch[64];
    bool reference = false;
    Datum candidate = source;
    if (position < 0 || capacity < position) {
      ret = OB_INVALID_ARGUMENT;
    } else if (OB_FAIL(prepare_legacy(source, prepared, scratch, reference, false))) {
    } else if (source.is_present() && needs_resource_clone() && !borrow_resources) {
      ret = OB_NOT_SUPPORTED;
    } else {
      const int64_t size = reference ? source.length() : 0;
      if (size > capacity - position) {
        ret = OB_BUF_NOT_ENOUGH;
      } else if (size > 0 && buffer == nullptr) {
        ret = OB_INVALID_ARGUMENT;
      } else {
        if (reference) {
          if (size > 0) { MEMMOVE(buffer + position, source.get_reference(), size); }
          candidate.set_reference(size == 0 ? nullptr : buffer + position, size, source.reference_flags());
        }
        destination = candidate;
        position += size;
      }
    }
    return ret;
  }

  bool needs_resource_clone() const
  { return type_ != nullptr ? type_->requires_resource_clone()
      : meta_.is_ext_sql_array() || meta_.is_pl_extend_type(); }

  static bool decimal_width(int64_t size)
  { return size == 0 || size == 4 || size == 8 || size == 16 || size == 32 || size == 64; }

  static int coefficient_width(ObPrecision precision)
  {
    return precision <= MAX_PRECISION_DECIMAL_INT_32 ? 4
        : precision <= MAX_PRECISION_DECIMAL_INT_64 ? 8
        : precision <= MAX_PRECISION_DECIMAL_INT_128 ? 16
        : precision <= MAX_PRECISION_DECIMAL_INT_256 ? 32 : 64;
  }

  int resolve_type(SqlType &temporary, const SqlType *&type) const
  {
    int ret = OB_SUCCESS;
    if (type_ != nullptr) { type = type_; }
    else if (OB_FAIL(SqlType::from_legacy(meta_, accuracy_, temporary))) {}
    else { type = &temporary; }
    if (OB_SUCC(ret)) {
      if (type->kind() < ObNullType || (type->kind() >= ObMaxType
          && type->kind() != ObTimestampLTZType && type->kind() != ObTimestampNanoType)) {
        ret = OB_NOT_SUPPORTED;
      } else {
        SqlTimeSemantics expected = SqlTimeSemantics::Unspecified;
        switch (type->kind()) {
          case ObDateType: case ObMySQLDateType: expected = SqlTimeSemantics::Date; break;
          case ObTimeType: expected = SqlTimeSemantics::TimeOfDay; break;
          case ObYearType: expected = SqlTimeSemantics::Year; break;
          case ObDateTimeType: case ObMySQLDateTimeType: expected = SqlTimeSemantics::WallClock; break;
          case ObTimestampType: expected = SqlTimeSemantics::Instant; break;
          case ObTimestampLTZType: expected = SqlTimeSemantics::SessionLocal; break;
          case ObTimestampNanoType: expected = SqlTimeSemantics::WallClock; break;
          default: break;
        }
        if (type->time_semantics() != expected) { ret = OB_NOT_SUPPORTED; }
      }
    }
    return ret;
  }

  static int extended_value(const ObDatum &source, int64_t &value)
  {
    if (source.ptr_ == nullptr || (source.len_ != DATUM_EXTEND_PAYLOAD_SIZE
        && source.len_ != DATUM_LEGACY_EXTEND_SIZE)) { return OB_INVALID_ARGUMENT; }
    if (source.len_ == DATUM_LEGACY_EXTEND_SIZE) {
      if (static_cast<uint8_t>(source.ptr_[0]) == ObNullType) { value = 0; return OB_SUCCESS; }
      if (static_cast<uint8_t>(source.ptr_[0]) != ObExtendType) { return OB_INVALID_ARGUMENT; }
    }
    MEMCPY(&value, source.ptr_ + (source.len_ == DATUM_EXTEND_PAYLOAD_SIZE ? 0 : 8), sizeof(value));
    return OB_SUCCESS;
  }

  static int validate_reference(const ObDatum &source, const SqlType &type)
  {
    if (source.len_ > 0 && source.ptr_ == nullptr) { return OB_INVALID_ARGUMENT; }
    const SqlValueType kind = type.kind();
    if (type.legacy_meta().is_ext_sql_array() && !source.is_ext()) {
      if (source.flag_ != ObDatumDesc::NONE || source.len_ != sizeof(int64_t)) { return OB_INVALID_ARGUMENT; }
    } else if (kind == ObExtendType) {
      int64_t value = 0;
      const int ret = source.is_ext() ? extended_value(source, value) : OB_INVALID_ARGUMENT;
      if (ret != OB_SUCCESS) { return ret; }
      if (source.is_extended_null() || value == DATUM_MIN_EXT_VALUE || value == DATUM_MAX_EXT_VALUE
          || value == ObActionFlag::OP_NOP) { return OB_INVALID_ARGUMENT; }
      const uint8_t subtype = source.get_extend_type();
      if (subtype > 0 && subtype <= T_EXT_SQL_ARRAY
          && subtype != type.legacy_meta().get_extend_type()) {
        return type.form() == SqlTypeForm::Scalar ? OB_NOT_SUPPORTED : OB_INVALID_ARGUMENT;
      }
    } else if (kind == ObDecimalIntType) {
      if (source.flag_ != ObDatumDesc::NONE || !decimal_width(source.len_)) { return OB_INVALID_ARGUMENT; }
    } else if (kind == ObNumberType || kind == ObUNumberType) {
      if (source.flag_ != ObDatumDesc::NONE || source.len_ < sizeof(number::ObNumber::Desc)) { return OB_INVALID_ARGUMENT; }
      number::ObNumber::Desc desc;
      MEMCPY(&desc, source.ptr_, sizeof(desc));
      if (desc.len_ > number::ObNumber::MAX_STORE_LEN
          || source.len_ != sizeof(desc) + sizeof(uint32_t) * desc.len_) { return OB_INVALID_ARGUMENT; }
    } else if (kind == ObUnknownType) {
      if (source.flag_ != ObDatumDesc::NONE || source.len_ != sizeof(int64_t)) { return OB_INVALID_ARGUMENT; }
    } else if (kind == ObTimestampLTZType || kind == ObTimestampNanoType) {
      if (source.flag_ != ObDatumDesc::NONE || source.len_ != sizeof(ObOTimestampTinyData)) { return OB_INVALID_ARGUMENT; }
    } else if (source.is_ext()) {
      return OB_INVALID_ARGUMENT;
    }
    return OB_SUCCESS;
  }

  static uint16_t reference_flags(const ObDatum &source)
  {
    return source.is_ext() ? Datum::LegacyExtended : source.is_outrow() ? Datum::OutRow
        : source.has_lob_header() ? Datum::HasLobHeader : Datum::NoReferenceFlags;
  }

  // A larger legacy coefficient is accepted only when all discarded bytes are
  // genuine sign extension. This is normalization, never truncation or a cast.
  static int inline_decimal(const ObDatum &source, const SqlType &type, int64_t &value)
  {
    if (source.flag_ != ObDatumDesc::NONE || !decimal_width(source.len_)) { return OB_INVALID_ARGUMENT; }
    value = 0;
    if (source.len_ == 0) { return OB_SUCCESS; }
    if (source.len_ == 4) {
      int32_t coefficient = 0;
      MEMCPY(&coefficient, source.ptr_, sizeof(coefficient)); value = coefficient;
    } else {
#if __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
      MEMCPY(&value, source.ptr_ + source.len_ - sizeof(value), sizeof(value));
#else
      MEMCPY(&value, source.ptr_, sizeof(value));
#endif
      const uint8_t extension = value < 0 ? 0xff : 0;
#if __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
      for (int64_t i = 0; i < source.len_ - sizeof(value); ++i) {
#else
      for (int64_t i = sizeof(value); i < source.len_; ++i) {
#endif
        if (static_cast<uint8_t>(source.ptr_[i]) != extension) { return OB_INVALID_ARGUMENT; }
      }
    }
    if (coefficient_width(type.precision()) == 4 && (value < INT32_MIN || value > INT32_MAX)) { return OB_INVALID_ARGUMENT; }
    return OB_SUCCESS;
  }

  int import_present(const ObDatum &source, Datum &output) const
  {
    SqlType temporary;
    const SqlType *type = nullptr;
    int ret = resolve_type(temporary, type);
    if (ret != OB_SUCCESS) { return ret; }
    const DatumRepresentation representation = type->canonical_representation();
    if (representation == DatumRepresentation::NoPayload) { return OB_INVALID_ARGUMENT; }
    if (representation == DatumRepresentation::Reference) {
      if (OB_FAIL(validate_reference(source, *type))) {}
      else { ret = output.set_reference(source.ptr_, source.len_, reference_flags(source)); }
    } else if (source.flag_ != ObDatumDesc::NONE) {
      ret = OB_INVALID_ARGUMENT;
    } else if (type->kind() == ObDecimalIntType) {
      int64_t value = 0;
      ret = inline_decimal(source, *type, value);
      if (OB_SUCC(ret)) { output.set_int(value); }
    } else {
      const SqlValueType kind = type->kind();
      const int64_t expected = kind == ObYearType ? 1 : (kind == ObFloatType || kind == ObUFloatType
          || kind == ObDateType || kind == ObMySQLDateType) ? 4 : 8;
      if (source.len_ != expected) { ret = OB_INVALID_ARGUMENT; }
      else if (kind == ObFloatType || kind == ObUFloatType) {
        float value = 0; MEMCPY(&value, source.ptr_, sizeof(value)); output.set_float(value);
      } else if (kind == ObDoubleType || kind == ObUDoubleType) {
        double value = 0; MEMCPY(&value, source.ptr_, sizeof(value)); output.set_double(value);
      } else if (kind == ObDateType || kind == ObMySQLDateType) {
        int32_t value = 0; MEMCPY(&value, source.ptr_, sizeof(value)); output.set_int(value);
      } else if (kind == ObYearType) {
        uint8_t value = 0; MEMCPY(&value, source.ptr_, sizeof(value)); output.set_uint(value);
      } else {
        uint64_t value = 0; MEMCPY(&value, source.ptr_, sizeof(value));
        if ((kind >= ObUTinyIntType && kind <= ObUInt64Type) || kind == ObBitType
            || kind == ObEnumType || kind == ObSetType) { output.set_uint(value); }
        else { int64_t signed_value = 0; MEMCPY(&signed_value, &value, sizeof(value)); output.set_int(signed_value); }
      }
    }
    return ret;
  }

  // Materialize a referenced coefficient only at the old execution ABI edge.
  // Native references retain their actual span. A narrowing is valid exactly
  // when every discarded byte extends the retained coefficient's sign.
  static int materialize_decimal(ObDatum &coefficient, int target_width, char *scratch)
  {
    const int64_t source_width = coefficient.len_;
    const char *source = coefficient.ptr_;
    uint8_t extension = 0;
    if (source_width > 0) {
#if __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
      const int64_t sign_index = source_width > target_width ? source_width - target_width : 0;
#else
      const int64_t sign_index = source_width > target_width ? target_width - 1 : source_width - 1;
#endif
      extension = (static_cast<uint8_t>(source[sign_index]) & 0x80) != 0 ? 0xff : 0;
    }
    if (source_width > target_width) {
#if __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
      for (int64_t i = 0; i < source_width - target_width; ++i) {
#else
      for (int64_t i = target_width; i < source_width; ++i) {
#endif
        if (static_cast<uint8_t>(source[i]) != extension) { return OB_INVALID_ARGUMENT; }
      }
    }
    MEMSET(scratch, extension, target_width);
    const int64_t retained = MIN(source_width, int64_t(target_width));
    if (retained > 0) {
#if __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
      MEMCPY(scratch + target_width - retained, source + source_width - retained, retained);
#else
      MEMCPY(scratch, source, retained);
#endif
    }
    coefficient.ptr_ = scratch;
    coefficient.pack_ = target_width;
    return OB_SUCCESS;
  }

  int prepare_legacy(const Datum &source, ObDatum &output, char *scratch, bool &reference,
                     bool materialize_coefficient) const
  {
    reference = false;
    output.ptr_ = nullptr;
    if (source.is_null()) { output.set_null(); return OB_SUCCESS; }
    if (source.is_min() || source.is_max() || source.is_nop()) {
      output.ptr_ = scratch;
      output.set_extend(source.is_min() ? DATUM_MIN_EXT_VALUE : source.is_max() ? DATUM_MAX_EXT_VALUE : ObActionFlag::OP_NOP,
          0, 0, meta_.get_cs_level(), meta_.get_cs_type());
      return OB_SUCCESS;
    }
    if (!source.is_present()) { return OB_INVALID_ARGUMENT; }
    SqlType temporary;
    const SqlType *type = nullptr;
    int ret = resolve_type(temporary, type);
    if (ret != OB_SUCCESS) { return ret; }
    const DatumRepresentation representation = type->canonical_representation();
    if (representation == DatumRepresentation::NoPayload) { return OB_INVALID_ARGUMENT; }
    if (source.is_inline_extended()) {
      if (representation != DatumRepresentation::Reference || type->kind() != ObExtendType) {
        return OB_INVALID_ARGUMENT;
      }
      ret = source.write_inline_extended_payload(scratch, 64);
      if (ret == OB_SUCCESS) {
        output.ptr_ = scratch;
        output.set_ext(source.inline_extended_payload_size());
        ret = validate_reference(output, *type);
      }
      return ret;
    }
    if (representation == DatumRepresentation::Reference) {
      const uint16_t flags = source.reference_flags();
      if (flags != Datum::NoReferenceFlags && flags != Datum::OutRow
          && flags != Datum::HasLobHeader && flags != Datum::LegacyExtended) { return OB_NOT_SUPPORTED; }
      if (source.length() > (uint32_t(1) << 29) - 1) { return OB_SIZE_OVERFLOW; }
      output.ptr_ = static_cast<const char *>(source.get_reference());
      output.pack_ = source.length();
      if (flags == Datum::OutRow) { output.set_outrow(); }
      else if (flags == Datum::HasLobHeader) { output.set_has_lob_header(); }
      else if (flags == Datum::LegacyExtended) { output.flag_ = ObDatumDesc::EXT; }
      if (OB_FAIL(validate_reference(output, *type))) {}
      else {
        reference = true;
        if (materialize_coefficient && type->kind() == ObDecimalIntType && type->has_precision()) {
          ret = materialize_decimal(output, coefficient_width(type->precision()), scratch);
        }
      }
      return ret;
    }
    if (source.length() != 0 || source.reference_flags() != Datum::NoReferenceFlags) { return OB_INVALID_ARGUMENT; }
    output.ptr_ = scratch;
    const SqlValueType kind = type->kind();
    if (kind == ObDecimalIntType) {
      const int width = coefficient_width(type->precision());
      const int64_t value = source.get_int();
      if (width == 4) {
        if (value < INT32_MIN || value > INT32_MAX) { return OB_INVALID_ARGUMENT; }
        const int32_t coefficient = static_cast<int32_t>(value);
        MEMCPY(scratch, &coefficient, sizeof(coefficient));
      } else { MEMCPY(scratch, &value, sizeof(value)); }
      output.pack_ = width;
    } else if (kind == ObFloatType || kind == ObUFloatType) {
      const float value = source.get_float(); MEMCPY(scratch, &value, sizeof(value)); output.pack_ = sizeof(value);
    } else if (kind == ObDoubleType || kind == ObUDoubleType) {
      const double value = source.get_double(); MEMCPY(scratch, &value, sizeof(value)); output.pack_ = sizeof(value);
    } else if (kind == ObDateType || kind == ObMySQLDateType) {
      const int64_t value = source.get_int();
      if (value < INT32_MIN || value > INT32_MAX) { return OB_INVALID_ARGUMENT; }
      const int32_t date = static_cast<int32_t>(value); MEMCPY(scratch, &date, sizeof(date)); output.pack_ = sizeof(date);
    } else if (kind == ObYearType) {
      const uint64_t value = source.get_uint();
      if (value > UINT8_MAX) { return OB_INVALID_ARGUMENT; }
      const uint8_t year = static_cast<uint8_t>(value); MEMCPY(scratch, &year, sizeof(year)); output.pack_ = sizeof(year);
    } else {
      if ((kind >= ObUTinyIntType && kind <= ObUInt64Type) || kind == ObBitType
          || kind == ObEnumType || kind == ObSetType) {
        const uint64_t value = source.get_uint(); MEMCPY(scratch, &value, sizeof(value)); output.pack_ = sizeof(value);
      } else {
        const int64_t value = source.get_int(); MEMCPY(scratch, &value, sizeof(value)); output.pack_ = sizeof(value);
      }
    }
    return OB_SUCCESS;
  }

  SqlTypeMeta meta_;
  ObAccuracy accuracy_;
  const SqlType *type_;
};

} // namespace common
} // namespace oceanbase
