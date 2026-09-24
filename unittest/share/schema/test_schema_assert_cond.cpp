/*
 * Copyright (c) 2026 OceanBase.
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

#include <gtest/gtest.h>

#include "share/schema/ob_schema_cache.h"
#include "share/schema/ob_schema_mem_mgr.h"

namespace oceanbase
{
namespace share
{
namespace schema
{
using namespace common;

TEST(SchemaAssertCond, tablet_cache_copy_preserves_buffer_validation)
{
  ObTabletCacheKey source(ObTabletID(1001), 1);
  alignas(ObTabletCacheKey) char key_buffer[sizeof(ObTabletCacheKey)];
  ObIKVCacheKey *key = nullptr;
  EXPECT_EQ(OB_INVALID_ARGUMENT, source.deep_copy(nullptr, sizeof(key_buffer), key));
  EXPECT_EQ(OB_INVALID_ARGUMENT, source.deep_copy(key_buffer, sizeof(key_buffer) - 1, key));
  EXPECT_EQ(nullptr, key);
  ASSERT_EQ(OB_SUCCESS, source.deep_copy(key_buffer, sizeof(key_buffer), key));
  ASSERT_NE(nullptr, key);
  EXPECT_TRUE(source == *key);
  static_cast<ObTabletCacheKey *>(key)->~ObTabletCacheKey();

  ObTabletCacheValue source_value(1001);
  alignas(ObTabletCacheValue) char value_buffer[sizeof(ObTabletCacheValue)];
  ObIKVCacheValue *value = nullptr;
  EXPECT_EQ(OB_INVALID_ARGUMENT, source_value.deep_copy(nullptr, sizeof(value_buffer), value));
  EXPECT_EQ(OB_INVALID_ARGUMENT, source_value.deep_copy(value_buffer, sizeof(value_buffer) - 1, value));
  EXPECT_EQ(nullptr, value);
  ASSERT_EQ(OB_SUCCESS, source_value.deep_copy(value_buffer, sizeof(value_buffer), value));
  ASSERT_NE(nullptr, value);
  EXPECT_EQ(1001, static_cast<ObTabletCacheValue *>(value)->get_table_id());
  static_cast<ObTabletCacheValue *>(value)->~ObTabletCacheValue();
}

TEST(SchemaAssertCond, schema_allocation_preserves_uninitialized_error)
{
  ObSchemaMemMgr mgr;
  ObSchemaMgr *schema = nullptr;
  EXPECT_EQ(OB_INNER_STAT_ERROR, mgr.alloc_schema_mgr(schema));
  EXPECT_EQ(nullptr, schema);
  ASSERT_EQ(OB_SUCCESS, mgr.init("AssertCondTest"));
  ASSERT_EQ(OB_SUCCESS, mgr.alloc_schema_mgr(schema));
  ASSERT_NE(nullptr, schema);
  EXPECT_EQ(OB_SUCCESS, mgr.free_schema_mgr(schema));
  EXPECT_EQ(nullptr, schema);
}

} // namespace schema
} // namespace share
} // namespace oceanbase
