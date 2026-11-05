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

// q share/vector VectorCmpExprFuncsHelper::get_cmp_set pure forwarding declaration
// (primitive-argument variant,impl lives in expr_cmp_func.cpp constructs sql::ObDatumMeta;for share-base callers)
#ifndef OCEANBASE_SHARE_VECTOR_OB_VECTOR_CMP_FUNC_BASIC_H_
#define OCEANBASE_SHARE_VECTOR_OB_VECTOR_CMP_FUNC_BASIC_H_

#include "share/datum/ob_datum_funcs.h"

namespace oceanbase
{
namespace common
{

void ob_vector_cmp_get_cmp_set(const ObObjType type, const ObCollationType cs_type,
                               const int8_t scale, const int16_t precision,
                               NullSafeRowCmpFunc &null_first_cmp,
                               NullSafeRowCmpFunc &null_last_cmp);

} // namespace common
} // namespace oceanbase
#endif
