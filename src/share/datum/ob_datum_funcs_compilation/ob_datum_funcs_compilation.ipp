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

#ifndef OCEANBASE_STR_DATUM_FUNCS_IPP
#define OCEANBASE_STR_DATUM_FUNCS_IPP

#include "share/datum/ob_datum_funcs.h"
#include "share/datum/ob_datum_cmp_func_def.h"
#include "common/object/ob_obj_funcs.h"
#include "sql/engine/ob_bit_vector.h"
#include "share/ob_cluster_version.h"
#include "share/datum/ob_datum_funcs_impl.h"

namespace oceanbase
{
using namespace sql;
namespace common
{
static const int COMPILATION_UNIT = 8;

#define DEF_COMPILATION_VARS(name, max_val, unit_idx)                                              \
  constexpr int name##_unit_size =                                                                 \
    max_val / COMPILATION_UNIT + (max_val % COMPILATION_UNIT == 0 ? 0 : 1);                        \
  constexpr int name##_start =                                                                     \
    (name##_unit_size * unit_idx < max_val ? name##_unit_size * unit_idx : max_val);               \
  constexpr int name##_end =                                                                       \
    (name##_start + name##_unit_size >= max_val ? max_val : name##_start + name##_unit_size);

#define DEF_DATUM_FUNC_INIT(unit_idx)                                                              \
  void __init_datum_func##unit_idx()                                                               \
  {                                                                                                \
    DEF_COMPILATION_VARS(ty, ObMaxType, unit_idx);                                                 \
    DEF_COMPILATION_VARS(tc, ObMaxTC, unit_idx);                                                   \
    DEF_COMPILATION_VARS(ty_basic, ObMaxType, unit_idx);                                           \
    Ob2DArrayConstIniter<ty_end, ObMaxType, TypeCmpIniter, ty_start, 0>::init();                   \
    Ob2DArrayConstIniter<tc_end, ObMaxTC, TCCmpIniter, tc_start, 0>::init();                       \
    ObArrayConstIniter<ty_basic_end, InitBasicFuncArray, ty_basic_start>::init();                  \
    if constexpr (unit_idx == 0) {                                                                 \
      ObArrayConstIniter<1, InitJsonCmpArray>::init();                                             \
      ObArrayConstIniter<1, InitGeoCmpArray>::init();                                              \
      ObArrayConstIniter<1, InitCollectionCmpArray>::init();                                       \
      ObArrayConstIniter<1, InitBasicJsonFuncArray>::init();                                       \
      ObArrayConstIniter<1, InitBasicGeoFuncArray>::init();                                        \
      ObArrayConstIniter<1, InitCollectionBasicFuncArray>::init();                                 \
      ObArrayConstIniter<1, InitUDTBasicFuncArray>::init();                                        \
    }                                                                                              \
    if constexpr (unit_idx == 1) {                                                                 \
      ObArrayConstIniter<OB_NOT_FIXED_SCALE, InitFixedDoubleCmpArray>::init();                     \
    }                                                                                              \
    if constexpr (unit_idx >= 2) {                                                                 \
      constexpr int fd_basic_unit_count = COMPILATION_UNIT - 2;                                    \
      constexpr int fd_basic_unit_idx = unit_idx - 2;                                              \
      constexpr int fd_basic_unit_size =                                                           \
        OB_NOT_FIXED_SCALE / fd_basic_unit_count +                                                 \
        (OB_NOT_FIXED_SCALE % fd_basic_unit_count == 0 ? 0 : 1);                                   \
      constexpr int fd_basic_start =                                                               \
        (fd_basic_unit_size * fd_basic_unit_idx < OB_NOT_FIXED_SCALE                               \
            ? fd_basic_unit_size * fd_basic_unit_idx                                               \
            : OB_NOT_FIXED_SCALE);                                                                 \
      constexpr int fd_basic_end =                                                                 \
        (fd_basic_start + fd_basic_unit_size >= OB_NOT_FIXED_SCALE                                 \
            ? OB_NOT_FIXED_SCALE                                                                   \
            : fd_basic_start + fd_basic_unit_size);                                                \
      ObArrayConstIniter<fd_basic_end, InitFixedDoubleBasicFuncArray, fd_basic_start>::init();     \
    }                                                                                              \
    if constexpr (unit_idx == 3) {                                                                 \
      Ob2DArrayConstIniter<DECIMAL_INT_MAX, DECIMAL_INT_MAX, InitDecintCmpArray>::init();          \
    }                                                                                              \
    if constexpr (unit_idx == 4) {                                                                 \
      ObArrayConstIniter<DECIMAL_INT_MAX, InitDecintBasicFuncArray>::init();                       \
    }                                                                                              \
  }

} // end common
} // end oceanbase
#endif // OCEANBASE_STR_DATUM_FUNCS_IPP
