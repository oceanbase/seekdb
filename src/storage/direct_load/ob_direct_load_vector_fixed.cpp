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

int ObDirectLoadVectorUtils::new_vector_fixed(VecValueTypeClass value_tc,
                                        ObIAllocator &allocator, ObIVector *&vector)
{
  int ret = OB_SUCCESS;
  vector = nullptr;
  switch (value_tc) {
#define FIXED_VECTOR_INIT_SWITCH(value_tc)                                                        \
  case value_tc: {                                                                                \
    using VecType = typename common::RTVectorTraits<VEC_FIXED, value_tc>::VectorType;       \
    vector = OB_NEWx(VecType, &allocator, nullptr, nullptr);                                      \
    break;                                                                                        \
  }
  FIXED_VECTOR_INIT_SWITCH(VEC_TC_INTEGER);
  FIXED_VECTOR_INIT_SWITCH(VEC_TC_UINTEGER);
  FIXED_VECTOR_INIT_SWITCH(VEC_TC_FLOAT);
  FIXED_VECTOR_INIT_SWITCH(VEC_TC_DOUBLE);
  FIXED_VECTOR_INIT_SWITCH(VEC_TC_FIXED_DOUBLE);
  FIXED_VECTOR_INIT_SWITCH(VEC_TC_DATETIME);
  FIXED_VECTOR_INIT_SWITCH(VEC_TC_DATE);
  FIXED_VECTOR_INIT_SWITCH(VEC_TC_TIME);
  FIXED_VECTOR_INIT_SWITCH(VEC_TC_YEAR);
  FIXED_VECTOR_INIT_SWITCH(VEC_TC_UNKNOWN);
  FIXED_VECTOR_INIT_SWITCH(VEC_TC_BIT);
  FIXED_VECTOR_INIT_SWITCH(VEC_TC_ENUM_SET);
  FIXED_VECTOR_INIT_SWITCH(VEC_TC_TIMESTAMP_TZ);
  FIXED_VECTOR_INIT_SWITCH(VEC_TC_TIMESTAMP_TINY);
  FIXED_VECTOR_INIT_SWITCH(VEC_TC_INTERVAL_YM);
  FIXED_VECTOR_INIT_SWITCH(VEC_TC_INTERVAL_DS);
  FIXED_VECTOR_INIT_SWITCH(VEC_TC_DEC_INT32);
  FIXED_VECTOR_INIT_SWITCH(VEC_TC_DEC_INT64);
  FIXED_VECTOR_INIT_SWITCH(VEC_TC_DEC_INT128);
  FIXED_VECTOR_INIT_SWITCH(VEC_TC_DEC_INT256);
  FIXED_VECTOR_INIT_SWITCH(VEC_TC_DEC_INT512);
  FIXED_VECTOR_INIT_SWITCH(VEC_TC_MYSQL_DATETIME);
  FIXED_VECTOR_INIT_SWITCH(VEC_TC_MYSQL_DATE);
#undef FIXED_VECTOR_INIT_SWITCH
  default:
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("unexpected fixed vector value type class", KR(ret), K(value_tc));
    break;
  }
  return ret;
}

} // namespace storage
} // namespace oceanbase
