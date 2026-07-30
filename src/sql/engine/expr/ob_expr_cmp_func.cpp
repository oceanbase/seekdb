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

#include "ob_expr_cmp_func.ipp"

namespace oceanbase
{
namespace sql
{
using namespace common;


ObExpr::EvalBatchFunc EVAL_BATCH_NULL_EXTEND_CMP_FUNCS[CO_MAX];
ObExpr::EvalBatchFunc EVAL_BATCH_STR_CMP_FUNCS[CO_MAX];
ObExpr::EvalBatchFunc EVAL_BATCH_TEXT_CMP_FUNCS[CO_MAX];
ObExpr::EvalBatchFunc EVAL_BATCH_TEXT_STR_CMP_FUNCS[CO_MAX];
ObExpr::EvalBatchFunc EVAL_BATCH_STR_TEXT_CMP_FUNCS[CO_MAX];
ObExpr::EvalBatchFunc EVAL_BATCH_JSON_CMP_FUNCS[CO_MAX];
ObExpr::EvalBatchFunc EVAL_BATCH_GEO_CMP_FUNCS[CO_MAX];
ObExpr::EvalBatchFunc EVAL_BATCH_COLLECTION_CMP_FUNCS[CO_MAX];

ObExpr::EvalFunc EVAL_TYPE_CMP_FUNCS[ObMaxType][ObMaxType][CO_MAX];
ObExpr::EvalBatchFunc EVAL_BATCH_TYPE_CMP_FUNCS[ObMaxType][ObMaxType][CO_MAX];
ObDatumCmpFuncType DATUM_TYPE_CMP_FUNCS[ObMaxType][ObMaxType];

// TODO serialize
ObExpr::EvalFunc EVAL_TC_CMP_FUNCS[ObMaxTC][ObMaxTC][CO_MAX];
ObExpr::EvalBatchFunc EVAL_BATCH_TC_CMP_FUNCS[ObMaxTC][ObMaxTC][CO_MAX];
ObDatumCmpFuncType DATUM_TC_CMP_FUNCS[ObMaxTC][ObMaxTC];

ObExpr::EvalFunc EVAL_STR_CMP_FUNCS[CS_TYPE_MAX][CO_MAX][2];
ObDatumCmpFuncType DATUM_STR_CMP_FUNCS[CS_TYPE_MAX][2];
ObExpr::EvalFunc EVAL_TEXT_CMP_FUNCS[CS_TYPE_MAX][CO_MAX][2];
ObDatumCmpFuncType DATUM_TEXT_CMP_FUNCS[CS_TYPE_MAX][2];
ObExpr::EvalFunc EVAL_TEXT_STR_CMP_FUNCS[CS_TYPE_MAX][CO_MAX][2];
ObDatumCmpFuncType DATUM_TEXT_STR_CMP_FUNCS[CS_TYPE_MAX][2];
ObExpr::EvalFunc EVAL_STR_TEXT_CMP_FUNCS[CS_TYPE_MAX][CO_MAX][2];
ObDatumCmpFuncType DATUM_STR_TEXT_CMP_FUNCS[CS_TYPE_MAX][2];
ObExpr::EvalFunc EVAL_JSON_CMP_FUNCS[CO_MAX][2];
ObDatumCmpFuncType DATUM_JSON_CMP_FUNCS[2];
ObExpr::EvalFunc EVAL_GEO_CMP_FUNCS[CO_MAX][2];
ObDatumCmpFuncType DATUM_GEO_CMP_FUNCS[2];
ObExpr::EvalFunc EVAL_COLLECTION_CMP_FUNCS[CO_MAX][2];
ObDatumCmpFuncType DATUM_COLLECTION_CMP_FUNCS[2];

ObExpr::EvalFunc EVAL_FIXED_DOUBLE_CMP_FUNCS[OB_NOT_FIXED_SCALE][CO_MAX];
ObExpr::EvalBatchFunc EVAL_BATCH_FIXED_DOUBLE_CMP_FUNCS[OB_NOT_FIXED_SCALE][CO_MAX];
ObDatumCmpFuncType DATUM_FIXED_DOUBLE_CMP_FUNCS[OB_NOT_FIXED_SCALE];

ObExpr::EvalFunc EVAL_DECINT_CMP_FUNCS[DECIMAL_INT_MAX][DECIMAL_INT_MAX][CO_MAX];
ObExpr::EvalBatchFunc EVAL_BATCH_DECINT_CMP_FUNCS[DECIMAL_INT_MAX][DECIMAL_INT_MAX][CO_MAX];

ObDatumCmpFuncType DATUM_DECINT_CMP_FUNCS[DECIMAL_INT_MAX][DECIMAL_INT_MAX];

ObExpr::EvalFunc EVAL_VEC_CMP_FUNCS[CO_MAX];
ObExpr::EvalBatchFunc EVAL_BATCH_VEC_CMP_FUNCS[CO_MAX];

namespace
{

// Keep the exact constants used by ObFixedDoubleCmp<SCALE>::P.  Looking the
// tolerance up once per evaluator avoids making SCALE a template axis while
// preserving fixed-double comparison semantics bit for bit.
constexpr double FIXED_DOUBLE_CMP_TOLERANCE[] = {
  5 / 1e001, 5 / 1e002, 5 / 1e003, 5 / 1e004,
  5 / 1e005, 5 / 1e006, 5 / 1e007, 5 / 1e008,
  5 / 1e009, 5 / 1e010, 5 / 1e011, 5 / 1e012,
  5 / 1e013, 5 / 1e014, 5 / 1e015, 5 / 1e016,
  5 / 1e017, 5 / 1e018, 5 / 1e019, 5 / 1e020,
  5 / 1e021, 5 / 1e022, 5 / 1e023, 5 / 1e024,
  5 / 1e025, 5 / 1e026, 5 / 1e027, 5 / 1e028,
  5 / 1e029, 5 / 1e030, 5 / 1e031,
};
static_assert(ARRAYSIZEOF(FIXED_DOUBLE_CMP_TOLERANCE) == OB_NOT_FIXED_SCALE,
              "fixed-double tolerance table must cover every supported scale");

int get_fixed_double_tolerance(const ObExpr &expr, double &tolerance)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(2 != expr.arg_cnt_)
      || OB_ISNULL(expr.args_)
      || OB_ISNULL(expr.args_[0])
      || OB_ISNULL(expr.args_[1])) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("invalid fixed-double comparison expression", K(ret), K(expr.arg_cnt_));
  } else {
    const ObDatumMeta &left_meta = expr.args_[0]->datum_meta_;
    const ObDatumMeta &right_meta = expr.args_[1]->datum_meta_;
    const ObScale left_scale = left_meta.scale_;
    const ObScale right_scale = right_meta.scale_;
    if (OB_UNLIKELY(!ob_is_double_type(left_meta.type_)
                    || !ob_is_double_type(right_meta.type_)
                    || left_scale <= SCALE_UNKNOWN_YET
                    || left_scale >= OB_NOT_FIXED_SCALE
                    || right_scale <= SCALE_UNKNOWN_YET
                    || right_scale >= OB_NOT_FIXED_SCALE)) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("invalid fixed-double comparison metadata",
               K(ret), K(left_meta), K(right_meta));
    } else {
      const ObScale scale = MAX(left_scale, right_scale);
      tolerance = FIXED_DOUBLE_CMP_TOLERANCE[scale];
    }
  }
  return ret;
}

