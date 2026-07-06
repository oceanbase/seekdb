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
#define USING_LOG_PREFIX SHARE
#include "share/object/ob_decint_scale_util.h"
#include "lib/utility/ob_macro_utils.h"
#include "lib/oblog/ob_log.h"

namespace oceanbase
{
namespace common
{
namespace decint_scale
{
WarnFn g_warn_from_exec_ctx = nullptr;

static inline bool decimal_int_truncated_check(const ObDecimalInt *decint, const int32_t int_bytes,
                                       const unsigned scale)
{
#define TRUNC_CHECK(int_type) \
  if (wide::ObDecimalIntConstValue::get_int_bytes_by_precision(scale) > int_bytes) {  \
    bret = (*reinterpret_cast<const int_type *>(decint)) != 0;              \
  } else {                                                                  \
    const int_type sf = get_scale_factor<int_type>(scale);                  \
    bret = (((*reinterpret_cast<const int_type *>(decint)) % sf) != 0);     \
  }

  int ret = OB_SUCCESS;
  bool bret = false;
  DISPATCH_WIDTH_TASK(int_bytes, TRUNC_CHECK);
  return bret;
#undef TRUNC_CHECK
}

template <typename T>
static int scale_down_decimalint(const T &x, unsigned scale, ObDecimalIntBuilder &res,
                                 const ObCastMode cm, bool &has_extra_decimals)
{
  static const int64_t pows[5] = {10, 100, 10000, 100000000, 10000000000000000};
  int ret = OB_SUCCESS;
  T result = x;
  bool is_neg = (x < 0);
  if (is_neg) {
    result = -result;
  }
  T remain;
  while (scale != 0 && result != 0) {
    for (int i = ARRAYSIZEOF(pows) - 1; scale != 0 && result != 0 && i >= 0; i--) {
      if (scale & (1 << i)) {
        if (!has_extra_decimals) {
          remain = result % pows[i];
          has_extra_decimals = (remain > 0);
        }
        result = result / pows[i];
        scale -= (1<<i);
      }
    }
    if (scale != 0) {
      if (!has_extra_decimals) {
        remain = result % 10;
        has_extra_decimals = (remain > 0);
      }
      result = result / 10;
      scale -= 1;
    }
  }
  if (is_neg) {
    result = -result;
  }
  if (has_extra_decimals) {
    if ((cm & CM_CONST_TO_DECIMAL_INT_UP) != 0) {
      if (!is_neg) { result = result + 1; }
    } else if ((cm & CM_CONST_TO_DECIMAL_INT_DOWN) != 0) {
      if (is_neg) { result = result - 1; }
    }
  }
  res.from(result);
  return ret;
}

static int align_decint_precision_unsafe(const ObDecimalInt *decint, const int32_t int_bytes,
                                               const int32_t expected_int_bytes,
                                               ObDecimalIntBuilder &res)
{
  int ret = OB_SUCCESS;
  res.from(decint, int_bytes);
  if (int_bytes > expected_int_bytes) {
    res.truncate(expected_int_bytes);
  } else if (int_bytes < expected_int_bytes) {
    res.extend(expected_int_bytes);
  } else {
    // do nothing
  }
  return ret;
}

int scale_const_decimalint_expr(const ObDecimalInt *decint, const int32_t int_bytes,
                                       const ObScale in_scale, const ObScale out_scale,
                                       const ObPrecision out_prec, const ObCastMode cast_mode,
                                       ObDecimalIntBuilder &res)
{
#define DO_SCALE(int_type)                                                                         \
  const int_type *v = reinterpret_cast<const int_type *>(decint);                                  \
  if (in_scale < out_scale) {                                                                      \
    ret = wide::scale_up_decimalint(*v, out_scale - in_scale, res);                                \
  } else if (OB_FAIL(scale_down_decimalint(*v, in_scale - out_scale, res,                          \
                                           cast_mode, has_extra_decimal))) {                       \
    LOG_WARN("scale down decimal int failed", K(ret));                                             \
  }

  int ret = OB_SUCCESS;
  bool has_extra_decimal = false;
  ObDecimalIntBuilder max_v, min_v;
  int32_t expected_int_bytes =
        wide::ObDecimalIntConstValue::get_int_bytes_by_precision(out_prec);
  min_v.from(wide::ObDecimalIntConstValue::get_min_lower(out_prec), expected_int_bytes);
  max_v.from(wide::ObDecimalIntConstValue::get_max_upper(out_prec), expected_int_bytes);
 if (in_scale != out_scale) {
    DISPATCH_WIDTH_TASK(int_bytes, DO_SCALE);
  } else {
    res.from(decint, int_bytes);
  }
  if (OB_FAIL(ret)) {
  } else if ((cast_mode & CM_CONST_TO_DECIMAL_INT_EQ) != 0 && has_extra_decimal) {
    res.from(max_v);
  } else {
    int cmp_max = 0, cmp_min = 0;
    if (OB_FAIL(wide::compare(res, min_v, cmp_min))) {
      LOG_WARN("compare failed", K(ret));
    } else if (OB_FAIL(wide::compare(res, max_v, cmp_max))) {
      LOG_WARN("compare failed", K(ret));
    } else if (cmp_max < 0 && cmp_min > 0) { // max(P, S) >= res >= min(P, S)
      if (expected_int_bytes > res.get_int_bytes()) {
        res.extend(expected_int_bytes);
      } else if (expected_int_bytes < res.get_int_bytes()) {
        res.truncate(expected_int_bytes);
      }
    } else if (cmp_max >= 0) {
      res.from(max_v);
    } else if (cmp_min <= 0) {
      res.from(min_v);
    }
  }

  return ret;
#undef DO_SCALE
}

int scale_decimalint(const ObDecimalInt *decint, const int32_t int_bytes,
                                         const ObScale in_scale, const ObScale out_scale,
                                         const ObPrecision out_prec, const ObCastMode cast_mode,
                                         ObDecimalIntBuilder &val,
                     const void *warn_payload, WarnFn warn_fn)
{
  int ret = OB_SUCCESS;
  ObDecimalIntBuilder max_v, min_v;
  ObDecimalIntBuilder scaled_val;
  int cmp_min = 0, cmp_max = 0;
  if (OB_ISNULL(decint)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid null decimal int", K(ret), K(decint));
  } else if (CM_IS_CONST_TO_DECIMAL_INT(cast_mode)) {
    ret = scale_const_decimalint_expr(decint, int_bytes, in_scale, out_scale, out_prec, cast_mode, val);
  } else if (CM_IS_COLUMN_CONVERT(cast_mode) || CM_IS_EXPLICIT_CAST(cast_mode)) {
    int32_t check_int_bytes = wide::ObDecimalIntConstValue::get_int_bytes_by_precision(out_prec);
    max_v.from(wide::ObDecimalIntConstValue::get_max_upper(out_prec), check_int_bytes);
    min_v.from(wide::ObDecimalIntConstValue::get_min_lower(out_prec), check_int_bytes);
    if (OB_FAIL(
          wide::common_scale_decimalint(decint, int_bytes, in_scale, out_scale, scaled_val))) {
      LOG_WARN("scale decimal int failed", K(ret));
    } else if (OB_FAIL(wide::compare(scaled_val, min_v, cmp_min))) {
      LOG_WARN("compare failed", K(ret));
    } else if (OB_FAIL(wide::compare(scaled_val, max_v, cmp_max))) {
      LOG_WARN("compare failed", K(ret));
    } else if (cmp_min > 0 && cmp_max < 0) {
      ret = align_decint_precision_unsafe(scaled_val.get_decimal_int(), scaled_val.get_int_bytes(),
                                          check_int_bytes, val);
    } else if (cmp_min <= 0) {
      val.from(min_v);
    } else if (cmp_max >= 0) {
      val.from(max_v);
    } else {
      // do nothing
    }
    if (OB_SUCC(ret)) {
      if (in_scale > out_scale &&
            decimal_int_truncated_check(decint, int_bytes, in_scale - out_scale)) {
        if (nullptr != warn_fn) {
          warn_fn(warn_payload, OB_ERR_DATA_TRUNCATED, ObString(""), ObString(""), cast_mode);
        }
      }
    }
  } else {
    if (OB_FAIL(
          wide::common_scale_decimalint(decint, int_bytes, in_scale, out_scale, scaled_val))) {
      LOG_WARN("scale decimal int failed", K(ret));
    }
    int32_t expected_int_bytes = wide::ObDecimalIntConstValue::get_int_bytes_by_precision(out_prec);
    if (OB_UNLIKELY(out_prec == PRECISION_UNKNOWN_YET)) {
      // tempory value may have unknown precision(-1), just set expected int bytes to input int_bytes
      expected_int_bytes = scaled_val.get_int_bytes();
      LOG_WARN("invalid out precision", K(out_prec), K(lbt()));
    }
    if (OB_FAIL(ret)) { // do nothing
    } else if (OB_FAIL(align_decint_precision_unsafe(scaled_val.get_decimal_int(),
                                                     scaled_val.get_int_bytes(), expected_int_bytes,
                                                     val))) {
      LOG_WARN("align decimal int precision failed", K(ret));
    }
  }
  return ret;
}

int check_decimalint_accuracy(const ObCastMode cast_mode,
                              const ObDecimalInt *res_decint, const int32_t int_bytes,
                              const ObPrecision precision, const ObScale scale,
                              ObDecimalIntBuilder &res_val, int &warning)
{
  int ret = OB_SUCCESS;
  bool is_finish = false;
  int &cast_ret = CM_IS_ERROR_ON_FAIL(cast_mode) ? ret : warning;
  if (int_bytes == 0) { // default zero value
    int32_t tmp_zero = 0;
    res_val.from(tmp_zero);
    int32_t out_int_bytes = wide::ObDecimalIntConstValue::get_int_bytes_by_precision(precision);
    if (out_int_bytes > res_val.get_int_bytes()) {
      res_val.extend(out_int_bytes);
    }
    is_finish = true;
  } else if (OB_ISNULL(res_decint)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid null decimal int", K(ret), K(res_decint));
  } else if (OB_UNLIKELY(precision < OB_MIN_DECIMAL_PRECISION
                         || precision > number::ObNumber::MAX_PRECISION)
             || OB_UNLIKELY(scale < 0 || scale > number::ObNumber::MAX_SCALE)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid precision and scale", K(ret), K(precision), K(scale));
  } else if (OB_UNLIKELY(precision < scale)) {
    ret = OB_ERR_M_BIGGER_THAN_D;
    LOG_WARN("invalid precision and scale", K(ret), K(precision), K(scale));
  }
  if (OB_SUCC(ret) && !is_finish) {
    const ObDecimalInt *min_decint = nullptr, *max_decint = nullptr;
    int32_t int_bytes2 = 0;
    min_decint = wide::ObDecimalIntConstValue::get_min_value(precision);
    max_decint = wide::ObDecimalIntConstValue::get_max_value(precision);
    int_bytes2 = wide::ObDecimalIntConstValue::get_int_bytes_by_precision(precision);

    decint_cmp_fp cmp_fp =
      wide::ObDecimalIntCmpSet::get_decint_decint_cmp_func(int_bytes, int_bytes2);
    if (OB_ISNULL(cmp_fp) || OB_ISNULL(res_decint) || OB_ISNULL(min_decint)
        || OB_ISNULL(max_decint)) {
      ret = OB_ERR_UNDEFINED;
      LOG_WARN("unexpected null cmp function", K(ret), K(int_bytes), K(int_bytes2), K(res_decint),
               K(min_decint), K(max_decint));
    } else {
      int cmp_min = cmp_fp(res_decint, min_decint);
      int cmp_max = cmp_fp(res_decint, max_decint);
      if (cmp_min >= 0 && cmp_max <= 0) { // min(p, s) <= res <= max(p, s)
        res_val.from(res_decint, int_bytes);
      } else if (cmp_min < 0) { // res < min(p, s)
        cast_ret = OB_DATA_OUT_OF_RANGE;
        res_val.from(min_decint, int_bytes2);
      } else if (cmp_max > 0) { // res > max(p, s)
        cast_ret = OB_DATA_OUT_OF_RANGE;
        res_val.from(max_decint, int_bytes2);
      }
    }
  }
  return ret;
}

}  // namespace decint_scale
}  // namespace common
}  // namespace oceanbase
