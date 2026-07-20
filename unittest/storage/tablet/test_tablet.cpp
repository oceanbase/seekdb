
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

#define USING_LOG_PREFIX STORAGE

#include "mtlenv/mock_tenant_module_env.h"
#include "storage/tablet/ob_tablet_binding_info.h"
#include "storage/ddl/ob_tablet_ddl_kv.h"

namespace oceanbase
{
namespace unittest
{

using namespace storage;
using namespace common;
using namespace share;

#define ALLOC_AND_INIT(allocator, addr, args...)                                  \
  do {                                                                            \
    if (OB_SUCC(ret)) {                                                           \
      if (OB_FAIL(ObTabletObjLoadHelper::alloc_and_new(allocator, addr.ptr_))) {  \
        LOG_WARN("fail to allocate and new object", K(ret));                      \
      } else if (OB_FAIL(addr.get_ptr()->init(allocator, args))) {                \
        LOG_WARN("fail to initialize tablet member", K(ret), K(addr));            \
      }                                                                           \
    }                                                                             \
  } while (false)                                                                 \

class TestTablet : public ::testing::Test
{
public:
  TestTablet();
  virtual ~TestTablet();
  virtual void SetUp();
  virtual void TearDown();
  static void SetUpTestCase();
  static void TearDownTestCase();
  void pull_ddl_memtables(ObIArray<ObDDLKV *> &ddl_kvs)
  {
    for (int64_t i = 0; i < ddl_kv_count_; ++i) {
      ASSERT_EQ(OB_SUCCESS, ddl_kvs.push_back(ddl_kvs_[i]));
    }
    std::cout<< "pull ddl memtables:" << ddl_kv_count_ << std::endl;
  }
  void reproducing_bug();
private:
  ObArenaAllocator allocator_;
  ObDDLKV **ddl_kvs_;
  volatile int64_t ddl_kv_count_;
};

TestTablet::TestTablet()
  : ddl_kvs_(nullptr),
    ddl_kv_count_(0)
{
}

TestTablet::~TestTablet()
{
}

void TestTablet::SetUpTestCase()
{
  ASSERT_EQ(OB_SUCCESS, ObTimerService::get_instance().start());
  EXPECT_EQ(OB_SUCCESS, MockTenantModuleEnv::get_instance().init());
}

void TestTablet::TearDownTestCase()
{
  MockTenantModuleEnv::get_instance().destroy();
  ObTimerService::get_instance().stop();
  ObTimerService::get_instance().wait();
  ObTimerService::get_instance().destroy();
}

void TestTablet::SetUp()
{
  OB_LOG(INFO, "ObTabletMeta", K(sizeof(ObTabletMeta)),
        K(sizeof(ObTabletRestoreState)), K(sizeof(ObTabletReportStatus)), K(sizeof(ObTabletTableStoreFlag)),
        K(sizeof(ObTabletSpaceUsage)));

  const int64_t tablet_size = sizeof(ObTablet);
  const int64_t rowkey_size = sizeof(ObRowkeyReadInfo);
  const int64_t assert_size = tablet_size + rowkey_size;

  OB_LOG(INFO, "ObTablet", K(assert_size), K(tablet_size), K(rowkey_size),
        K(sizeof(ObTabletMdsData)), K(sizeof(ObTabletHandle)),
        K(sizeof(ObTabletComplexAddr<ObTabletMacroInfo>)), K(sizeof(ObTabletPointerHandle)),
        K(sizeof(ObMetaDiskAddr)), K(sizeof(common::SpinRWLock)), K(sizeof(ObTabletStatusCache)),
        K(sizeof(ObDDLInfoCache)), K(sizeof(ObTableStoreCache)));
}

void TestTablet::TearDown()
{
}

class TestTableStore
{
public:
  int init(ObArenaAllocator &allocator, TestTablet &tablet)
  {
    int ret = OB_SUCCESS;
    ObArray<ObDDLKV *> ddl_kvs;
    tablet.pull_ddl_memtables(ddl_kvs);
    ret = ddl_kvs_.init(allocator, ddl_kvs);
    const int64_t count = ddl_kvs_.count();
    std::cout<< "init table store:" << ddl_kvs.count() << ", " << count <<std::endl;
    STORAGE_LOG(ERROR, "ddl kvs", K(ddl_kvs), K(ddl_kvs_));
    return ret;
  }
  void reproducing_bug(ObArenaAllocator &allocator)
  {
    ObArray<ObDDLKV *> ddl_kvs;
    for (int64_t i = 0; i < 3; ++i) {
      ObDDLKV *ddl_kv = new ObDDLKV();
      ddl_kvs.push_back(ddl_kv);
    }
    ddl_kvs_.init(allocator, ddl_kvs);
    const int64_t count = ddl_kvs_.count();
    std::cout<< "table store reproducing_bug:" << ddl_kvs.count() << ", " << count <<std::endl;
  }
  TO_STRING_KV(K(ddl_kvs_));
private:
  ObDDLKVArray ddl_kvs_;
};

void TestTablet::reproducing_bug()
{
  int ret = OB_SUCCESS;
  ObTabletComplexAddr<TestTableStore> table_store_addr;
  ddl_kvs_ = static_cast<ObDDLKV**>(allocator_.alloc(sizeof(ObDDLKV*) * ObTablet::DDL_KV_ARRAY_SIZE));
  ASSERT_TRUE(nullptr != ddl_kvs_);
  ddl_kvs_[0] = new ObDDLKV();
  ddl_kvs_[1] = new ObDDLKV();
  ddl_kvs_[2] = new ObDDLKV();
  std::cout<< "reproducing_bug 1:" << ddl_kv_count_ << std::endl;
  ddl_kv_count_ = 3;
  std::cout<< "reproducing_bug 2:" << ddl_kv_count_ << std::endl;
  ALLOC_AND_INIT(allocator_, table_store_addr, (*this));
  if (ddl_kv_count_ != table_store_addr.get_ptr()->ddl_kvs_.count()) {
    std::cout<< "reproducing_bug 3:" << ddl_kv_count_ << ", " << table_store_addr.get_ptr()->ddl_kvs_.count() << std::endl;
    // This is defense code. If it runs at here, it must be a bug. And, just abort to preserve the enviroment
    // for debugging. Please remove me, after the problem is found.
    ob_abort();
  }
}

TEST_F(TestTablet, reproducing_bug_53174886)
{
  TestTableStore table_store;
  table_store.reproducing_bug(allocator_);
  reproducing_bug();
}

}  // end namespace unittest
}  // end namespace oceanbase

int main(int argc, char **argv)
{
  system("rm -f test_tablet.log*");
  oceanbase::common::ObLogger::get_logger().set_log_level("INFO");
  OB_LOGGER.set_file_name("test_tablet.log", true, true);
  testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
