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
