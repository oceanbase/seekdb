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

#include <cstdint>
#include <type_traits>
#include "lib/ob_errno.h"
#include "common/object/ob_obj_type.h"
#include "common/enumset/enumset_value.h"
#include "common/lob/lob_types.h"
#include "common/number/ob_number_v2.h"
#include "common/ob_action_flag.h"
#include "common/wide_integer/ob_wide_integer.h"
#include "common/timezone/ob_time_def.h"
#include "common/timezone/ob_timezone_info.h"

namespace oceanbase
{
namespace common
{

// Execution value only. A Type or a prepared operation determines which member
// is active; an ordinary integer always uses its inline scalar member. The
// legacy pointer Datum and the stored-cell codec are separate boundary types.
// This carrier owns neither referenced bytes nor resources. A shallow copy of
// a reference does not prevent its backing storage from being reused.
class alignas(8) Datum
{
public:
  enum class State : uint8_t { Null, Present, Nop, Min, Max };
  enum ReferenceFlag : uint16_t {
    NoReferenceFlags = 0, OutRow = 1, HasLobHeader = 2, LegacyExtended = 4
  };

  Datum() : payload_(), length_(0), state_(static_cast<uint8_t>(State::Null)), reserved_(0), flags_(0) {}

  // Logical SQL state is independent of the physical extended-value carrier.
  State state() const { return static_cast<State>(state_ & LOGICAL_STATE_MASK); }
  // The physical tag is only set for Present. Every other setter goes through
  // clear(), retaining the original single-byte tests on scalar hot paths.
  bool is_null() const { return state_ == static_cast<uint8_t>(State::Null); }
  bool is_present() const { return state() == State::Present; }
  bool is_nop() const { return state_ == static_cast<uint8_t>(State::Nop); }
  bool is_min() const { return state_ == static_cast<uint8_t>(State::Min); }
  bool is_max() const { return state_ == static_cast<uint8_t>(State::Max); }
  bool is_inline_extended() const { return (state_ & INLINE_EXTENDED) != 0; }

  // Scalar accessors require a present value whose prepared type selects the
  // corresponding canonical representation. They do not inspect payload bits
  // or perform runtime type lookup. Signed/unsigned conversion belongs to cast.
  int64_t get_int() const { return payload_.i64_; }
  bool get_bool() const { return 0 != get_int(); }
  uint64_t get_uint() const { return payload_.u64_; }
  float get_float() const { return payload_.f32_; }
  double get_double() const { return payload_.f64_; }

  int8_t get_int8() const { return static_cast<int8_t>(get_int()); }
  int8_t get_tinyint() const { return get_int8(); }
  int16_t get_smallint() const { return static_cast<int16_t>(get_int()); }
  int32_t get_mediumint() const { return static_cast<int32_t>(get_int()); }
  int32_t get_int32() const { return static_cast<int32_t>(get_int()); }
  uint8_t get_uint8() const { return static_cast<uint8_t>(get_uint()); }
  uint8_t get_utinyint() const { return get_uint8(); }
  uint16_t get_usmallint() const { return static_cast<uint16_t>(get_uint()); }
  uint32_t get_umediumint() const { return static_cast<uint32_t>(get_uint()); }
  uint32_t get_uint32() const { return static_cast<uint32_t>(get_uint()); }
  uint64_t get_uint64() const { return get_uint(); }
  float get_ufloat() const { return get_float(); }
  double get_udouble() const { return get_double(); }
  uint64_t get_bit() const { return get_uint(); }
  uint64_t get_enum() const { return get_uint(); }
  uint64_t get_set() const { return get_uint(); }
  uint64_t get_enumset() const { return get_uint(); }
  int64_t get_datetime() const { return get_int(); }
  int64_t get_timestamp() const { return get_int(); }
  int64_t get_time() const { return get_int(); }
  int32_t get_date() const { return static_cast<int32_t>(get_int()); }
  uint8_t get_year() const { return static_cast<uint8_t>(get_uint()); }
  ObMySQLDate get_mysql_date() const { return ObMySQLDate(get_date()); }
  ObMySQLDateTime get_mysql_datetime() const { return ObMySQLDateTime(get_int()); }
  int32_t get_decimal_int32() const { return static_cast<int32_t>(get_int()); }
  int64_t get_decimal_int64() const { return get_int(); }
  // These require a prepared Reference Decimal representation. Actual span
  // width need not equal declared precision width, including a 0-byte zero.
  // A wider span may narrow only by discarding genuine sign-extension bytes.
  int get_decimal_int128(int128_t &value) const { return read_decimal_reference(value); }
  int get_decimal_int256(int256_t &value) const { return read_decimal_reference(value); }
  int get_decimal_int512(int512_t &value) const { return read_decimal_reference(value); }
  int64_t get_unknown() const
  { int64_t value; MEMCPY(&value, get_reference(), sizeof(value)); return value; }

