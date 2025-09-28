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

int ObDirectLoadVectorUtils::new_vector_continuous(VecValueTypeClass value_tc,
                                        ObIAllocator &allocator, ObIVector *&vector)
{
  int ret = OB_SUCCESS;
  vector = nullptr;
  switch (value_tc) {
#define CONTINUOUS_VECTOR_INIT_SWITCH(value_tc)                         \
  case value_tc: {                                                      \
    using VecType = typename common::RTVectorTraits<VEC_CONTINUOUS, value_tc>::VectorType;       \
    vector = OB_NEWx(VecType, &allocator, nullptr, nullptr, nullptr);   \
    break;                                                              \
  }
  CONTINUOUS_VECTOR_INIT_SWITCH(VEC_TC_NUMBER);
  CONTINUOUS_VECTOR_INIT_SWITCH(VEC_TC_EXTEND);
  CONTINUOUS_VECTOR_INIT_SWITCH(VEC_TC_STRING);
  CONTINUOUS_VECTOR_INIT_SWITCH(VEC_TC_ENUM_SET_INNER);
  CONTINUOUS_VECTOR_INIT_SWITCH(VEC_TC_RAW);
  CONTINUOUS_VECTOR_INIT_SWITCH(VEC_TC_ROWID);
  CONTINUOUS_VECTOR_INIT_SWITCH(VEC_TC_LOB);
  CONTINUOUS_VECTOR_INIT_SWITCH(VEC_TC_JSON);
  CONTINUOUS_VECTOR_INIT_SWITCH(VEC_TC_GEO);
  CONTINUOUS_VECTOR_INIT_SWITCH(VEC_TC_UDT);
  CONTINUOUS_VECTOR_INIT_SWITCH(VEC_TC_COLLECTION);
  CONTINUOUS_VECTOR_INIT_SWITCH(VEC_TC_ROARINGBITMAP);
#undef CONTINUOUS_VECTOR_INIT_SWITCH
  default:
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("unexpected continuous vector value type class", KR(ret), K(value_tc));
    break;
  }
  return ret;
}

} // namespace storage
} // namespace oceanbase