struct RuntimeFixedDoubleCmp
{
  int operator()(ObDatum &res,
                 const ObDatum &l_datum,
                 const ObDatum &r_datum,
                 const double &tolerance,
                 const ObCmpOp &cmp_op) const
  {
    int cmp_ret = 0;
    const double l = l_datum.get_double();
    const double r = r_datum.get_double();
    if (isnan(l) || isnan(r)) {
      if (isnan(l) && isnan(r)) {
        cmp_ret = 0;
      } else if (isnan(l)) {
        cmp_ret = 1;
      } else {
        cmp_ret = -1;
      }
    } else if (l == r || fabs(l - r) < tolerance) {
      cmp_ret = 0;
    } else {
      cmp_ret = l < r ? -1 : 1;
    }
    res.set_int(get_cmp_ret(cmp_op, cmp_ret));
    return OB_SUCCESS;
  }
};

int get_runtime_decint_cmp_func(const ObExpr &expr, decint_cmp_fp &cmp_func)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(2 != expr.arg_cnt_)
      || OB_ISNULL(expr.args_)
      || OB_ISNULL(expr.args_[0])
      || OB_ISNULL(expr.args_[1])) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("invalid decimal-int comparison expression", K(ret), K(expr.arg_cnt_));
  } else {
    const ObDatumMeta &left_meta = expr.args_[0]->datum_meta_;
    const ObDatumMeta &right_meta = expr.args_[1]->datum_meta_;
    const ObDecimalIntWideType left_width = get_decimalint_type(left_meta.precision_);
    const ObDecimalIntWideType right_width = get_decimalint_type(right_meta.precision_);
    if (OB_UNLIKELY(!ob_is_decimal_int(left_meta.type_)
                    || !ob_is_decimal_int(right_meta.type_)
                    || left_width < DECIMAL_INT_32
                    || left_width >= DECIMAL_INT_MAX
                    || right_width < DECIMAL_INT_32
                    || right_width >= DECIMAL_INT_MAX)) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("invalid decimal-int comparison metadata",
               K(ret), K(left_meta), K(right_meta));
    } else {
      const int32_t left_bytes = 2 << (static_cast<int32_t>(left_width) + 1);
      const int32_t right_bytes = 2 << (static_cast<int32_t>(right_width) + 1);
      cmp_func = wide::ObDecimalIntCmpSet::get_decint_decint_cmp_func(
          left_bytes, right_bytes);
      if (OB_ISNULL(cmp_func)) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("decimal-int comparison function is null",
                 K(ret), K(left_width), K(right_width));
      }
    }
  }
  return ret;
}