  void set_int(int64_t value) { clear(State::Present); payload_.i64_ = value; }
  void set_uint(uint64_t value) { clear(State::Present); payload_.u64_ = value; }
  void set_float(float value) { clear(State::Present); payload_.f32_ = value; }
  void set_double(double value) { clear(State::Present); payload_.f64_ = value; }
  void set_int32(int32_t value) { set_int(value); }
  void set_uint32(uint32_t value) { set_uint(value); }
  void set_bit(uint64_t value) { set_uint(value); }
  void set_enum(uint64_t value) { set_uint(value); }
  void set_set(uint64_t value) { set_uint(value); }
  void set_bool(bool value) { set_int(value); }
  void set_true() { set_bool(true); }
  void set_false() { set_bool(false); }
  void set_datetime(int64_t value) { set_int(value); }
  void set_timestamp(int64_t value) { set_int(value); }
  void set_time(int64_t value) { set_int(value); }
  void set_date(int32_t value) { set_int(value); }
  void set_year(uint8_t value) { set_uint(value); }
  void set_mysql_date(ObMySQLDate value) { set_int(value.date_); }
  void set_mysql_datetime(ObMySQLDateTime value) { set_int(value.datetime_); }
  void set_datum(const Datum &other) { *this = other; }

  void set_null() { clear(State::Null); }
  void reset() { set_null(); }
  void set_nop() { clear(State::Nop); }
  void set_min() { clear(State::Min); }
  void set_max() { clear(State::Max); }

  // Reference access requires the caller's type/layout to select a reference
  // representation. An empty, non-NULL byte string may have a null address.
  const void *get_reference() const { return payload_.reference_; }
  uint32_t length() const { return length_; }
  // Inline Extend uses the physical flags bytes for collation metadata. Do
  // not expose them as LOB/reference flags, even for unknown legacy byte values.
  uint16_t reference_flags() const { return is_inline_extended() ? LegacyExtended : flags_; }
  bool is_outrow() const { return !is_inline_extended() && (flags_ & OutRow) != 0; }
  bool has_lob_header() const { return !is_inline_extended() && (flags_ & HasLobHeader) != 0; }
  bool is_ext() const { return is_inline_extended() || (flags_ & LegacyExtended) != 0; }
  void set_outrow() { OB_ASSERT(!is_inline_extended()); flags_ = OutRow; }
  void set_has_lob_header() { OB_ASSERT(!is_inline_extended()); flags_ = HasLobHeader; }
  void clear_lob_header() { OB_ASSERT(!is_inline_extended()); flags_ &= ~HasLobHeader; }
  int set_reference(const void *bytes, uint32_t length, uint16_t flags = NoReferenceFlags)
  {
    int ret = OB_SUCCESS;
    if (length > 0 && bytes == nullptr) {
      ret = OB_INVALID_ARGUMENT;
    } else {
      clear(State::Present);
      payload_.reference_ = bytes;
      length_ = length;
      flags_ = flags;
    }
    return ret;
  }

