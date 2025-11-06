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

static const int COMPILATION_UNIT = 7;

#define DEF_COMPILATION_VARS(name, max_val, unit_idx)                                              \
  constexpr int name##_unit_size =                                                                 \
    max_val / COMPILATION_UNIT + (max_val % COMPILATION_UNIT == 0 ? 0 : 1);                        \
  constexpr int name##_start =                                                                     \
    (name##_unit_size * unit_idx < max_val ? name##_unit_size * unit_idx : max_val);               \
  constexpr int name##_end =                                                                       \
    (name##_start + name##_unit_size >= max_val ? max_val : name##_start + name##_unit_size);

#define DEF_COMPILE_FUNC_INIT(unit_idx)                                                                 \
  void __init_vec_cast_func##unit_idx()                                                                 \
  {                                                                                                     \
    DEF_COMPILATION_VARS(tc, MAX_VEC_TC, unit_idx);                                                        \
    Ob2DArrayConstIniter<tc_end, MAX_VEC_TC, VectorCastIniter, tc_start, VEC_TC_INTEGER>::init();          \
    Ob2DArrayConstIniter<tc_end, MAX_VEC_TC, EvalArgVecCasterIniter, tc_start, VEC_TC_INTEGER>::init();    \
  }

} // end sql
} // end oceanbase
