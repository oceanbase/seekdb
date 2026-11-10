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

#ifndef _OB_ENGINE_EXPR_EXPR_UTIL_H_
#define _OB_ENGINE_EXPR_EXPR_UTIL_H_

#include "common/number/ob_number_v2.h"
#include "query/engine/expr/ob_expr_operator.h"
#include <type_traits>

namespace oceanbase
{
namespace share { namespace schema { class ObSchemaGetterGuard; } }
namespace pl { class ObPLINS; class ObPLDataType; }
namespace common
{
class ObIVector;
class ObIArrayType;
}
namespace sql
{

//
// temporary allocator for number operation. e.g.:
//  ObNumStackAllocator num_alloc;
//  number::ObNumber num;
//  num.from(int_val, num_alloc);
//  expr_datum.set_number(num); // set_datum will deep copy num value.
//
template <int64_t NUM_CNT = 1>
class ObNumStackAllocator : public common::ObDataBuffer
{
public:
  ObNumStackAllocator() : ObDataBuffer(local_buf_, sizeof(local_buf_))
  {
  }

private:
  char local_buf_[NUM_CNT * common::number::ObNumber::MAX_BYTE_LEN];
};
typedef ObNumStackAllocator<1> ObNumStackOnceAlloc;

#define array_elements(A) ((uint) (sizeof(A)/sizeof(A[0])))

template <typename T>
class ObTextStringVectorResult;
using ObTextStringDatumResult = ObTextStringVectorResult<common::ObIVector>;

class ObExprUtil
{
public:
  // The bound evaluator has already selected a coefficient width from its
  // prepared SQL type. Dynamic coefficient consumers use CopyPlan::get_decimal.
  template <typename Coefficient>
  static int get_decimal_int(const common::Datum &value, Coefficient &out)
  {
    if constexpr (std::is_same<Coefficient, int32_t>::value) {
      out = value.get_decimal_int32(); return common::OB_SUCCESS;
    } else if constexpr (std::is_same<Coefficient, int64_t>::value) {
      out = value.get_decimal_int64(); return common::OB_SUCCESS;
    } else if constexpr (std::is_same<Coefficient, uint64_t>::value) {
      out = value.get_uint(); return common::OB_SUCCESS;
    } else if constexpr (std::is_same<Coefficient, common::int128_t>::value) {
      return value.get_decimal_int128(out);
    } else if constexpr (std::is_same<Coefficient, common::int256_t>::value) {
      return value.get_decimal_int256(out);
    } else {
      static_assert(std::is_same<Coefficient, common::int512_t>::value,
          "A Decimal evaluator requires a prepared coefficient width");
      return value.get_decimal_int512(out);
    }
  }

  // Prepare once from the actual declaration/schema owner. No value loop may
  // reconstruct a type from its payload or a four-byte metadata fragment.
  // actual_meta must carry the definition's collation, never a SubSchema ID.
  static int resolve_enum_sql_type(const common::SqlTypeMeta &actual_meta,
                                   const common::ObAccuracy &accuracy,
                                   const common::ObIArray<common::ObString> &labels,
                                   common::ObIAllocator &owner, const common::SqlType *&out);
  // A PL declaration source is borrowed only during preparation. Each actual
  // definition must already own its origin; no current-unit or value fallback.
  // Call while the actual routine declaration namespace is alive. The
  // resulting complete Type and its children belong to the explicit owner.
  static int resolve_pl_parameter_sql_type(const pl::ObPLDataType &declaration,
      const pl::ObPLINS &declarations, const ObExecContext &sql_metadata,
      common::ObIAllocator &owner, const common::SqlType *&out);
  static int resolve_raw_sql_type(const ObRawExprResType &resolved,
                                  const ObSQLSessionInfo *session,
                                  share::schema::ObSchemaGetterGuard *schema_guard,
                                  common::ObIAllocator &owner, const common::SqlType *&out,
                                  const pl::ObPLINS *pl_declarations = nullptr);
  static int resolve_sql_type(ObExecContext &ctx, const common::SqlTypeMeta &meta,
                              const common::ObAccuracy &accuracy, uint16_t subschema_id,
                              common::ObIAllocator &owner, const common::SqlType *&out,
                              const pl::ObPLINS *pl_declarations = nullptr);

