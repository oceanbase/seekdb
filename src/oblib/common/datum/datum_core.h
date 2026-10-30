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

#include "common/object/ob_type_metadata.h"
#include "common/enumset/enumset_value.h"
#include "common/lob/lob_types.h"
#include "common/number/ob_number_v2.h"
#include "common/wide_integer/ob_wide_integer.h"
#include "common/ob_action_flag.h"
#include "lib/checksum/ob_crc64.h"
#include "common/timezone/ob_time_def.h"
#include "common/timezone/ob_timezone_info.h"

namespace oceanbase
{
namespace common
{

//
// ObDatum stores the value bytes for one cell; type and accuracy metadata are stored separately.
// All data types are stored in continuous memory, here is the memory layout of
// all types:
//
//   | type                | memory layout                 | min length | max length          |
//   |---------------------|-------------------------------|------------|---------------------|
//   | ObNullType          | char[0]                       | 0          | 0                   |
//   | ObTinyIntType       | int64_t                       | 8          | 8                   |
//   | ObSmallIntType      | int64_t                       | 8          | 8                   |
//   | ObMediumIntType     | int64_t                       | 8          | 8                   |
//   | ObInt32Type         | int64_t                       | 8          | 8                   |
//   | ObIntType           | int64_t                       | 8          | 8                   |
//   | ObUTinyIntType      | uint64_t                      | 8          | 8                   |
//   | ObUSmallIntType     | uint64_t                      | 8          | 8                   |
//   | ObUMediumIntType    | uint64_t                      | 8          | 8                   |
//   | ObUInt32Type        | uint64_t                      | 8          | 8                   |
//   | ObUInt64Type        | uint64_t                      | 8          | 8                   |
//   | ObFloatType         | float                         | 4          | 4                   |
//   | ObDoubleType        | double                        | 8          | 8                   |
//   | ObUFloatType        | float                         | 4          | 4                   |
//   | ObUDoubleType       | double                        | 8          | 8                   |
//   | ObNumberType        | ObNumberDesc + uint32_t[0~9]  | 4          | 40                  |
//   | ObUNumberType       | ObNumberDesc + uint32_t[0~9]  | 4          | 40                  |
//   | ObDateTimeType      | int64_t                       | 8          | 8                   |
//   | ObTimestampType     | int64_t                       | 8          | 8                   |
//   | ObDateType          | int32_t                       | 4          | 4                   |
//   | ObTimeType          | int64_t                       | 8          | 8                   |
//   | ObYearType          | uint8_t                       | 1          | 1                   |
//   | ObVarcharType       | char[]                        | 0          | type maximum length |
//   | ObCharType          | char[]                        | 0          | type maximum length |
//   | ObHexStringType     | char[]                        | 0          | type maximum length |
//   | ObExtendType        | typed Extend payload          | 15         | 16 (legacy view)    |
//   | ObUnknownType       | int64_t                       | 8          | 8                   |
//   | ObTinyTextType      | char[]                        | 0          | type maximum length |
//   | ObTextType          | char[]                        | 0          | type maximum length |
//   | ObMediumTextType    | char[]                        | 0          | type maximum length |
//   | ObLongTextType      | char[]                        | 0          | type maximum length |
//   | ObBitType           | uint64_t                      | 8          | 8                   |
//   | ObEnumType          | uint64_t                      | 8          | 8                   |
//   | ObSetType           | uint64_t                      | 8          | 8                   |
//   | ObEnumInnerType     | char[]                        | 0          | type maximum length |
//   | ObSetInnerType      | char[]                        | 0          | type maximum length |
//   | ObDecimalIntType    | char[]                        | 4          |  64                 |
#define OB_DATUM_SIZE 12

// Datum memory layout mapping type, see ObDatum::get_sql_value_datum_map_type()
enum SqlValueDatumMapType : uint8_t {
  OBJ_DATUM_NULL, // null
  OBJ_DATUM_STRING, // string
  OBJ_DATUM_NUMBER, // number
  OBJ_DATUM_8BYTE_DATA, // 8-byte value
  OBJ_DATUM_4BYTE_DATA, // 4-byte value
  OBJ_DATUM_1BYTE_DATA, // 1-byte value
  OBJ_DATUM_4BYTE_LEN_DATA, // 4-byte length + 8-byte value
  OBJ_DATUM_2BYTE_LEN_DATA, // 2-byte length + 8-byte value
  OBJ_DATUM_EXTEND,  // typed extended value (subtype, size, payload)
  OBJ_DATUM_DECIMALINT, // decimal int
  OBJ_DATUM_MAPPING_MAX,
};

// Native Extend payload has no general-purpose value object or type tag:
// [value:8][size:4][subtype:1][collation-level:1][collation:1]. The last two
// attributes preserve legacy metadata, including sentinels cast to a column
// collation. Persisted version-1 payloads remain readable as byte views.
constexpr uint32_t DATUM_EXTEND_PAYLOAD_SIZE = 15;
constexpr uint32_t DATUM_LEGACY_EXTEND_SIZE = 16;
constexpr int64_t DATUM_MIN_EXT_VALUE = -3;
constexpr int64_t DATUM_MAX_EXT_VALUE = -2;

enum DatumReserveSize {
  OBJ_DATUM_NULL_RES_SIZE = 0,
  OBJ_DATUM_1BYTE_DATA_RES_SIZE = 1,
  OBJ_DATUM_4BYTE_DATA_RES_SIZE = 4,
  OBJ_DATUM_8BYTE_DATA_RES_SIZE = 8,
  OBJ_DATUM_2BYTE_LEN_DATA_RES_SIZE = 10,
  OBJ_DATUM_4BYTE_LEN_DATA_RES_SIZE = 12,
  OBJ_DATUM_EXTEND_RES_SIZE = DATUM_LEGACY_EXTEND_SIZE, // accommodates old byte views too
  OBJ_DATUM_NUMBER_RES_SIZE = 40,
  OBJ_DATUM_DECIMALINT_MAX_RES_SIZE = 64,
  OBJ_DATUM_STRING_RES_SIZE = 128,
  OBJ_DATUM_MAX_RES_SIZE = 128
};

// Pointer to data
struct ObDatumPtr {
  union {
    const char *ptr_;
    const int64_t *int_;
    const uint64_t *uint_;
    const float *float_;
    const double *double_;
    const int64_t *unknown_;
    const number::ObCompactNumber *num_;
    const int64_t *datetime_;
    const int32_t *date_;
    const int64_t *time_;
    const uint8_t *year_;
    const ObOTimestampData *timestamp_tz_;
    const ObOTimestampTinyData *timestamp_tiny_;
    const char *inner_enumset_;
    const ObLobCommon *lob_data_;
    const ObMemLobCommon *mem_lob_;
    const ObMySQLDate *mysql_date_;
    const ObMySQLDateTime *mysql_datetime_;
    const ObDecimalInt *decimal_int_;
  };
#if defined(__EMSCRIPTEN__) && __SIZEOF_POINTER__ == 4
  // SQL datum arrays and storage datums share a 12-byte slot contract. Keep
  // the descriptor at offset 8 even though a wasm32 pointer occupies 4 bytes.
  uint32_t reserved_ptr_ = 0;
#endif

