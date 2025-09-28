/**
 * Copyright (c) 2021 OceanBase
 * OceanBase CE is licensed under Mulan PubL v2.
 * You can use this software according to the terms and conditions of the Mulan PubL v2.
 * You may obtain a copy of Mulan PubL v2 at:
 *          http://license.coscl.org.cn/MulanPubL-2.0
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND,
 * EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
 * MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
 * See the Mulan PubL v2 for more details.
 */

#ifndef OCEANBASE_ENGINE_OB_SERIALIZABLE_FUNCTION_H_
#define OCEANBASE_ENGINE_OB_SERIALIZABLE_FUNCTION_H_

#include "lib/utility/serialization.h"
#include "sql/engine/ob_bit_vector.h"

namespace oceanbase
{
namespace sql
{
struct EvalBound;

struct ObSerializeFuncTag {};
typedef void (*serializable_function)(ObSerializeFuncTag &);

struct ObExpr;
struct ObEvalCtx;
// Implemented in ob_expr.cpp
extern int expr_default_eval_batch_func(const ObExpr &expr,
                                        ObEvalCtx &ctx,
                                        const ObBitVector &skip,
                                        const int64_t batch_size);
// Implemented in ob_expr.cpp
extern int expr_default_eval_vector_func(const ObExpr &expr,
                                         ObEvalCtx &ctx,
                                         const ObBitVector &skip,
                                         const EvalBound &bound);

struct ObBatchEvalFuncTag {};
typedef void (*ser_eval_batch_function)(ObBatchEvalFuncTag &);

struct ObEvalVectorFuncTag {};
typedef void (*ser_eval_vector_function)(ObEvalVectorFuncTag &);

// serialize help macro, can be used in OB_SERIALIZE_MEMBER like this:
// OB_SERIALIZE_MEMBER(Foo, SER_FUNC(func_));
#define SER_FUNC(f) *(oceanbase::sql::serializable_function *)(&f)


// Serialize function array (SFA) id define, append only (before OB_SFA_MAX)
// can not delete or reorder.
#define SER_FUNC_ARRAY_ID_ENUM                   \
  OB_SFA_MIN,                                    \
  OB_SFA_ALL_MISC,                               \
  OB_SFA_DATUM_NULLSAFE_CMP,                     \
  OB_SFA_DATUM_NULLSAFE_STR_CMP,                 \
  OB_SFA_EXPR_BASIC_PART1,                       \
  OB_SFA_EXPR_STR_BASIC_PART1,                   \
  OB_SFA_RELATION_EXPR_EVAL,                     \
  OB_SFA_RELATION_EXPR_EVAL_STR,                 \
  OB_SFA_DATUM_CMP,                              \
  OB_SFA_DATUM_CMP_STR,                          \
  OB_SFA_DATUM_CAST_ORACLE_IMPLICIT,             \
  OB_SFA_DATUM_CAST_ORACLE_EXPLICIT,             \
  OB_SFA_DATUM_CAST_MYSQL_IMPLICIT,              \
  OB_SFA_DATUM_CAST_MYSQL_ENUMSET_IMPLICIT,      \
  OB_SFA_SQL_EXPR_EVAL,                          \
  OB_SFA_SQL_EXPR_ABS_EVAL,                      \
  OB_SFA_SQL_EXPR_NEG_EVAL,                      \
  OB_SFA_RELATION_EXPR_EVAL_BATCH,               \
  OB_SFA_RELATION_EXPR_STR_EVAL_BATCH,           \
  OB_SFA_SQL_EXPR_EVAL_BATCH,                    \
  OB_SFA_EXPR_BASIC_PART2,                       \
  OB_SFA_EXPR_STR_BASIC_PART2,                   \
  OB_SFA_RELATION_EXPR_EVAL_TEXT,                \
  OB_SFA_RELATION_EXPR_TEXT_EVAL_BATCH,          \
  OB_SFA_DATUM_CMP_TEXT,                         \
  OB_SFA_RELATION_EXPR_EVAL_TEXT_STR,            \
  OB_SFA_RELATION_EXPR_TEXT_STR_EVAL_BATCH,      \
  OB_SFA_DATUM_CMP_TEXT_STR,                     \
  OB_SFA_RELATION_EXPR_EVAL_STR_TEXT,            \
  OB_SFA_RELATION_EXPR_STR_TEXT_EVAL_BATCH,      \
  OB_SFA_DATUM_CMP_STR_TEXT,                     \
  OB_SFA_EXPR_JSON_BASIC_PART1,                  \
  OB_SFA_EXPR_JSON_BASIC_PART2,                  \
  OB_SFA_RELATION_EXPR_EVAL_JSON,                \
  OB_SFA_RELATION_EXPR_JSON_EVAL_BATCH,          \
  OB_SFA_DATUM_CMP_JSON,                         \
  OB_SFA_FIXED_DOUBLE_NULLSAFE_CMP,              \
  OB_SFA_FIXED_DOUBLE_BASIC_PART1,               \
  OB_SFA_FIXED_DOUBLE_BASIC_PART2,               \
  OB_SFA_FIXED_DOUBLE_CMP_EVAL,                  \
  OB_SFA_FIXED_DOUBLE_CMP_EVAL_BATCH,            \
  OB_SFA_DATUM_FIXED_DOUBLE_CMP,                 \
  OB_SFA_EXPR_GEO_BASIC_PART1,                   \
  OB_SFA_EXPR_GEO_BASIC_PART2,                   \
  OB_SFA_RELATION_EXPR_EVAL_GEO,                 \
  OB_SFA_RELATION_EXPR_GEO_EVAL_BATCH,           \
  OB_SFA_DATUM_CMP_GEO,                          \
  OB_SFA_DATUM_NULLSAFE_TEXT_CMP,                \
  OB_SFA_DATUM_NULLSAFE_TEXT_STR_CMP,            \
  OB_SFA_DATUM_NULLSAFE_STR_TEXT_CMP,            \
  OB_SFA_DATUM_NULLSAFE_JSON_CMP,                \
  OB_SFA_DATUM_NULLSAFE_GEO_CMP,                 \
  OB_SFA_EXPR_UDT_BASIC_PART1,                   \
  OB_SFA_EXPR_UDT_BASIC_PART2,                   \
  OB_SFA_SQL_EXPR_EVAL_VECTOR,                   \
  OB_SFA_CMP_EXPR_EVAL_VECTOR,                   \
  OB_SFA_VECTOR_NULLSAFE_CMP,                    \
  OB_SFA_DECIMAL_INT_EXPR_EVAL,                  \
  OB_SFA_DECIMAL_INT_EXPR_EVAL_BATCH,            \
  OB_SFA_DECIMAL_INT_CAST_EXPR_EVAL,             \
  OB_SFA_DECIMAL_INT_CAST_EXPR_EVAL_BATCH,       \
  OB_SFA_DECIMAL_INT_CMP_EVAL,                   \
  OB_SFA_DECIMAL_INT_CMP_EVAL_BATCH,             \
  OB_SFA_DECIMAL_INT_CMP,                        \
  OB_SFA_DECIMAL_INT_BASIC_PART1,                \
  OB_SFA_DECIMAL_INT_BASIC_PART2,                \
  OB_SFA_DECIMAL_INT_NULLSAFE_CMP,               \
  OB_SFA_VECTOR_CMP,                             \
  OB_SFA_SQL_EXPR_ABS_EVAL_VEC,                  \
  OB_SFA_VECTOR_CAST,                            \
  OB_SFA_VECTOR_EVAL_ARG_CAST,                   \
  OB_SFA_COLLECTION_EXPR_EVAL,                   \
  OB_SFA_COLLECTION_EXPR_EVAL_BATCH,             \
  OB_SFA_COLLECTION_EXPR_EVAL_VEC,               \
  OB_SFA_RELATION_EXPR_COLLECTION_EVAL,          \
  OB_SFA_RELATION_EXPR_COLLECTION_EVAL_BATCH,    \
  OB_SFA_DATUM_CMP_COLLECTION,                   \
  OB_SFA_DATUM_NULLSAFE_COLLECTION_CMP,          \
  OB_SFA_EXPR_COLLECTION_BASIC_PART1,            \
  OB_SFA_EXPR_COLLECTION_BASIC_PART2,            \
  OB_SFA_FAST_CALC_PART_VEC,                     \
  OB_SFA_DATUM_NULLSAFE_ROARINGBITMAP_CMP,       \
  OB_SFA_EXPR_ROARINGBITMAP_BASIC_PART1,         \
  OB_SFA_EXPR_ROARINGBITMAP_BASIC_PART2,         \
  OB_SFA_MAX

enum ObSerFuncArrayID {
  SER_FUNC_ARRAY_ID_ENUM
};

// add unused ObSerFuncArrayID here
#define UNUSED_SER_FUNC_ARRAY_ID_ENUM            \
  OB_SFA_MIN,                                    \
  OB_SFA_MAX

class ObFuncSerialization
{
public:
  // called before worker threads started.
  static void init() { }