  static int set_number(const ObExpr &expr, ObEvalCtx &ctx, common::Datum &out,
                        const common::number::ObNumber &value, int64_t index = -1);
  static int set_number(const ObExpr &expr, ObEvalCtx &ctx, common::Datum &out,
                        const common::number::ObCompactNumber &value, int64_t index = -1);
  static int export_legacy_value(const ObExpr &expr, const common::Datum &value,
                                 common::ObIAllocator &owner, common::ObDatum &output);

  static int set_array_element(const ObExpr &expr, ObEvalCtx &ctx,
                               const common::ObIArrayType &array, uint32_t element,
                               common::Datum &out, int64_t index = -1);

  static int set_otimestamp_tiny(const ObExpr &expr, ObEvalCtx &ctx, common::Datum &out,
                                const common::ObOTimestampData &value, int64_t index = -1);

  static int set_decimal_int(const ObExpr &expr, ObEvalCtx &ctx, common::Datum &out,
                             const common::ObDecimalInt *coefficient, int32_t bytes,
                             int64_t index = -1);

  static int get_trunc_int64(const common::DatumValue &value, common::ObExprCtx &expr_ctx, int64_t &out);
  static int get_round_int64(const common::DatumValue &value, common::ObExprCtx &expr_ctx, int64_t &out);


  // This function relies on `kmp_next` to do the calculation of next array
  static int kmp(const char *pattern,
                 const int64_t pattern_len,
                 const char *text,
                 const int64_t text_len,
                 const int64_t nth_appearance,
                 const int32_t *next, /* calculated, size same with pattern */
                 int64_t &result);
  static int kmp_next(const char *pattern, const int64_t pattern_len, int32_t *next);

  // This function relies on `kmp_next_reverse` to do the calculation of next array
  static int kmp_reverse(const char *pattern,
                         const int64_t pattern_len,
                         const char *text,
                         const int64_t text_len,
                         const int64_t nth_appearance,
                         const int32_t *next, /* calculated, size same with pattern */
                         int64_t &result);
  static int kmp_next_reverse(const char *pattern,
                              const int64_t pattern_len,
                              int32_t *next);

  static int get_mb_str_info(const common::ObString &str,
                             common::ObCollationType cs_type,
                             common::ObIArray<size_t> &byte_num,
                             common::ObIArray<size_t> &byte_offset);
  // Round double to the specified position after or before the decimal point
  static double round_double(double val, int64_t dec);
  static uint64_t round_uint64(uint64_t val, int64_t dec);
  // Truncate double to specified position after or before the decimal point
  static double trunc_double(double val, int64_t dec);
  template <typename T>
  static T trunc_integer(T val, int64_t dec);

  // truncate the decimal part, truncate to INT64_MAX/INT64_MIN too if out of range.
  static int trunc_num2int64(const common::number::ObNumber &nmb, int64_t &v);

  static int trunc_decint2int64(const ObDecimalInt *decint, const int32_t int_bytes, const ObScale scale, int64_t &v);
  // get number out of Datum and truncate to int64
  static int trunc_num2int64(const common::Datum &datum, int64_t &v)
  {
    return trunc_num2int64(common::number::ObNumber(datum.get_number()), v);
  }

  // round the decimal part, round to INT64_MAX/INT64_MIN too if out of range.
  static int round_num2int64(const common::number::ObNumber &nmb, int64_t &v);

  // get number out of Datum and round to int64
  static int round_num2int64(const common::Datum &datum, int64_t &v)
  {
    return round_num2int64(common::number::ObNumber(datum.get_number()), v);
  }
  // Get integer value from integer parameter.
  //
  // Keep %int_val unchanged if datum is NULL or datum->is_null().
  static int get_int_param_val(const common::Datum *datum, const ObExpr *input_expr, int64_t &int_val);

