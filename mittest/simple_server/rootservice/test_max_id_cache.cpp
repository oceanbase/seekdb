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

#define USING_LOG_PREFIX SHARE

#include <gtest/gtest.h>
#include <gmock/gmock.h>
#define  private public
#include "share/ob_max_id_cache.h"
#include "rootserver/ob_root_service.h"
#include "mittest/simple_server/env/ob_simple_cluster_test_base.h"
#include "deps/oblib/src/common/ob_tablet_id.h"
#include "share/ob_max_id_fetcher.h"

#define ASSERT_SUCCESS(x) ASSERT_EQ((x), OB_SUCCESS)

namespace oceanbase
{
namespace pl
{
int ObPLPackageManager::load_all_sys_package(common::ObMySQLProxy &sql_proxy)
{
  return OB_SUCCESS;
}
}
using namespace common;
class TestMaxIdCache : public unittest::ObSimpleClusterTestBase
{
public:
  TestMaxIdCache() : unittest::ObSimpleClusterTestBase("test_max_id_cache") {}
};
// Test various max_id_cache interfaces.
TEST_F(TestMaxIdCache, basic)
{
  ASSERT_NE(GCTX.root_service_, nullptr);
  ASSERT_NE(GCTX.sql_proxy_, nullptr);
  while (!GCTX.root_service_->is_full_service()) {
    ob_usleep(1_s);
  }
  rootserver::ObSysStat sys_stat;
  ASSERT_SUCCESS(sys_stat.set_initial_values(OB_SYS_TENANT_ID));
  ObMaxIdFetcher id_fetcher(*GCTX.sql_proxy_);
  uint64_t id = OB_INVALID_ID;
  int64_t extended_tablet_init_id = sys_stat.ob_max_used_extended_rowid_table_tablet_id_.value_.get_int();
  uint64_t normal_tablet_init_id = sys_stat.ob_max_used_normal_rowid_table_tablet_id_.value_.get_int();
  uint64_t table_init_id = sys_stat.ob_max_used_object_id_.value_.get_int();

  // This interface returns [id, id + size).
  id = OB_INVALID_ID;
  ASSERT_SUCCESS(id_fetcher.fetch_new_max_ids(OB_SYS_TENANT_ID, OB_MAX_USED_EXTENDED_ROWID_TABLE_TABLET_ID_TYPE, id, 1));
  ASSERT_EQ(extended_tablet_init_id + 1, id);
  id = OB_INVALID_ID;
  ASSERT_SUCCESS(id_fetcher.fetch_new_max_ids(OB_SYS_TENANT_ID, OB_MAX_USED_EXTENDED_ROWID_TABLE_TABLET_ID_TYPE, id, 2));
  ASSERT_EQ(extended_tablet_init_id + 2, id);
  id = OB_INVALID_ID;
  ASSERT_SUCCESS(id_fetcher.fetch_new_max_ids(OB_SYS_TENANT_ID, OB_MAX_USED_EXTENDED_ROWID_TABLE_TABLET_ID_TYPE, id, 3));
  ASSERT_EQ(extended_tablet_init_id + 4, id);

  // This interface returns [id, id + size).
  id = OB_INVALID_ID;
  ASSERT_SUCCESS(id_fetcher.fetch_new_max_ids(OB_SYS_TENANT_ID, OB_MAX_USED_NORMAL_ROWID_TABLE_TABLET_ID_TYPE, id, 1));
  ASSERT_EQ(normal_tablet_init_id + 1, id);
  id = OB_INVALID_ID;
  ASSERT_SUCCESS(id_fetcher.fetch_new_max_ids(OB_SYS_TENANT_ID, OB_MAX_USED_NORMAL_ROWID_TABLE_TABLET_ID_TYPE, id, 2));
  ASSERT_EQ(normal_tablet_init_id + 2, id);
  id = OB_INVALID_ID;
  ASSERT_SUCCESS(id_fetcher.fetch_new_max_ids(OB_SYS_TENANT_ID, OB_MAX_USED_NORMAL_ROWID_TABLE_TABLET_ID_TYPE, id, 3));
  ASSERT_EQ(normal_tablet_init_id + 4, id);

  // This interface returns (id - size, id].
  id = OB_INVALID_ID;
  ASSERT_SUCCESS(id_fetcher.fetch_new_max_id(OB_SYS_TENANT_ID, OB_MAX_USED_TABLE_ID_TYPE, id, UINT64_MAX, 1));
  ASSERT_EQ(table_init_id + 1, id); // 1
  id = OB_INVALID_ID;
  ASSERT_SUCCESS(id_fetcher.fetch_new_max_id(OB_SYS_TENANT_ID, OB_MAX_USED_TABLE_ID_TYPE, id, UINT64_MAX, 2));
  ASSERT_EQ(table_init_id + 3, id); // 2 3
  id = OB_INVALID_ID;
  ASSERT_SUCCESS(id_fetcher.fetch_new_max_id(OB_SYS_TENANT_ID, OB_MAX_USED_TABLE_ID_TYPE, id, UINT64_MAX, 3));
  ASSERT_EQ(table_init_id + 6, id); // 4 5 6
}

// Cached IDs are wasted after RS leader switch.
TEST_F(TestMaxIdCache, rs_change)
{
  ASSERT_NE(GCTX.root_service_, nullptr);
  ASSERT_SUCCESS(GCTX.root_service_->revoke_rs());
  while (!GCTX.root_service_->is_full_service()) {
    ob_usleep(1_s);
  }
  rootserver::ObSysStat sys_stat;
  ASSERT_SUCCESS(sys_stat.set_initial_values(OB_SYS_TENANT_ID));
  ObMaxIdFetcher id_fetcher(*GCTX.sql_proxy_);
  uint64_t id = OB_INVALID_ID;
  int64_t extended_tablet_init_id = sys_stat.ob_max_used_extended_rowid_table_tablet_id_.value_.get_int() + ObMaxIdCacheItem::CACHE_SIZE;
  uint64_t normal_tablet_init_id = sys_stat.ob_max_used_normal_rowid_table_tablet_id_.value_.get_int() + ObMaxIdCacheItem::CACHE_SIZE;
  uint64_t table_init_id = sys_stat.ob_max_used_object_id_.value_.get_int() + ObMaxIdCacheItem::CACHE_SIZE;

  // This interface returns [id, id + size).
  id = OB_INVALID_ID;
  ASSERT_SUCCESS(id_fetcher.fetch_new_max_ids(OB_SYS_TENANT_ID, OB_MAX_USED_EXTENDED_ROWID_TABLE_TABLET_ID_TYPE, id, 1));
  ASSERT_EQ(extended_tablet_init_id + 1, id);
  id = OB_INVALID_ID;
  ASSERT_SUCCESS(id_fetcher.fetch_new_max_ids(OB_SYS_TENANT_ID, OB_MAX_USED_EXTENDED_ROWID_TABLE_TABLET_ID_TYPE, id, 2));
  ASSERT_EQ(extended_tablet_init_id + 2, id);
  id = OB_INVALID_ID;
  ASSERT_SUCCESS(id_fetcher.fetch_new_max_ids(OB_SYS_TENANT_ID, OB_MAX_USED_EXTENDED_ROWID_TABLE_TABLET_ID_TYPE, id, 3));
  ASSERT_EQ(extended_tablet_init_id + 4, id);

  // This interface returns [id, id + size).
  id = OB_INVALID_ID;
  ASSERT_SUCCESS(id_fetcher.fetch_new_max_ids(OB_SYS_TENANT_ID, OB_MAX_USED_NORMAL_ROWID_TABLE_TABLET_ID_TYPE, id, 1));
  ASSERT_EQ(normal_tablet_init_id + 1, id);
  id = OB_INVALID_ID;
  ASSERT_SUCCESS(id_fetcher.fetch_new_max_ids(OB_SYS_TENANT_ID, OB_MAX_USED_NORMAL_ROWID_TABLE_TABLET_ID_TYPE, id, 2));
  ASSERT_EQ(normal_tablet_init_id + 2, id);
  id = OB_INVALID_ID;
  ASSERT_SUCCESS(id_fetcher.fetch_new_max_ids(OB_SYS_TENANT_ID, OB_MAX_USED_NORMAL_ROWID_TABLE_TABLET_ID_TYPE, id, 3));
  ASSERT_EQ(normal_tablet_init_id + 4, id);

  // This interface returns (id - size, id].
  id = OB_INVALID_ID;
  ASSERT_SUCCESS(id_fetcher.fetch_new_max_id(OB_SYS_TENANT_ID, OB_MAX_USED_TABLE_ID_TYPE, id, UINT64_MAX, 1));
  ASSERT_EQ(table_init_id + 1, id); // 1
  id = OB_INVALID_ID;
  ASSERT_SUCCESS(id_fetcher.fetch_new_max_id(OB_SYS_TENANT_ID, OB_MAX_USED_TABLE_ID_TYPE, id, UINT64_MAX, 2));
  ASSERT_EQ(table_init_id + 3, id); // 2 3
  id = OB_INVALID_ID;
  ASSERT_SUCCESS(id_fetcher.fetch_new_max_id(OB_SYS_TENANT_ID, OB_MAX_USED_TABLE_ID_TYPE, id, UINT64_MAX, 3));
  ASSERT_EQ(table_init_id + 6, id); // 4 5 6
}

TEST_F(TestMaxIdCache, max_id)
{
  ObMaxIdFetcher id_fetcher(*GCTX.sql_proxy_);
  uint64_t id = OB_INVALID_ID;
  ASSERT_NE(GCTX.root_service_, nullptr);
  ASSERT_NE(GCTX.sql_proxy_, nullptr);
  uint64_t current_id = OB_INVALID_ID;
  ASSERT_SUCCESS(id_fetcher.fetch_new_max_ids(OB_SYS_TENANT_ID, OB_MAX_USED_EXTENDED_ROWID_TABLE_TABLET_ID_TYPE, current_id, 1));
  id = OB_INVALID_ID;
  ASSERT_SUCCESS(id_fetcher.fetch_new_max_ids(OB_SYS_TENANT_ID, OB_MAX_USED_EXTENDED_ROWID_TABLE_TABLET_ID_TYPE,
        id, ObTabletID::MAX_USER_EXTENDED_ROWID_TABLE_TABLET_ID - 1 - current_id));
  id = OB_INVALID_ID;
  ASSERT_NE(id_fetcher.fetch_new_max_ids(OB_SYS_TENANT_ID, OB_MAX_USED_EXTENDED_ROWID_TABLE_TABLET_ID_TYPE, current_id, 1),
      OB_SUCCESS) << current_id;

  current_id = OB_INVALID_ID;
  ASSERT_SUCCESS(id_fetcher.fetch_new_max_ids(OB_SYS_TENANT_ID, OB_MAX_USED_NORMAL_ROWID_TABLE_TABLET_ID_TYPE, current_id, 1));
  id = OB_INVALID_ID;
  ASSERT_SUCCESS(id_fetcher.fetch_new_max_ids(OB_SYS_TENANT_ID, OB_MAX_USED_NORMAL_ROWID_TABLE_TABLET_ID_TYPE,
        id, ObTabletID::MAX_USER_NORMAL_ROWID_TABLE_TABLET_ID - 1 - current_id));
  id = OB_INVALID_ID;
  ASSERT_NE(id_fetcher.fetch_new_max_ids(OB_SYS_TENANT_ID, OB_MAX_USED_NORMAL_ROWID_TABLE_TABLET_ID_TYPE, current_id, 1),
      OB_SUCCESS) << current_id;

  current_id = OB_INVALID_ID;
  ASSERT_SUCCESS(id_fetcher.fetch_new_max_id(OB_SYS_TENANT_ID, OB_MAX_USED_TABLE_ID_TYPE, current_id, UINT64_MAX, 1));
  id = OB_INVALID_ID;
  ASSERT_SUCCESS(id_fetcher.fetch_new_max_id(OB_SYS_TENANT_ID, OB_MAX_USED_TABLE_ID_TYPE, id, UINT64_MAX, INT64_MAX - current_id));
  id = OB_INVALID_ID;
  ASSERT_NE(id_fetcher.fetch_new_max_id(OB_SYS_TENANT_ID, OB_MAX_USED_TABLE_ID_TYPE, current_id, UINT64_MAX, 1),
      OB_SUCCESS) << current_id;
}
}

int main(int argc, char **argv)
{
  oceanbase::unittest::init_log_and_gtest(argc, argv);
  OB_LOGGER.set_log_level("INFO");
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
