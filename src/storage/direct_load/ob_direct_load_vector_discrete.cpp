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

int ObDirectLoadVectorUtils::new_vector_discrete(VecValueTypeClass value_tc,
                                        ObIAllocator &allocator, ObIVector *&vector)
{
  int ret = OB_SUCCESS;
  vector = nullptr;
  switch (value_tc) {
#define DISCRETE_VECTOR_INIT_SWITCH(value_tc)                           \
  case value_tc: {                                                      \
    using VecType = typename common::RTVectorTraits<VEC_DISCRETE, value_tc>::VectorType;       \
    vector = OB_NEWx(VecType, &allocator, nullptr, nullptr, nullptr);   \
    break;                                                              \
  }
  DISCRETE_VECTOR_INIT_SWITCH(VEC_TC_NUMBER);
  DISCRETE_VECTOR_INIT_SWITCH(VEC_TC_EXTEND);
  DISCRETE_VECTOR_INIT_SWITCH(VEC_TC_STRING);
  DISCRETE_VECTOR_INIT_SWITCH(VEC_TC_ENUM_SET_INNER);
  DISCRETE_VECTOR_INIT_SWITCH(VEC_TC_RAW);
  DISCRETE_VECTOR_INIT_SWITCH(VEC_TC_ROWID);
  DISCRETE_VECTOR_INIT_SWITCH(VEC_TC_LOB);
  DISCRETE_VECTOR_INIT_SWITCH(VEC_TC_JSON);
  DISCRETE_VECTOR_INIT_SWITCH(VEC_TC_GEO);
  DISCRETE_VECTOR_INIT_SWITCH(VEC_TC_UDT);
  DISCRETE_VECTOR_INIT_SWITCH(VEC_TC_COLLECTION);
  DISCRETE_VECTOR_INIT_SWITCH(VEC_TC_ROARINGBITMAP);
#undef DISCRETE_VECTOR_INIT_SWITCH
    default:
      ret = OB_ERR_UNEXPECTED;
      SQL_LOG(WARN, "unexpected vector value type class", KR(ret), K(value_tc));
      break;
  }
  return ret;
}

} // namespace storage
} // namespace oceanbase