  ObDatumPtr() : ptr_(NULL) {}
}__attribute__ ((packed)) ;

// Datum description, is null or length of data
struct ObDatumDesc {
  enum FlagType {
    NONE = 0,
    OUTROW,
    EXT,
    HAS_LOB_HEADER,
  };
  union {
    struct {
      uint32_t len_         : 29;
      uint32_t flag_        : 2;
      uint32_t null_        : 1;
    };
    // Pack null flag and length, you can use it to unset null flag and set length of data.
    uint32_t pack_;
  };

  ObDatumDesc() : pack_(0) {}
  void set_none() { null_ = 0; flag_ = FlagType::NONE; }
  void set_null() { len_ = 0; null_ = 1; flag_ = FlagType::NONE; }
  bool is_null() const { return null_ == 1; }
  // interfaces following used for storage layer only
  void set_ext(uint32_t payload_size = DATUM_EXTEND_PAYLOAD_SIZE)
  { len_ = payload_size; null_ = 0; flag_ = FlagType::EXT; }
  bool is_ext() const { return flag_ == FlagType::EXT; }
  void set_outrow() { null_ = 0; flag_ = FlagType::OUTROW; }
  bool is_outrow() const { return flag_ == FlagType::OUTROW; }

  void set_flag(const FlagType &flag_type) { flag_ = flag_type; }
  void set_has_lob_header() { flag_ = FlagType::HAS_LOB_HEADER; }
  bool has_lob_header() const { return flag_ == FlagType::HAS_LOB_HEADER; }
  void set_flag_none() { flag_ = FlagType::NONE; }
} __attribute__ ((packed)) ;

// Datum structure, multiple inheritance from ObDatumPtr and ObDatumDesc makes
// %ptr_, %len_ access easier.
struct ObDatum : public ObDatumPtr, public ObDatumDesc {
OB_UNIS_VERSION(1);
public:
  ObDatumPtr &ptr() { return *this; }
  const ObDatumPtr &ptr() const { return *this; }
  ObDatumDesc &desc() { return *this; };
  const ObDatumDesc &desc() const { return *this; };