  // used in REG_SER_FUNC_ARRAY macro, can not used directly
  static bool reg_func_array(const ObSerFuncArrayID id, void **array, const int64_t size);

  //
  // Convert N x N two dimension array to single dimension array which index is stable
  // while N extending. e.g:
  //
  //  00 01 02
  //  10 11 12   ==>  00 01 10 11 02 20 12 21 22
  //  20 21 22
  //
  //
  // Usage:
  //
  // fuc_array[N][N][2] can convert to ser_func_array[N * N][2] with:
  // convert_NxN_function_array(ser_func_array, func_array, N, 2, 0, 2).
  //
  // func_array[N][N][2] extend to func_array[N][N][3], the serialize function array should
  // split into to array:
  //   ser_func_array0[N * N][2] with: convert_NxN_array(ser_func_array0, func_array, N, 3, 0, 2).
  //   ser_func_array1[N * N][2] with: convert_NxN_array(ser_func_array0, func_array, N, 3, 2, 1).
  //
  //
  static bool convert_NxN_array(void **dst, void **src, const int64_t n,
                                const int64_t row_size = 1,
                                const int64_t copy_row_idx = 0,
                                const int64_t copy_row_cnt = 1);
};

#define REG_SER_FUNC_ARRAY(id, array, size) \
  static_assert(id >= 0 && id < OB_SFA_MAX, "too big id" #id); \
  bool g_reg_ser_func_##id = ObFuncSerialization::reg_func_array( \
      id, reinterpret_cast<void **>(array), size);

} // end namespace sql

namespace common
{
namespace serialization
{

inline int64_t encoded_length(sql::serializable_function func)
{
  return encoded_length(reinterpret_cast<uint64_t>(func));
}

inline int64_t encoded_length(sql::ser_eval_batch_function func)
{
  return encoded_length(reinterpret_cast<sql::serializable_function>(func));
}

inline int64_t encoded_length(sql::ser_eval_vector_function func)
{
  return encoded_length(reinterpret_cast<sql::serializable_function>(func));
}

inline int encode(char *buf, const int64_t buf_len, int64_t &pos,
                  sql::serializable_function func)
{
  int ret = OB_SUCCESS;
  ret = encode(buf, buf_len, pos, reinterpret_cast<uint64_t>(func));
  return ret;
}

inline int encode(char *buf, const int64_t buf_len, int64_t &pos,
                  sql::ser_eval_batch_function func)
{
  return encode(buf, buf_len, pos, reinterpret_cast<sql::serializable_function>(func));
}

inline int encode(char *buf, const int64_t buf_len, int64_t &pos,
                  sql::ser_eval_vector_function func)
{
  return encode(buf, buf_len, pos, reinterpret_cast<sql::serializable_function>(func));
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

inline int decode(const char *buf, const int64_t data_len, int64_t &pos,
                  sql::ser_eval_batch_function &func)
{
  int ret = OB_SUCCESS;
  uint64_t ptr = 0;
  ret = decode(buf, data_len, pos, ptr);
  if (OB_SUCC(ret)) {
    func = reinterpret_cast<sql::ser_eval_batch_function>(ptr);
  }
  return ret;
}

inline int decode(const char *buf, const int64_t data_len, int64_t &pos,
                  sql::ser_eval_vector_function &func)
{
  int ret = OB_SUCCESS;
  uint64_t ptr = 0;
  ret = decode(buf, data_len, pos, ptr);
  if (OB_SUCC(ret)) {
    func = reinterpret_cast<sql::ser_eval_vector_function>(ptr);
  }
  return ret;
}


} // end namespace serialization
} // end namespace common
} // end namespace oceanbase

#endif // OCEANBASE_ENGINE_OB_SERIALIZABLE_FUNCTION_H_