struct RuntimeDecintCmp
{
  int operator()(ObDatum &res,
                 const ObDatum &l_datum,
                 const ObDatum &r_datum,
                 const decint_cmp_fp &cmp_func,
                 const ObCmpOp &cmp_op) const
  {
    OB_ASSERT(nullptr != cmp_func);
    const int cmp_ret = cmp_func(l_datum.get_decimal_int(), r_datum.get_decimal_int());
    res.set_int(get_cmp_ret(cmp_op, cmp_ret));
    return OB_SUCCESS;
  }
};

} // namespace

int ObFixedDoubleRelationFunc::eval(const ObExpr &expr,
                                    ObEvalCtx &ctx,
                                    ObDatum &expr_datum)
{
  int ret = OB_SUCCESS;
  double tolerance = 0;
  if (OB_FAIL(get_fixed_double_tolerance(expr, tolerance))) {
    LOG_WARN("get fixed-double comparison tolerance failed", K(ret));
  } else {
    ObCmpOp cmp_op = ObExprCmpFuncsHelper::get_cmp_op(expr.type_);
    ret = def_relational_eval_func<RuntimeFixedDoubleCmp>(
        expr, ctx, expr_datum, tolerance, cmp_op);
  }
  return ret;
}

int ObFixedDoubleRelationFunc::eval_batch(BATCH_EVAL_FUNC_ARG_DECL)
{
  int ret = OB_SUCCESS;
  double tolerance = 0;
  if (OB_FAIL(get_fixed_double_tolerance(expr, tolerance))) {
    LOG_WARN("get fixed-double comparison tolerance failed", K(ret));
  } else {
    ObCmpOp cmp_op = ObExprCmpFuncsHelper::get_cmp_op(expr.type_);
    ret = def_relational_eval_batch_func<RuntimeFixedDoubleCmp>(
        BATCH_EVAL_FUNC_ARG_LIST, tolerance, cmp_op);
  }
  return ret;
}

