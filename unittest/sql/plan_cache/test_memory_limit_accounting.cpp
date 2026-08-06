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

#include "lib/rc/context.h"
#include "sql/engine/ob_sql_mem_mgr_processor.h"
#include "sql/plan_cache/ob_i_lib_cache_node.h"
#include "sql/plan_cache/ob_i_lib_cache_object.h"
#include "sql/plan_cache/ob_plan_cache.h"
#include "sql/plan_cache/ob_prepare_stmt_struct.h"

#define private public
#include "sql/plan_cache/ob_ps_cache.h"
#undef private

namespace oceanbase
{
namespace sql
{

using common::ObCtxIds;
using common::ObMallocAllocator;
using lib::ContextParam;
using lib::MemoryContext;

class TestMemoryLimitAccounting : public ::testing::Test
{
public:
  static void SetUpTestSuite()
  {
    ASSERT_EQ(common::OB_SUCCESS,
              ObMallocAllocator::get_instance()->create_and_add_tenant_allocator());
  }

  static MemoryContext create_context(const char *label)
  {
    MemoryContext context;
    ContextParam param;
    param.set_properties(lib::USE_TL_PAGE_OPTIONAL)
        .set_mem_attr(label, ObCtxIds::PLAN_CACHE_CTX_ID);
    EXPECT_EQ(common::OB_SUCCESS, ROOT_CONTEXT->CREATE_CONTEXT(context, param));
    return context;
  }
};

class TestLibCacheNode : public ObILibCacheNode
{
public:
  TestLibCacheNode(ObPlanCache *plan_cache, MemoryContext &context)
    : ObILibCacheNode(plan_cache, context)
  {}

protected:
  int inner_get_cache_obj(ObILibCacheCtx &ctx,
                          ObILibCacheKey *key,
                          ObILibCacheObject *&cache_obj) override
  {
    static_cast<void>(ctx);
    static_cast<void>(key);
    static_cast<void>(cache_obj);
    return common::OB_NOT_SUPPORTED;
  }

  int inner_add_cache_obj(ObILibCacheCtx &ctx,
                          ObILibCacheKey *key,
                          ObILibCacheObject *cache_obj) override
  {
    static_cast<void>(ctx);
    static_cast<void>(key);
    static_cast<void>(cache_obj);
    return common::OB_NOT_SUPPORTED;
  }
};

TEST_F(TestMemoryLimitAccounting, plan_object_and_node_release_at_final_lifetime)
{
  ObPlanCache plan_cache;
  MemoryContext object_context = create_context("PlanObjectAccounting");
  MemoryContext node_context = create_context("PlanNodeAccounting");
  ASSERT_NE(nullptr, object_context.ref_context());
  ASSERT_NE(nullptr, node_context.ref_context());

  {
    ObILibCacheObject object(NS_CRSR, object_context);
    ASSERT_NE(nullptr, object.get_allocator().alloc(128));
    const int64_t object_charge =
        object.get_mem_size() + MemoryContext::metadata_size();
    ASSERT_GT(object_charge, MemoryContext::metadata_size());

    plan_cache.account_cache_object(object);
    EXPECT_EQ(object_charge, plan_cache.get_managed_used());
    plan_cache.account_cache_object(object);
    EXPECT_EQ(object_charge, plan_cache.get_managed_used());

    TestLibCacheNode node(&plan_cache, node_context);
    ASSERT_NE(nullptr, node.get_mem_context()->get_safe_arena_allocator().alloc(256));
    const int64_t node_charge =
        node.get_own_mem_size() + MemoryContext::metadata_size();
    plan_cache.refresh_cache_node(node);
    EXPECT_EQ(object_charge + node_charge, plan_cache.get_managed_used());

    // Logical eviction does not release either charge. Final destruction does.
    EXPECT_EQ(object_charge + node_charge, plan_cache.get_managed_used());
    plan_cache.release_cache_node(node);
    EXPECT_EQ(object_charge, plan_cache.get_managed_used());
    plan_cache.release_cache_object(object);
    EXPECT_EQ(0, plan_cache.get_managed_used());
    plan_cache.release_cache_object(object);
    EXPECT_EQ(0, plan_cache.get_managed_used());
  }

  DESTROY_CONTEXT(node_context);
  DESTROY_CONTEXT(object_context);
}

TEST_F(TestMemoryLimitAccounting, ps_entry_and_live_object_have_separate_charges)
{
  common::ObArenaAllocator allocator;
  ObPsCache ps_cache;
  ObPsStmtItem item(&allocator, &allocator);
  ObPsStmtInfo info(&allocator, &allocator);
  const int64_t item_size = 123;
  const int64_t info_size = 456;

  ps_cache.account_stmt_item(item, item_size);
  ps_cache.account_stmt_info(info, info_size);
  const int64_t item_entry_charge = ObPsCache::stmt_id_entry_charge();
  const int64_t info_entry_charge = ObPsCache::stmt_info_entry_charge();
  EXPECT_EQ(item_size + info_size + item_entry_charge + info_entry_charge,
            ps_cache.get_managed_used());

  // Removing map entries leaves objects charged while external references live.
  ps_cache.release_managed_memory(item_entry_charge + info_entry_charge);
  EXPECT_EQ(item_size + info_size, ps_cache.get_managed_used());
  item.release_memory_account();
  EXPECT_EQ(info_size, ps_cache.get_managed_used());
  info.release_memory_account();
  EXPECT_EQ(0, ps_cache.get_managed_used());
}

TEST_F(TestMemoryLimitAccounting, ps_failed_insert_rolls_back_object_and_entry)
{
  common::ObArenaAllocator allocator;
  ObPsCache ps_cache;
  ObPsStmtItem item(&allocator, &allocator);
  ObPsStmtInfo info(&allocator, &allocator);

  ps_cache.account_stmt_item(item, 321);
  ps_cache.rollback_stmt_item(item);
  EXPECT_EQ(0, ps_cache.get_managed_used());

  ps_cache.account_stmt_info(info, 654);
  ps_cache.rollback_stmt_info(info);
  EXPECT_EQ(0, ps_cache.get_managed_used());
}

TEST_F(TestMemoryLimitAccounting, workarea_tracks_sub_megabyte_allocations)
{
  ObSqlWorkAreaProfile profile(ObSqlWorkAreaType::SORT_WORK_AREA);
  {
    ObSqlMemMgrProcessor processor(profile);
    const int64_t small_alloc = 64L << 10;
    processor.alloc(small_alloc);
    processor.alloc(small_alloc);
    EXPECT_EQ(2 * small_alloc, profile.get_profile_data_used());
    EXPECT_EQ(2 * small_alloc, profile.get_profile_total_used());

    ASSERT_EQ(common::OB_SUCCESS, processor.update_used_mem_size(3 * small_alloc));
    EXPECT_EQ(3 * small_alloc, profile.get_profile_total_used());
    processor.free(small_alloc);
    EXPECT_EQ(small_alloc, profile.get_profile_data_used());
    EXPECT_EQ(2 * small_alloc, profile.get_profile_total_used());
  }
  EXPECT_EQ(0, profile.get_profile_data_used());
  EXPECT_EQ(0, profile.get_profile_total_used());
}

} // namespace sql
} // namespace oceanbase

int main(int argc, char **argv)
{
  OB_LOGGER.set_file_name("test_memory_limit_accounting.log", true);
  testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
