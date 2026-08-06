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

#include "lib/alloc/alloc_func.h"
#include "lib/utility/utility.h"
#include "share/cache/ob_kvcache_struct.h"
#include "share/config/ob_server_config.h"

namespace oceanbase
{
namespace common
{

namespace
{
constexpr int64_t ONE_GIB = 1LL << 30;

class ServerConfigRestore
{
public:
  ServerConfigRestore()
    : memory_limit_(GCONF.memory_limit),
      memory_budget_(GCONF._memory_budget),
      kvcache_memory_limit_(GCONF.kvcache_memory_limit),
      memstore_memory_limit_(GCONF.memstore_memory_limit),
      vector_memory_limit_(GCONF.vector_memory_limit),
      effective_memory_budget_(lib::get_memory_budget())
  {}

  ~ServerConfigRestore()
  {
    GCONF.memory_limit = memory_limit_;
    GCONF._memory_budget = memory_budget_;
    GCONF.kvcache_memory_limit = kvcache_memory_limit_;
    GCONF.memstore_memory_limit = memstore_memory_limit_;
    GCONF.vector_memory_limit = vector_memory_limit_;
    lib::set_memory_budget(effective_memory_budget_);
  }

private:
  int64_t memory_limit_;
  int64_t memory_budget_;
  int64_t kvcache_memory_limit_;
  int64_t memstore_memory_limit_;
  int64_t vector_memory_limit_;
  int64_t effective_memory_budget_;
};
}

TEST(TestServerMemoryConfig, resolves_automatic_and_explicit_limits)
{
  EXPECT_EQ(ONE_GIB,
            ObServerMemoryConfig::calculate_automatic_memory_budget(0));
  EXPECT_EQ(4 * ONE_GIB,
            ObServerMemoryConfig::calculate_automatic_memory_budget(10 * ONE_GIB));

  EXPECT_EQ(lib::get_memory_by_percentage(10 * ONE_GIB, 15),
            ObServerMemoryConfig::resolve_kvcache_memory_limit(0, 10 * ONE_GIB));
  EXPECT_EQ(lib::get_memory_by_percentage(10 * ONE_GIB, 10),
            ObServerMemoryConfig::resolve_memstore_memory_limit(0, 10 * ONE_GIB));
  EXPECT_EQ(lib::get_memory_by_percentage(10 * ONE_GIB, 5),
            ObServerMemoryConfig::resolve_vector_memory_limit(0, 10 * ONE_GIB));

  const int64_t automatic_limit_sum =
      ObServerMemoryConfig::calculate_automatic_memory_budget(10 * ONE_GIB)
      + ObServerMemoryConfig::resolve_kvcache_memory_limit(0, 10 * ONE_GIB)
      + ObServerMemoryConfig::resolve_memstore_memory_limit(0, 10 * ONE_GIB)
      + ObServerMemoryConfig::resolve_vector_memory_limit(0, 10 * ONE_GIB);
  EXPECT_EQ(lib::get_memory_by_percentage(10 * ONE_GIB, 70), automatic_limit_sum);

  EXPECT_EQ(12345, ObServerMemoryConfig::resolve_kvcache_memory_limit(12345, INT64_MAX));
  EXPECT_EQ(23456, ObServerMemoryConfig::resolve_memstore_memory_limit(23456, INT64_MAX));
  EXPECT_EQ(34567, ObServerMemoryConfig::resolve_vector_memory_limit(34567, INT64_MAX));
  EXPECT_EQ(MAX_KVCACHE_MEMORY_SIZE,
            ObServerMemoryConfig::resolve_kvcache_memory_limit(INT64_MAX, INT64_MAX));
}

TEST(TestServerMemoryConfig, reload_uses_explicit_memory_budget)
{
  ServerConfigRestore restore;
  ObServerMemoryConfig memory_config;

  GCONF._memory_budget = 3 * ONE_GIB;
  GCONF.memory_limit = 8 * ONE_GIB;
  GCONF.kvcache_memory_limit = 3 * ONE_GIB;
  GCONF.memstore_memory_limit = 4 * ONE_GIB;
  GCONF.vector_memory_limit = 5 * ONE_GIB;

  ASSERT_EQ(OB_SUCCESS, memory_config.reload_config(GCONF));
  EXPECT_EQ(3 * ONE_GIB, memory_config.get_server_memory_budget());
  EXPECT_EQ(3 * ONE_GIB, memory_config.get_kvcache_memory_limit());
  EXPECT_EQ(4 * ONE_GIB, memory_config.get_memstore_memory_limit());
  EXPECT_EQ(5 * ONE_GIB, memory_config.get_vector_memory_limit());

  GCONF.memory_limit = 16 * ONE_GIB;
  ASSERT_EQ(OB_SUCCESS, memory_config.reload_config(GCONF));
  EXPECT_EQ(3 * ONE_GIB, memory_config.get_server_memory_budget());
  EXPECT_EQ(3 * ONE_GIB, memory_config.get_kvcache_memory_limit());
  EXPECT_EQ(4 * ONE_GIB, memory_config.get_memstore_memory_limit());
  EXPECT_EQ(5 * ONE_GIB, memory_config.get_vector_memory_limit());
}

TEST(TestServerMemoryConfig, reload_uses_automatic_budget_independent_of_memory_limit)
{
  ServerConfigRestore restore;
  ObServerMemoryConfig memory_config;
  const int64_t physical_memory = get_phy_mem_size();
  const int64_t expected_memory_budget =
      ObServerMemoryConfig::calculate_automatic_memory_budget(
          physical_memory);

  GCONF._memory_budget = 0;
  GCONF.memory_limit = 4 * ONE_GIB;
  GCONF.kvcache_memory_limit = 0;
  GCONF.memstore_memory_limit = 0;
  GCONF.vector_memory_limit = 0;

  ASSERT_EQ(OB_SUCCESS, memory_config.reload_config(GCONF));
  EXPECT_EQ(expected_memory_budget, memory_config.get_server_memory_budget());
  EXPECT_EQ(ObServerMemoryConfig::resolve_kvcache_memory_limit(
                0, physical_memory),
            memory_config.get_kvcache_memory_limit());
  EXPECT_EQ(ObServerMemoryConfig::resolve_memstore_memory_limit(
                0, physical_memory),
            memory_config.get_memstore_memory_limit());
  EXPECT_EQ(ObServerMemoryConfig::resolve_vector_memory_limit(
                0, physical_memory),
            memory_config.get_vector_memory_limit());

  GCONF.memory_limit = 16 * ONE_GIB;
  ASSERT_EQ(OB_SUCCESS, memory_config.reload_config(GCONF));
  EXPECT_EQ(expected_memory_budget, memory_config.get_server_memory_budget());
}

TEST(TestServerMemoryConfig, kvcache_limit_does_not_exceed_startup_capacity)
{
  ServerConfigRestore restore;
  ObServerMemoryConfig memory_config;

  GCONF._memory_budget = ONE_GIB;
  GCONF.kvcache_memory_limit = ONE_GIB;
  GCONF.memstore_memory_limit = 0;
  GCONF.vector_memory_limit = 0;

  ASSERT_EQ(OB_SUCCESS, memory_config.reload_config(GCONF));
  EXPECT_EQ(ONE_GIB, memory_config.get_kvcache_memory_limit());
  EXPECT_EQ(2 * ONE_GIB, memory_config.get_kvcache_memory_capacity());

  GCONF.kvcache_memory_limit = 3 * ONE_GIB;
  ASSERT_EQ(OB_SUCCESS, memory_config.reload_config(GCONF));
  EXPECT_EQ(2 * ONE_GIB, memory_config.get_kvcache_memory_limit());
}

} // namespace common
} // namespace oceanbase

int main(int argc, char **argv)
{
  testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