int ObDecintRelationFunc::eval(const ObExpr &expr,
                               ObEvalCtx &ctx,
                               ObDatum &expr_datum)
{
  int ret = OB_SUCCESS;
  decint_cmp_fp cmp_func = nullptr;
  if (OB_FAIL(get_runtime_decint_cmp_func(expr, cmp_func))) {
    LOG_WARN("get decimal-int comparison function failed", K(ret));
  } else {
    ObCmpOp cmp_op = ObExprCmpFuncsHelper::get_cmp_op(expr.type_);
    ret = def_relational_eval_func<RuntimeDecintCmp>(
        expr, ctx, expr_datum, cmp_func, cmp_op);
  }
  return ret;
}

int ObDecintRelationFunc::eval_batch(BATCH_EVAL_FUNC_ARG_DECL)
{
  int ret = OB_SUCCESS;
  decint_cmp_fp cmp_func = nullptr;
  if (OB_FAIL(get_runtime_decint_cmp_func(expr, cmp_func))) {
    LOG_WARN("get decimal-int comparison function failed", K(ret));
  } else {
    ObCmpOp cmp_op = ObExprCmpFuncsHelper::get_cmp_op(expr.type_);
    ret = def_relational_eval_batch_func<RuntimeDecintCmp>(
        BATCH_EVAL_FUNC_ARG_LIST, cmp_func, cmp_op);
  }
  return ret;
}

OB_NOINLINE void init_expr_cmp_func_array(ObExpr::EvalFunc *eval_funcs,
                              ObExpr::EvalBatchFunc *batch_eval_funcs,
                              ObDatumCmpFuncType &datum_cmp_func,
                              ObExpr::EvalFunc eval_func,
                              ObExpr::EvalBatchFunc batch_eval_func,
                              ObDatumCmpFuncType datum_func,
                              const ObExpr::EvalBatchFunc *batch_eval_overrides)
{
  static_assert(CO_EQ == 0 && CO_CMP + 1 == CO_MAX, "comparison operators must be contiguous");
  for (int64_t cmp_op = CO_EQ; cmp_op < CO_MAX; ++cmp_op) {
    eval_funcs[cmp_op] = eval_func;
    batch_eval_funcs[cmp_op] = NULL != batch_eval_overrides
        ? batch_eval_overrides[cmp_op]
        : (CO_CMP == cmp_op ? NULL : batch_eval_func);
  }
  datum_cmp_func = datum_func;
}

OB_NOINLINE void init_str_cmp_func_array(const ObCollationType cs_type)
{
  OB_ASSERT(cs_type > CS_TYPE_INVALID && cs_type < CS_TYPE_MAX);
  for (int64_t cmp_op = CO_EQ; cmp_op < CO_MAX; ++cmp_op) {
    EVAL_STR_CMP_FUNCS[cs_type][cmp_op][0] = &ObStrRelationEvalWrap<false>::eval;
    EVAL_STR_CMP_FUNCS[cs_type][cmp_op][1] = &ObStrRelationEvalWrap<true>::eval;
    EVAL_TEXT_CMP_FUNCS[cs_type][cmp_op][0] = &ObTextRelationEvalWrap<false>::eval;
    EVAL_TEXT_CMP_FUNCS[cs_type][cmp_op][1] = &ObTextRelationEvalWrap<true>::eval;
    EVAL_TEXT_STR_CMP_FUNCS[cs_type][cmp_op][0] = &ObTextStrRelationEvalWrap<false>::eval;
    EVAL_TEXT_STR_CMP_FUNCS[cs_type][cmp_op][1] = &ObTextStrRelationEvalWrap<true>::eval;
    EVAL_STR_TEXT_CMP_FUNCS[cs_type][cmp_op][0] = &ObStrTextRelationEvalWrap<false>::eval;
    EVAL_STR_TEXT_CMP_FUNCS[cs_type][cmp_op][1] = &ObStrTextRelationEvalWrap<true>::eval;
  }
  DATUM_STR_CMP_FUNCS[cs_type][0] = NULL;
  DATUM_STR_CMP_FUNCS[cs_type][1] = NULL;
  DATUM_TEXT_CMP_FUNCS[cs_type][0] = NULL;
  DATUM_TEXT_CMP_FUNCS[cs_type][1] = NULL;
  DATUM_TEXT_STR_CMP_FUNCS[cs_type][0] = NULL;
  DATUM_TEXT_STR_CMP_FUNCS[cs_type][1] = NULL;
  DATUM_STR_TEXT_CMP_FUNCS[cs_type][0] = NULL;
  DATUM_STR_TEXT_CMP_FUNCS[cs_type][1] = NULL;
}