  ObDatum() : ObDatumPtr(), ObDatumDesc() {}
  ObDatum(const char *ptr, uint32_t len, bool null) {
    ptr_ = ptr;
    len_ = len;
    null_ = null;
  }

  inline void reset() { new (this) ObDatum(); }
  static bool binary_equal(const ObDatum &r, const ObDatum &l)
  {
    bool equal = true;
    if (r.is_null() != l.is_null()) {
      equal = false;
    } else if (!r.is_null()) {
      if (r.pack_ != l.pack_) {
        equal = false;
        if (r.is_ext() && l.is_ext() && r.get_wire_pack() == l.get_wire_pack()) {
          char rbuf[DATUM_LEGACY_EXTEND_SIZE];
          char lbuf[DATUM_LEGACY_EXTEND_SIZE];
          equal = (0 == MEMCMP(r.get_wire_ext_payload(rbuf), l.get_wire_ext_payload(lbuf),
                               DATUM_LEGACY_EXTEND_SIZE));
        }
      } else {
        equal = (0 == MEMCMP(r.ptr_, l.ptr_, r.len_));
      }
    }
    return equal;
  }
  static SqlValueDatumMapType get_sql_value_datum_map_type(const SqlValueType type);
  // Get reserved size for specified type.
  // For some types, we can get a finer size through precision, so if you get precison,
  // plz be sure to pass in the function instead of using the default value.
  static uint32_t get_reserved_size(const SqlValueDatumMapType type,
                                    const int16_t prec = PRECISION_UNKNOWN_YET);
  inline int from_storage_datum(const ObDatum &datum, const SqlValueDatumMapType map_type, bool need_copy = false);
  inline int64_t checksum(const int64_t current) const;

