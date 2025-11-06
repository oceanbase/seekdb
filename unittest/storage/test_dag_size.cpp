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
#define protected public
#define private public
#include "storage/ob_partition_merge_task.h"
#include "storage/ob_build_index_task.h"
#include "share/scheduler/ob_tenant_dag_scheduler.h"

namespace oceanbase
{
using namespace blocksstable;
using namespace common;
using namespace storage;
using namespace share::schema;
using namespace ::testing;
using namespace share;

namespace unittest
{

class TestDagSize : public ::testing::Test
{
public:
  TestDagSize()
    : tenant_id_(500),
      tenant_base_(500)
  {}
  ~TestDagSize() {}
  void SetUp()
  {


    ObTenantDagScheduler *scheduler = OB_NEW(ObTenantDagScheduler, ObModIds::TEST);
    tenant_base_.set(scheduler);
    ObTenantEnv::set_tenant(&tenant_base_);
    ASSERT_EQ(OB_SUCCESS, tenant_base_.init());

    ASSERT_EQ(OB_SUCCESS, scheduler->init(500));
  }
  void TearDown()
  {
    tenant_base_.destroy();
    ObTenantEnv::set_tenant(nullptr);
  }

private:
  const uint64_t tenant_id_;
  ObTenantBase tenant_base_;
};

TEST_F(TestDagSize, test_size)
{
  ObTenantDagScheduler *scheduler = MTL(ObTenantDagScheduler*);
  ASSERT_TRUE(nullptr != scheduler);

  ObBuildIndexDag *build_index_dag = NULL;
  ASSERT_EQ(OB_SUCCESS, scheduler->alloc_dag(build_index_dag));
  scheduler->free_dag(*build_index_dag);
  ObMigrateDag *migrate_dag = NULL;
  ASSERT_EQ(OB_SUCCESS, scheduler->alloc_dag(migrate_dag));
  scheduler->free_dag(*migrate_dag);
  ObSSTableMajorMergeDag *major_merge_dag = NULL;
  ASSERT_EQ(OB_SUCCESS, scheduler->alloc_dag(major_merge_dag));
  scheduler->free_dag(*major_merge_dag);
  ObSSTableMinorMergeDag *minor_merge_dag = NULL;
  ASSERT_EQ(OB_SUCCESS, scheduler->alloc_dag(minor_merge_dag));
  scheduler->free_dag(*minor_merge_dag);
  ObUniqueCheckingDag *unique_check_dag = NULL;
  ASSERT_EQ(OB_SUCCESS, scheduler->alloc_dag(unique_check_dag));
  scheduler->free_dag(*unique_check_dag);

  scheduler->destroy();
}

}
}

int main(int argc, char **argv)
{
  OB_LOGGER.set_log_level("INFO");
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