  // These are borrowed spans. The caller owns their bytes, including locator
  // bytes; copying a locator does not acquire the external LOB resource.
  ObString get_string() const
  { return ObString(length_, static_cast<const char *>(get_reference())); }
  ObString get_json() const { return get_string(); }
  int set_string(const char *bytes, uint32_t length) { return set_reference(bytes, length); }
  int set_string(const ObString &value)
  { return value.length() < 0 ? OB_INVALID_ARGUMENT : set_string(value.ptr(), value.length()); }
  int set_json(const ObString &value) { return set_string(value); }
  int set_enumset_inner(const ObString &value) { return set_string(value); }
  int set_enumset_inner(const char *bytes, uint32_t length) { return set_string(bytes, length); }
  int get_enumset_inner(ObEnumSetInnerValue &value) const
  { int64_t pos = 0; return value.deserialize(static_cast<const char *>(get_reference()), length_, pos); }
  void get_mem_lob(ObLobLocatorV2 &value) const
  { value.assign_ptr(static_cast<const ObMemLobCommon *>(get_reference()), length_, has_lob_header()); }
  const ObLobCommon &get_lob_data() const
  { return *static_cast<const ObLobCommon *>(get_reference()); }
  int set_lob_data(const ObLobCommon &value, int64_t length)
  { return length < 0 || length > UINT32_MAX ? OB_INVALID_ARGUMENT : set_reference(&value, length); }

  const number::ObCompactNumber &get_number() const
  { return *static_cast<const number::ObCompactNumber *>(get_reference()); }
  const number::ObNumber::Desc &get_number_desc() const { return get_number().desc_; }
  const uint32_t *get_number_digits() const { return get_number().digits_; }
  int set_number(const number::ObNumber &value, void *buffer, uint32_t capacity)
  {
    const auto desc = value.d_;
    const uint32_t size = sizeof(desc) + sizeof(uint32_t) * desc.len_;
    if (desc.len_ > number::ObNumber::MAX_STORE_LEN
        || (desc.len_ > 0 && value.get_digits() == nullptr)) { return OB_INVALID_ARGUMENT; }
    if (capacity < size) { return OB_BUF_NOT_ENOUGH; }
    if (buffer == nullptr) { return OB_INVALID_ARGUMENT; }
    // Copy digits first: the source may be a compact number in this buffer.
    if (size > sizeof(desc)) {
      MEMMOVE(static_cast<char *>(buffer) + sizeof(desc), value.get_digits(), size - sizeof(desc));
    }
    MEMCPY(buffer, &desc, sizeof(desc));
    return set_reference(buffer, size);
  }
  int set_number(const number::ObCompactNumber &value, void *buffer, uint32_t capacity)
  {
    const uint32_t size = sizeof(value.desc_) + sizeof(uint32_t) * value.desc_.len_;
    if (value.desc_.len_ > number::ObNumber::MAX_STORE_LEN) { return OB_INVALID_ARGUMENT; }
    if (capacity < size) { return OB_BUF_NOT_ENOUGH; }
    if (buffer == nullptr) { return OB_INVALID_ARGUMENT; }
    MEMMOVE(buffer, &value, size);
    return set_reference(buffer, size);
  }
  int set_number_shallow(const number::ObCompactNumber &value)
  {
    return value.desc_.len_ > number::ObNumber::MAX_STORE_LEN ? OB_INVALID_ARGUMENT
        : set_reference(&value, sizeof(value.desc_) + sizeof(uint32_t) * value.desc_.len_);
  }

  int set_otimestamp_tiny(const ObOTimestampData &value, void *buffer, uint32_t capacity)
  {
    ObOTimestampTinyData copy;
    copy.desc_ = value.time_ctx_.time_desc_; copy.time_us_ = value.time_us_;
    return copy_reference_bytes(&copy, sizeof(copy), buffer, capacity);
  }
  int set_otimestamp(const ObOTimestampData &value, void *buffer, uint32_t capacity)
  { return copy_reference_bytes(&value, sizeof(value), buffer, capacity); }
  ObOTimestampData get_otimestamp_tiny() const
  {
    ObOTimestampTinyData copy;
    MEMCPY(&copy, get_reference(), sizeof(copy));
    ObOTimestampData value;
    value.time_us_ = copy.time_us_; value.time_ctx_.time_desc_ = copy.desc_; value.time_ctx_.tz_desc_ = 0;
    return value;
  }
  ObOTimestampData get_otimestamp() const
  { ObOTimestampData value; MEMCPY(&value, get_reference(), sizeof(value)); return value; }

