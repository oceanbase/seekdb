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
#include "rootserver/ob_root_service.h"
#include "share/ob_max_id_cache.h"
#include "share/ob_max_id_fetcher.h"
#include "deps/oblib/src/lib/mysqlclient/ob_mysql_proxy.h"

#define ASSERT_SUCCESS(x) ASSERT_EQ((x), OB_SUCCESS)

// Mock the internal table query to verify cache correctness.

namespace oceanbase
{
namespace share
{
uint64_t current_max_id = OB_INVALID_ID;
uint64_t target_size = OB_INVALID_SIZE;
bool hit = false;
int ObMaxIdFetcher::batch_fetch_new_max_id_from_inner_table(
    const uint64_t tenant_id,
    ObMaxIdType id_type,
    uint64_t &max_id,
    const uint64_t size)
{
  int ret = OB_SUCCESS;
  hit = true;
  if (size == target_size) {
    max_id = current_max_id + size;
  } else {
    max_id = OB_INVALID_ID;
  }
  LOG_INFO("hit inner table", K(hit), K(id_type), K(max_id), K(size), K(current_max_id), K(target_size));
  return ret;
}
void set_id_size(const uint64_t id, const uint64_t size)
{
  current_max_id = id;
  target_size = size;
  hit = false;
}
void reset_id_size()
{
  current_max_id = OB_INVALID_ID;
  target_size = OB_INVALID_SIZE;
  hit = false;
}
TEST(MaxIdCache, basic)
{
  rootserver::ObRootService rs;
  GCTX.root_service_ = &rs;
  rs.rs_status_.rs_status_ = share::status::ObRootServiceStatus::FULL_SERVICE;
  ObMaxIdCacheMgr mgr;
  ObMySQLProxy proxy;
  uint64_t id = OB_INVALID_ID;
  ASSERT_SUCCESS(mgr.init(&proxy));

  // The first fetch caches `ObMaxIdCacheItem::CACHE_SIZE` IDs.
  // The internal table value is 1 before the fetch and becomes
  // `ObMaxIdCacheItem::CACHE_SIZE + 1` after the fetch.
  set_id_size(1, ObMaxIdCacheItem::CACHE_SIZE);
  ASSERT_SUCCESS(mgr.fetch_max_id(OB_SYS_TENANT_ID, OB_MAX_USED_OBJECT_ID_TYPE, id, 1));
  ASSERT_EQ(id, 2);
  ASSERT_TRUE(hit);
  reset_id_size();

  // Fetch `ObMaxIdCacheItem::CACHE_SIZE - 1` times without hitting the internal table.
  for (int64_t i = 1; i < ObMaxIdCacheItem::CACHE_SIZE; i++) {
    ASSERT_SUCCESS(mgr.fetch_max_id(OB_SYS_TENANT_ID, OB_MAX_USED_OBJECT_ID_TYPE, id, 1));
    ASSERT_EQ(id, i + 2);
    ASSERT_FALSE(hit);
  }

  // Once the cache is exhausted, the internal table is queried again.
  // The internal table value is `ObMaxIdCacheItem::CACHE_SIZE + 1` before the fetch
  // and becomes `ObMaxIdCacheItem::CACHE_SIZE * 2 + 1` after the fetch.
  set_id_size(ObMaxIdCacheItem::CACHE_SIZE + 1, ObMaxIdCacheItem::CACHE_SIZE);
  ASSERT_SUCCESS(mgr.fetch_max_id(OB_SYS_TENANT_ID, OB_MAX_USED_OBJECT_ID_TYPE, id, 1));
  ASSERT_EQ(id, ObMaxIdCacheItem::CACHE_SIZE + 2);
  ASSERT_TRUE(hit);
  reset_id_size();

  // Fetch `ObMaxIdCacheItem::CACHE_SIZE - 1` values without hitting the internal table.
  ASSERT_SUCCESS(mgr.fetch_max_id(OB_SYS_TENANT_ID, OB_MAX_USED_OBJECT_ID_TYPE, id, ObMaxIdCacheItem::CACHE_SIZE - 1));
  ASSERT_EQ(id, ObMaxIdCacheItem::CACHE_SIZE + 3);
  ASSERT_FALSE(hit);

  // Once the cache is exhausted, the internal table is queried again.
  // The internal table value is `ObMaxIdCacheItem::CACHE_SIZE * 2 + 1` before the fetch
  // and becomes `ObMaxIdCacheItem::CACHE_SIZE * 3 + 1` after the fetch.
  set_id_size(ObMaxIdCacheItem::CACHE_SIZE * 2 + 1, ObMaxIdCacheItem::CACHE_SIZE);
  ASSERT_SUCCESS(mgr.fetch_max_id(OB_SYS_TENANT_ID, OB_MAX_USED_OBJECT_ID_TYPE, id, 1));
  ASSERT_EQ(id, ObMaxIdCacheItem::CACHE_SIZE * 2 + 2);
  ASSERT_TRUE(hit);
  reset_id_size();

  // The internal table value is `ObMaxIdCacheItem::CACHE_SIZE * 3 + 1` before the fetch
  // and becomes `ObMaxIdCacheItem::CACHE_SIZE * 4 + 2` after the fetch.
  set_id_size(ObMaxIdCacheItem::CACHE_SIZE * 3 + 1, ObMaxIdCacheItem::CACHE_SIZE + 1);
  ASSERT_SUCCESS(mgr.fetch_max_id(OB_SYS_TENANT_ID, OB_MAX_USED_OBJECT_ID_TYPE, id, ObMaxIdCacheItem::CACHE_SIZE + 1));
  // The remaining IDs from the previous cached range can still be reused.
  ASSERT_EQ(id, ObMaxIdCacheItem::CACHE_SIZE * 2 + 3);
  ASSERT_TRUE(hit);
  reset_id_size();

  // The current cached range is
  // [ObMaxIdCacheItem::CACHE_SIZE * 3 + 4, ObMaxIdCacheItem::CACHE_SIZE * 4 + 2].
  // If the internal table reports the max used ID as
  // `ObMaxIdCacheItem::CACHE_SIZE * 4 + 3`, it means IDs were allocated elsewhere,
  // so the sequence is no longer contiguous and the cached range must be discarded.
  set_id_size(ObMaxIdCacheItem::CACHE_SIZE * 4 + 3, ObMaxIdCacheItem::CACHE_SIZE);
  ASSERT_SUCCESS(mgr.fetch_max_id(OB_SYS_TENANT_ID, OB_MAX_USED_OBJECT_ID_TYPE, id, ObMaxIdCacheItem::CACHE_SIZE));
  ASSERT_EQ(id, ObMaxIdCacheItem::CACHE_SIZE * 4 + 4);
  ASSERT_TRUE(hit);
  reset_id_size();
}
}
}
int main(int argc, char **argv)
{
  OB_LOGGER.set_log_level("INFO");
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
