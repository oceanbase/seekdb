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
#include <pthread.h>
#include "share/ob_rpc_struct.h"


namespace oceanbase
{
using namespace obrpc;
using namespace common;
namespace share
{

class TestRPCStruct: public ::testing::Test
{
public:
  TestRPCStruct() {}
  virtual ~TestRPCStruct() {}

};

#define PRINT_SIZE(T) \
do  \
{ \
  const int64_t size = sizeof(T); \
  const int64_t count = OB_MALLOC_BIG_BLOCK_SIZE / size; \
  STORAGE_LOG(INFO, "print size", K(#T), K(size), K(count)); \
} while(0); 

TEST(TestRPCStruct, print_size)
{
  PRINT_SIZE(ObMigrateReplicaArg);
  PRINT_SIZE(ObMigrateReplicaRes);
  PRINT_SIZE(ObChangeReplicaArg);
  PRINT_SIZE(ObChangeReplicaRes);
}

}
}

int main(int argc, char **argv)
{
  oceanbase::common::ObLogger::get_logger().set_log_level("INFO");
  testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