  // Native extended values keep all address/size/metadata bits inside Datum.
  // This does not acquire the pointee. Explicit borrowed legacy references
  // remain supported; only their real owners may release the referenced span.
  void set_inline_extended(int64_t value, uint8_t subtype, int32_t size,
                           uint8_t collation_level = CS_LEVEL_INVALID,
                           uint8_t collation = CS_TYPE_INVALID, bool legacy_16 = false)
  {
    clear(State::Present);
    state_ |= INLINE_EXTENDED | (legacy_16 ? LEGACY_EXTENDED_16 : 0);
    payload_.i64_ = value;
    MEMCPY(&length_, &size, sizeof(size));
    reserved_ = subtype;
    flags_ = static_cast<uint16_t>(collation_level) | (static_cast<uint16_t>(collation) << 8);
  }
  uint32_t inline_extended_payload_size() const
  { OB_ASSERT(is_inline_extended()); return (state_ & LEGACY_EXTENDED_16) != 0 ? 16 : 15; }
  // Reconstitute the old bytes in caller storage. Never return a pointer into
  // a temporary, and never confuse the native size field with a byte span.
  int write_inline_extended_payload(void *buffer, uint32_t capacity) const
  {
    if (!is_inline_extended() || !is_present()) { return OB_INVALID_ARGUMENT; }
    const uint32_t bytes = inline_extended_payload_size();
    if (capacity < bytes) { return OB_BUF_NOT_ENOUGH; }
    if (buffer == nullptr) { return OB_INVALID_ARGUMENT; }
    char *out = static_cast<char *>(buffer);
    if (bytes == 15) {
      MEMCPY(out, &payload_.i64_, 8); MEMCPY(out + 8, &length_, 4);
      out[12] = reserved_; out[13] = flags_ & 0xff; out[14] = flags_ >> 8;
    } else {
      out[0] = ObExtendType; out[1] = flags_ & 0xff; out[2] = flags_ >> 8; out[3] = reserved_;
      MEMCPY(out + 4, &length_, 4); MEMCPY(out + 8, &payload_.i64_, 8);
    }
    return OB_SUCCESS;
  }
  // Special SQL states have no address or payload and are read separately.
  bool has_ext_value() const
  {
    return is_present() && (is_inline_extended() || (is_ext() && get_reference() != nullptr
        && (length_ == 15 || (length_ == 16
            && static_cast<const uint8_t *>(get_reference())[0] == ObExtendType))));
  }
  int64_t get_ext() const
  {
    if (is_min()) { return -3; }
    if (is_max()) { return -2; }
    if (is_nop()) { return ObActionFlag::OP_NOP; }
    OB_ASSERT(has_ext_value());
    if (is_inline_extended()) { return payload_.i64_; }
    int64_t value;
    MEMCPY(&value, static_cast<const char *>(get_reference()) + (length_ == 15 ? 0 : 8), sizeof(value));
    return value;
  }
  int32_t get_extend_size() const
  {
    OB_ASSERT(has_ext_value());
    int32_t value;
    if (is_inline_extended()) { MEMCPY(&value, &length_, sizeof(value)); return value; }
    MEMCPY(&value, static_cast<const char *>(get_reference()) + (length_ == 15 ? 8 : 4), sizeof(value));
    return value;
  }
  uint8_t get_extend_type() const
  { return is_inline_extended() ? reserved_ : has_ext_value()
      ? static_cast<const uint8_t *>(get_reference())[length_ == 15 ? 12 : 3] : UINT8_MAX; }
  uint8_t get_extend_collation_level() const
  { OB_ASSERT(has_ext_value()); return is_inline_extended() ? flags_ & 0xff
      : static_cast<const uint8_t *>(get_reference())[length_ == 15 ? 13 : 1]; }
  uint8_t get_extend_collation() const
  { OB_ASSERT(has_ext_value()); return is_inline_extended() ? flags_ >> 8
      : static_cast<const uint8_t *>(get_reference())[length_ == 15 ? 14 : 2]; }
  int set_extend(int64_t value, uint8_t subtype, int32_t size,
                 void *buffer, uint32_t capacity,
                 uint8_t collation_level = CS_LEVEL_INVALID, uint8_t collation = CS_TYPE_INVALID)
  {
    if (subtype == 0 && value == -3) { set_min(); return OB_SUCCESS; }
    if (subtype == 0 && value == -2) { set_max(); return OB_SUCCESS; }
    if (subtype == 0 && value == ObActionFlag::OP_NOP) { set_nop(); return OB_SUCCESS; }
    if (capacity < 15) { return OB_BUF_NOT_ENOUGH; }
    if (buffer == nullptr) { return OB_INVALID_ARGUMENT; }
    char *bytes = static_cast<char *>(buffer);
    MEMCPY(bytes, &value, sizeof(value)); MEMCPY(bytes + 8, &size, sizeof(size));
    bytes[12] = subtype; bytes[13] = collation_level; bytes[14] = collation;
    return set_reference(bytes, 15, LegacyExtended);
  }
  int set_ext_value(int64_t value, void *buffer, uint32_t capacity)
  { return set_extend(value, 0, 0, buffer, capacity); }