  // Set the ASCII string to expression result.
  // e.g.:
  //   dump() needs database charset output, but we can only generate ASCII string in code,
  //   this function is used to convert the result characterset.
  static int set_expr_asscii_result(const ObExpr &expr,
                                      ObEvalCtx &ctx,
                                      ObTextStringDatumResult &out_res,
                                      const ObString &ascii, const int64_t datum_idx,
                                      const bool is_ascii,
                                      const common::ObCollationType src_coll_type = CS_TYPE_UTF8MB4_BIN);
  static int set_expr_ascii_result(const ObExpr &expr,
                                ObEvalCtx &ctx,
                                common::Datum &expr_datum,
                                const common::ObString &ascii_str,
                                const int64_t datum_idx,
                                const bool is_ascii = true,
                                const common::ObCollationType src_coll_type = CS_TYPE_UTF8MB4_BIN);
  static int set_expr_ascii_result(const ObExpr &expr,
                                ObEvalCtx &ctx,
                                common::Datum &expr_datum,
                                const common::ObString &ascii_str,
                                const bool is_ascii = true,
                                const common::ObCollationType src_coll_type = CS_TYPE_UTF8MB4_BIN);
  static int convert_string_collation(const common::ObString &in_str,
                                      const common::ObCollationType &in_collation,
                                      common::ObString &out_str,
                                      const common::ObCollationType &out_collation,
                                      common::ObIAllocator &alloc);
  static int convert_string_collation_for_regexp(const common::ObString &in_str,
                                                 const common::ObCollationType &in_collation,
                                                 common::ObString &out_str,
                                                 const common::ObCollationType &out_collation,
                                                 common::ObIAllocator &alloc);
  static int need_convert_string_collation(const common::ObCollationType &in_collation,
                                           const common::ObCollationType &out_collation,
                                           bool &need_convert);
  // deep copy src to out.
  // it is ok if src and out is same. like this deep_copy_str(str1, str1, alloc)
  static int deep_copy_str(const common::ObString &src, common::ObString &out,
                           common::ObIAllocator &alloc);

  static int eval_stack_overflow_check(const ObExpr &rt_expr,
                                       ObEvalCtx &ctx,
                                       Datum &expr_datum);

  static int eval_batch_stack_overflow_check(const ObExpr &expr,
                                             ObEvalCtx &ctx,
                                             const ObBitVector &skip,
                                             const int64_t batch_size);