  // remember to check is_null() before get_bool()
  inline bool get_bool() const { return 0 != get_int(); }
  inline bool is_false() const { return !is_null() && 0 == get_int(); }
  inline bool is_true() const { return  !is_null() && 0 != get_int(); }
  inline bool is_min() const { return has_ext_value() && get_ext() == DATUM_MIN_EXT_VALUE; }
  inline bool is_max() const { return has_ext_value() && get_ext() == DATUM_MAX_EXT_VALUE; }
  inline bool is_nop() const { return has_ext_value() && get_ext() == ObActionFlag::OP_NOP; }
  inline bool is_null_or_nop() const { return is_null() || is_nop(); }
  inline int8_t get_int8() const { return  *(reinterpret_cast<const int8_t*> (int_)); }
  inline int8_t get_tinyint() const { return *(reinterpret_cast<const int8_t*> (int_)); }
  inline int16_t get_smallint() const { return *(reinterpret_cast<const int16_t*> (int_)); }
  inline int32_t get_mediumint() const { return *(reinterpret_cast<const int32_t*> (int_)); }
  inline int32_t get_int32() const { return *(reinterpret_cast<const int32_t*> (int_)); }
  inline int64_t get_int() const { return *int_; }
  inline uint8_t get_uint8() const { return *(reinterpret_cast<const uint8_t*> (uint_)); }
  inline uint8_t get_utinyint() const { return *(reinterpret_cast<const uint8_t*> (uint_)); }
  inline uint16_t get_usmallint() const { return *(reinterpret_cast<const uint16_t*> (uint_)); }
  inline uint32_t get_umediumint() const { return *(reinterpret_cast<const uint32_t*> (uint_)); }
  inline uint32_t get_uint32() const { return *(reinterpret_cast<const uint32_t*> (uint_)); }
  inline uint64_t get_uint64() const { return *uint_; }
  inline uint64_t get_uint() const { return *uint_; }
  inline float get_float() const { return *float_; }
  inline double get_double() const { return *double_; }
  inline float get_ufloat() const { return *float_; }
  inline double get_udouble() const { return *double_; }
  inline bool is_native_extend() const
  { return is_ext() && len_ == DATUM_EXTEND_PAYLOAD_SIZE; }
  inline bool has_ext_value() const
  { return is_native_extend() || (is_ext() && len_ == DATUM_LEGACY_EXTEND_SIZE
                                  && static_cast<uint8_t>(ptr_[0]) == ObExtendType); }
  inline bool is_extended_null() const
  { return is_ext() && len_ == DATUM_LEGACY_EXTEND_SIZE
           && static_cast<uint8_t>(ptr_[0]) == ObNullType; }
  inline int64_t get_ext() const
  {
    OB_ASSERT(has_ext_value());
    int64_t value = 0;
    MEMCPY(&value, ptr_ + (is_native_extend() ? 0 : 8), sizeof(value));
    return value;
  }
  inline int32_t get_extend_size() const
  {
    OB_ASSERT(has_ext_value());
    int32_t size = 0;
    MEMCPY(&size, ptr_ + (is_native_extend() ? 8 : 4), sizeof(size));
    return size;
  }
  inline uint8_t get_extend_type() const
  {
    return has_ext_value() ? static_cast<uint8_t>(ptr_[is_native_extend() ? 12 : 3])
                           : UINT8_MAX;
  }
  // Raw compatibility metadata bytes: cs_level also carries the high four
  // collation-ID bits. Do not normalize or mask these in the byte codec.
  inline uint8_t get_extend_collation_level() const
  { OB_ASSERT(has_ext_value()); return static_cast<uint8_t>(ptr_[is_native_extend() ? 13 : 1]); }
  inline uint8_t get_extend_collation() const
  { OB_ASSERT(has_ext_value()); return static_cast<uint8_t>(ptr_[is_native_extend() ? 14 : 2]); }
  // The caller provides at least get_reserved_size(OBJ_DATUM_EXTEND) bytes.
  inline void set_extend(int64_t value, uint8_t subtype, int32_t size = 0,
                          uint8_t collation_level = CS_LEVEL_INVALID,
                          uint8_t collation = CS_TYPE_INVALID)
  {
    MEMCPY(no_cv(ptr_), &value, sizeof(value));
    MEMCPY(no_cv(ptr_) + 8, &size, sizeof(size));
    no_cv(ptr_)[12] = static_cast<char>(subtype);
    no_cv(ptr_)[13] = static_cast<char>(collation_level);
    no_cv(ptr_)[14] = static_cast<char>(collation);
    set_ext();
  }
  inline void set_ext_value(int64_t value) { set_extend(value, 0); }
  // Canonical version-1 encoding and checksums are independent of the native
  // layout. No value object is reconstructed by this compatibility codec.
  inline uint32_t get_wire_pack() const
  {
    ObDatumDesc wire = desc();
    if (is_native_extend()) { wire.len_ = DATUM_LEGACY_EXTEND_SIZE; }
    return wire.pack_;
  }
  inline const char *get_wire_ext_payload(char (&buf)[DATUM_LEGACY_EXTEND_SIZE]) const
  {
    if (is_native_extend()) {
      buf[0] = static_cast<char>(ObExtendType);
      buf[1] = ptr_[13];
      buf[2] = ptr_[14];
      buf[3] = ptr_[12];
      MEMCPY(buf + 4, ptr_ + 8, 4);
      MEMCPY(buf + 8, ptr_, 8);
      return buf;
    }
    return ptr_;
  }
  inline int64_t get_unknown() const {return *unknown_; }
  inline uint64_t get_bit() const { return *uint_; }
  inline uint64_t get_enum() const { return *uint_; }
  inline uint64_t get_set() const { return *uint_; }
  inline uint64_t get_enumset() const { return *uint_; }
  inline const number::ObCompactNumber &get_number() const { return *num_; }
  inline const number::ObNumber::Desc &get_number_desc() const { return num_->desc_; }
  inline const uint32_t *get_number_digits() const { return &num_->digits_[0]; }
  inline int64_t get_datetime() const { return *datetime_;  }
  inline ObMySQLDateTime get_mysql_datetime() const { return *mysql_datetime_; }
  inline int64_t get_timestamp() const { return *datetime_;  }
  inline int32_t get_date() const { return *date_; }
  inline ObMySQLDate get_mysql_date() const { return *mysql_date_; }
  inline int64_t get_time() const { return *time_; }
  inline uint8_t get_year() const { return *year_; }
  // for ObTimestampNanoType (both ObOTimestampTC type class)
  inline ObOTimestampData get_otimestamp_tiny() const {
    ObOTimestampData res;
    res.time_ctx_.time_desc_ = timestamp_tiny_->desc_;
    res.time_ctx_.tz_desc_ = 0;
    res.time_us_ = timestamp_tiny_->time_us_;
    return res;
  }
  inline const ObString get_string() const { return ObString(len_, ptr_); }
  inline const ObString get_json() const { return get_string(); }
  inline int get_enumset_inner(ObEnumSetInnerValue &inner_value) const
  {
    int64_t pos = 0;
    return inner_value.deserialize(ptr_, len_, pos);
  }
  inline void get_mem_lob(ObLobLocatorV2 &lob_locator) const
  {
    lob_locator.assign_ptr(mem_lob_, len_, has_lob_header());
  }
  inline const ObLobCommon &get_lob_data() const { return *lob_data_; }

