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

#include "sql/engine/expr/vector_cast/vector_cast_impl.ipp"

namespace oceanbase
{
namespace sql
{

static const int COMPILATION_UNIT = 16;

static constexpr int vec_cast_compilation_bound(const int unit_idx)
{
  constexpr int BOUNDS[COMPILATION_UNIT + 1] = {
    VEC_TC_NULL,
    VEC_TC_UINTEGER,
    VEC_TC_DOUBLE,
    VEC_TC_NUMBER,
    VEC_TC_DATE,
    VEC_TC_YEAR,
    VEC_TC_STRING,
    VEC_TC_ENUM_SET_INNER,
    VEC_TC_INTERVAL_YM,
    VEC_TC_JSON,
    VEC_TC_DEC_INT32,
    VEC_TC_DEC_INT64,
    VEC_TC_DEC_INT128,
    VEC_TC_DEC_INT256,
    VEC_TC_DEC_INT512,
    VEC_TC_COLLECTION,
    MAX_VEC_TC
  };
  return BOUNDS[unit_idx];
}

#define DEF_COMPILATION_VARS(name, max_val, unit_idx)                                              \
  static_assert(max_val == MAX_VEC_TC, "vector cast compilation bounds only support MAX_VEC_TC");  \
  constexpr int name##_start = vec_cast_compilation_bound(unit_idx);                               \
  constexpr int name##_end = vec_cast_compilation_bound(unit_idx + 1);

#define DEF_COMPILE_FUNC_INIT(unit_idx)                                                                 \
  void __init_vec_cast_func##unit_idx()                                                                 \
  {                                                                                                     \
    DEF_COMPILATION_VARS(tc, MAX_VEC_TC, unit_idx);                                                        \
    Ob2DArrayConstIniter<tc_end, MAX_VEC_TC, VectorCastIniter, tc_start, VEC_TC_INTEGER>::init();          \
    Ob2DArrayConstIniter<tc_end, MAX_VEC_TC, EvalArgVecCasterIniter, tc_start, VEC_TC_INTEGER>::init();    \
  }

} // end sql
} // end oceanbase
