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
