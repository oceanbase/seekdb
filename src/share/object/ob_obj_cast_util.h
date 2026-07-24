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

#ifndef OCEANBASE_COMMON_OB_OBJ_CAST_UTIL_
#define OCEANBASE_COMMON_OB_OBJ_CAST_UTIL_

#include <cmath>
#include "share/ob_errno.h"
#include "common/object/ob_object.h"
#include "common/object/ob_obj_type.h"

namespace oceanbase
{
namespace common
{

// moved down from sql ob_expr_json_func_helper.h:binary-json zero-value constant,depends only on ObLobCommon(oblib)
struct ObJsonZeroVal
{
  static const int32_t OB_JSON_ZERO_VAL_LENGTH = sizeof(ObLobCommon) + 2;
  ObJsonZeroVal() : header_(), json_bin_() {
    json_bin_[0] = '\0';
    json_bin_[1] = '\0';
  }
  ObLobCommon header_;
  char json_bin_[4];
};

// Floating-point values at and beyond these exact powers of two cannot be
// converted to int64_t/uint64_t before checking the range. Such conversions
// have undefined behavior in C++ and produce different results on x86_64 and
// arm64.
static constexpr double INT64_MIN_AS_DOUBLE = -9223372036854775808.0;
static constexpr double INT64_UPPER_BOUND_AS_DOUBLE = 9223372036854775808.0;
static constexpr double UINT64_UPPER_BOUND_AS_DOUBLE = 18446744073709551616.0;

// MySQL Field_bit/Field_enum/Field_set::store(double) first clamps the value to
// the signed 64-bit range and then applies the destination type's own range
// and warning rules.
template <typename FloatingType>
OB_INLINE int64_t truncate_floating_to_int64_clamped(const FloatingType in_val)
{
  const double value = static_cast<double>(in_val);
  int64_t out_val = 0;
  if (std::isnan(value)) {
    out_val = 0;
  } else if (value < INT64_MIN_AS_DOUBLE) {
    out_val = INT64_MIN;
  } else if (value >= INT64_UPPER_BOUND_AS_DOUBLE) {
    out_val = INT64_MAX;
  } else {
    out_val = static_cast<int64_t>(value);
  }
  return out_val;
}

// Convert FLOAT/DOUBLE to an unsigned integer without executing an
// out-of-range floating-to-integer cast. FLOAT has a MySQL-compatible
// CAST-AS-UNSIGNED special case at 2^63, while DOUBLE keeps the existing
// INT64_MAX result for that non-column conversion.
OB_INLINE int round_floating_to_uint64(const double in_val,
                                       const bool is_float_source,
                                       const bool is_column_convert,
                                       uint64_t &out_val)
{
  int ret = OB_SUCCESS;
  const double rounded = std::rint(in_val);
  out_val = 0;
  if (std::isnan(rounded)) {
    ret = OB_DATA_OUT_OF_RANGE;
  } else if (rounded <= INT64_MIN_AS_DOUBLE) {
    out_val = static_cast<uint64_t>(INT64_MIN);
    ret = OB_DATA_OUT_OF_RANGE;
  } else if (rounded >= UINT64_UPPER_BOUND_AS_DOUBLE) {
    out_val = is_float_source
        ? static_cast<uint64_t>(INT64_MIN)
        : static_cast<uint64_t>(INT64_MAX);
    ret = OB_DATA_OUT_OF_RANGE;
  } else if (is_column_convert) {
    if (rounded < 0) {
      out_val = 0;
      ret = OB_DATA_OUT_OF_RANGE;
    } else {
      out_val = static_cast<uint64_t>(rounded);
    }
  } else if (rounded < 0) {
    out_val = static_cast<uint64_t>(static_cast<int64_t>(rounded));
    ret = OB_DATA_OUT_OF_RANGE;
  } else if (rounded >= INT64_UPPER_BOUND_AS_DOUBLE) {
    out_val = is_float_source
        ? static_cast<uint64_t>(INT64_MIN)
        : static_cast<uint64_t>(INT64_MAX);
  } else {
    out_val = static_cast<uint64_t>(static_cast<int64_t>(rounded));
  }
  return ret;
}


// check with given lower and upper limit.
template <typename InType, typename OutType>
OB_INLINE int numeric_range_check(const InType in_val,
                                  const OutType min_out_val,
                                  const OutType max_out_val,
                                  OutType &out_val)
{
  int ret = OB_SUCCESS;
  // Casting value from InType to OutType to prevent number overflow. 
  OutType cast_in_val = static_cast<OutType>(in_val);
  if (cast_in_val < min_out_val) {
    ret = OB_DATA_OUT_OF_RANGE;
    out_val = min_out_val;
  } else if (cast_in_val > max_out_val) {
    ret = OB_DATA_OUT_OF_RANGE;
    out_val = max_out_val;
  }
  return ret;
}

// explicit for int_uint check, because we need use out_val to compare with max_out_val instead
// of in_val, since we can't cast UINT64_MAX to int64.
template <>
OB_INLINE int numeric_range_check<int64_t, uint64_t>(const int64_t in_val,
                                                     const uint64_t min_out_val,
                                                     const uint64_t max_out_val,
                                                     uint64_t &out_val)
{
  int ret = OB_SUCCESS;
  if (in_val < 0) {
    ret = OB_DATA_OUT_OF_RANGE;
    out_val = 0;
  } else if (out_val > max_out_val) {
    ret = OB_DATA_OUT_OF_RANGE;
    out_val = max_out_val;
  }
  UNUSED(min_out_val);
  return ret;
}

// explicit for float to handle infinity
template <>
OB_INLINE int numeric_range_check<double, float>(const double in_val,
                                                     const float min_out_val,
                                                     const float max_out_val,
                                                     float &out_val)
{
  int ret = OB_SUCCESS;
  if (isinf(in_val)) {
    out_val = static_cast<float>(in_val);
  } else {
    if (in_val < min_out_val) {
      ret = OB_DATA_OUT_OF_RANGE;
      out_val = min_out_val;
    } else if (in_val > max_out_val) {
      ret = OB_DATA_OUT_OF_RANGE;
      out_val = max_out_val;
    }
  }
  return ret;
}

// check if is negative only.
template <typename OutType>
OB_INLINE int numeric_negative_check(OutType &out_val)
{
  int ret = OB_SUCCESS;
  if (out_val < static_cast<OutType>(0)) {
    ret = OB_DATA_OUT_OF_RANGE;
    out_val = static_cast<OutType>(0);
  }
  return ret;
}

// explicit for number check.
template <>
OB_INLINE int numeric_negative_check<number::ObNumber>(number::ObNumber &out_val)
{
  int ret = OB_SUCCESS;
  if (out_val.is_negative()) {
    ret = OB_DATA_OUT_OF_RANGE;
    out_val.set_zero();
  }
  return ret;
}

// check upper limit only.
template <typename InType, typename OutType>
OB_INLINE int numeric_upper_check(const InType in_val,
                                  const OutType max_out_val,
                                  OutType &out_val)
{
  int ret = OB_SUCCESS;
  if (in_val > static_cast<InType>(max_out_val)) {
    ret = OB_DATA_OUT_OF_RANGE;
    out_val = max_out_val;
  }
  return ret;
}

template <typename InType>
OB_INLINE int int_range_check(const ObObjType out_type,
                              const InType in_val,
                              int64_t &out_val)
{
  return numeric_range_check(in_val, INT_MIN_VAL[out_type], INT_MAX_VAL[out_type], out_val);
}

template <typename InType>
OB_INLINE int int_upper_check(const ObObjType out_type,
                              InType in_val,
                              int64_t &out_val)
{
  return numeric_upper_check(in_val, INT_MAX_VAL[out_type], out_val);
}

OB_INLINE int uint_upper_check(const ObObjType out_type, uint64_t &out_val)
{
  return numeric_upper_check(out_val, UINT_MAX_VAL[out_type], out_val);
}

template <typename InType>
OB_INLINE int uint_range_check(const ObObjType out_type,
                               const InType in_val,
                               uint64_t &out_val)
{
  return numeric_range_check(in_val, static_cast<uint64_t>(0),
                             UINT_MAX_VAL[out_type], out_val);
}

template <typename InType, typename OutType>
OB_INLINE int real_range_check(const ObObjType out_type,
                               const InType in_val,
                               OutType &out_val)
{
  return numeric_range_check(in_val, static_cast<OutType>(REAL_MIN_VAL[out_type]),
                             static_cast<OutType>(REAL_MAX_VAL[out_type]), out_val);
}

template <typename Type>
int real_range_check(const ObAccuracy &accuracy, Type &value)
{
  int ret = OB_SUCCESS;
  const ObPrecision precision = accuracy.get_precision();
  const ObScale scale = accuracy.get_scale();
  if (OB_LIKELY(precision > 0) &&
      OB_LIKELY(scale >= 0) &&
      OB_LIKELY(precision >= scale)) {
    Type integer_part = static_cast<Type>(pow(10.0, static_cast<double>(precision - scale)));
    Type decimal_part = static_cast<Type>(pow(10.0, static_cast<double>(scale)));
    Type max_value = static_cast<Type>(integer_part - 1 / decimal_part);
    Type min_value = static_cast<Type>(-max_value);
    if (OB_FAIL(numeric_range_check(value, min_value, max_value, value))) {
    } else {
      value = static_cast<Type>(rint((value - 
                                      floor(static_cast<double>(value)))* decimal_part) / 
                                      decimal_part + floor(static_cast<double>(value)));
    }
  }
  return ret;
}

inline uint64_t hex_to_uint64(const ObString &str)
{
  int32_t N = str.length();
  const uint8_t *p = reinterpret_cast<const uint8_t*>(str.ptr());
  uint64_t value = 0;
  if (OB_LIKELY(NULL != p)) {
    for (int32_t i = 0; i < N; ++i, ++p) {
      // After testing, MySQL does not perform overflow checks
      value = value * 256 + *p;
    }
  }
  return value;
}

int check_convert_str_err(const char *str,
                          const char *endptr,
                          const int32_t len,
                          const int err,
                          const ObCollationType &in_cs_type);

// decimal(aka NumberType) cast to double/float precision increment. If it is an unsigned decimal,
// don’t need to increment precision, otherwise increment 1 to cover sign bit. If scale is
// equal to 0, don’t need to increment precision, otherwise increment 1 to cover dot bit.
inline int16_t decimal_to_double_precision_inc(const ObObjType type, const ObScale s)
{
  return ((type == ObUNumberType) ? 0 : 1) + ((s > 0) ? 1 : 0);
}

} // end namespace common
} // end namespace oceanbase

#endif // OCEANBASE_COMMON_OB_OBJ_CAST_UTIL_