  // Diagnostics cannot infer the active payload member without a prepared type.
  TO_STRING_KV("state", static_cast<int>(state()), "inline_extended", is_inline_extended(),
              "length", length_, "flags", reference_flags());

private:
  friend class DatumCopyPlan;

  template <typename T>
  int read_decimal_reference(T &value) const
  {
    if (!is_present() || is_inline_extended() || flags_ != NoReferenceFlags
        || (length_ != 0 && length_ != 4 && length_ != 8 && length_ != 16 && length_ != 32 && length_ != 64)
        || (length_ > 0 && get_reference() == nullptr)) { return OB_INVALID_ARGUMENT; }
    const auto *source = static_cast<const uint8_t *>(get_reference());
    const uint32_t retained = length_ < sizeof(T) ? length_ : sizeof(T);
    uint8_t extension = 0;
    if (retained > 0) {
#if __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
      extension = (source[length_ - retained] & 0x80) != 0 ? 0xff : 0;
      for (uint32_t i = 0; i < length_ - retained; ++i) {
#else
      extension = (source[retained - 1] & 0x80) != 0 ? 0xff : 0;
      for (uint32_t i = retained; i < length_; ++i) {
#endif
        if (source[i] != extension) { return OB_INVALID_ARGUMENT; }
      }
    }
    T candidate;
    MEMSET(&candidate, extension, sizeof(candidate));
    if (retained > 0) {
#if __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
      MEMCPY(reinterpret_cast<char *>(&candidate) + sizeof(candidate) - retained,
             source + length_ - retained, retained);
#else
      MEMCPY(&candidate, source, retained);
#endif
    }
    value = candidate;
    return OB_SUCCESS;
  }

  union Payload
  {
    int64_t i64_;
    uint64_t u64_;
    double f64_;
    float f32_;
    const void *reference_;
    Payload() : u64_(0) {}
  } payload_;
  uint32_t length_;
  static constexpr uint8_t INLINE_EXTENDED = 0x80;
  static constexpr uint8_t LEGACY_EXTENDED_16 = 0x40;
  static constexpr uint8_t LOGICAL_STATE_MASK = 0x3f;
  uint8_t state_;
  uint8_t reserved_;
  uint16_t flags_;

  void clear(State state)
  {
    payload_.u64_ = 0;
    length_ = 0;
    state_ = static_cast<uint8_t>(state);
    reserved_ = 0;
    flags_ = 0;
  }

  int copy_reference_bytes(const void *source, uint32_t size, void *buffer, uint32_t capacity)
  {
    if (capacity < size) { return OB_BUF_NOT_ENOUGH; }
    if (buffer == nullptr || (size > 0 && source == nullptr)) { return OB_INVALID_ARGUMENT; }
    if (size > 0) { MEMMOVE(buffer, source, size); }
    return set_reference(buffer, size);
  }
};

static_assert(sizeof(Datum) == 16, "Execution Datum must remain 16 bytes");
static_assert(alignof(Datum) == 8, "Inline 64-bit scalars require 8-byte alignment");
static_assert(std::is_standard_layout<Datum>::value, "Datum needs a stable in-memory layout");
static_assert(std::is_trivially_copyable<Datum>::value, "Datum copying must not acquire ownership");

} // namespace common
} // namespace oceanbase
