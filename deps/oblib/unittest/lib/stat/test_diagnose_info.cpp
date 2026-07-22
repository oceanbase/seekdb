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
#include "lib/stat/ob_diagnose_info.h"
#include "lib/stat/ob_diagnostic_info_guard.h"

namespace oceanbase
{
namespace common
{

TEST(ObDiagnoseRuntimeInfo, statistic_event)
{
  ObDIGlobalRuntimeCache::get_instance().reset();
  ObDiagnoseSessionInfo *session_info = ObDiagnoseSessionInfo::get_local_diagnose_info();
  ASSERT_NE(nullptr, session_info);
  session_info->reset();

  EVENT_INC(RPC_PACKET_IN);
  EVENT_ADD(RPC_PACKET_IN, 3);

  ObDiagnoseRuntimeInfo runtime_info;
  ASSERT_EQ(OB_SUCCESS,
      ObDIGlobalRuntimeCache::get_instance().get_runtime_info(runtime_info));
  EXPECT_EQ(4, runtime_info.get_add_stat_stats()
      .get(ObStatEventIds::RPC_PACKET_IN)->get_stat_value());
  EXPECT_EQ(4, session_info->get_add_stat_stats()
      .get(ObStatEventIds::RPC_PACKET_IN)->get_stat_value());
}

TEST(ObDiagnoseRuntimeInfo, wait_event_and_audit_guards)
{
  ObDIGlobalRuntimeCache::get_instance().reset();
  ObDiagnoseSessionInfo *session_info = ObDiagnoseSessionInfo::get_local_diagnose_info();
  ASSERT_NE(nullptr, session_info);
  session_info->reset();

  ObWaitEventDesc max_wait;
  ObWaitEventStat total_wait;
  {
    ObMaxWaitGuard max_guard(&max_wait);
    ObTotalWaitGuard total_guard(&total_wait);
    ObWaitEventGuard wait_guard(ObWaitEventIds::DEFAULT_COND_WAIT);
    ::usleep(50);
  }

  EXPECT_EQ(1, total_wait.total_waits_);
  EXPECT_EQ(ObWaitEventIds::DEFAULT_COND_WAIT, max_wait.event_no_);
  ObWaitEventDesc *last_wait = nullptr;
  ASSERT_EQ(OB_SUCCESS, session_info->get_event_history().get_last_wait(last_wait));
  ASSERT_NE(nullptr, last_wait);
  EXPECT_EQ(ObWaitEventIds::DEFAULT_COND_WAIT, last_wait->event_no_);

  ObDiagnoseRuntimeInfo runtime_info;
  ASSERT_EQ(OB_SUCCESS,
      ObDIGlobalRuntimeCache::get_instance().get_runtime_info(runtime_info));
  EXPECT_EQ(1, runtime_info.get_event_stats()
      .get(ObWaitEventIds::DEFAULT_COND_WAIT)->total_waits_);
}

TEST(ObDiagnoseRuntimeInfo, trace_guard_restores_context)
{
  ObDiagnoseRuntimeTraceContext *context = ObLocalDiagnosticInfo::get();
  ASSERT_NE(nullptr, context);
  ObDiagnoseRuntimeTraceContext::set_text(context->action_, "outer");
  {
    ObDIActionGuard guard("inner");
    EXPECT_STREQ("inner", context->action_);
  }
  EXPECT_STREQ("outer", context->action_);
}

} // namespace common
} // namespace oceanbase

int main(int argc, char **argv)
{
  oceanbase::common::ObLogger::get_logger().set_log_level("INFO");
  testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