static int64_t fill_type_with_tc_eval_func(void)
{
  int64_t cnt = 0;
  for (int64_t i = 0; i < ObMaxType; i++) {
    ObObjTypeClass i_tc = ob_obj_type_class((ObObjType)i);
    for (int64_t j = 0; j < ObMaxType; j++) {
      ObObjTypeClass j_tc = ob_obj_type_class((ObObjType)j);
      if (NULL == EVAL_TYPE_CMP_FUNCS[i][j][0]) {
        const int64_t size = sizeof(void *) * CO_MAX;
        memcpy(&EVAL_TYPE_CMP_FUNCS[i][j][0],
               &EVAL_TC_CMP_FUNCS[i_tc][j_tc][0],
               size);
        memcpy(&EVAL_BATCH_TYPE_CMP_FUNCS[i][j][0],
               &EVAL_BATCH_TC_CMP_FUNCS[i_tc][j_tc][0],
               size);
        cnt++;
      }
      if (NULL == DATUM_TYPE_CMP_FUNCS[i][j]) {
        DATUM_TYPE_CMP_FUNCS[i][j] = DATUM_TC_CMP_FUNCS[i_tc][j_tc];
        cnt++;
      }
    }
  }
  return cnt;
}

extern void __init_all_expr_cmp_funcs();
extern void __init_all_str_expr_cmp_func();

static int64_t init_all_funcs()
{
  int g_init_extra_expr_ret = ObArrayConstIniter<CO_MAX, ExtraExprCmpIniter>::init();
  __init_all_expr_cmp_funcs();
  __init_all_str_expr_cmp_func();
  int g_init_json_ret = ObArrayConstIniter<CO_MAX, JsonExprFuncIniter>::init();
  int g_init_json_datum_ret = ObArrayConstIniter<1, DatumJsonExprCmpIniter>::init();
  int g_init_geo_ret = ObArrayConstIniter<CO_MAX, GeoExprFuncIniter>::init();
  int g_init_geo_datum_ret = ObArrayConstIniter<1, DatumGeoExprCmpIniter>::init();

  int g_init_collection_ret = ObArrayConstIniter<CO_MAX, CollectionExprFuncIniter>::init();
  int g_init_collection_datum_ret = ObArrayConstIniter<1, DatumCollectionExprCmpIniter>::init();
  int g_init_fixed_double_ret =
    ObArrayConstIniter<OB_NOT_FIXED_SCALE, FixedDoubleCmpFuncIniter>::init();

  int g_init_decint_cmp_ret =
    Ob2DArrayConstIniter<DECIMAL_INT_MAX, DECIMAL_INT_MAX, DecintCmpFuncIniter>::init();

  return fill_type_with_tc_eval_func();
}

int64_t g_init_all_funcs = init_all_funcs();

ObCmpOp ObExprCmpFuncsHelper::get_cmp_op(const ObExprOperatorType type)
{
  const ObCmpOp cmp_op = ObRelationalExprOperator::get_cmp_op(type);
  // Comparison evaluators are installed only for relational expressions (and
  // STRCMP).  Fail fast in debug builds if a future caller reuses one with an
  // unrelated expression type instead of silently producing false.
  OB_ASSERT(ob_is_valid_cmp_op(cmp_op));
  return cmp_op;
}

