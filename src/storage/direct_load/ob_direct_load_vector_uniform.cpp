/**
 * Copyright (c) 2024 OceanBase
 * OceanBase CE is licensed under Mulan PubL v2.
 * You can use this software according to the terms and conditions of the Mulan PubL v2.
 * You may obtain a copy of Mulan PubL v2 at:
 *          http://license.coscl.org.cn/MulanPubL-2.0
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND,
 * EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
 * MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
 * See the Mulan PubL v2 for more details.
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

int ObDirectLoadVectorUtils::new_vector_uniform(VecValueTypeClass value_tc,
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
#define UNIFORM_VECTOR_INIT_SWITCH(value_tc)                            \
  case value_tc: {                                                      \
    using VecType = typename common::RTVectorTraits<VEC_UNIFORM, value_tc>::VectorType;       \
    vector = OB_NEWx(VecType, &allocator, nullptr, eval_info);          \
    break;                                                              \
  }
    UNIFORM_VECTOR_INIT_SWITCH(VEC_TC_NULL);
    UNIFORM_VECTOR_INIT_SWITCH(VEC_TC_INTEGER);
    UNIFORM_VECTOR_INIT_SWITCH(VEC_TC_UINTEGER);
    UNIFORM_VECTOR_INIT_SWITCH(VEC_TC_FLOAT);
    UNIFORM_VECTOR_INIT_SWITCH(VEC_TC_DOUBLE);
    UNIFORM_VECTOR_INIT_SWITCH(VEC_TC_FIXED_DOUBLE);
    UNIFORM_VECTOR_INIT_SWITCH(VEC_TC_DATETIME);
    UNIFORM_VECTOR_INIT_SWITCH(VEC_TC_DATE);
    UNIFORM_VECTOR_INIT_SWITCH(VEC_TC_TIME);
    UNIFORM_VECTOR_INIT_SWITCH(VEC_TC_YEAR);
    UNIFORM_VECTOR_INIT_SWITCH(VEC_TC_UNKNOWN);
    UNIFORM_VECTOR_INIT_SWITCH(VEC_TC_BIT);
    UNIFORM_VECTOR_INIT_SWITCH(VEC_TC_ENUM_SET);
    UNIFORM_VECTOR_INIT_SWITCH(VEC_TC_TIMESTAMP_TZ);
    UNIFORM_VECTOR_INIT_SWITCH(VEC_TC_TIMESTAMP_TINY);
    UNIFORM_VECTOR_INIT_SWITCH(VEC_TC_INTERVAL_YM);
    UNIFORM_VECTOR_INIT_SWITCH(VEC_TC_INTERVAL_DS);
    UNIFORM_VECTOR_INIT_SWITCH(VEC_TC_DEC_INT32);
    UNIFORM_VECTOR_INIT_SWITCH(VEC_TC_DEC_INT64);
    UNIFORM_VECTOR_INIT_SWITCH(VEC_TC_DEC_INT128);
    UNIFORM_VECTOR_INIT_SWITCH(VEC_TC_DEC_INT256);
    UNIFORM_VECTOR_INIT_SWITCH(VEC_TC_DEC_INT512);
    UNIFORM_VECTOR_INIT_SWITCH(VEC_TC_NUMBER);
    UNIFORM_VECTOR_INIT_SWITCH(VEC_TC_EXTEND);
    UNIFORM_VECTOR_INIT_SWITCH(VEC_TC_STRING);
    UNIFORM_VECTOR_INIT_SWITCH(VEC_TC_ENUM_SET_INNER);
    UNIFORM_VECTOR_INIT_SWITCH(VEC_TC_RAW);
    UNIFORM_VECTOR_INIT_SWITCH(VEC_TC_ROWID);
    UNIFORM_VECTOR_INIT_SWITCH(VEC_TC_LOB);
    UNIFORM_VECTOR_INIT_SWITCH(VEC_TC_JSON);
    UNIFORM_VECTOR_INIT_SWITCH(VEC_TC_GEO);
    UNIFORM_VECTOR_INIT_SWITCH(VEC_TC_UDT);
    UNIFORM_VECTOR_INIT_SWITCH(VEC_TC_COLLECTION);
    UNIFORM_VECTOR_INIT_SWITCH(VEC_TC_MYSQL_DATETIME);
    UNIFORM_VECTOR_INIT_SWITCH(VEC_TC_MYSQL_DATE);
    UNIFORM_VECTOR_INIT_SWITCH(VEC_TC_ROARINGBITMAP);
#undef UNIFORM_VECTOR_INIT_SWITCH
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
