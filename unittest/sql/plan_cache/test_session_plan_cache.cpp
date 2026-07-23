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

#define USING_LOG_PREFIX SQL_PC
#include <gtest/gtest.h>
#include "lib/alloc/ob_malloc_allocator.h"
#include "lib/allocator/page_arena.h"
#include "observer/ob_req_time_service.h"
#include "share/rc/ob_tenant_base.h"
#include "sql/engine/ob_exec_context.h"
#include "sql/engine/ob_physical_plan.h"
#include "sql/ob_sql_init.h"
#include "sql/ob_sql_context.h"
#include "sql/plan_cache/ob_cache_object_factory.h"
#include "sql/plan_cache/ob_plan_cache.h"
#include "sql/plan_cache/ob_plan_cache_callback.h"
#include "sql/session/ob_sql_session_info.h"
#include "sql/session/ob_sql_session_mgr.h"

namespace oceanbase
{
namespace sql
{

class TestableSessionPlanCache : public ObPlanCache
{
public:
  using ObPlanCache::ref_alloc_plan;

  static void fill_test_key(const common::ObString &sql, ObPlanCacheKey &key)
  {
    key.name_ = sql;
    key.key_id_ = common::OB_INVALID_ID;
    key.db_id_ = common::OB_MOCK_DEFAULT_DATABASE_ID;
    key.mode_ = PC_PS_MODE;
    key.namespace_ = NS_CRSR;
  }

  static int add_test_plan(ObPlanCache &cache,
                           ObPlanCacheCtx &pc_ctx,
                           const ObPhyPlanType plan_type,
                           ObCacheObjGuard &guard,
                           ObCacheObjID &plan_id)
  {
    int ret = ObCacheObjectFactory::alloc(&cache, guard, NS_CRSR);
    if (OB_SUCC(ret)) {
      ObPhysicalPlan *plan =
          static_cast<ObPhysicalPlan *>(guard.get_cache_obj());
      plan->set_stmt_type(stmt::T_SELECT);
      plan->set_plan_type(plan_type);
      plan->set_signature(plan->get_object_id());
      fill_test_key(pc_ctx.raw_sql_, pc_ctx.fp_result_.pc_key_);
      plan_id = plan->get_object_id();
      ret = cache.add_plan_cache(pc_ctx, plan);
    }
    return ret;
  }

  static int access_test_node(ObPlanCache &cache,
                              const common::ObString &sql,
                              const int64_t *new_timestamp,
                              bool &exists)
  {
    int ret = common::OB_SUCCESS;
    ObPlanCacheKey key;
    ObILibCacheNode *node = NULL;
    ObLibCacheRlockAndRef lock_and_ref(LC_NODE_RD_HANDLE);
    fill_test_key(sql, key);
    if (OB_FAIL(cache.get_value(&key, node, lock_and_ref))) {
      // return the lookup error
    } else {
      exists = (NULL != node);
      if (exists && NULL != new_timestamp) {
        ATOMIC_STORE(
            &node->get_node_stat()->last_active_timestamp_, *new_timestamp);
      }
    }
    if (NULL != node) {
      (void)node->unlock();
      (void)node->dec_ref_count(LC_NODE_RD_HANDLE);
    }
    return ret;
  }
};

class TestSessionPlanCache : public ::testing::Test
{
public:
  static void SetUpTestCase()
  {
    lib::ObMallocAllocator *malloc = lib::ObMallocAllocator::get_instance();
    if (nullptr == malloc->get_tenant_ctx_allocator(0)) {
      ASSERT_EQ(common::OB_SUCCESS, malloc->create_and_add_tenant_allocator());
    }
    ASSERT_EQ(common::OB_SUCCESS, tenant_base_.init());
    share::ObTenantEnv::set_tenant(&tenant_base_);
    share::g_modules_ready = true;
    ASSERT_EQ(common::OB_SUCCESS, init_sql_factories());
  }