ObExpr::EvalFunc ObExprCmpFuncsHelper::get_eval_expr_cmp_func(const ObObjType type1,
                                                              const ObObjType type2,
                                                              const ObScale scale1,
                                                              const ObScale scale2,
                                                              const ObPrecision prec1,
                                                              const ObPrecision prec2,
                                                              const ObCmpOp cmp_op,
                                                              const ObCollationType cs_type,
                                                              const bool has_lob_header)
{
  OB_ASSERT(type1 >= ObNullType && type1 < ObMaxType);
  OB_ASSERT(type2 >= ObNullType && type2 < ObMaxType);
  OB_ASSERT(cmp_op >= CO_EQ && cmp_op <= CO_MAX);

  ObObjTypeClass tc1 = ob_obj_type_class(type1);
  ObObjTypeClass tc2 = ob_obj_type_class(type2);
  ObExpr::EvalFunc func_ptr = NULL;
  if (OB_UNLIKELY(ob_is_invalid_cmp_op(cmp_op)) ||
      OB_UNLIKELY(ob_is_invalid_obj_tc(tc1) ||
      OB_UNLIKELY(ob_is_invalid_obj_tc(tc2)))) {
    func_ptr = NULL;
  } else if (tc1 == ObJsonTC && tc2 == ObJsonTC) {
    func_ptr = EVAL_JSON_CMP_FUNCS[cmp_op][has_lob_header];
  } else if (tc1 == ObGeometryTC && tc2 == ObGeometryTC) {
    func_ptr = EVAL_GEO_CMP_FUNCS[cmp_op][has_lob_header];
  } else if (tc1 == ObCollectionSQLTC && tc2 == ObCollectionSQLTC) {
    func_ptr = EVAL_COLLECTION_CMP_FUNCS[cmp_op][has_lob_header];
  } else if (IS_FIXED_DOUBLE) {
    func_ptr = EVAL_FIXED_DOUBLE_CMP_FUNCS[MAX(scale1, scale2)][cmp_op];
  } else if (tc1 == ObDecimalIntTC && tc2 == ObDecimalIntTC) {
    ObDecimalIntWideType lw = get_decimalint_type(prec1);
    ObDecimalIntWideType rw = get_decimalint_type(prec2);
    OB_ASSERT(lw < DECIMAL_INT_MAX && lw >= 0);
    OB_ASSERT(rw < DECIMAL_INT_MAX && rw >= 0);
    func_ptr = EVAL_DECINT_CMP_FUNCS[lw][rw][cmp_op];
  } else if (tc1 == ObCollectionSQLTC && tc2 == ObCollectionSQLTC) {
    func_ptr = EVAL_VEC_CMP_FUNCS[cmp_op];
  } else if (tc1 == ObUserDefinedSQLTC || tc2 == ObUserDefinedSQLTC) {
    func_ptr = NULL; //?
  } else if (!ObDatumFuncs::is_string_type(type1) || !ObDatumFuncs::is_string_type(type2)) {
    func_ptr = EVAL_TYPE_CMP_FUNCS[type1][type2][cmp_op];
  } else {
    OB_ASSERT(cs_type > CS_TYPE_INVALID && cs_type < CS_TYPE_MAX);
    if (has_lob_header && (ob_is_large_text(type1) || ob_is_large_text(type2))) {
      if (ob_is_large_text(type1) && ob_is_large_text(type2)) {
        func_ptr = EVAL_TEXT_CMP_FUNCS[cs_type][cmp_op][0];
      } else if (ob_is_large_text(type1)) { // type2 not large text
        func_ptr = EVAL_TEXT_STR_CMP_FUNCS[cs_type][cmp_op][0];
      } else { // type1 not large text
        func_ptr = EVAL_STR_TEXT_CMP_FUNCS[cs_type][cmp_op][0];
      }
    } else { // no lob header or tinytext use original str cmp func
      func_ptr = EVAL_STR_CMP_FUNCS[cs_type][cmp_op][0];
    }
  }
  return func_ptr;
}

