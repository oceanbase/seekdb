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
// moved down from sql ObDatumCast: decimal-int scaling/precision check(pure wide:: math with a single implementation);
// user truncation warningthrough the WarnFn callback,sql statically registers the exec_ctx adapter(share does not touch sql types)。
#include "common/object/ob_object.h"
#include "common/wide_integer/ob_wide_integer_helper.h"
#include "share/datum/ob_datum_funcs.h"
#include "share/object/ob_obj_cast.h"  // ObCastMode

namespace oceanbase
{
namespace common
{
namespace decint_scale
{
typedef void (*WarnFn)(const void *payload, const int64_t code,
                       const ObString &type_str, const ObString &input, const ObCastMode cast_mode);
extern WarnFn g_warn_from_exec_ctx;   // sql ob_datum_cast.cpp statically registered(payload=ObExecContext*)

inline bool need_scale(const ObScale in_scale, const int32_t in_bytes,
                       const ObScale out_scale, const int32_t out_bytes)
{
  return (in_scale != out_scale) || (in_bytes != out_bytes);
}

int scale_decimalint(const ObDecimalInt *decint, const int32_t int_bytes,
                     const ObScale in_scale, const ObScale out_scale,
                     const ObPrecision out_prec, const ObCastMode cast_mode,
                     ObDecimalIntBuilder &val,
                     const void *warn_payload, WarnFn warn_fn);

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