  inline const ObDecimalInt *get_decimal_int() const { return decimal_int_; }
  inline int32_t get_decimal_int32() const { return *decimal_int_->int32_v_; }
  inline int64_t get_decimal_int64() const { return *decimal_int_->int64_v_; }
  inline int128_t get_decimal_int128() const { return *decimal_int_->int128_v_; }
  inline int256_t get_decimal_int256() const { return *decimal_int_->int256_v_; }
  inline int512_t get_decimal_int512() const { return *decimal_int_->int512_v_; }

  inline int32_t get_int_bytes() const { return len_; }

  // Setter functions for ObDatum.
  // CAUTION: The caller is responsible for ensuring %ptr_ has enough memory.
  //
  // We reserve enough memory in sql expression, it's safe to use the setter functions in sql
  // expression evaluate.
  // Set a signed integer value.
  inline void set_int(const int64_t v) { *no_cv(int_) = v; pack_ = sizeof(int64_t); };
  inline void set_int32(const int32_t v) { set_int(static_cast<int64_t>(v)); }
  // Set an unsigned integer value.
  inline void set_uint(const uint64_t v) { *no_cv(uint_) = v; pack_ = sizeof(uint64_t); }
  inline void set_uint32(const uint32_t v) { set_uint(static_cast<uint64_t>(v)); }
  inline void set_bit(const uint64_t v) { set_uint(v); }

  inline void set_bool(const bool v) { set_int(static_cast<int64_t>(v)); }
  inline void set_true() { set_bool(true); }
  inline void set_false() { set_bool(false); }

  inline void set_float(const float v) { *no_cv(float_) = v; pack_ = sizeof(float); }
  inline void set_double(const double v) { *no_cv(double_) = v; pack_ = sizeof(double); }
  inline void set_enum(const uint64_t v) { *no_cv(uint_) = v; pack_ = sizeof(uint64_t); }
  inline void set_set(const uint64_t v) { *no_cv(uint_) = v; pack_ = sizeof(uint64_t); }
  inline void set_datetime(const int64_t v) { *no_cv(datetime_) = v; pack_ = sizeof(int64_t); }
  inline void set_timestamp(const int64_t v) { *no_cv(datetime_) = v; pack_ = sizeof(int64_t); }
  //set otimestamp value of ObTimestampLTZType and ObTimestampNanoType
  inline void set_otimestamp_tiny(const ObOTimestampData &v)
  {
    no_cv(timestamp_tiny_)->desc_ = v.time_ctx_.time_desc_;
    no_cv(timestamp_tiny_)->time_us_ = v.time_us_;
    pack_ = sizeof(ObOTimestampTinyData);
  }
  // Set a time value.
  inline void set_time(const int64_t v) { set_int(v); }
  inline void set_otimestamp(const ObOTimestampData &v)
  { memcpy(no_cv(ptr_), &v, sizeof(v)); pack_ = sizeof(v); }
  inline void set_otimestamp(const int64_t time_us, const uint16_t time_desc)
  {
    memcpy(no_cv(ptr_), &time_us, sizeof(time_us));
    memcpy(no_cv(ptr_) + sizeof(time_us), &time_desc, sizeof(time_desc));
  }

