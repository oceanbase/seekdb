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
#ifndef OCEANBASE_SHARE_OBJECT_OB_DECINT_SCALE_UTIL_H_
#define OCEANBASE_SHARE_OBJECT_OB_DECINT_SCALE_UTIL_H_
// Decimal-int scaling and precision checks shared by object and datum casts.
#include "common/object/ob_object.h"
#include "common/wide_integer/ob_wide_integer_helper.h"
#include "share/datum/ob_datum_funcs.h"
#include "share/object/ob_obj_cast.h"  // ObCastMode
#include "share/object/ob_obj_cast_runtime.h"

namespace oceanbase
{
namespace common
{
namespace decint_scale
{
inline bool need_scale(const ObScale in_scale, const int32_t in_bytes,
                       const ObScale out_scale, const int32_t out_bytes)
{
  return (in_scale != out_scale) || (in_bytes != out_bytes);
}

int scale_decimalint(const ObDecimalInt *decint, const int32_t int_bytes,
                     const ObScale in_scale, const ObScale out_scale,
                     const ObPrecision out_prec, const ObCastMode cast_mode,
                     ObDecimalIntBuilder &val,
                     const ObIObjCastRuntime *runtime);

int check_decimalint_accuracy(const ObCastMode cast_mode,
                              const ObDecimalInt *res_decint, const int32_t int_bytes,
                              const ObPrecision precision, const ObScale scale,
                              ObDecimalIntBuilder &res_val, int &warning);

int scale_const_decimalint_expr(const ObDecimalInt *decint, const int32_t int_bytes,
                                const ObScale in_scale, const ObScale out_scale,
                                const ObPrecision out_prec, const ObCastMode cast_mode,
                                ObDecimalIntBuilder &res_val);
}  // namespace decint_scale
}  // namespace common
}  // namespace oceanbase
#endif
