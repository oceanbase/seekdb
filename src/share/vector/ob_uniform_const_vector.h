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

#ifndef OCEANBASE_SHARE_VECTOR_OB_UNIFORM_CONST_VECTOR_H_
#define OCEANBASE_SHARE_VECTOR_OB_UNIFORM_CONST_VECTOR_H_

#include "share/vector/ob_uniform_vector.h"

namespace oceanbase
{
namespace common
{
extern template class ObUniformVector<false, VectorBasicOp<VEC_TC_NULL>>;
extern template class ObUniformVector<false, VectorBasicOp<VEC_TC_INTEGER>>;
extern template class ObUniformVector<false, VectorBasicOp<VEC_TC_UINTEGER>>;
extern template class ObUniformVector<false, VectorBasicOp<VEC_TC_FLOAT>>;
extern template class ObUniformVector<false, VectorBasicOp<VEC_TC_DOUBLE>>;
extern template class ObUniformVector<false, VectorBasicOp<VEC_TC_FIXED_DOUBLE>>;
extern template class ObUniformVector<false, VectorBasicOp<VEC_TC_NUMBER>>;
extern template class ObUniformVector<false, VectorBasicOp<VEC_TC_DATETIME>>;
extern template class ObUniformVector<false, VectorBasicOp<VEC_TC_DATE>>;
extern template class ObUniformVector<false, VectorBasicOp<VEC_TC_TIME>>;
extern template class ObUniformVector<false, VectorBasicOp<VEC_TC_YEAR>>;
extern template class ObUniformVector<false, VectorBasicOp<VEC_TC_EXTEND>>;
extern template class ObUniformVector<false, VectorBasicOp<VEC_TC_UNKNOWN>>;
extern template class ObUniformVector<false, VectorBasicOp<VEC_TC_STRING>>;
extern template class ObUniformVector<false, VectorBasicOp<VEC_TC_BIT>>;
extern template class ObUniformVector<false, VectorBasicOp<VEC_TC_ENUM_SET>>;
extern template class ObUniformVector<false, VectorBasicOp<VEC_TC_ENUM_SET_INNER>>;
extern template class ObUniformVector<false, VectorBasicOp<VEC_TC_TIMESTAMP_TZ>>;
extern template class ObUniformVector<false, VectorBasicOp<VEC_TC_TIMESTAMP_TINY>>;
extern template class ObUniformVector<false, VectorBasicOp<VEC_TC_RAW>>;
extern template class ObUniformVector<false, VectorBasicOp<VEC_TC_INTERVAL_YM>>;
extern template class ObUniformVector<false, VectorBasicOp<VEC_TC_INTERVAL_DS>>;
extern template class ObUniformVector<false, VectorBasicOp<VEC_TC_ROWID>>;
extern template class ObUniformVector<false, VectorBasicOp<VEC_TC_LOB>>;
extern template class ObUniformVector<false, VectorBasicOp<VEC_TC_JSON>>;
extern template class ObUniformVector<false, VectorBasicOp<VEC_TC_GEO>>;
extern template class ObUniformVector<false, VectorBasicOp<VEC_TC_UDT>>;
extern template class ObUniformVector<false, VectorBasicOp<VEC_TC_DEC_INT32>>;
extern template class ObUniformVector<false, VectorBasicOp<VEC_TC_DEC_INT64>>;
extern template class ObUniformVector<false, VectorBasicOp<VEC_TC_DEC_INT128>>;
extern template class ObUniformVector<false, VectorBasicOp<VEC_TC_DEC_INT256>>;
extern template class ObUniformVector<false, VectorBasicOp<VEC_TC_DEC_INT512>>;
extern template class ObUniformVector<false, VectorBasicOp<VEC_TC_COLLECTION>>;
extern template class ObUniformVector<false, VectorBasicOp<VEC_TC_MYSQL_DATETIME>>;
extern template class ObUniformVector<false, VectorBasicOp<VEC_TC_MYSQL_DATE>>;
extern template class ObUniformVector<false, VectorBasicOp<VEC_TC_ROARINGBITMAP>>;
} // end namespace common
} // end namespace oceanbase

#endif // OCEANBASE_SHARE_VECTOR_OB_UNIFORM_CONST_VECTOR_H_
