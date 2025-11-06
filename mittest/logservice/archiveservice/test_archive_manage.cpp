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

#include "basic_archive.h"
#include "lib/ob_errno.h"
#include <cstdint>
#include "cluster/logservice/env/ob_simple_log_cluster_env.h"
#include "lib/time/ob_time_utility.h"

namespace oceanbase
{
namespace unittest
{
class MySimpleArchiveInstance : public ObSimpleArchive
{
public:
  MySimpleArchiveInstance() : ObSimpleArchive() {}
};
static const int64_t ONE_MINUTE = 60L * 1000 * 1000 * 1000;
TEST_F(MySimpleArchiveInstance, test_archive_mgr)
{
  int ret = OB_SUCCESS;
  // Create normal tenant and user table
  ret = prepare();
  EXPECT_EQ(OB_SUCCESS, ret);

  ret = prepare_dest();
  EXPECT_EQ(OB_SUCCESS, ret);
  const uint64_t tenant_id = tenant_ids_[0];
  int64_t round_id = 0;
  // =============== First Archive Initialization ================ //
  // Enable archiving
  round_id = 1;  // First round starts, round_id == 1
  ret = run_archive(tenant_id);
  EXPECT_EQ(OB_SUCCESS, ret);
  // Check rs archive status is BEGINNING
  ret = check_rs_beginning(tenant_id, round_id);
  EXPECT_EQ(OB_SUCCESS, ret);
  // Check rs archive status is DOING
  ret = check_rs_doing(tenant_id, round_id);
  EXPECT_EQ(OB_SUCCESS, ret);
  // Check rs archive progress
  ret = check_rs_archive_progress(tenant_id);
  EXPECT_EQ(OB_SUCCESS, ret);
  // Check the log stream archive status advancement
  ret = check_archive_progress(tenant_id);
  EXPECT_EQ(OB_SUCCESS, ret);
  // Check log stream archiving task
  ret = check_ls_archive_task(tenant_id);
  EXPECT_EQ(OB_SUCCESS, ret);
  // fake close archive component
  ret = fake_stop_component(tenant_id);
  EXPECT_EQ(OB_SUCCESS, ret);

  /*
   * TODO temporarily disable this part of the unit test content
   * functions to be completed:
   * 1. INTERRUPT persistence internal table is not covered in scenarios without piece records
   * 2. Switching piece and switching archive server may cause archive progress to retreat, rs cannot advance progress
   *
  // Check if all archive tasks are processed
  ret = check_task_finish(tenant_id);
  EXPECT_EQ(OB_SUCCESS, ret);

  // fake modify piece related information, change piece interval to second level
  ret = fake_piece_info_after_fake_stop(tenant_id, ONE_MINUTE);
  EXPECT_EQ(OB_SUCCESS, ret);

  // fake delete log stream archive task
  ret = fake_remove_ls(tenant_id);
  EXPECT_EQ(OB_SUCCESS, ret);

  // fake restart archive component
  ret = fake_restart_component(tenant_id);
  EXPECT_EQ(OB_SUCCESS, ret);

  // Check archive progress of rs after restarting archive component
  ret = check_rs_archive_progress(tenant_id);
  EXPECT_EQ(OB_SUCCESS, ret);

  // Check if log stream archive status advances
  ret = check_archive_progress(tenant_id, true);
  EXPECT_EQ(OB_SUCCESS, ret);
  */
  // =============== Close Archive ================ //
  ret = stop_archive();
  EXPECT_EQ(OB_SUCCESS, ret);

  ret = check_rs_stop(tenant_id, round_id);
  EXPECT_EQ(OB_SUCCESS, ret);
  // =============== Reopen Archive ================ //
  round_id = 2;
  ret = run_archive(tenant_id);
  EXPECT_EQ(OB_SUCCESS, ret);
  // Check if the archive component is in doing state
  ret = check_rs_doing(tenant_id, round_id);
  EXPECT_EQ(OB_SUCCESS, ret);
}

} // namespace unittest
} // namespace oceanbase

int main(int argc, char **argv)
{
  OB_LOGGER.set_file_name("test_archive_mgr.log", true, false);
  OB_LOGGER.set_log_level("INFO");
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
