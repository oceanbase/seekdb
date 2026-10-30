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

#include <cstring>
#include <memory>
#include <gtest/gtest.h>
#include "sql/engine/expr/ob_expr_operator.h"
#include "sql/engine/ob_exec_context.h"
#include "sql/pl/ob_pl_user_type.h"

using namespace oceanbase::common;
using namespace oceanbase::sql;
using namespace oceanbase::pl;

namespace {
void collection_value(ObDatum &value, char *buf, ObPLCollection *collection, bool legacy)
{
  const int64_t address = reinterpret_cast<int64_t>(collection);
  const int32_t size = sizeof(ObPLCollection);
  value.ptr_ = buf;
  if (!legacy) {
    value.set_extend(address, PL_NESTED_TABLE_TYPE, size);
  } else {
    memset(buf, 0, DATUM_LEGACY_EXTEND_SIZE);
    buf[0] = static_cast<char>(ObExtendType);
    buf[1] = static_cast<char>(CS_LEVEL_INVALID);
    buf[3] = static_cast<char>(PL_NESTED_TABLE_TYPE);
    memcpy(buf + 4, &size, sizeof(size));
    memcpy(buf + 8, &address, sizeof(address));
    value.set_ext(DATUM_LEGACY_EXTEND_SIZE);
  }
}
} // namespace

TEST(DatumCollectionCompare, native_and_old_collections_preserve_null_and_empty_results)
{
  ObArenaAllocator allocator;
  std::unique_ptr<ObExecContext> ctx(new ObExecContext(allocator));
  for (bool legacy : {false, true}) {
    ObPLCollection left(PL_NESTED_TABLE_TYPE, 1), right(PL_NESTED_TABLE_TYPE, 1);
    ObDataType element;
    element.set_obj_type(ObIntType);
    left.set_element_type(element); right.set_element_type(element);
    char lbuf[DATUM_LEGACY_EXTEND_SIZE], rbuf[DATUM_LEGACY_EXTEND_SIZE];
    ObDatum l, r;
    collection_value(l, lbuf, &left, legacy);
    collection_value(r, rbuf, &right, legacy);
    CollectionPredRes result = COLL_PRED_INVALID;
    ASSERT_EQ(OB_SUCCESS, ObRelationalExprOperator::pl_udt_compare2(result, l, r, *ctx, CO_EQ));
    ASSERT_EQ(COLL_PRED_NULL, result);
    left.set_inited(); right.set_inited();
    ASSERT_EQ(OB_SUCCESS, ObRelationalExprOperator::pl_udt_compare2(result, l, r, *ctx, CO_EQ));
    ASSERT_EQ(COLL_PRED_TRUE, result);
    ASSERT_EQ(OB_SUCCESS, ObRelationalExprOperator::pl_udt_compare2(result, l, r, *ctx, CO_NE));
    ASSERT_EQ(COLL_PRED_FALSE, result);
  }
}

TEST(DatumCollectionCompare, null_collection_pointer_is_rejected)
{
  ObArenaAllocator allocator;
  std::unique_ptr<ObExecContext> ctx(new ObExecContext(allocator));
  char lbuf[DATUM_LEGACY_EXTEND_SIZE], rbuf[DATUM_LEGACY_EXTEND_SIZE];
  ObDatum l, r;
  collection_value(l, lbuf, nullptr, false);
  collection_value(r, rbuf, nullptr, true);
  CollectionPredRes result = COLL_PRED_INVALID;
  ASSERT_EQ(OB_ERR_UNEXPECTED, ObRelationalExprOperator::pl_udt_compare2(result, l, r, *ctx, CO_EQ));
}

int main(int argc, char **argv)
{
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