  // Set a date value.
  inline void set_date(const int32_t v)
  { memcpy(no_cv(ptr_), &v, sizeof(v)); pack_ = sizeof(v); }
  // Set a year value.
  inline void set_year(const int8_t v)
  { memcpy(no_cv(ptr_), &v, sizeof(v)); pack_ = sizeof(v); }

  // Set number, deep copy all number digits here.
  OB_INLINE void set_number(const number::ObNumber &num);
  // Set compact number, deep copy all number digits too.
  OB_INLINE void set_number(const number::ObCompactNumber &cnum);
  OB_INLINE void set_number_shallow(const number::ObCompactNumber &cnum);
  OB_INLINE void set_pack(const int64_t len);
  OB_INLINE void set_decimal_int(const ObDecimalInt *decint, int32_t len);
  OB_INLINE void set_decimal_int_shallow(const ObDecimalInt *decint, int32_t len);
  template<typename T>
  OB_INLINE void set_decimal_int(const T &decint)
  {
    memcpy(no_cv(ptr_), &decint, sizeof(T));
    pack_ = sizeof(T);
  }
  inline void set_string(const ObString &v) { ptr_ = v.ptr(); pack_ = v.length(); }
  inline void set_string(const char *ptr, const uint32_t len) { ptr_ = ptr; pack_ = len; }
  inline void set_enumset_inner(const ObString &v) { set_string(v); }
  inline void set_enumset_inner(const char *ptr, const uint32_t len) { set_string(ptr, len); }
  inline void set_lob_data(const ObLobCommon &value, int64_t length)
  {
    lob_data_ = &value;
    pack_ = static_cast<uint32_t>(length);//TODO(yuanzhi.zy):need check
  }
  inline void set_mysql_date(const ObMySQLDate v)
  { memcpy(no_cv(ptr_), &v, sizeof(v)); pack_ = sizeof(v); }
  inline void set_mysql_datetime(const ObMySQLDateTime v)
  { *no_cv(mysql_datetime_) = v; pack_ = sizeof(int64_t);  }
  inline void set_datum(const ObDatum &other) { *this = other; }
  inline int64_t get_deep_copy_size() const { return is_null() ? 0 : len_; }
  inline int deep_copy(const ObDatum &src, char *buf, int64_t max_size, int64_t &pos)
  {
    int ret = OB_SUCCESS;
    *this = src;
    if (!is_null()) {
      if (pos + len_ > max_size) {
        ret = OB_BUF_NOT_ENOUGH;
      } else {
        MEMCPY(buf + pos, src.ptr_, len_);
        ptr_ = buf + pos;
        pos += len_;
      }
    }
    return ret;
  }

  inline int deep_copy(const ObDatum &src, ObIAllocator &alloc)
  {
    int ret = OB_SUCCESS;
    *this = src;
    if (!is_null()) {
      const int64_t alloc_len = src.len_ > 0 ? src.len_ : 1;
      char * buf = static_cast<char *>(alloc.alloc(alloc_len));
      if (OB_ISNULL(buf)) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        COMMON_LOG(WARN, "allocate memory failed", K(alloc_len), K(ret));
      } else {
        MEMCPY(buf, src.ptr_, src.len_);
        // need set ptr_ after memory copy, if this == &src
        ptr_ = buf;
      }
    }
    return ret;
  }

  // Print HEX value of ptr_, more detailed is printed by type guessing.
  // Try not to use this, but use DATUM2STR which interprets it via meta.
  DECLARE_TO_STRING;

  // Remove const value qualification for pointer.
  template <typename T>
  inline __attribute__((always_inline)) T *no_cv(const T *ptr) { return const_cast<T *>(ptr); }

  template <SqlValueDatumMapType type>
  inline void datum2datum(const ObDatum &datum);

}__attribute__ ((packed)) ;

struct ObDatumVector {
  ObDatum  *at(const int64_t i) const { return datums_ + (mask_ & i); }

  void set_batch(const bool is) { mask_ = is ? UINT64_MAX : 0; }
  bool is_batch() const { return UINT64_MAX == mask_;  }

