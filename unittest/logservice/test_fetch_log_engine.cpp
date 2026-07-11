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

#include <gtest/gtest.h>
#include "lib/oblog/ob_log.h"
#include "logservice/ob_tenant_mutil_allocator.h"

#define private public
#include "logservice/palf/fetch_log_engine.h"
#include "logservice/palf/palf_env_impl.h"
#include "logservice/palf/palf_options.h"
#undef private

namespace oceanbase
{
namespace palf
{

TEST(TestFetchLogEngine, disabled_lifecycle)
{
  PalfEnvImpl palf_env;
  common::ObTenantMutilAllocator allocator;
  FetchLogEngine engine;

  ASSERT_EQ(OB_SUCCESS, engine.init(&palf_env, &allocator, false));
  EXPECT_FALSE(engine.is_enabled());
  EXPECT_EQ(-1, engine.tg_id_);
  EXPECT_EQ(OB_SUCCESS, engine.start());
  EXPECT_EQ(OB_SUCCESS, engine.stop());
  EXPECT_EQ(OB_SUCCESS, engine.wait());
  EXPECT_EQ(nullptr, engine.alloc_fetch_log_task());
  EXPECT_EQ(OB_NOT_SUPPORTED, engine.submit_fetch_log_task(nullptr));
  EXPECT_EQ(OB_SUCCESS, engine.update_replayable_point(share::SCN::base_scn()));

  engine.destroy();
  engine.destroy();
}

TEST(TestPalfEnvImpl, fetch_log_engine_option_is_read_only)
{
  PalfEnvImpl palf_env;
  palf_env.is_inited_ = true;
  palf_env.enable_fetch_log_engine_ = false;

  PalfOptions current_options;
  ASSERT_EQ(OB_SUCCESS, palf_env.get_options(current_options));
  EXPECT_FALSE(current_options.enable_fetch_log_engine_);

  current_options.disk_options_.log_disk_usage_limit_size_ = 1024 * 1024 * 1024L;
  current_options.disk_options_.log_disk_utilization_threshold_ = 80;
  current_options.disk_options_.log_disk_utilization_limit_threshold_ = 95;
  current_options.disk_options_.log_disk_throttling_percentage_ = 60;
  current_options.disk_options_.log_disk_throttling_maximum_duration_ = 7200LL * 1000 * 1000;
  current_options.disk_options_.log_writer_parallelism_ = 1;
  current_options.enable_fetch_log_engine_ = true;
  ASSERT_TRUE(current_options.is_valid());
  EXPECT_EQ(OB_NOT_SUPPORTED, palf_env.update_options(current_options));

  palf_env.is_inited_ = false;
}

TEST(TestPalfHandleImpl, get_log_is_not_supported_when_fetch_is_disabled)
{
  PalfEnvImpl palf_env;
  common::ObTenantMutilAllocator allocator;
  FetchLogEngine engine;
  PalfHandleImpl palf_handle;
  common::ObAddr server;

  ASSERT_TRUE(server.set_ip_addr("127.0.0.1", 1000));
  ASSERT_EQ(OB_SUCCESS, engine.init(&palf_env, &allocator, false));
  palf_handle.is_inited_ = true;
  palf_handle.palf_id_ = 1;
  palf_handle.fetch_log_engine_ = &engine;
  EXPECT_EQ(OB_NOT_SUPPORTED,
            palf_handle.get_log(server, FETCH_LOG_FOLLOWER, 1, LSN(0), LSN(0), 1, 1, 1));

  palf_handle.is_inited_ = false;
  palf_handle.fetch_log_engine_ = nullptr;
}

TEST(TestPalfOptions, fetch_log_engine_default)
{
  PalfOptions options;
  EXPECT_TRUE(options.enable_fetch_log_engine_);
  options.enable_fetch_log_engine_ = false;
  options.reset();
  EXPECT_TRUE(options.enable_fetch_log_engine_);
}

} // namespace palf
} // namespace oceanbase

int main(int argc, char **argv)
{
  OB_LOGGER.set_file_name("test_fetch_log_engine.log", true);
  testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