  static int get_real_expr_without_cast(const ObExpr *expr, const ObExpr *&out_expr);

private:
  static int get_int64_from_num(common::number::ObNumber &nmb,
                                common::ObExprCtx &expr_ctx,
                                const bool is_trunc, //true: trunc; false: round
                                int64_t &out);
  static int get_int64_from_value(const common::DatumValue &value,
                                  common::ObExprCtx &expr_ctx,
                                  bool is_trunc,
                                  int64_t &out);
  DISALLOW_COPY_AND_ASSIGN(ObExprUtil);
};

// make sure T is int/uint type
template <typename T>
T ObExprUtil::trunc_integer(T val, int64_t dec)
{
  volatile T res = 0;
  if (dec >= 0) {
    res = val;
  } else {
    const int64_t max_integer_desc = 19;
    if (std::abs(dec) > max_integer_desc) {
      res = 0;
    } else {
      const T pow_val = std::pow(10, static_cast<int64_t>(std::abs(dec)));
      res = (val / pow_val) * pow_val;
    }
  }
  return res;
}
// Define the calculation function for trigonometric functions
// eg: sin/cos/tan sinh/cosh/tanh asin/acos/atan
// atan2 calculation function implemented separately
#define DEF_CALC_TRIGONOMETRIC_EXPR(tritype, INVALID_DOUBLE_ARG_CHECK, INVALID_DOUBLE_ARG_ERRNO) \
int calc_##tritype##_expr(const ObExpr &expr, ObEvalCtx &ctx,                 \
                               Datum &res_datum)                        \
{                                                                             \
  int ret = OB_SUCCESS;                                                       \
  Datum *radian = NULL;                                                     \
  if (OB_FAIL(expr.args_[0]->eval(ctx, radian))) {                            \
    LOG_WARN("eval radian arg failed", K(ret), K(expr));                      \
  } else if (radian->is_null()) {                                             \
    /* radian is already cast to number type, no extra null check is needed */ \
    res_datum.set_null();                                                     \
  } else if (ObNumberType == expr.args_[0]->datum_meta_.type_) {              \
    number::ObNumber res_nmb;                                                 \
    number::ObNumber radian_nmb(radian->get_number());                        \
    ObEvalCtx::TempAllocGuard alloc_guard(ctx);                                          \
    if (OB_FAIL(radian_nmb.tritype(res_nmb, alloc_guard.get_allocator()))) {    \
      LOG_WARN("calc expr failed", K(ret), K(radian_nmb), K(expr));           \
    } else {                                                                  \
      ret = ObExprUtil::set_number(expr, ctx, res_datum, res_nmb);                                          \
    }                                                                         \
  } else if (ObDoubleType == expr.args_[0]->datum_meta_.type_) {              \
    const double arg = radian->get_double();                                  \
    if (INVALID_DOUBLE_ARG_CHECK) {                                           \
      res_datum.set_null();                                                   \
    } else {                                                                  \
      res_datum.set_double(tritype(arg));                                     \
    }                                                                         \
  } else {                                                                    \
    ret = OB_ERR_UNEXPECTED;                                                  \
  }                                                                           \
  return ret;                                                                 \
}

class ObSolidifiedVarsContext
{
public:
  ObSolidifiedVarsContext() :
    local_session_var_(NULL),
    alloc_(NULL),
    local_tz_wrap_(NULL)
  {
  }
  ObSolidifiedVarsContext(ObLocalSessionVar *local_var, common::ObIAllocator *alloc) :
    local_session_var_(local_var),
    alloc_(alloc),
    local_tz_wrap_(NULL)
  {
  }
  virtual ~ObSolidifiedVarsContext()
  {
    if (NULL != local_tz_wrap_ && NULL != alloc_) {
      local_tz_wrap_->~ObTimeZoneInfoWrap();
      alloc_->free(local_tz_wrap_);
      local_tz_wrap_ = NULL;
    }
  }
  int get_local_tz_info(const sql::ObBasicSessionInfo *session, const common::ObTimeZoneInfo *&tz_info);
  ObLocalSessionVar *get_local_vars() const { return local_session_var_; }
  DECLARE_TO_STRING;
private:
  ObLocalSessionVar *local_session_var_;
  common::ObIAllocator *alloc_;
  //cached vars
  ObTimeZoneInfoWrap *local_tz_wrap_;
};

//Get the merged values of solidified vars and current session vars
//If a var is solidified, return the solidified value. Otherwise return currrent session value.
class ObSolidifiedVarsGetter
{
public:
  ObSolidifiedVarsGetter(const ObExpr &expr, const ObEvalCtx &ctx, const ObBasicSessionInfo *session);
  int get_dtc_params(ObDataTypeCastParams &dtc_params);
  int get_time_zone_info(const common::ObTimeZoneInfo *&tz_info);
  int get_sql_mode(ObSQLMode &sql_mode);
  int get_max_allowed_packet(int64_t &max_size);
  //get the specified solidified var
  int get_local_var(share::ObSysVarClassType var_type, ObSessionSysVar *&sys_var);
private:
  const ObSolidifiedVarsContext *local_session_var_;
  const ObBasicSessionInfo *session_;
};

}
}
#endif  /* _OB_ENGINE_EXPR_EXPR_UTIL_H_ */
