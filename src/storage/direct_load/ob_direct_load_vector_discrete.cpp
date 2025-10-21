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
