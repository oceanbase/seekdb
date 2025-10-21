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

#include "ob_uniform_vector.ipp"
#include "sql/engine/expr/ob_array_expr_utils.h"
// init uniform const vectors

namespace oceanbase
{
namespace common
{
DEF_SET_COLLECTION_PAYLOAD(false);
template class ObUniformVector<false, VectorBasicOp<VEC_TC_NULL>>;
template class ObUniformVector<false, VectorBasicOp<VEC_TC_INTEGER>>;
template class ObUniformVector<false, VectorBasicOp<VEC_TC_UINTEGER>>;
template class ObUniformVector<false, VectorBasicOp<VEC_TC_FLOAT>>;
template class ObUniformVector<false, VectorBasicOp<VEC_TC_DOUBLE>>;
template class ObUniformVector<false, VectorBasicOp<VEC_TC_FIXED_DOUBLE>>;
template class ObUniformVector<false, VectorBasicOp<VEC_TC_NUMBER>>;
template class ObUniformVector<false, VectorBasicOp<VEC_TC_DATETIME>>;
template class ObUniformVector<false, VectorBasicOp<VEC_TC_DATE>>;
template class ObUniformVector<false, VectorBasicOp<VEC_TC_TIME>>;
template class ObUniformVector<false, VectorBasicOp<VEC_TC_YEAR>>;
template class ObUniformVector<false, VectorBasicOp<VEC_TC_EXTEND>>;
template class ObUniformVector<false, VectorBasicOp<VEC_TC_UNKNOWN>>;
template class ObUniformVector<false, VectorBasicOp<VEC_TC_STRING>>;
template class ObUniformVector<false, VectorBasicOp<VEC_TC_BIT>>;
template class ObUniformVector<false, VectorBasicOp<VEC_TC_ENUM_SET>>;
template class ObUniformVector<false, VectorBasicOp<VEC_TC_ENUM_SET_INNER>>;
template class ObUniformVector<false, VectorBasicOp<VEC_TC_TIMESTAMP_TZ>>;
template class ObUniformVector<false, VectorBasicOp<VEC_TC_TIMESTAMP_TINY>>;
template class ObUniformVector<false, VectorBasicOp<VEC_TC_RAW>>;
template class ObUniformVector<false, VectorBasicOp<VEC_TC_INTERVAL_YM>>;
template class ObUniformVector<false, VectorBasicOp<VEC_TC_INTERVAL_DS>>;
template class ObUniformVector<false, VectorBasicOp<VEC_TC_ROWID>>;
template class ObUniformVector<false, VectorBasicOp<VEC_TC_LOB>>;
template class ObUniformVector<false, VectorBasicOp<VEC_TC_JSON>>;
template class ObUniformVector<false, VectorBasicOp<VEC_TC_GEO>>;
template class ObUniformVector<false, VectorBasicOp<VEC_TC_UDT>>;
template class ObUniformVector<false, VectorBasicOp<VEC_TC_DEC_INT32>>;
template class ObUniformVector<false, VectorBasicOp<VEC_TC_DEC_INT64>>;
template class ObUniformVector<false, VectorBasicOp<VEC_TC_DEC_INT128>>;
template class ObUniformVector<false, VectorBasicOp<VEC_TC_DEC_INT256>>;
template class ObUniformVector<false, VectorBasicOp<VEC_TC_DEC_INT512>>;
template class ObUniformVector<false, VectorBasicOp<VEC_TC_COLLECTION>>;
template class ObUniformVector<false, VectorBasicOp<VEC_TC_MYSQL_DATETIME>>;
template class ObUniformVector<false, VectorBasicOp<VEC_TC_MYSQL_DATE>>;
template class ObUniformVector<false, VectorBasicOp<VEC_TC_ROARINGBITMAP>>;
} // end common
} // end
