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
#include "deps/oblib/src/lib/alloc/ob_tenant_ctx_allocator.h"
using namespace oceanbase::common;
using namespace oceanbase::lib;

class TestSQLArenaAllocator: public ::testing::Test
{
public:
  TestSQLArenaAllocator();
  virtual ~TestSQLArenaAllocator();
  virtual void SetUp();
  virtual void TearDown();
private:
  // disallow copy
  DISALLOW_COPY_AND_ASSIGN(TestSQLArenaAllocator);
protected:
  // function members
protected:
  // data members
};

TestSQLArenaAllocator::TestSQLArenaAllocator()
{
}

TestSQLArenaAllocator::~TestSQLArenaAllocator()
{
}

void TestSQLArenaAllocator::SetUp()
{
}

void TestSQLArenaAllocator::TearDown()
{
}

#define GET_DEFAULT() \
  ({ \
    ma->get_tenant_ctx_mod_usage(tenant_id, ObCtxIds::DEFAULT_CTX_ID, label, item); \
    int64_t hold = item.hold_; \
    hold; \
  })
#define GET_AREA() \
  ({ \
    ma->get_tenant_ctx_mod_usage(tenant_id, ObCtxIds::WORK_AREA, label, item); \
    int64_t hold = item.hold_; \
    hold; \
  })

int main(int argc, char **argv)
{
  OB_LOGGER.set_log_level("INFO");
  ::testing::InitGoogleTest(&argc,argv);
  return RUN_ALL_TESTS();
}