ObExpr::EvalBatchFunc ObExprCmpFuncsHelper::get_eval_batch_expr_cmp_func(
    const ObObjType type1,
    const ObObjType type2,
    const ObScale scale1,
    const ObScale scale2,
    const ObPrecision prec1,
    const ObPrecision prec2,
    const ObCmpOp cmp_op,
    const ObCollationType cs_type,
    const bool has_lob_header)
{
  OB_ASSERT(type1 >= ObNullType && type1 < ObMaxType);
  OB_ASSERT(type2 >= ObNullType && type2 < ObMaxType);
  OB_ASSERT(cmp_op >= CO_EQ && cmp_op <= CO_MAX);

  ObObjTypeClass tc1 = ob_obj_type_class(type1);
  ObObjTypeClass tc2 = ob_obj_type_class(type2);
  ObExpr::EvalBatchFunc func_ptr = NULL;
  if (OB_UNLIKELY(ob_is_invalid_cmp_op(cmp_op)) ||
      OB_UNLIKELY(ob_is_invalid_obj_tc(tc1) ||
      OB_UNLIKELY(ob_is_invalid_obj_tc(tc2)))) {
    func_ptr = NULL;
  } else if (type1 == ObJsonType && type2 == ObJsonType) {
    if (NULL != EVAL_JSON_CMP_FUNCS[cmp_op][has_lob_header]) {
      func_ptr = EVAL_BATCH_JSON_CMP_FUNCS[cmp_op];
    }
  } else if (tc1 == ObGeometryTC && tc2 == ObGeometryTC) {
    if (NULL != EVAL_GEO_CMP_FUNCS[cmp_op][has_lob_header]) {
      func_ptr = EVAL_BATCH_GEO_CMP_FUNCS[cmp_op];
    }
  } else if (tc1 == ObCollectionSQLTC && tc2 == ObCollectionSQLTC) {
    func_ptr = EVAL_BATCH_COLLECTION_CMP_FUNCS[cmp_op];
  } else if (IS_FIXED_DOUBLE) {
    func_ptr = EVAL_BATCH_FIXED_DOUBLE_CMP_FUNCS[MAX(scale1, scale2)][cmp_op];
  } else if (ob_is_decimal_int(type1) && ob_is_decimal_int(type2)) {
    ObDecimalIntWideType lw = get_decimalint_type(prec1);
    ObDecimalIntWideType rw = get_decimalint_type(prec2);
    OB_ASSERT(lw < DECIMAL_INT_MAX && lw >= 0);
    OB_ASSERT(rw < DECIMAL_INT_MAX && rw >= 0);
    func_ptr = EVAL_BATCH_DECINT_CMP_FUNCS[lw][rw][cmp_op];
  } else if (tc1 == ObUserDefinedSQLTC || tc2 == ObUserDefinedSQLTC) {
    func_ptr = NULL; //?
  } else if (!ObDatumFuncs::is_string_type(type1) || !ObDatumFuncs::is_string_type(type2)) {
    func_ptr = EVAL_BATCH_TYPE_CMP_FUNCS[type1][type2][cmp_op];
  } else {
    OB_ASSERT(cs_type > CS_TYPE_INVALID && cs_type < CS_TYPE_MAX);
    if (has_lob_header && (ob_is_large_text(type1) || ob_is_large_text(type2))) {
      if (ob_is_large_text(type1) && ob_is_large_text(type2)) {
        if (NULL != EVAL_TEXT_CMP_FUNCS[cs_type][cmp_op][0]) {
          func_ptr = EVAL_BATCH_TEXT_CMP_FUNCS[cmp_op];
        }
      } else if (ob_is_large_text(type1)) { // type2 not large text
        if (NULL != EVAL_TEXT_STR_CMP_FUNCS[cs_type][cmp_op][0]) {
          func_ptr = EVAL_BATCH_TEXT_STR_CMP_FUNCS[cmp_op];
        }
      } else { // type1 not large text
        if (NULL != EVAL_STR_TEXT_CMP_FUNCS[cs_type][cmp_op][0]) {
          func_ptr = EVAL_BATCH_STR_TEXT_CMP_FUNCS[cmp_op];
        }
      }
    } else { // no lob header or tinytext use original str cmp func
      if (NULL != EVAL_STR_CMP_FUNCS[cs_type][cmp_op][0]) {
        func_ptr = EVAL_BATCH_STR_CMP_FUNCS[cmp_op];
      }
    }
  }
  return func_ptr;
}

