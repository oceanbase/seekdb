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
#define USING_LOG_PREFIX STORAGE

#include "storage/direct_load/ob_direct_load_vector_utils.h"
#include "share/ob_tablet_autoincrement_param.h"
#include "share/schema/ob_table_param.h"
#include "share/vector/ob_continuous_vector.h"
#include "share/vector/ob_discrete_vector.h"
#include "share/vector/ob_fixed_length_vector.h"
#include "share/vector/ob_uniform_vector.h"
#include "storage/blocksstable/ob_storage_datum.h"
#include "storage/direct_load/ob_direct_load_batch_rows.h"

namespace oceanbase
{
namespace storage
{
using namespace blocksstable;
using namespace common;
using namespace share;
using namespace share::schema;
using namespace sql;

int ObDirectLoadVectorUtils::new_vector_uniform_const(VecValueTypeClass value_tc,
                                        ObIAllocator &allocator, ObIVector *&vector)
{
  int ret = OB_SUCCESS;
  vector = nullptr;
  ObEvalInfo *eval_info = nullptr;
  if (OB_ISNULL(eval_info = OB_NEWx(ObEvalInfo, &allocator))) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
    LOG_WARN("fail to new ObEvalInfo", KR(ret));
  } else {
    MEMSET(eval_info, 0, sizeof(ObEvalInfo));
    switch (value_tc) {
#define UNIFORM_CONST_VECTOR_INIT_SWITCH(value_tc)                      \
  case value_tc: {                                                      \
    using VecType = typename common::RTVectorTraits<VEC_UNIFORM_CONST, value_tc>::VectorType;       \
    vector = OB_NEWx(VecType, &allocator, nullptr, eval_info);          \
    break;                                                              \
  }
    UNIFORM_CONST_VECTOR_INIT_SWITCH(VEC_TC_NULL);
    UNIFORM_CONST_VECTOR_INIT_SWITCH(VEC_TC_INTEGER);
    UNIFORM_CONST_VECTOR_INIT_SWITCH(VEC_TC_UINTEGER);
    UNIFORM_CONST_VECTOR_INIT_SWITCH(VEC_TC_FLOAT);
    UNIFORM_CONST_VECTOR_INIT_SWITCH(VEC_TC_DOUBLE);
    UNIFORM_CONST_VECTOR_INIT_SWITCH(VEC_TC_FIXED_DOUBLE);
    UNIFORM_CONST_VECTOR_INIT_SWITCH(VEC_TC_DATETIME);
    UNIFORM_CONST_VECTOR_INIT_SWITCH(VEC_TC_DATE);
    UNIFORM_CONST_VECTOR_INIT_SWITCH(VEC_TC_TIME);
    UNIFORM_CONST_VECTOR_INIT_SWITCH(VEC_TC_YEAR);
    UNIFORM_CONST_VECTOR_INIT_SWITCH(VEC_TC_UNKNOWN);
    UNIFORM_CONST_VECTOR_INIT_SWITCH(VEC_TC_BIT);
    UNIFORM_CONST_VECTOR_INIT_SWITCH(VEC_TC_ENUM_SET);
    UNIFORM_CONST_VECTOR_INIT_SWITCH(VEC_TC_TIMESTAMP_TZ);
    UNIFORM_CONST_VECTOR_INIT_SWITCH(VEC_TC_TIMESTAMP_TINY);
    UNIFORM_CONST_VECTOR_INIT_SWITCH(VEC_TC_INTERVAL_YM);
    UNIFORM_CONST_VECTOR_INIT_SWITCH(VEC_TC_INTERVAL_DS);
    UNIFORM_CONST_VECTOR_INIT_SWITCH(VEC_TC_DEC_INT32);
    UNIFORM_CONST_VECTOR_INIT_SWITCH(VEC_TC_DEC_INT64);
    UNIFORM_CONST_VECTOR_INIT_SWITCH(VEC_TC_DEC_INT128);
    UNIFORM_CONST_VECTOR_INIT_SWITCH(VEC_TC_DEC_INT256);
    UNIFORM_CONST_VECTOR_INIT_SWITCH(VEC_TC_DEC_INT512);
    UNIFORM_CONST_VECTOR_INIT_SWITCH(VEC_TC_NUMBER);
    UNIFORM_CONST_VECTOR_INIT_SWITCH(VEC_TC_EXTEND);
    UNIFORM_CONST_VECTOR_INIT_SWITCH(VEC_TC_STRING);
    UNIFORM_CONST_VECTOR_INIT_SWITCH(VEC_TC_ENUM_SET_INNER);
    UNIFORM_CONST_VECTOR_INIT_SWITCH(VEC_TC_RAW);
    UNIFORM_CONST_VECTOR_INIT_SWITCH(VEC_TC_ROWID);
    UNIFORM_CONST_VECTOR_INIT_SWITCH(VEC_TC_LOB);
    UNIFORM_CONST_VECTOR_INIT_SWITCH(VEC_TC_JSON);
    UNIFORM_CONST_VECTOR_INIT_SWITCH(VEC_TC_GEO);
    UNIFORM_CONST_VECTOR_INIT_SWITCH(VEC_TC_UDT);
    UNIFORM_CONST_VECTOR_INIT_SWITCH(VEC_TC_COLLECTION);
    UNIFORM_CONST_VECTOR_INIT_SWITCH(VEC_TC_MYSQL_DATETIME);
    UNIFORM_CONST_VECTOR_INIT_SWITCH(VEC_TC_MYSQL_DATE);
    UNIFORM_CONST_VECTOR_INIT_SWITCH(VEC_TC_ROARINGBITMAP);
#undef UNIFORM_CONST_VECTOR_INIT_SWITCH
      default:
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("unexpected vector value type class", KR(ret), K(value_tc));
        break;
    }
  }
  return ret;
}

} // namespace storage
} // namespace oceanbase