  TO_STRING_KV(KP(datums_), K(mask_));
  ObDatum  *datums_   = nullptr;
  uint64_t mask_ = 0;
};

// Get payload of datum for reading, some type class will specialization this.
template <SqlTypeClass TC>
struct ObDatumPayload
{
  // return ptr_ for default implement
  static inline const char *get(const ObDatum &d) { return d.ptr_; }
};

template <> struct ObDatumPayload<ObNullTC>
{ static inline void get(const ObDatum &d) { UNUSED(d); return; } };

template <> struct ObDatumPayload<ObIntTC>
{ static inline int64_t get(const ObDatum &d) { return *d.int_; } };

template <> struct ObDatumPayload<ObUIntTC>
{ static inline uint64_t get(const ObDatum &d) { return *d.uint_; } };

template <> struct ObDatumPayload<ObFloatTC>
{ static inline float get(const ObDatum &d) { return *d.float_; } };

template <> struct ObDatumPayload<ObDoubleTC>
{ static inline double get(const ObDatum &d) { return *d.double_; } };

template <> struct ObDatumPayload<ObNumberTC>
{ static inline const number::ObCompactNumber &get(const ObDatum &d) { return *d.num_; } };

template <> struct ObDatumPayload<ObDateTimeTC>
{ static inline int64_t get(const ObDatum &d) { return *d.int_; } };

template <> struct ObDatumPayload<ObDateTC>
{
  static inline int32_t get(const ObDatum &d)
  { return *reinterpret_cast<const int32_t *>(d.ptr_); }
};

template <> struct ObDatumPayload<ObTimeTC>
{ static inline int64_t get(const ObDatum &d) { return *d.int_; } };

template <> struct ObDatumPayload<ObYearTC>
{
  static inline uint8_t get(const ObDatum &d)
  { return *reinterpret_cast<const uint8_t *>(d.year_); }
};

// ObStringTC: default implement, return ptr_

template <> struct ObDatumPayload<ObExtendTC>
{ static inline int64_t get(const ObDatum &d) { return *d.int_; } };

template <> struct ObDatumPayload<ObUnknownTC>
{ static inline int64_t get(const ObDatum &d) { return *d.int_; } };

// ObTextTC: default implement, return ptr_

template <> struct ObDatumPayload<ObBitTC>
{ static inline uint64_t get(const ObDatum &d) { return *d.uint_; } };

template <> struct ObDatumPayload<ObEnumSetTC>
{ static inline uint64_t get(const ObDatum &d) { return *d.uint_; } };

template <> struct ObDatumPayload<ObMySQLDateTC>
{
  static inline int32_t get(const ObDatum &d)
  { return *reinterpret_cast<const int32_t *>(d.ptr_); }
};

template <> struct ObDatumPayload<ObMySQLDateTimeTC>
{ static inline int64_t get(const ObDatum &d) { return *d.int_; } };

// ObEnumSetInnerTC: default implement, return ptr_
// ObOTimestampTC: no corresponding structure defined, need interpret ptr_
// ObRawTC: default implement, return ptr_
// ObIntervalTC : no corresponding structure defined, need interpret ptr_

OB_INLINE void ObDatum::set_number(const number::ObNumber &num)
{
  no_cv(num_)->desc_ = num.d_;
  const int64_t len = num.d_.len_ * sizeof(*num.get_digits());
  memcpy(&no_cv(num_)->digits_[0], num.get_digits(), len);
  pack_ = static_cast<uint32_t>(len + sizeof(*num_));
}

OB_INLINE void ObDatum::set_number(const number::ObCompactNumber &cnum)
{
  pack_ = static_cast<uint32_t>(sizeof(cnum) + cnum.desc_.len_ * sizeof(cnum.digits_[0]));
  memcpy(no_cv(num_), &cnum, len_);
}

OB_INLINE void ObDatum::set_number_shallow(const number::ObCompactNumber &cnum)
{
  pack_ = static_cast<uint32_t>(sizeof(cnum) + cnum.desc_.len_ * sizeof(cnum.digits_[0]));
  num_ = &cnum;
}

OB_INLINE void ObDatum::set_decimal_int(const ObDecimalInt *decint, int32_t len)
{
  memcpy(no_cv(ptr_), decint, len);
  pack_ = len;
}

OB_INLINE void ObDatum::set_decimal_int_shallow(const ObDecimalInt *decint, int32_t len)
{
  pack_ = len;
  decimal_int_ = decint;
}


OB_INLINE void ObDatum::set_pack(const int64_t len)
{
  //pack_ = desc.len_ * sizeof(no_cv(num_)->digits_[0]) + sizeof(*num_);
  pack_ = static_cast<uint32_t>(len);
}


template <>
inline void ObDatum::datum2datum<OBJ_DATUM_NULL>(const ObDatum &datum)
{
  set_null();
}


template <>
inline void ObDatum::datum2datum<OBJ_DATUM_STRING>(const ObDatum &datum)
{
  ptr_ = datum.ptr_;
  pack_ = datum.pack_;
}


template <>
inline void ObDatum::datum2datum<OBJ_DATUM_NUMBER>(const ObDatum &datum)
{
  memcpy(no_cv(ptr_), datum.ptr_, datum.pack_);
  pack_ = datum.pack_;
}


template <>
inline void ObDatum::datum2datum<OBJ_DATUM_8BYTE_DATA>(const ObDatum &datum)
{
  memcpy(no_cv(ptr_), datum.ptr_, sizeof(uint64_t));
  pack_ = sizeof(uint64_t);
}


template <>
inline void ObDatum::datum2datum<OBJ_DATUM_EXTEND>(const ObDatum &datum)
{
  MEMCPY(no_cv(ptr_), datum.ptr_, datum.len_);
  pack_ = datum.pack_;
}


template <>
inline void ObDatum::datum2datum<OBJ_DATUM_DECIMALINT>(const ObDatum &datum)
{
  memcpy(no_cv(ptr_), datum.ptr_, datum.pack_);
  pack_ = datum.pack_;
}


inline int ObDatum::from_storage_datum(const ObDatum &datum, const SqlValueDatumMapType map_type, bool need_copy)
{
  int ret = common::OB_SUCCESS;
  if (datum.is_ext()) {
    ret = common::OB_INVALID_ARGUMENT;
    COMMON_LOG(WARN, "Invalid argument for ext storage datum to datum", K(ret), K(datum));
  } else if (datum.is_null()) {
    set_null();
  } else if (need_copy) {
    memcpy(no_cv(ptr_), datum.ptr_, datum.len_);
    pack_ = datum.pack_;
  } else {
    switch (map_type) {
      case OBJ_DATUM_NULL: { datum2datum<OBJ_DATUM_NULL>(datum); break; }
      case OBJ_DATUM_STRING: { datum2datum<OBJ_DATUM_STRING>(datum); break; }
      case OBJ_DATUM_NUMBER:
      case OBJ_DATUM_8BYTE_DATA:
      case OBJ_DATUM_4BYTE_DATA:
      case OBJ_DATUM_1BYTE_DATA:
      case OBJ_DATUM_4BYTE_LEN_DATA:
      case OBJ_DATUM_2BYTE_LEN_DATA:
        { datum2datum<OBJ_DATUM_NUMBER>(datum); break; }
      case OBJ_DATUM_EXTEND: { datum2datum<OBJ_DATUM_EXTEND>(datum); break; }
      case OBJ_DATUM_DECIMALINT: { datum2datum<OBJ_DATUM_DECIMALINT>(datum); break; }
      case OBJ_DATUM_MAPPING_MAX: {
        ret = common::OB_ERR_UNEXPECTED;
        COMMON_LOG(WARN, "invalid datum datum mapping", K(ret), K(datum), K(map_type));
      }
    }
  }
  return ret;
}

inline int64_t ObDatum::checksum(const int64_t current) const
{
  int64_t result = current;
  if (OB_UNLIKELY(is_native_extend())) {
    const uint32_t wire_pack = get_wire_pack();
    char buf[DATUM_LEGACY_EXTEND_SIZE];
    result = ob_crc64_sse42(result, &wire_pack, sizeof(wire_pack));
    result = ob_crc64_sse42(result, get_wire_ext_payload(buf), sizeof(buf));
  } else {
    result = ob_crc64_sse42(result, &pack_, sizeof(pack_));
    if (len_ > 0) {
      result = ob_crc64_sse42(result, ptr_, len_);
    }
  }
  return result;
}


template <typename T> struct ObDecimalIntPayload
{
  static inline T get(const ObDatum &d) { return *reinterpret_cast<const T *>(d.ptr_); }
};


STATIC_ASSERT(sizeof(ObDatum) == 12, "datum size error");

} // end namespace common
} // end namespace oceanbase


