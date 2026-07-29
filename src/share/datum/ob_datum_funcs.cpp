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

#include "ob_datum_funcs.h"
#include "ob_datum_cmp_func_def.h"
#include "common/object/ob_obj_funcs.h"
#include "share/ob_version_parser.h"
#include "sql/engine/expr/ob_expr_basic_funcs.h"

namespace oceanbase {
namespace common {
namespace serialization {

inline int64_t encoded_length(sql::serializable_function func)
{
  return encoded_length(reinterpret_cast<uint64_t>(func));
}

inline int encode(char *buf, const int64_t buf_len, int64_t &pos,
                  sql::serializable_function func)
{
  return encode(buf, buf_len, pos, reinterpret_cast<uint64_t>(func));
}

inline int decode(const char *buf, const int64_t data_len, int64_t &pos,
                  sql::serializable_function &func)
{
  int ret = OB_SUCCESS;
  uint64_t ptr = 0;
  ret = decode(buf, data_len, pos, ptr);
  if (OB_SUCC(ret)) {
    func = reinterpret_cast<sql::serializable_function>(ptr);
  }
  return ret;
}

} // end namespace serialization
} // end namespace common

using namespace sql;
namespace common {

ObDatumCmpFuncType NULLSAFE_TYPE_CMP_FUNCS[ObMaxType][ObMaxType][2];

// bool g_type_cmp_array_inited = Ob2DArrayConstIniter<ObMaxType, ObMaxType, InitTypeCmpArray>::init();

ObDatumCmpFuncType NULLSAFE_TC_CMP_FUNCS[ObMaxTC][ObMaxTC][2];

static int64_t fill_type_with_tc_cmp_func()
{
  int64_t cnt = 0;
  for (int64_t i = 0; i < ObMaxType; i++) {
    ObObjTypeClass i_tc = ob_obj_type_class((ObObjType)i);
    for (int64_t j = 0; j < ObMaxType; j++) {
      ObObjTypeClass j_tc = ob_obj_type_class((ObObjType)j);
      if (NULL == NULLSAFE_TYPE_CMP_FUNCS[i][j][0]) {
        NULLSAFE_TYPE_CMP_FUNCS[i][j][0] = NULLSAFE_TC_CMP_FUNCS[i_tc][j_tc][0];
        NULLSAFE_TYPE_CMP_FUNCS[i][j][1] = NULLSAFE_TC_CMP_FUNCS[i_tc][j_tc][1];
        cnt++;
      }
    }
  }
  return cnt;
}

// cs_type, compatibility mode, calc_with_end_space
// now only RawTC, StringTC, TextTC defined str cmp funcs
ObDatumCmpFuncType NULLSAFE_STR_CMP_FUNCS[CS_TYPE_MAX][2][2];
ObDatumCmpFuncType NULLSAFE_TEXT_CMP_FUNCS[CS_TYPE_MAX][2][2];
ObDatumCmpFuncType NULLSAFE_TEXT_STR_CMP_FUNCS[CS_TYPE_MAX][2][2];
ObDatumCmpFuncType NULLSAFE_STR_TEXT_CMP_FUNCS[CS_TYPE_MAX][2][2];

ObDatumCmpFuncType NULLSAFE_JSON_CMP_FUNCS[2][2];

ObDatumCmpFuncType NULLSAFE_GEO_CMP_FUNCS[2][2];

ObDatumCmpFuncType NULLSAFE_COLLECTION_CMP_FUNCS[2][2];

ObDatumCmpFuncType FIXED_DOUBLE_CMP_FUNCS[OB_NOT_FIXED_SCALE][2];

ObDatumCmpFuncType DECINT_CMP_FUNCS[DECIMAL_INT_MAX][DECIMAL_INT_MAX][2];

ObDatumCmpFuncType ObDatumFuncs::get_nullsafe_cmp_func(
    const ObObjType type1, const ObObjType type2, const ObCmpNullPos null_pos,
    const ObCollationType cs_type, const ObScale max_scale,
    const bool has_lob_header, const ObPrecision prec1, const ObPrecision prec2) {
  OB_ASSERT(type1 >= ObNullType && type1 < ObMaxType);
  OB_ASSERT(type2 >= ObNullType && type2 < ObMaxType);
  OB_ASSERT(cs_type > CS_TYPE_INVALID && cs_type < CS_TYPE_MAX);
  OB_ASSERT(null_pos >= NULL_LAST && null_pos < MAX_NULL_POS);

  ObDatumCmpFuncType func_ptr = NULL;
  int null_pos_idx = NULL_LAST == null_pos ? 0 : 1;
  if (is_string_type(type1) && is_string_type(type2)) {
    if (has_lob_header && (ob_is_large_text(type1) || ob_is_large_text(type2))) {
      if (ob_is_large_text(type1) && ob_is_large_text(type2)) {
        func_ptr = NULLSAFE_TEXT_CMP_FUNCS[cs_type][0][null_pos_idx];
      } else if (ob_is_large_text(type1)) { // type2 not large text
        func_ptr = NULLSAFE_TEXT_STR_CMP_FUNCS[cs_type][0][null_pos_idx];
      } else if (ob_is_large_text(type2)) { // type1 not large text
        func_ptr = NULLSAFE_STR_TEXT_CMP_FUNCS[cs_type][0][null_pos_idx];
      }
    } else { // no lob header or tinytext use original str cmp func
      func_ptr = NULLSAFE_STR_CMP_FUNCS[cs_type][0][null_pos_idx];
    }
  } else if (is_json(type1) && is_json(type2)) {
    func_ptr = NULLSAFE_JSON_CMP_FUNCS[null_pos_idx][has_lob_header];
  } else if (ob_is_double_type(type1) && ob_is_double_type(type1)
       && max_scale > SCALE_UNKNOWN_YET && max_scale < OB_NOT_FIXED_SCALE) {
    func_ptr = FIXED_DOUBLE_CMP_FUNCS[max_scale][null_pos_idx];
  } else if (is_geometry(type1) && is_geometry(type2)) {
    func_ptr = NULLSAFE_GEO_CMP_FUNCS[null_pos_idx][has_lob_header];
  } else if (is_collection(type1) && is_collection(type2)) {
    func_ptr = NULLSAFE_COLLECTION_CMP_FUNCS[null_pos_idx][has_lob_header];
  } else if (ob_is_decimal_int(type1) && ob_is_decimal_int(type2) && prec1 != PRECISION_UNKNOWN_YET
             && prec2 != PRECISION_UNKNOWN_YET) {
    ObDecimalIntWideType lw = get_decimalint_type(prec1);
    ObDecimalIntWideType rw = get_decimalint_type(prec2);
    OB_ASSERT(lw >= 0 && lw < DECIMAL_INT_MAX);
    OB_ASSERT(rw >= 0 && rw < DECIMAL_INT_MAX);
    func_ptr = DECINT_CMP_FUNCS[lw][rw][null_pos_idx];
  } else {
    func_ptr = NULLSAFE_TYPE_CMP_FUNCS[type1][type2][null_pos_idx];
  }
  return func_ptr;
}

bool ObDatumFuncs::is_collection(const ObObjType type)
{
  const ObObjTypeClass tc = OBJ_TYPE_TO_CLASS[type];
  return (tc == ObCollectionSQLTC);
}


ObExprBasicFuncs EXPR_BASIC_FUNCS[ObMaxType];

// [CS_TYPE][CALC_END_SPACE][IS_LOB_LOCATOR]
ObExprBasicFuncs EXPR_BASIC_STR_FUNCS[CS_TYPE_MAX][2][2];

ObExprBasicFuncs EXPR_BASIC_JSON_FUNCS[2];

ObExprBasicFuncs EXPR_BASIC_GEO_FUNCS[2];
ObExprBasicFuncs EXPR_BASIC_COLLECTION_FUNCS[2];
ObExprBasicFuncs FIXED_DOUBLE_BASIC_FUNCS[OB_NOT_FIXED_SCALE];
ObExprBasicFuncs EXPR_BASIC_UDT_FUNCS[1];



ObExprBasicFuncs DECINT_BASIC_FUNCS[DECIMAL_INT_MAX];

extern void __init_datum_funcs_all();
extern void __init_all_str_funcs();

static bool init_all_str_funcs()
{
  __init_datum_funcs_all();
  __init_all_str_funcs();
  int64_t g_fill_type_with_tc_cmp_func = fill_type_with_tc_cmp_func();
  return true;
}

bool g_all_str_funcs_intied = init_all_str_funcs();

ObExprBasicFuncs* ObDatumFuncs::get_basic_func(const ObObjType type,
                                               const ObCollationType cs_type,
                                               const ObScale scale,
                                               const bool has_lob_locator,
                                               const ObPrecision precision)
{
  ObExprBasicFuncs *res = NULL;
  if ((type >= ObNullType && type < ObMaxType)) {
    if (is_string_type(type)) {
      OB_ASSERT(cs_type > CS_TYPE_INVALID && cs_type < CS_TYPE_MAX);
      bool calc_end_space = false;
      if (ob_is_large_text(type)) {
        res = &EXPR_BASIC_STR_FUNCS[cs_type][calc_end_space][has_lob_locator];
      } else {
        // string is always without lob locator
        res = &EXPR_BASIC_STR_FUNCS[cs_type][calc_end_space][false];
      }
    } else if (ob_is_json(type)) {
      res = &EXPR_BASIC_JSON_FUNCS[has_lob_locator];
    } else if (ob_is_geometry(type)) {
      res = &EXPR_BASIC_GEO_FUNCS[has_lob_locator];
    } else if (ob_is_collection_sql_type(type)) {
      res = &EXPR_BASIC_COLLECTION_FUNCS[has_lob_locator];
    } else if (ob_is_double_type(type) &&
                scale > SCALE_UNKNOWN_YET && scale < OB_NOT_FIXED_SCALE) {
      res = &FIXED_DOUBLE_BASIC_FUNCS[scale];
    } else if (ob_is_decimal_int(type) && precision != PRECISION_UNKNOWN_YET) {
      ObDecimalIntWideType width = get_decimalint_type(precision);
      OB_ASSERT(width >= 0 && width < DECIMAL_INT_MAX);
      res = &DECINT_BASIC_FUNCS[width];
    } else {
      res = &EXPR_BASIC_FUNCS[type];
      // set row cmp funcs
      // FIXME: add precision here
    }
  } else {
    LOG_WARN_RET(common::OB_INVALID_ARGUMENT, "invalid obj type", K(type));
  }
  return res;
}

bool ObDatumFuncs::is_string_type(const ObObjType type)
{
  const ObObjTypeClass tc = OBJ_TYPE_TO_CLASS[type];
  return (tc == ObStringTC || tc == ObRawTC || tc == ObTextTC);
}

bool ObDatumFuncs::is_json(const ObObjType type)
{
  const ObObjTypeClass tc = OBJ_TYPE_TO_CLASS[type];
  return (tc == ObJsonTC);
}

bool ObDatumFuncs::is_geometry(const ObObjType type)
{
  const ObObjTypeClass tc = OBJ_TYPE_TO_CLASS[type];
  return (tc == ObGeometryTC);
}

/**
 * This function is primarily responsible for handling inconsistent hash computations
 * for null types and the null values of those types, such as string, float, double, etc.
 * It ensures that the hashing process treats null values and null type representations
 * consistently across such data types, avoiding discrepancies in hash results.
 */
bool ObDatumFuncs::is_null_aware_hash_type(const ObObjType type)
{
  const ObObjTypeClass tc = OBJ_TYPE_TO_CLASS[type];
  return is_string_type(type) || is_json(type) || is_geometry(type) ||
            (tc == ObUserDefinedSQLTC) || (tc == ObFloatTC) || (tc == ObDoubleTC);
}

OB_SERIALIZE_MEMBER(ObCmpFunc, ser_cmp_func_);
OB_SERIALIZE_MEMBER(ObHashFunc, ser_hash_func_, ser_batch_hash_func_);

} // end namespace common


} // end namespace oceanbase