  static void TearDownTestCase()
  {
    share::g_modules_ready = false;
    share::ObTenantEnv::set_tenant(nullptr);
    tenant_base_.destroy();
  }

protected:
  static share::ObTenantBase tenant_base_;
};

share::ObTenantBase TestSessionPlanCache::tenant_base_;

class TestPlanCacheAddCtx
{
public:
  explicit TestPlanCacheAddCtx(ObSQLSessionInfo &session)
    : allocator_("SessPCCtx"), exec_ctx_(allocator_)
  {
    EXPECT_EQ(common::OB_SUCCESS, exec_ctx_.create_physical_plan_ctx());
    exec_ctx_.set_my_session(&session);
    exec_ctx_.set_sql_ctx(&sql_ctx_);
    sql_ctx_.session_info_ = &session;
    sql_ctx_.schema_guard_ = &schema_guard_;
    sql_ctx_.all_plan_const_param_constraints_ = &plan_constraints_;
    sql_ctx_.all_possible_const_param_constraints_ = &possible_constraints_;
    sql_ctx_.all_equal_param_constraints_ = &equal_constraints_;
    sql_ctx_.all_pre_calc_constraints_ = &pre_calc_constraints_;
  }

  int add(ObPlanCache &cache,
          const char *sql_text,
          const ObPhyPlanType plan_type,
          ObCacheObjGuard &guard,
          ObCacheObjID &plan_id)
  {
    common::ObString sql = common::ObString::make_string(sql_text);
    ObPlanCacheCtx pc_ctx(
        sql, PC_PS_MODE, allocator_, sql_ctx_, exec_ctx_);
    pc_ctx.fp_result_.cache_params_ =
        &exec_ctx_.get_physical_plan_ctx()->get_param_store_for_update();
    return TestableSessionPlanCache::add_test_plan(
        cache, pc_ctx, plan_type, guard, plan_id);
  }

private:
  common::ObArenaAllocator allocator_;
  share::schema::ObSchemaGetterGuard schema_guard_;
  ObSqlCtx sql_ctx_;
  ObExecContext exec_ctx_;
  common::ObSEArray<ObPCConstParamInfo, 1> plan_constraints_;
  common::ObSEArray<ObPCConstParamInfo, 1> possible_constraints_;
  common::ObSEArray<ObPCParamEqualInfo, 1> equal_constraints_;
  common::ObDList<ObPreCalcExprConstraint> pre_calc_constraints_;
};

TEST_F(TestSessionPlanCache, independent_caches_share_unique_object_id_counter)
{
  volatile ObCacheObjID next_object_id = 0;
  TestableSessionPlanCache first_cache;
  TestableSessionPlanCache second_cache;
  ASSERT_EQ(common::OB_SUCCESS, first_cache.init_session_cache(1001, &next_object_id));
  ASSERT_EQ(common::OB_SUCCESS, second_cache.init_session_cache(1002, &next_object_id));

  ObCacheObjGuard first_guard(PLAN_GEN_HANDLE);
  ObCacheObjGuard second_guard(PLAN_GEN_HANDLE);
  ASSERT_EQ(common::OB_SUCCESS,
            ObCacheObjectFactory::alloc(&first_cache, first_guard, NS_CRSR));
  ASSERT_EQ(common::OB_SUCCESS,
            ObCacheObjectFactory::alloc(&second_cache, second_guard, NS_CRSR));
  ASSERT_NE(nullptr, first_guard.get_cache_obj());
  ASSERT_NE(nullptr, second_guard.get_cache_obj());

  const ObCacheObjID first_id = first_guard.get_cache_obj()->get_object_id();
  const ObCacheObjID second_id = second_guard.get_cache_obj()->get_object_id();
  EXPECT_EQ(1, first_id);
  EXPECT_EQ(2, second_id);
  EXPECT_NE(first_id, second_id);
  EXPECT_EQ(&first_cache, first_guard.get_owner_cache());
  EXPECT_EQ(&second_cache, second_guard.get_owner_cache());
  EXPECT_EQ(&first_cache, first_guard.get_cache_obj()->get_lib_cache());
  EXPECT_EQ(&second_cache, second_guard.get_cache_obj()->get_lib_cache());

  ObCacheObjGuard first_lookup(PC_DIAG_HANDLE);
  ObCacheObjGuard wrong_cache_lookup(PC_DIAG_HANDLE);
  EXPECT_EQ(common::OB_SUCCESS, first_cache.ref_alloc_plan(first_id, first_lookup));
  EXPECT_EQ(common::OB_HASH_NOT_EXIST,
            second_cache.ref_alloc_plan(first_id, wrong_cache_lookup));
  EXPECT_EQ(first_guard.get_cache_obj(), first_lookup.get_cache_obj());
  EXPECT_EQ(&first_cache, first_lookup.get_owner_cache());

  EXPECT_EQ(common::OB_SUCCESS, first_lookup.force_early_release(nullptr));
  EXPECT_EQ(common::OB_SUCCESS, first_guard.force_early_release(nullptr));
  EXPECT_EQ(common::OB_SUCCESS, second_guard.force_early_release(nullptr));
  EXPECT_EQ(0, first_cache.get_cache_obj_mgr().get_alloc_cache_obj_map().size());
  EXPECT_EQ(0, second_cache.get_cache_obj_mgr().get_alloc_cache_obj_map().size());
}

TEST_F(TestSessionPlanCache, guard_swap_and_release_always_use_object_owner)
{
  volatile ObCacheObjID next_object_id = 0;
  TestableSessionPlanCache first_cache;
  TestableSessionPlanCache second_cache;
  ASSERT_EQ(common::OB_SUCCESS, first_cache.init_session_cache(2001, &next_object_id));
  ASSERT_EQ(common::OB_SUCCESS, second_cache.init_session_cache(2002, &next_object_id));

  ObCacheObjGuard first_guard(PLAN_GEN_HANDLE);
  ObCacheObjGuard second_guard(PLAN_GEN_HANDLE);
  ASSERT_EQ(common::OB_SUCCESS,
            ObCacheObjectFactory::alloc(&first_cache, first_guard, NS_CRSR));
  ASSERT_EQ(common::OB_SUCCESS,
            ObCacheObjectFactory::alloc(&second_cache, second_guard, NS_CRSR));
  EXPECT_EQ(1, first_cache.get_cache_obj_mgr().get_alloc_cache_obj_map().size());
  EXPECT_EQ(1, second_cache.get_cache_obj_mgr().get_alloc_cache_obj_map().size());

  first_guard.swap(second_guard);
  EXPECT_EQ(&second_cache, first_guard.get_owner_cache());
  EXPECT_EQ(&first_cache, second_guard.get_owner_cache());

  // Deliberately pass the non-owner cache. The guard must release through the
  // owner captured when the object was allocated.
  EXPECT_EQ(common::OB_SUCCESS, first_guard.force_early_release(&first_cache));
  EXPECT_EQ(1, first_cache.get_cache_obj_mgr().get_alloc_cache_obj_map().size());
  EXPECT_EQ(0, second_cache.get_cache_obj_mgr().get_alloc_cache_obj_map().size());

  EXPECT_EQ(common::OB_SUCCESS, second_guard.force_early_release(&second_cache));
  EXPECT_EQ(0, first_cache.get_cache_obj_mgr().get_alloc_cache_obj_map().size());
  EXPECT_EQ(0, second_cache.get_cache_obj_mgr().get_alloc_cache_obj_map().size());
}

TEST_F(TestSessionPlanCache, foreign_owner_plan_cannot_enter_another_session_cache)
{
  volatile ObCacheObjID next_object_id = 0;
  TestableSessionPlanCache first_cache;
  TestableSessionPlanCache second_cache;
  ASSERT_EQ(common::OB_SUCCESS, first_cache.init_session_cache(2501, &next_object_id));
  ASSERT_EQ(common::OB_SUCCESS, second_cache.init_session_cache(2502, &next_object_id));

  ObCacheObjGuard plan_guard(PLAN_GEN_HANDLE);
  ASSERT_EQ(common::OB_SUCCESS,
            ObCacheObjectFactory::alloc(&first_cache, plan_guard, NS_CRSR));
  ASSERT_NE(nullptr, plan_guard.get_cache_obj());

  common::ObArenaAllocator allocator("SessPCTest");
  ObSqlCtx sql_ctx;
  ObExecContext exec_ctx(allocator);
  const common::ObString sql = common::ObString::make_string("select 1");
  ObPlanCacheCtx pc_ctx(sql, PC_TEXT_MODE, allocator, sql_ctx, exec_ctx);
  EXPECT_EQ(common::OB_INVALID_ARGUMENT,
            second_cache.add_plan(
                static_cast<ObPhysicalPlan *>(plan_guard.get_cache_obj()),
                pc_ctx));
  EXPECT_EQ(0, second_cache.get_cache_obj_size());
  EXPECT_EQ(0, second_cache.get_cache_node_size());
  EXPECT_EQ(1, first_cache.get_cache_obj_mgr().get_alloc_cache_obj_map().size());

  EXPECT_EQ(common::OB_SUCCESS, plan_guard.force_early_release(nullptr));
  EXPECT_EQ(0, first_cache.get_cache_obj_mgr().get_alloc_cache_obj_map().size());
}

TEST_F(TestSessionPlanCache, namespace_modes_are_enforced)
{
  volatile ObCacheObjID next_object_id = 0;
  TestableSessionPlanCache session_cache;
  ObPlanCache tenant_cache;
  ASSERT_EQ(common::OB_SUCCESS, session_cache.init_session_cache(3001, &next_object_id));
  ASSERT_EQ(common::OB_SUCCESS, tenant_cache.init(8));
  EXPECT_TRUE(session_cache.is_session_sql_cache());
  EXPECT_EQ(SESSION_SQL_CACHE, session_cache.get_mode());
  EXPECT_FALSE(tenant_cache.is_session_sql_cache());
  EXPECT_EQ(TENANT_LIBRARY_CACHE, tenant_cache.get_mode());
  static_assert(64 == ObPlanCache::SESSION_PLAN_CACHE_CAPACITY,
                "session plan cache capacity must stay at 64 plans");

  ObCacheObjGuard rejected_session_obj(SQL_STAT_NODE_HANDLE);
  ObCacheObjGuard rejected_tenant_obj(PLAN_GEN_HANDLE);
  EXPECT_EQ(common::OB_NOT_SUPPORTED,
            ObCacheObjectFactory::alloc(&session_cache, rejected_session_obj, NS_SQLSTAT));
  EXPECT_EQ(common::OB_NOT_SUPPORTED,
            ObCacheObjectFactory::alloc(&tenant_cache, rejected_tenant_obj, NS_CRSR));
  EXPECT_EQ(nullptr, rejected_session_obj.get_cache_obj());
  EXPECT_EQ(nullptr, rejected_tenant_obj.get_cache_obj());

  ObCacheObjGuard session_plan(PLAN_GEN_HANDLE);
  ObCacheObjGuard tenant_sql_stat(SQL_STAT_NODE_HANDLE);
  ASSERT_EQ(common::OB_SUCCESS,
            ObCacheObjectFactory::alloc(&session_cache, session_plan, NS_CRSR));
  ASSERT_EQ(common::OB_SUCCESS,
            ObCacheObjectFactory::alloc(&tenant_cache, tenant_sql_stat, NS_SQLSTAT));
  EXPECT_EQ(NS_CRSR, session_plan.get_cache_obj()->get_ns());
  EXPECT_EQ(NS_SQLSTAT, tenant_sql_stat.get_cache_obj()->get_ns());

  EXPECT_EQ(common::OB_SUCCESS, session_plan.force_early_release(nullptr));
  EXPECT_EQ(common::OB_SUCCESS, tenant_sql_stat.force_early_release(nullptr));
}

TEST_F(TestSessionPlanCache, sixty_fifth_plan_evicts_true_lru_node)
{
  volatile ObCacheObjID next_object_id = 0;
  TestableSessionPlanCache cache;
  ObSQLSessionInfo session;
  ASSERT_EQ(common::OB_SUCCESS,
            cache.init_session_cache(4001, &next_object_id));
  ASSERT_EQ(common::OB_SUCCESS, session.test_init(0, 4001, NULL));
  TestPlanCacheAddCtx add_ctx(session);

  char sql_texts[65][48] = {{0}};
  ObCacheObjID plan_ids[65] = {0};
  for (int64_t i = 0; i < 65; ++i) {
    snprintf(sql_texts[i],
             sizeof(sql_texts[i]),
             "select session_lru_%ld",
             i);
  }
  for (int64_t i = 0; i < 64; ++i) {
    ObCacheObjGuard guard(PLAN_GEN_HANDLE);
    ASSERT_EQ(common::OB_SUCCESS,
              add_ctx.add(cache,
                          sql_texts[i],
                          OB_PHY_PLAN_REMOTE,
                          guard,
                          plan_ids[i]));
    ASSERT_EQ(common::OB_SUCCESS, guard.force_early_release(NULL));
  }
  ASSERT_EQ(64, cache.get_cache_obj_size());
  ASSERT_EQ(64, cache.get_cache_node_size());

  for (int64_t i = 0; i < 64; ++i) {
    bool exists = false;
    const int64_t timestamp = 100 + i;
    ASSERT_EQ(common::OB_SUCCESS,
              TestableSessionPlanCache::access_test_node(
                  cache,
                  common::ObString::make_string(sql_texts[i]),
                  &timestamp,
                  exists));
    ASSERT_TRUE(exists);
  }
  // Key 0 was oldest, then gets touched; key 1 must become the LRU.
  bool exists = false;
  const int64_t recently_used_timestamp = 10000;
  ASSERT_EQ(common::OB_SUCCESS,
            TestableSessionPlanCache::access_test_node(
                cache,
                common::ObString::make_string(sql_texts[0]),
                &recently_used_timestamp,
                exists));
  ASSERT_TRUE(exists);

  ObCacheObjGuard new_plan_guard(PLAN_GEN_HANDLE);
  ASSERT_EQ(common::OB_SUCCESS,
            add_ctx.add(cache,
                        sql_texts[64],
                        OB_PHY_PLAN_REMOTE,
                        new_plan_guard,
                        plan_ids[64]));
  ASSERT_EQ(common::OB_SUCCESS,
            new_plan_guard.force_early_release(NULL));
  EXPECT_EQ(64, cache.get_cache_obj_size());
  EXPECT_EQ(64, cache.get_cache_node_size());

  bool key0_exists = false;
  bool key1_exists = false;
  bool key64_exists = false;
  ASSERT_EQ(common::OB_SUCCESS,
            TestableSessionPlanCache::access_test_node(
                cache,
                common::ObString::make_string(sql_texts[0]),
                NULL,
                key0_exists));
  ASSERT_EQ(common::OB_SUCCESS,
            TestableSessionPlanCache::access_test_node(
                cache,
                common::ObString::make_string(sql_texts[1]),
                NULL,
                key1_exists));
  ASSERT_EQ(common::OB_SUCCESS,
            TestableSessionPlanCache::access_test_node(
                cache,
                common::ObString::make_string(sql_texts[64]),
                NULL,
                key64_exists));
  EXPECT_TRUE(key0_exists);
  EXPECT_FALSE(key1_exists);
  EXPECT_TRUE(key64_exists);

  ObCacheObjGuard retained(PC_DIAG_HANDLE);
  ObCacheObjGuard evicted(PC_DIAG_HANDLE);
  EXPECT_EQ(common::OB_SUCCESS,
            cache.ref_plan(plan_ids[0], retained));
  EXPECT_EQ(common::OB_HASH_NOT_EXIST,
            cache.ref_plan(plan_ids[1], evicted));
  EXPECT_EQ(common::OB_SUCCESS, retained.force_early_release(NULL));
}

TEST_F(TestSessionPlanCache, whole_pcvset_eviction_keeps_live_guard_safe)
{
  volatile ObCacheObjID next_object_id = 0;
  TestableSessionPlanCache cache;
  ObSQLSessionInfo session;
  ASSERT_EQ(common::OB_SUCCESS,
            cache.init_session_cache(5001, &next_object_id));
  ASSERT_EQ(common::OB_SUCCESS, session.test_init(0, 5001, NULL));
  TestPlanCacheAddCtx add_ctx(session);

  ObCacheObjID remote_id = 0;
  ObCacheObjID local_id = 0;
  ObCacheObjGuard retained_guard(PLAN_GEN_HANDLE);
  ASSERT_EQ(common::OB_SUCCESS,
            add_ctx.add(cache,
                        "select multi_plan_key",
                        OB_PHY_PLAN_REMOTE,
                        retained_guard,
                        remote_id));
  ObCacheObjGuard local_guard(PLAN_GEN_HANDLE);
  ASSERT_EQ(common::OB_SUCCESS,
            add_ctx.add(cache,
                        "select multi_plan_key",
                        OB_PHY_PLAN_LOCAL,
                        local_guard,
                        local_id));
  ASSERT_EQ(common::OB_SUCCESS, local_guard.force_early_release(NULL));
  ASSERT_EQ(2, cache.get_cache_obj_size());
  ASSERT_EQ(1, cache.get_cache_node_size());
  bool exists = false;
  const int64_t oldest = 1;
  ASSERT_EQ(common::OB_SUCCESS,
            TestableSessionPlanCache::access_test_node(
                cache,
                common::ObString::make_string("select multi_plan_key"),
                &oldest,
                exists));
  ASSERT_TRUE(exists);

  char sql_texts[62][48] = {{0}};
  for (int64_t i = 0; i < 62; ++i) {
    snprintf(sql_texts[i],
             sizeof(sql_texts[i]),
             "select pcv_fill_%ld",
             i);
    ObCacheObjID id = 0;
    ObCacheObjGuard guard(PLAN_GEN_HANDLE);
    ASSERT_EQ(common::OB_SUCCESS,
              add_ctx.add(cache,
                          sql_texts[i],
                          OB_PHY_PLAN_REMOTE,
                          guard,
                          id));
    ASSERT_EQ(common::OB_SUCCESS, guard.force_early_release(NULL));
    const int64_t timestamp = 10 + i;
    exists = false;
    ASSERT_EQ(common::OB_SUCCESS,
              TestableSessionPlanCache::access_test_node(
                  cache,
                  common::ObString::make_string(sql_texts[i]),
                  &timestamp,
                  exists));
    ASSERT_TRUE(exists);
  }
  ASSERT_EQ(64, cache.get_cache_obj_size());
  ASSERT_EQ(63, cache.get_cache_node_size());

  ObCacheObjID trigger_id = 0;
  ObCacheObjGuard trigger_guard(PLAN_GEN_HANDLE);
  ASSERT_EQ(common::OB_SUCCESS,
            add_ctx.add(cache,
                        "select pcv_trigger",
                        OB_PHY_PLAN_REMOTE,
                        trigger_guard,
                        trigger_id));
  ASSERT_EQ(common::OB_SUCCESS,
            trigger_guard.force_early_release(NULL));
  EXPECT_EQ(63, cache.get_cache_obj_size());
  EXPECT_EQ(63, cache.get_cache_node_size());

  bool multi_exists = true;
  ASSERT_EQ(common::OB_SUCCESS,
            TestableSessionPlanCache::access_test_node(
                cache,
                common::ObString::make_string("select multi_plan_key"),
                NULL,
                multi_exists));
  EXPECT_FALSE(multi_exists);
  ObCacheObjGuard remote_probe(PC_DIAG_HANDLE);
  ObCacheObjGuard local_probe(PC_DIAG_HANDLE);
  EXPECT_EQ(common::OB_HASH_NOT_EXIST,
            cache.ref_plan(remote_id, remote_probe));
  EXPECT_EQ(common::OB_HASH_NOT_EXIST,
            cache.ref_plan(local_id, local_probe));
  ASSERT_NE(nullptr, retained_guard.get_cache_obj());
  EXPECT_EQ(remote_id, retained_guard.get_cache_obj()->get_object_id());
  EXPECT_EQ(&cache, retained_guard.get_owner_cache());
  EXPECT_EQ(
      64, cache.get_cache_obj_mgr().get_alloc_cache_obj_map().size());
  ASSERT_EQ(common::OB_SUCCESS,
            retained_guard.force_early_release(NULL));
  EXPECT_EQ(
      63, cache.get_cache_obj_mgr().get_alloc_cache_obj_map().size());
}

TEST_F(TestSessionPlanCache, lazy_epoch_flush_and_session_destroy_clear_cache)
{
  ObTenantSQLSessionMgr session_mgr;
  ASSERT_EQ(common::OB_SUCCESS, session_mgr.init());
  ObSQLSessionInfo session;
  ASSERT_EQ(common::OB_SUCCESS, session.test_init(0, 6001, NULL));
  session.set_tenant_session_mgr(&session_mgr);
  ObPlanCache *cache = session.get_sql_plan_cache();
  ASSERT_NE(nullptr, cache);
  {
    TestPlanCacheAddCtx add_ctx(session);
    ObCacheObjID first_id = 0;
    ObCacheObjGuard first_guard(PLAN_GEN_HANDLE);
    ASSERT_EQ(common::OB_SUCCESS,
              add_ctx.add(*cache,
                          "select epoch_old",
                          OB_PHY_PLAN_REMOTE,
                          first_guard,
                          first_id));
    ASSERT_EQ(common::OB_SUCCESS,
              first_guard.force_early_release(NULL));
    ASSERT_EQ(1, cache->get_cache_obj_size());

    (void)session_mgr.inc_sql_plan_flush_epoch();
    EXPECT_EQ(cache, session.get_sql_plan_cache());
    EXPECT_EQ(0, cache->get_cache_obj_size());
    EXPECT_EQ(0, cache->get_cache_node_size());
    EXPECT_EQ(
        0, cache->get_cache_obj_mgr().get_alloc_cache_obj_map().size());
    ObCacheObjGuard old_probe(PC_DIAG_HANDLE);
    EXPECT_EQ(common::OB_HASH_NOT_EXIST,
              cache->ref_plan(first_id, old_probe));

    ObCacheObjID second_id = 0;
    ObCacheObjGuard second_guard(PLAN_GEN_HANDLE);
    ASSERT_EQ(common::OB_SUCCESS,
              add_ctx.add(*cache,
                          "select destroy_old",
                          OB_PHY_PLAN_REMOTE,
                          second_guard,
                          second_id));
    ASSERT_EQ(common::OB_SUCCESS,
              second_guard.force_early_release(NULL));
    ASSERT_EQ(1, cache->get_cache_obj_size());
  }

  session.destroy(false);
  {
    lib::ObMutexGuard guard(session.get_sql_plan_cache_mutex());
    ASSERT_EQ(common::OB_SUCCESS, guard.get_ret());
    EXPECT_EQ(nullptr, session.peek_sql_plan_cache());
  }
}

} // namespace sql
} // namespace oceanbase

int main(int argc, char **argv)
{
  oceanbase::observer::ObReqTimeGuard req_time_guard;
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
