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
#include <unistd.h>
#include <pthread.h>
#include <sys/syscall.h>
#include "lib/resource/ob_affinity_ctrl.h"
#include "lib/atomic/ob_atomic.h"
#include "lib/ob_define.h"
#include "lib/ob_errno.h"
#include "lib/oblog/ob_log.h"

using namespace oceanbase::common;

#define USING_LOG_PREFIX SERVER

TEST(TestAffiCtrl, test0)
{
  int ret;
  int node_status;

  ret = AFFINITY_CTRL.init();
  ASSERT_EQ(ret, 0);

  ASSERT_EQ(AFFINITY_CTRL.get_tls_node(), OB_NUMA_SHARED_INDEX);

  ret = oceanbase::lib::ObAffinityCtrl::get_instance().thread_bind_to_node(0);
  ASSERT_EQ(ret, OB_SUCCESS);
  ASSERT_EQ(oceanbase::lib::ObAffinityCtrl::get_tls_node(), 0);

  ret = oceanbase::lib::ObAffinityCtrl::get_instance().run_on_node(-1000);
  ASSERT_EQ(ret, OB_INVALID_ARGUMENT);
  ASSERT_EQ(oceanbase::lib::ObAffinityCtrl::get_tls_node(), 0);

  ret = oceanbase::lib::ObAffinityCtrl::get_instance().run_on_node(1000);
  ASSERT_EQ(ret, OB_INVALID_ARGUMENT);
  ASSERT_EQ(oceanbase::lib::ObAffinityCtrl::get_tls_node(), 0);
}


int main(int argc, char *argv[])
{
  OB_LOGGER.set_file_name("test_affinity_ctrl.log", true);
  OB_LOGGER.set_log_level("INFO");
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}

