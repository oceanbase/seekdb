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

#include "share/datum/typed_datum_compare.h"
#include "share/datum/typed_datum_murmur.h"
#include "share/datum/ob_datum_funcs.h"
#include "common/wide_integer/ob_wide_integer_helper.h"
#include "common/datum/datum_parameter_array.h"

namespace oceanbase { namespace common {
namespace {
SqlTypeMeta effective_meta(const DatumValue &value)
{
  SqlTypeMeta meta = value.meta_;
  if (value.datum_.is_null() && !meta.is_null()) { meta.set_null(); }
  return meta;
}
bool bound(const DatumValue &v) { return v.datum_.is_min() || v.datum_.is_max(); }
bool valid(const DatumValue &v, bool check_collation = true)
{
  const SqlValueType type = v.meta_.get_type();
  if (type < ObNullType || type >= ObMaxType) { return false; }
  if (v.datum_.is_null() || v.meta_.is_null()) { return true; }
  const uint32_t len = v.datum_.len_;
  if (v.datum_.ptr_ == nullptr && len > 0) { return false; }
  if (v.meta_.is_ext_sql_array()) { return len == sizeof(int64_t); }
  switch (ObDatum::get_sql_value_datum_map_type(type)) {
    case OBJ_DATUM_8BYTE_DATA: return len == sizeof(int64_t);
    case OBJ_DATUM_4BYTE_DATA: return len == sizeof(int32_t);
    case OBJ_DATUM_1BYTE_DATA: return len == sizeof(uint8_t);
    case OBJ_DATUM_EXTEND:
      return v.datum_.ptr_ != nullptr && v.datum_.has_ext_value();
    case OBJ_DATUM_NUMBER:
      return len >= sizeof(number::ObNumber::Desc)
          && len == sizeof(number::ObNumber::Desc) + sizeof(uint32_t) * v.datum_.get_number_desc().len_;
    case OBJ_DATUM_DECIMALINT:
      return len == 0 || len == 4 || len == 8 || len == 16 || len == 32 || len == 64;
    case OBJ_DATUM_STRING:
      return !check_collation || !v.meta_.is_string_type()
          || (v.meta_.get_collation_type() > CS_TYPE_INVALID
          && v.meta_.get_collation_type() < CS_TYPE_MAX);
    default: return true;
  }
}
bool needs_external_lob(const DatumValue &value)
{
  bool external = false;
  if (!value.datum_.is_null() && value.meta_.is_lob_storage()
      && value.meta_.has_lob_header() && value.datum_.len_ > 0) {
    ObLobLocatorV2 locator(value.datum_.get_string(), true);
    external = !locator.is_valid() || !locator.has_inrow_data();
  }
  return external;
}
int compare_decimals(const ObDecimalInt *left, int32_t left_bytes, ObScale left_scale,
                     const ObDecimalInt *right, int32_t right_bytes, ObScale right_scale, int &cmp)
{
  // Empty coefficients are a valid representation of zero. The wide integer
  // builder expects a concrete width even for zero.
  const int32_t zero = 0;
  if (left_bytes == 0) { left = reinterpret_cast<const ObDecimalInt *>(&zero); left_bytes = sizeof(zero); }
  if (right_bytes == 0) { right = reinterpret_cast<const ObDecimalInt *>(&zero); right_bytes = sizeof(zero); }
  ObDecimalIntBuilder l, r;
  int ret = OB_SUCCESS;
  if (left_scale < right_scale) {
    if (OB_FAIL(wide::common_scale_decimalint(left, left_bytes, left_scale, right_scale, l))) {}
    else { r.from(right, right_bytes); }
  } else if (left_scale > right_scale) {
    if (OB_FAIL(wide::common_scale_decimalint(right, right_bytes, right_scale, left_scale, r))) {}
    else { l.from(left, left_bytes); }
  } else { l.from(left, left_bytes); r.from(right, right_bytes); }
  if (OB_SUCC(ret)) { ret = wide::compare(l, r, cmp); }
  return ret;
}
int compare_decimal_other(const DatumValue &decimal, const DatumValue &other, int &cmp)
{
  int ret = OB_SUCCESS;
  ObDecimalIntBuilder allocator;
  ObDecimalInt *integer = nullptr;
  int32_t bytes = 0;
  ObScale scale = 0;
  switch (other.meta_.get_type_class()) {
    case ObIntTC: ret = wide::from_integer(other.datum_.get_int(), allocator, integer, bytes); break;
    case ObUIntTC:
    case ObBitTC:
    case ObEnumSetTC: ret = wide::from_integer(other.datum_.get_uint64(), allocator, integer, bytes); break;
    case ObNumberTC: {
      number::ObNumber number(other.datum_.get_number()); scale = number.get_scale();
      ret = wide::from_number(number, allocator, scale, integer, bytes); break;
    }
    case ObDecimalIntTC:
      return compare_decimals(decimal.datum_.get_decimal_int(), decimal.datum_.get_int_bytes(), decimal.meta_.get_scale(),
                              other.datum_.get_decimal_int(), other.datum_.get_int_bytes(), other.meta_.get_scale(), cmp);
    default: ret = OB_NOT_SUPPORTED; break;
  }
  if (OB_SUCC(ret)) {
    ret = compare_decimals(decimal.datum_.get_decimal_int(), decimal.datum_.get_int_bytes(), decimal.meta_.get_scale(),
                           integer, bytes, scale, cmp);
  }
  return ret;
}
bool real_type(SqlTypeClass tc) { return tc == ObFloatTC || tc == ObDoubleTC; }
bool unsigned_type(SqlTypeClass tc) { return tc == ObUIntTC || tc == ObEnumSetTC; }
bool numeric_type(SqlTypeClass tc)
{
  return tc == ObIntTC || tc == ObUIntTC || tc == ObFloatTC || tc == ObDoubleTC
      || tc == ObNumberTC || tc == ObEnumSetTC || tc == ObDecimalIntTC;
}
bool numeric_pair_supported(SqlTypeClass left, SqlTypeClass right)
{
  // This is the legacy no-cast comparison matrix, expressed in Datum type
  // classes. Keep the asymmetric gaps (FLOAT/NUMBER, DOUBLE/NUMBER, etc.):
  // they require SQL coercion rather than a direct comparison.
  if (left == right) { return true; }
  if (left == ObIntTC || right == ObIntTC) {
    const auto other = left == ObIntTC ? right : left;
    return other == ObUIntTC || other == ObFloatTC || other == ObDoubleTC
        || other == ObNumberTC || other == ObEnumSetTC || other == ObDecimalIntTC;
  }
  if (left == ObUIntTC || right == ObUIntTC) {
    const auto other = left == ObUIntTC ? right : left;
    return other == ObFloatTC || other == ObDoubleTC || other == ObNumberTC
        || other == ObEnumSetTC || other == ObDecimalIntTC;
  }
  if (left == ObFloatTC || right == ObFloatTC) {
    const auto other = left == ObFloatTC ? right : left;
    return other == ObDoubleTC || other == ObEnumSetTC;
  }
  if (left == ObDoubleTC || right == ObDoubleTC) {
    return (left == ObDoubleTC ? right : left) == ObEnumSetTC;
  }
  if (left == ObNumberTC || right == ObNumberTC) {
    const auto other = left == ObNumberTC ? right : left;
    return other == ObDecimalIntTC || other == ObEnumSetTC;
  }
  if (left == ObDecimalIntTC || right == ObDecimalIntTC) {
    return (left == ObDecimalIntTC ? right : left) == ObEnumSetTC;
  }
  return false;
}
double real_value(const DatumValue &v)
{
  return v.meta_.get_type_class() == ObFloatTC ? v.datum_.get_float() : v.datum_.get_double();
}
int compare_number_other(const DatumValue &number, const DatumValue &other, int &cmp)
{
  number::ObNumber n(number.datum_.get_number());
  switch (other.meta_.get_type_class()) {
    case ObIntTC: cmp = n.compare(other.datum_.get_int()); break;
    case ObUIntTC:
    case ObEnumSetTC: cmp = n.compare(other.datum_.get_uint64()); break;
    case ObNumberTC: cmp = n.compare(number::ObNumber(other.datum_.get_number())); break;
    default: return OB_NOT_SUPPORTED;
  }
  return OB_SUCCESS;
}
int compare_numeric_pair(const DatumValue &left, const DatumValue &right, int &cmp)
{
  const auto ltc = left.meta_.get_type_class(), rtc = right.meta_.get_type_class();
  int ret = OB_SUCCESS;
  if (ltc == ObNumberTC) { ret = compare_number_other(left, right, cmp); }
  else if (rtc == ObNumberTC) { ret = compare_number_other(right, left, cmp); cmp = -cmp; }
  else if (real_type(ltc) || real_type(rtc)) {
    const double l = real_type(ltc) ? real_value(left) : ltc == ObIntTC
        ? static_cast<double>(left.datum_.get_int()) : static_cast<double>(left.datum_.get_uint64());
    const double r = real_type(rtc) ? real_value(right) : rtc == ObIntTC
        ? static_cast<double>(right.datum_.get_int()) : static_cast<double>(right.datum_.get_uint64());
    if (ltc == rtc && real_type(ltc) && (isnan(l) || isnan(r))) {
      cmp = isnan(l) == isnan(r) ? 0 : isnan(l) ? 1 : -1;
    } else { cmp = l < r ? -1 : l > r ? 1 : 0; }
  } else if (ltc == ObIntTC && unsigned_type(rtc)) {
    const int64_t l = left.datum_.get_int(); const uint64_t r = right.datum_.get_uint64();
    cmp = l < 0 || static_cast<uint64_t>(l) < r ? -1 : static_cast<uint64_t>(l) > r ? 1 : 0;
  } else if (unsigned_type(ltc) && rtc == ObIntTC) {
    ret = compare_numeric_pair(right, left, cmp); cmp = -cmp;
  } else if (unsigned_type(ltc) && unsigned_type(rtc)) {
    const uint64_t l = left.datum_.get_uint64(), r = right.datum_.get_uint64();
    cmp = l < r ? -1 : l > r ? 1 : 0;
  } else { ret = OB_NOT_SUPPORTED; }
  return ret;
}
uint64_t explicit_murmur_charset_hash(const void *data, uint64_t length, uint64_t seed)
{
  return murmurhash64A(data, static_cast<int32_t>(length), seed);
}
ObCollationType collation(const SqlTypeMeta &meta)
{
  return meta.get_collation_type() == CS_TYPE_INVALID ? CS_TYPE_BINARY : meta.get_collation_type();
}
}
bool can_compare_datum_values(const DatumValue &left, const DatumValue &right)
{
  if (!valid(left) || !valid(right)) { return false; }
  if (bound(left) || bound(right)) { return true; }
  if (left.meta_.is_unknown() && right.meta_.is_unknown()) { return true; }
  // SQL NULL has an ordering in this typed helper independent of the
  // declared type on the other operand.
  if (left.datum_.is_null() || left.meta_.is_null()
      || right.datum_.is_null() || right.meta_.is_null()) { return true; }
  const SqlTypeMeta l = effective_meta(left), r = effective_meta(right);
  if (l.is_ext() || r.is_ext() || needs_external_lob(left) || needs_external_lob(right)) { return false; }
  const SqlTypeClass ltc = l.get_type_class(), rtc = r.get_type_class();
  if (numeric_type(ltc) || numeric_type(rtc)) {
    return numeric_type(ltc) && numeric_type(rtc) && numeric_pair_supported(ltc, rtc);
  }
  if (l.is_string_type() && r.is_string_type()) {
    return l.get_collation_type() == r.get_collation_type();
  }
  // The legacy API deliberately rejects DATETIME/TIMESTAMP despite their
  // shared type class. Timestamp-with-time-zone comparisons are supported.
  if ((l.get_type() == ObTimestampType && r.get_type() == ObDateTimeType)
      || (l.get_type() == ObDateTimeType && r.get_type() == ObTimestampType)) { return false; }
  if (ltc == ObLobTC || rtc == ObLobTC || ltc == ObUserDefinedSQLTC
      || rtc == ObUserDefinedSQLTC || ltc == ObCollectionSQLTC || rtc == ObCollectionSQLTC) { return false; }
  if (ltc != rtc && !(ltc == ObOTimestampTC && rtc == ObDateTimeTC)
      && !(rtc == ObOTimestampTC && ltc == ObDateTimeTC)) { return false; }
  // Non-numeric direct comparisons are limited to identical classes (plus
  // the timestamp-with-zone case above); their native Datum comparator is
  // the final capability check. No legacy object-value bridge is involved.
  const ObScale scale = ob_is_double_type(l.get_type()) && ob_is_double_type(r.get_type())
      && l.get_scale() >= 0 && l.get_scale() < OB_NOT_FIXED_SCALE
      && r.get_scale() >= 0 && r.get_scale() < OB_NOT_FIXED_SCALE
      ? MAX(l.get_scale(), r.get_scale()) : SCALE_UNKNOWN_YET;
  return ObDatumFuncs::get_nullsafe_cmp_func(l.get_type(), r.get_type(), NULL_FIRST,
      collation(l), scale, l.has_lob_header() || r.has_lob_header()) != nullptr;
}
bool is_datetime_timestamp_type_pair(const SqlValueType left, const SqlValueType right)
{
  return (left == ObTimestampType && right == ObDateTimeType)
      || (left == ObDateTimeType && right == ObTimestampType);
}
bool can_compare_types_without_cast(const SqlTypeMeta &left, const SqlTypeMeta &right)
{
  const SqlValueType lt = left.get_type(), rt = right.get_type();
  if (lt < ObNullType || lt >= ObMaxType || rt < ObNullType || rt >= ObMaxType
      || is_datetime_timestamp_type_pair(lt, rt)
      || (left.is_string_type() && right.is_string_type()
          && left.get_collation_type() != right.get_collation_type())) { return false; }
  const SqlTypeClass l = left.get_type_class(), r = right.get_type_class();
  // This is the supported directed pair set from the legacy comparison
  // dispatch table. Keep this check metadata-only: SQL coercion handles every
  // pair absent here.
  switch (l) {
    case ObNullTC: return r != ObCollectionSQLTC && r != ObMaxTC;
    case ObIntTC: return r == ObNullTC || r == ObIntTC || r == ObUIntTC || r == ObFloatTC
        || r == ObDoubleTC || r == ObNumberTC || r == ObExtendTC || r == ObEnumSetTC || r == ObDecimalIntTC;
    case ObUIntTC: return r == ObNullTC || r == ObIntTC || r == ObUIntTC || r == ObFloatTC
        || r == ObDoubleTC || r == ObNumberTC || r == ObExtendTC || r == ObEnumSetTC || r == ObDecimalIntTC;
    case ObFloatTC: return r == ObNullTC || r == ObIntTC || r == ObUIntTC || r == ObFloatTC
        || r == ObDoubleTC || r == ObExtendTC || r == ObEnumSetTC;
    case ObDoubleTC: return r == ObNullTC || r == ObIntTC || r == ObUIntTC || r == ObFloatTC
        || r == ObDoubleTC || r == ObExtendTC || r == ObEnumSetTC;
    case ObNumberTC: return r == ObNullTC || r == ObIntTC || r == ObUIntTC || r == ObNumberTC
        || r == ObExtendTC || r == ObEnumSetTC || r == ObDecimalIntTC;
    case ObDateTimeTC: return r == ObNullTC || r == ObDateTimeTC || r == ObExtendTC || r == ObOTimestampTC;
    case ObDateTC: return r == ObNullTC || r == ObDateTC || r == ObExtendTC;
    case ObTimeTC: return r == ObNullTC || r == ObTimeTC || r == ObExtendTC;
    case ObYearTC: return r == ObNullTC || r == ObYearTC || r == ObExtendTC;
    case ObStringTC: return r == ObNullTC || r == ObStringTC || r == ObExtendTC || r == ObTextTC;
    case ObExtendTC: return r != ObLobTC && r != ObJsonTC && r != ObGeometryTC
        && r != ObUserDefinedSQLTC && r != ObCollectionSQLTC && r != ObMaxTC;
    case ObUnknownTC: return r == ObNullTC || r == ObExtendTC || r == ObUnknownTC;
    case ObTextTC: return r == ObNullTC || r == ObStringTC || r == ObExtendTC || r == ObTextTC;
    case ObBitTC: return r == ObNullTC || r == ObExtendTC || r == ObBitTC;
    case ObEnumSetTC: return r == ObNullTC || r == ObIntTC || r == ObUIntTC || r == ObFloatTC
        || r == ObDoubleTC || r == ObNumberTC || r == ObExtendTC || r == ObEnumSetTC || r == ObDecimalIntTC;
    case ObEnumSetInnerTC: return r == ObNullTC || r == ObIntTC || r == ObUIntTC || r == ObFloatTC
        || r == ObDoubleTC || r == ObNumberTC || r == ObExtendTC || r == ObDecimalIntTC;
    case ObOTimestampTC: return r == ObNullTC || r == ObDateTimeTC || r == ObExtendTC || r == ObOTimestampTC;
    case ObRawTC: return r == ObNullTC || r == ObExtendTC || r == ObRawTC;
    case ObIntervalTC: return false;
    case ObRowIDTC: return r == ObNullTC || r == ObExtendTC;
    case ObLobTC: return false;
    case ObJsonTC: return r == ObNullTC || r == ObExtendTC || r == ObJsonTC;
    case ObGeometryTC: return r == ObNullTC || r == ObGeometryTC;
    case ObUserDefinedSQLTC: return r == ObNullTC || r == ObUserDefinedSQLTC;
    case ObDecimalIntTC: return r == ObNullTC || r == ObIntTC || r == ObUIntTC || r == ObNumberTC
        || r == ObExtendTC || r == ObEnumSetTC || r == ObDecimalIntTC;
    case ObCollectionSQLTC: return r == ObNullTC || r == ObCollectionSQLTC;
    case ObMySQLDateTC: return r == ObNullTC || r == ObExtendTC || r == ObMySQLDateTC;
    case ObMySQLDateTimeTC: return r == ObNullTC || r == ObExtendTC || r == ObMySQLDateTimeTC;
    case ObMaxTC: return false;
    default: return false;
  }
}
int compare_datum_values(const DatumValue &left, const DatumValue &right, int &cmp)
{
  int ret = OB_SUCCESS;
  cmp = 0;
  if (!can_compare_datum_values(left, right)) { ret = OB_NOT_SUPPORTED; }
  else if (bound(left) || bound(right)) {
    cmp = (left.datum_.is_min() && right.datum_.is_min()) || (left.datum_.is_max() && right.datum_.is_max()) ? 0
        : left.datum_.is_min() || right.datum_.is_max() ? -1 : 1;
  } else if (left.datum_.is_null() || left.meta_.is_null() || right.datum_.is_null() || right.meta_.is_null()) {
    const bool l = left.datum_.is_null() || left.meta_.is_null();
    const bool r = right.datum_.is_null() || right.meta_.is_null();
    cmp = l == r ? 0 : l ? -1 : 1;
  } else if (left.meta_.is_unknown() && right.meta_.is_unknown()) {
    const int64_t l = left.datum_.get_int(), r = right.datum_.get_int();
    cmp = (l > r) - (l < r);
  } else if (left.meta_.is_decimal_int()) { ret = compare_decimal_other(left, right, cmp); }
  else if (right.meta_.is_decimal_int()) { ret = compare_decimal_other(right, left, cmp); cmp = -cmp; }
  else {
    const ObScale scale = ob_is_double_type(left.meta_.get_type()) && ob_is_double_type(right.meta_.get_type())
        && left.meta_.get_scale() >= 0 && left.meta_.get_scale() < OB_NOT_FIXED_SCALE
        && right.meta_.get_scale() >= 0 && right.meta_.get_scale() < OB_NOT_FIXED_SCALE
        ? MAX(left.meta_.get_scale(), right.meta_.get_scale()) : SCALE_UNKNOWN_YET;
    const auto fn = ObDatumFuncs::get_nullsafe_cmp_func(left.meta_.get_type(), right.meta_.get_type(),
        NULL_FIRST, collation(left.meta_), scale, left.meta_.has_lob_header() || right.meta_.has_lob_header());
    ret = fn == nullptr ? compare_numeric_pair(left, right, cmp) : fn(left.datum_, right.datum_, cmp, nullptr);
  }
  if (OB_SUCC(ret)) { cmp = (cmp > 0) - (cmp < 0); }
  return ret;
}
int datum_values_equal(const DatumValue &left, const DatumValue &right, bool &equal)
{
  int cmp = 0;
  const int ret = compare_datum_values(left, right, cmp);
  equal = OB_SUCCESS == ret && cmp == 0;
  // Mixed REAL/integer and FLOAT/DOUBLE pairs use ordinary IEEE equality in
  // the legacy cache API; their three-way comparison alone is insufficient
  // for unordered NaN. Same-class REAL equality treats two NaNs as equal.
  const auto ltc = effective_meta(left).get_type_class(), rtc = effective_meta(right).get_type_class();
  if (equal && ltc != rtc && (real_type(ltc) || real_type(rtc))) {
    if ((real_type(ltc) && isnan(real_value(left))) || (real_type(rtc) && isnan(real_value(right)))) {
      equal = false;
    }
  }
  return ret;
}
bool binary_equal_datum_values(const DatumValue &left, const DatumValue &right)
{
  DatumValue l = left, r = right;
  if (l.meta_.is_string_type()) { l.meta_.set_collation_type(CS_TYPE_BINARY); }
  if (r.meta_.is_string_type()) { r.meta_.set_collation_type(CS_TYPE_BINARY); }
  bool equal = false;
  return OB_SUCCESS == datum_values_equal(l, r, equal) && equal;
}
bool strict_equal_datum_values(const DatumValue &left, const DatumValue &right)
{
  if (left.get_type() != right.get_type()) { return false; }
  DatumValue l = left, r = right;
  if (l.meta_.is_string_type()) { l.meta_.set_collation_type(CS_TYPE_BINARY); }
  if (r.meta_.is_string_type()) { r.meta_.set_collation_type(CS_TYPE_BINARY); }
  int cmp = 0;
  return OB_SUCCESS == compare_datum_values(l, r, cmp) && cmp == 0;
}
int hash_datum_value(const DatumValue &value, uint64_t seed, uint64_t &hash)
{
  int ret = OB_SUCCESS;
  if (!valid(value)) { ret = OB_INVALID_ARGUMENT; }
  else if (needs_external_lob(value)) { ret = OB_NOT_SUPPORTED; }
  else if (bound(value)) {
    const int64_t ext = value.datum_.get_ext(); hash = murmurhash(&ext, sizeof(ext), seed);
  } else if (value.meta_.is_decimal_int() && !value.datum_.is_null()) {
    hash = murmurhash(value.datum_.ptr_, value.datum_.len_, seed);
  } else if ((value.meta_.is_ext() || value.meta_.is_unknown()) && !value.datum_.is_null()) {
    // The old value hash uses the 64-bit marker/address or question index;
    // neither the native descriptor size nor adjacent metadata is hashed.
    const int64_t raw = value.meta_.is_ext() ? value.get_ext() : value.datum_.get_int();
    hash = murmurhash(&raw, sizeof(raw), seed);
  } else if (value.datum_.is_null() || value.meta_.is_null()) {
    const int type = ObNullType; hash = murmurhash(&type, sizeof(type), seed);
  } else if (value.meta_.get_type_class() == ObNumberTC) {
    hash = murmurhash(&value.datum_.get_number_desc().se_, 1, seed);
    hash = murmurhash(value.datum_.get_number_digits(), sizeof(uint32_t) * value.datum_.get_number_desc().len_, hash);
  } else if (value.meta_.is_string_type() && !value.meta_.has_lob_header()) {
    hash = ObCharset::hash(collation(value.meta_), value.datum_.ptr_, value.datum_.len_, seed, false, nullptr);
  } else if (value.meta_.get_type_class() == ObDateTC || value.meta_.get_type_class() == ObYearTC) {
    const int64_t n = value.meta_.get_type_class() == ObDateTC ? value.datum_.get_date() : value.datum_.get_year();
    hash = murmurhash(&n, sizeof(n), seed);
  } else {
    const SqlTypeMeta meta = effective_meta(value);
    const ObScale scale = ob_is_double_type(meta.get_type()) && value.datum_.get_double() != 0.0
        && !isnan(value.datum_.get_double()) ? meta.get_scale() : SCALE_UNKNOWN_YET;
    auto *funcs = ObDatumFuncs::get_basic_func(meta.get_type(), collation(meta), scale, meta.has_lob_header());
    ret = funcs == nullptr || funcs->murmur_hash_v2_ == nullptr ? OB_NOT_SUPPORTED
        : funcs->murmur_hash_v2_(value.datum_, seed, hash, nullptr);
  }
  return ret;
}
int hash_datum_value_murmur(const DatumValue &value, uint64_t seed, uint64_t &hash)
{
  int ret = OB_SUCCESS;
  if (!valid(value)) { ret = OB_INVALID_ARGUMENT; }
  else if (needs_external_lob(value)) { ret = OB_NOT_SUPPORTED; }
  else if (!value.is_null() && !bound(value)
      && value.meta_.is_string_type() && !value.meta_.has_lob_header()) {
    // The default charset callback differs from the explicit Murmur callback
    // for both binary and case-insensitive collations. TopK historically uses
    // this explicit variant, while rowkey/default-value users use the default.
    hash = ObCharset::hash(collation(value.meta_), value.datum_.ptr_, value.datum_.len_,
                           seed, false, explicit_murmur_charset_hash);
  } else {
    ret = hash_datum_value(value, seed, hash);
  }
  return ret;
}
int clone_typed_value(const DatumValue &source, DatumValue &target, ObIAllocator &allocator)
{
  int ret = OB_SUCCESS;
  DatumValue copy;
  // Owning a payload does not interpret its collation. Schema defaults and
  // generated expressions can legitimately retain an unspecified raw charset.
  // Comparisons and hashing still call the strict validation above.
  if (!valid(source, false /*check_collation*/)) { ret = OB_INVALID_ARGUMENT; }
  else if (source.meta_.is_ext_sql_array() && !source.datum_.is_null()) {
    DatumParameter input, owned;
    input.datum_ = source.datum_; input.set_type_meta(source.meta_);
    if (OB_FAIL(DatumParameterArray::clone_parameter(input, owned, allocator))) {}
    else { target = owned.value_view(); }
    return ret;
  } else if (OB_FAIL(copy.datum_.deep_copy(source.datum_, allocator))) {}
  else { copy.meta_ = source.meta_; target = copy; }
  return ret;
}
} }