DatumCmpFunc ObExprCmpFuncsHelper::get_datum_expr_cmp_func(const ObObjType type1,
                                           const ObObjType type2,
                                           const ObScale scale1,
                                           const ObScale scale2,
                                           const ObPrecision prec1,
                                           const ObPrecision prec2,
                                           const ObCollationType cs_type,
                                           const bool has_lob_header)
{
  OB_ASSERT(type1 >= ObNullType && type1 < ObMaxType);
  OB_ASSERT(type2 >= ObNullType && type2 < ObMaxType);

  ObObjTypeClass tc1 = ob_obj_type_class(type1);
  ObObjTypeClass tc2 = ob_obj_type_class(type2);
  ObDatumCmpFuncType func_ptr = NULL;
  if (type1 == ObJsonType && type2 == ObJsonType) {
    func_ptr = DATUM_JSON_CMP_FUNCS[has_lob_header];
  } else if (type1 == ObGeometryType && type2 == ObGeometryType) {
    func_ptr = DATUM_GEO_CMP_FUNCS[has_lob_header];
  } else if (type1 == ObCollectionSQLType && type2 == ObCollectionSQLType) {
    func_ptr = DATUM_COLLECTION_CMP_FUNCS[has_lob_header];
  } else if (IS_FIXED_DOUBLE) {
    func_ptr = DATUM_FIXED_DOUBLE_CMP_FUNCS[MAX(scale1, scale2)];
  } else if (ob_is_decimal_int(type1) && ob_is_decimal_int(type2)) {
    ObDecimalIntWideType lw = get_decimalint_type(prec1);
    ObDecimalIntWideType rw = get_decimalint_type(prec2);
    OB_ASSERT(lw < DECIMAL_INT_MAX && lw >= 0);
    OB_ASSERT(rw < DECIMAL_INT_MAX && rw >= 0);
    func_ptr = DATUM_DECINT_CMP_FUNCS[lw][rw];
  } else if (tc1 == ObUserDefinedSQLTC || tc2 == ObUserDefinedSQLTC) {
    func_ptr = NULL; //?
  } else if (!ObDatumFuncs::is_string_type(type1) || !ObDatumFuncs::is_string_type(type2)) {
    func_ptr = DATUM_TYPE_CMP_FUNCS[type1][type2];
    if (NULL == func_ptr) {
      func_ptr = DATUM_TC_CMP_FUNCS[tc1][tc2];
    }
  } else {
    OB_ASSERT(cs_type > CS_TYPE_INVALID && cs_type < CS_TYPE_MAX);
    if (has_lob_header && (ob_is_large_text(type1) || ob_is_large_text(type2))) {
      if (ob_is_large_text(type1) && ob_is_large_text(type2)) {
        func_ptr = DATUM_TEXT_CMP_FUNCS[cs_type][0];
      } else if (ob_is_large_text(type1)) { // type2 not large text
        func_ptr = DATUM_TEXT_STR_CMP_FUNCS[cs_type][0];
      } else { // type1 not large text
        func_ptr = DATUM_STR_TEXT_CMP_FUNCS[cs_type][0];
      }
    } else { // no lob header or tinytext use original str cmp func
      func_ptr = DATUM_STR_CMP_FUNCS[cs_type][0];
    }
  }
  return func_ptr;
}

} // end namespace sql;
} // end namespace oceanbase
