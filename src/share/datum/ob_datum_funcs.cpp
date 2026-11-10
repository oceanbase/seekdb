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
#include "datum_ndv_hash.h"
#include "ob_datum_cmp_func_def.h"
#include "common/datum/datum_copy_plan.h"

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

namespace common {

ObDatumCmpFuncType NULLSAFE_TYPE_CMP_FUNCS[ObMaxType][ObMaxType][2];

// bool g_type_cmp_array_inited = Ob2DArrayConstIniter<ObMaxType, ObMaxType, InitTypeCmpArray>::init();

ObDatumCmpFuncType NULLSAFE_TC_CMP_FUNCS[ObMaxTC][ObMaxTC][2];

static int64_t fill_type_with_tc_cmp_func()
{
  int64_t cnt = 0;
  for (int64_t i = 0; i < ObMaxType; i++) {
    SqlTypeClass i_tc = sql_type_class((SqlValueType)i);
    for (int64_t j = 0; j < ObMaxType; j++) {
      SqlTypeClass j_tc = sql_type_class((SqlValueType)j);
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
    const SqlValueType type1, const SqlValueType type2, const ObCmpNullPos null_pos,
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

bool ObDatumFuncs::is_collection(const SqlValueType type)
{
  const SqlTypeClass tc = OBJ_TYPE_TO_CLASS[type];
  return (tc == ObCollectionSQLTC);
}


ObDatumBasicFuncs EXPR_BASIC_FUNCS[ObMaxType];

// [CS_TYPE][CALC_END_SPACE][IS_LOB_LOCATOR]
ObDatumBasicFuncs EXPR_BASIC_STR_FUNCS[CS_TYPE_MAX][2][2];

ObDatumBasicFuncs EXPR_BASIC_JSON_FUNCS[2];

ObDatumBasicFuncs EXPR_BASIC_GEO_FUNCS[2];
ObDatumBasicFuncs EXPR_BASIC_COLLECTION_FUNCS[2];
ObDatumBasicFuncs FIXED_DOUBLE_BASIC_FUNCS[OB_NOT_FIXED_SCALE];
ObDatumBasicFuncs EXPR_BASIC_UDT_FUNCS[1];



ObDatumBasicFuncs DECINT_BASIC_FUNCS[DECIMAL_INT_MAX];

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

ObDatumBasicFuncs* ObDatumFuncs::get_basic_func(const SqlValueType type,
                                                const ObCollationType cs_type,
                                                const ObScale scale,
                                                const bool has_lob_locator,
                                                const ObPrecision precision)
{
  ObDatumBasicFuncs *res = NULL;
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
    }
  } else {
    LOG_WARN_RET(common::OB_INVALID_ARGUMENT, "invalid obj type", K(type));
  }
  return res;
}

int hash_datum_for_ndv(const ObDatum &datum, const SqlTypeMeta &meta,
                               const uint64_t seed, uint64_t &result)
{
  int ret = OB_SUCCESS;
  const SqlValueType type = meta.get_type();
  if (OB_UNLIKELY(type < ObNullType || type >= ObMaxType)) {
    ret = OB_INVALID_ARGUMENT;
  } else if ((meta.is_string_type() || meta.is_enumset_inner_type()
              || ob_is_json(type) || ob_is_geometry(type))
             && !datum.is_null()) {
    // VALUES statistics historically hash the stored bytes, including any
    // locator header, without dereferencing external LOB data. ENUM/SET inner
    // values use the same string representation. JSON and geometry's legacy
    // value hashes also use these raw bytes; expression hashes may parse/read LOBs.
    result = ObCharset::hash(meta.get_collation_type(), datum.ptr_, datum.len_,
                             seed, false, NULL);
  } else if (OB_ISNULL(EXPR_BASIC_FUNCS[type].default_hash_)) {
    ret = OB_NOT_SUPPORTED;
  } else {
    // Use the unspecialized scalar table: get_basic_func() may select rounded
    // DOUBLE, width-specific DECIMAL or LOB-reading expression semantics.
    ret = EXPR_BASIC_FUNCS[type].default_hash_(datum, seed, result, NULL);
  }
  return ret;
}

bool ObDatumFuncs::is_string_type(const SqlValueType type)
{
  const SqlTypeClass tc = OBJ_TYPE_TO_CLASS[type];
  return (tc == ObStringTC || tc == ObRawTC || tc == ObTextTC);
}

bool ObDatumFuncs::is_json(const SqlValueType type)
{
  const SqlTypeClass tc = OBJ_TYPE_TO_CLASS[type];
  return (tc == ObJsonTC);
}

bool ObDatumFuncs::is_geometry(const SqlValueType type)
{
  const SqlTypeClass tc = OBJ_TYPE_TO_CLASS[type];
  return (tc == ObGeometryTC);
}

/**
 * This function is primarily responsible for handling inconsistent hash computations
 * for null types and the null values of those types, such as string, float, double, etc.
 * It ensures that the hashing process treats null values and null type representations
 * consistently across such data types, avoiding discrepancies in hash results.
 */
bool ObDatumFuncs::is_null_aware_hash_type(const SqlValueType type)
{
  const SqlTypeClass tc = OBJ_TYPE_TO_CLASS[type];
  return is_string_type(type) || is_json(type) || is_geometry(type) ||
            (tc == ObUserDefinedSQLTC) || (tc == ObFloatTC) || (tc == ObDoubleTC);
}

int ObDatumFuncs::bind_legacy_cmp(const SqlType &left, const SqlType &right,
                                 ObDatumCmpFuncType function, DatumCmpOp &output)
{
  if (function == nullptr) { return OB_INVALID_ARGUMENT; }
  DatumCmpOp candidate;
  candidate.left_ = &left; candidate.right_ = &right; candidate.function_ = function;
  output = candidate;
  return OB_SUCCESS;
}

int ObDatumFuncs::bind_legacy_hash(const SqlType &type, ObDatumHashFuncType function,
                                  DatumHashOp &output, ObDatumHashFuncType special_state_function)
{
  if (function == nullptr) { return OB_INVALID_ARGUMENT; }
  DatumHashOp candidate;
  candidate.type_ = &type; candidate.function_ = function; candidate.state_function_ = special_state_function;
  output = candidate;
  return OB_SUCCESS;
}

int ObDatumFuncs::prepare_cmp(const SqlType &left, const SqlType &right,
                              ObCmpNullPos null_pos, ObCollationType collation, DatumCmpOp &output)
{
  int ret = OB_SUCCESS;
  DatumCmpOp candidate;
  if (left.kind() < ObNullType || left.kind() >= ObMaxType
      || right.kind() < ObNullType || right.kind() >= ObMaxType
      || null_pos < NULL_LAST || null_pos >= MAX_NULL_POS) {
    ret = OB_INVALID_ARGUMENT;
  } else if (left.kind() == ObDecimalIntType && right.kind() == ObDecimalIntType
      && (left.has_scale() != right.has_scale()
          || (left.has_scale() && left.scale() != right.scale()))) {
    // The old coefficient comparator is for a cast-normalized common scale.
    // Binding two arbitrary scales to that function would silently compare
    // coefficients instead of SQL values. Cast planning must establish it.
    ret = OB_INVALID_ARGUMENT;
  } else {
    if (collation == CS_TYPE_INVALID && !is_string_type(left.kind()) && !is_string_type(right.kind())) {
      collation = CS_TYPE_BINARY;
    }
    if (collation <= CS_TYPE_INVALID || collation >= CS_TYPE_MAX) {
      ret = OB_INVALID_ARGUMENT;
    } else {
      candidate.function_ = get_nullsafe_cmp_func(left.kind(), right.kind(), null_pos, collation,
          max_scale(left.has_scale() ? left.scale() : SCALE_UNKNOWN_YET,
                    right.has_scale() ? right.scale() : SCALE_UNKNOWN_YET),
          left.legacy_meta().has_lob_header() || right.legacy_meta().has_lob_header(),
          left.has_precision() ? left.precision() : PRECISION_UNKNOWN_YET,
          right.has_precision() ? right.precision() : PRECISION_UNKNOWN_YET);
      if (candidate.function_ == nullptr) { ret = OB_NOT_SUPPORTED; }
      else {
        candidate.left_ = &left; candidate.right_ = &right; candidate.null_pos_ = null_pos;
        output = candidate;
      }
    }
  }
  return ret;
}

int ObDatumFuncs::prepare_basic(const SqlType &type, DatumBasicFuncs &output)
{
  int ret = OB_SUCCESS;
  ObCollationType collation = type.collation();
  if (type.kind() < ObNullType || type.kind() >= ObMaxType) { return OB_INVALID_ARGUMENT; }
  if (collation == CS_TYPE_INVALID && !is_string_type(type.kind())) { collation = CS_TYPE_BINARY; }
  if (collation <= CS_TYPE_INVALID || collation >= CS_TYPE_MAX) { return OB_INVALID_ARGUMENT; }
  const ObDatumBasicFuncs *legacy = get_basic_func(type.kind(), collation,
      type.has_scale() ? type.scale() : SCALE_UNKNOWN_YET, type.legacy_meta().has_lob_header(),
      type.has_precision() ? type.precision() : PRECISION_UNKNOWN_YET);
  if (legacy == nullptr) { ret = OB_NOT_SUPPORTED; }
  else {
    DatumBasicFuncs candidate;
    const ObDatumBasicFuncs *states = get_basic_func(ObExtendType, CS_TYPE_BINARY);
#define BIND_NATIVE_HASH(NAME) \
    candidate.NAME.type_ = &type; candidate.NAME.function_ = legacy->NAME; \
    candidate.NAME.state_function_ = states == nullptr ? nullptr : states->NAME
    BIND_NATIVE_HASH(default_hash_);
    BIND_NATIVE_HASH(murmur_hash_);
    BIND_NATIVE_HASH(xx_hash_);
    BIND_NATIVE_HASH(wy_hash_);
    BIND_NATIVE_HASH(murmur_hash_v2_);
#undef BIND_NATIVE_HASH
    candidate.default_hash_batch_ = candidate.default_hash_;
    candidate.murmur_hash_batch_ = candidate.murmur_hash_;
    candidate.xx_hash_batch_ = candidate.xx_hash_;
    candidate.wy_hash_batch_ = candidate.wy_hash_;
    candidate.murmur_hash_v2_batch_ = candidate.murmur_hash_v2_;
    candidate.null_first_cmp_.left_ = candidate.null_first_cmp_.right_ = &type;
    candidate.null_first_cmp_.function_ = legacy->null_first_cmp_;
    candidate.null_first_cmp_.null_pos_ = NULL_FIRST;
    candidate.null_last_cmp_.left_ = candidate.null_last_cmp_.right_ = &type;
    candidate.null_last_cmp_.function_ = legacy->null_last_cmp_;
    candidate.null_last_cmp_.null_pos_ = NULL_LAST;
    candidate.legacy_functions_ = legacy;
    output = candidate;
  }
  return ret;
}

int DatumCmpOp::operator()(const Datum &left, const Datum &right, int &result,
                           const ObDatumAccessContext *access_ctx) const
{
  int ret = OB_SUCCESS;
  int candidate = 0;
  if (!is_valid()) { ret = OB_NOT_INIT; }
  else if (left.is_nop() || right.is_nop()) { ret = OB_INVALID_ARGUMENT; }
  else if (!left.is_null() && !right.is_null()
      && (left.is_min() || left.is_max() || right.is_min() || right.is_max())) {
    candidate = (left.is_min() && right.is_min()) || (left.is_max() && right.is_max()) ? 0
        : left.is_min() || right.is_max() ? -1 : 1;
  } else {
    alignas(8) char left_scratch[64];
    alignas(8) char right_scratch[64];
    ObDatum left_view, right_view;
    if (OB_FAIL(DatumCopyPlan(*left_).legacy_view(left, left_view, left_scratch, sizeof(left_scratch)))) {
    } else if (OB_FAIL(DatumCopyPlan(*right_).legacy_view(right, right_view, right_scratch, sizeof(right_scratch)))) {
    } else {
      ret = function_(left_view, right_view, candidate, access_ctx);
    }
  }
  if (ret == OB_SUCCESS) { result = candidate; }
  return ret;
}

int DatumHashOp::operator()(const Datum &value, uint64_t seed, uint64_t &result,
                            const ObDatumAccessContext *access_ctx) const
{
  int ret = OB_SUCCESS;
  uint64_t candidate = 0;
  if (!is_valid()) { ret = OB_NOT_INIT; }
  else if (value.is_nop()) { ret = OB_INVALID_ARGUMENT; }
  else {
    alignas(8) char scratch[64];
    ObDatum view;
    if (OB_FAIL(DatumCopyPlan(*type_).legacy_view(value, view, scratch, sizeof(scratch)))) {
    } else if (value.is_min() || value.is_max()) {
      ret = state_function_ == nullptr ? OB_NOT_SUPPORTED : state_function_(view, seed, candidate, access_ctx);
    } else { ret = function_(view, seed, candidate, access_ctx); }
  }
  if (ret == OB_SUCCESS) { result = candidate; }
  return ret;
}

int DatumHashOp::operator()(uint64_t *results, const Datum *values, bool batch_values,
                            const sql::ObBitVector &skip, int64_t count,
                            const uint64_t *seeds, bool batch_seeds,
                            const ObDatumAccessContext *access_ctx) const
{
  int ret = OB_SUCCESS;
  if (!is_valid()) { ret = OB_NOT_INIT; }
  else if (count < 0 || (count > 0 && (results == nullptr || values == nullptr || seeds == nullptr))) {
    ret = OB_INVALID_ARGUMENT;
  } else {
    for (int64_t i = 0; ret == OB_SUCCESS && i < count; ++i) {
      if (!skip.at(i)) { ret = (*this)(values[batch_values ? i : 0], seeds[batch_seeds ? i : 0], results[i], access_ctx); }
    }
  }
  return ret;
}

OB_SERIALIZE_MEMBER(ObCmpFunc, ser_cmp_func_);
OB_SERIALIZE_MEMBER(ObHashFunc, ser_hash_func_, ser_batch_hash_func_);

} // end namespace common


} // end namespace oceanbase
