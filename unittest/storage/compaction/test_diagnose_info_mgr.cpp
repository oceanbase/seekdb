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
#define USING_LOG_PREFIX STORAGE
#include <gtest/gtest.h>
#define private public
#define protected public
#include "src/storage/ob_i_store.h"
#include "mtlenv/mock_server_runtime_env.h"
namespace oceanbase
{
using namespace common;
using namespace compaction;
using namespace storage;

namespace unittest
{

class TestDiagnoseInfoMgr : public ::testing::Test
{
public:
  TestDiagnoseInfoMgr()
  : suspect_info_mgr_(NULL),
    diagnose_tablet_mgr_(NULL)
  {}
  ~TestDiagnoseInfoMgr() = default;
  void SetUp();
  void TearDown();
  int gene_suspect_info(
    const ObDiagnoseInfoPrio &prio,
    const ObMergeType &merge_type,
    const ObTabletID &tablet_id,
    ObScheduleSuspectInfo &info);
  ObScheduleSuspectInfoMgr *suspect_info_mgr_;
  ObDiagnoseTabletMgr *diagnose_tablet_mgr_;
  ObDiagnoseInfoParam<2, 0> param_;
};

void TestDiagnoseInfoMgr::SetUp()
{
  if (OB_ISNULL(suspect_info_mgr_)) {
    suspect_info_mgr_ = OB_NEW(ObScheduleSuspectInfoMgr, ObModIds::TEST);
  }

  if (OB_ISNULL(diagnose_tablet_mgr_)) {
    diagnose_tablet_mgr_ = OB_NEW(ObDiagnoseTabletMgr, ObModIds::TEST);
  }

  ObMallocAllocator *ma = ObMallocAllocator::get_instance();
  ASSERT_EQ(OB_SUCCESS, ma->set_allocator_limit(1LL << 30));
}

void TestDiagnoseInfoMgr::TearDown()
{
  if (OB_NOT_NULL(suspect_info_mgr_)) {
    suspect_info_mgr_->destroy();
    suspect_info_mgr_ = nullptr;
  }
  if (OB_NOT_NULL(diagnose_tablet_mgr_)) {
    diagnose_tablet_mgr_->destroy();
    diagnose_tablet_mgr_ = nullptr;
  }
}

int TestDiagnoseInfoMgr::gene_suspect_info(
  const ObDiagnoseInfoPrio &prio,
  const ObMergeType &merge_type,
  const ObTabletID &tablet_id,
  ObScheduleSuspectInfo &info)
{
  int ret = OB_SUCCESS;
  info.priority_ = static_cast<uint32_t>(prio);
  info.merge_type_ = merge_type;
  info.tablet_id_ = tablet_id;
  info.info_param_ = &param_;
  return ret;
}

bool judge_equal(const ObScheduleSuspectInfo &a, const ObScheduleSuspectInfo &b)
{
  return a.priority_ == b.priority_
    && a.tablet_id_ == b.tablet_id_
    && a.merge_type_ == b.merge_type_;
}

TEST_F(TestDiagnoseInfoMgr, test_add_del_suspect_info)
{
  int ret = OB_SUCCESS;
  ObArenaAllocator allocator;
  const ObTabletID tablet_id(1);
  ASSERT_EQ(OB_SUCCESS, ObScheduleSuspectInfoMgr::server_module_init(suspect_info_mgr_));

  ObScheduleSuspectInfo info;
  ObScheduleSuspectInfo ret_info;
  ret = gene_suspect_info(ObDiagnoseInfoPrio::DIAGNOSE_PRIORITY_LOW, MINOR_MERGE, tablet_id, info);
  ASSERT_EQ(OB_SUCCESS, ret);
  ret = suspect_info_mgr_->add_suspect_info(info.hash(), info);
  ASSERT_EQ(OB_SUCCESS, ret);

  // high priority could cover low priority
  info.priority_ = static_cast<uint32_t>(ObDiagnoseInfoPrio::DIAGNOSE_PRIORITY_HIGH);
  ret = suspect_info_mgr_->add_suspect_info(info.hash(), info);
  ASSERT_EQ(OB_SUCCESS, ret);

  ret = suspect_info_mgr_->get_with_param(info.hash(), ret_info, allocator);
  ASSERT_EQ(OB_SUCCESS, ret);
  ASSERT_EQ(true, judge_equal(info, ret_info));

  // mid priority couldn't cover high priority
  info.priority_ = static_cast<uint32_t>(ObDiagnoseInfoPrio::DIAGNOSE_PRIORITY_MID);
  ret = suspect_info_mgr_->add_suspect_info(info.hash(), info);
  ASSERT_EQ(OB_SUCCESS, ret);

  ret = suspect_info_mgr_->get_with_param(info.hash(), ret_info, allocator);
  ASSERT_EQ(OB_SUCCESS, ret);
  // still be high priority
  ASSERT_EQ(static_cast<uint32_t>(ObDiagnoseInfoPrio::DIAGNOSE_PRIORITY_HIGH), ret_info.priority_);
}

TEST_F(TestDiagnoseInfoMgr, test_diagnose_tablet_mgr)
{
  int ret = OB_SUCCESS;
  const ObTabletID tablet_id(1);
  ASSERT_EQ(OB_SUCCESS, ObDiagnoseTabletMgr::server_module_init(diagnose_tablet_mgr_));

  ret = diagnose_tablet_mgr_->add_diagnose_tablet(tablet_id, TYPE_DIAGNOSE_TABLET_MAX);
  ASSERT_EQ(OB_INVALID_ARGUMENT, ret);

  ret = diagnose_tablet_mgr_->add_diagnose_tablet(tablet_id, TYPE_MINOR_MERGE);
  ASSERT_EQ(OB_SUCCESS, ret);

  ret = diagnose_tablet_mgr_->add_diagnose_tablet(tablet_id, TYPE_MINOR_MERGE);
  ASSERT_EQ(OB_SUCCESS, ret);
  // same diagnose type is registed, return success anyway
  ret = diagnose_tablet_mgr_->add_diagnose_tablet(tablet_id, TYPE_MINOR_MERGE);
  ASSERT_EQ(OB_SUCCESS, ret);

  // same diagnose type is registed, return success anyway
  ret = diagnose_tablet_mgr_->add_diagnose_tablet(tablet_id, TYPE_RS_MAJOR_MERGE);
  ASSERT_EQ(OB_SUCCESS, ret);

  ret = diagnose_tablet_mgr_->delete_diagnose_tablet(tablet_id, TYPE_MINOR_MERGE);
  ASSERT_EQ(OB_SUCCESS, ret);
  ASSERT_EQ(1, diagnose_tablet_mgr_->diagnose_tablet_map_.size());
  // after remove all flag, registed tablet is deleted
  ret = diagnose_tablet_mgr_->delete_diagnose_tablet(tablet_id, TYPE_RS_MAJOR_MERGE);
  ASSERT_EQ(OB_SUCCESS, ret);
  ASSERT_EQ(0, diagnose_tablet_mgr_->diagnose_tablet_map_.size());
}

}//end namespace unittest
}//end namespace oceanbase

int main(int argc, char **argv)
{
  system("rm -f test_diagnose_info_mgr.log*");
  OB_LOGGER.set_file_name("test_diagnose_info_mgr.log");
  OB_LOGGER.set_log_level("DEBUG");
  testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
