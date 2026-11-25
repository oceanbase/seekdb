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
#include "lib/worker.h"
#include "share/ob_share_util.h"

namespace oceanbase
{
namespace share
{
using namespace common;

class TestShareTimeout : public ::testing::Test
{
protected:
  void SetUp() override
  {
    saved_worker_deadline_ = THIS_WORKER.get_timeout_ts();
    THIS_WORKER.set_timeout_ts(INT64_MAX);
  }

  void TearDown() override
  {
    THIS_WORKER.set_timeout_ts(saved_worker_deadline_);
  }

  int64_t saved_worker_deadline_;
};

TEST_F(TestShareTimeout, default_budget)
{
  const int64_t before = ObTimeUtility::current_time();
  ObTimeoutCtx ctx;
  ASSERT_EQ(OB_SUCCESS, ObShareUtil::get_rs_default_timeout_ctx(ctx));
  EXPECT_GE(ctx.get_abs_timeout(), before + config::rpc_timeout());
#if defined(__APPLE__) || defined(_WIN32)
  EXPECT_GE(ctx.get_abs_timeout(), before + 10000000);
#else
  const int64_t after = ObTimeUtility::current_time();
  EXPECT_LE(ctx.get_abs_timeout(), after + config::rpc_timeout());
#endif
}

TEST_F(TestShareTimeout, nested_proxy_preserves_explicit_deadline)
{
  const int64_t deadline = ObTimeUtility::current_time() + 30000000;
  ObTimeoutCtx caller_ctx;
  ASSERT_EQ(OB_SUCCESS, caller_ctx.set_abs_timeout(deadline));
  ASSERT_EQ(OB_SUCCESS, caller_ctx.set_trx_timeout_us(30000000));
  {
    ObTimeoutCtx proxy_ctx;
    ASSERT_EQ(OB_SUCCESS, ObShareUtil::get_rs_default_timeout_ctx(proxy_ctx));
    EXPECT_EQ(deadline, proxy_ctx.get_abs_timeout());
    EXPECT_EQ(30000000, proxy_ctx.get_trx_timeout_us());
  }
  EXPECT_EQ(deadline, ObTimeoutCtx::get_ctx().get_abs_timeout());
}

TEST_F(TestShareTimeout, caller_deadline_is_not_extended)
{
  const int64_t deadline = ObTimeUtility::current_time() + 1000000;
  THIS_WORKER.set_timeout_ts(deadline);
  ObTimeoutCtx ctx;
  ASSERT_EQ(OB_SUCCESS, ObShareUtil::get_rs_default_timeout_ctx(ctx));
  EXPECT_EQ(deadline, ctx.get_abs_timeout());
}

TEST_F(TestShareTimeout, expired_caller_deadline_is_rejected)
{
  THIS_WORKER.set_timeout_ts(ObTimeUtility::current_time() - 1);
  ObTimeoutCtx ctx;
  EXPECT_EQ(OB_TIMEOUT, ObShareUtil::get_rs_default_timeout_ctx(ctx));
}

} // namespace share
} // namespace oceanbase

int main(int argc, char **argv)
{
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
