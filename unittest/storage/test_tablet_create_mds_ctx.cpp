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

#define private public
#define protected public

#include "lib/ob_errno.h"
#include "storage/multi_data_source/ob_tablet_create_mds_ctx.h"
#include "storage/tx/ob_trans_define.h"

using namespace oceanbase::common;
using namespace oceanbase::share;
using namespace oceanbase::storage;
using namespace oceanbase::unittest;

namespace oceanbase
{
namespace unittest
{
class TestTabletCreateMdsCtx : public ::testing::Test
{
public:
  TestTabletCreateMdsCtx() = default;
  virtual ~TestTabletCreateMdsCtx() = default;
};

TEST_F(TestTabletCreateMdsCtx, start_transfer_mds_ctx)
{
  int ret = OB_SUCCESS;

  mds::ObTabletCreateMdsCtx mds_ctx{mds::MdsWriter{transaction::ObTransID{123}}};
  mds_ctx.set_ls_id(share::ObLSID(1001));

  // serialize
  const int64_t serialize_size = mds_ctx.get_serialize_size();
  char *buffer = new char[serialize_size]();
  int64_t pos = 0;
  ret = mds_ctx.serialize(buffer, serialize_size, pos);
  ASSERT_EQ(OB_SUCCESS, ret);

  // deserialize
  mds::ObTabletCreateMdsCtx ctx;
  pos = 0;
  ret = ctx.deserialize(buffer, serialize_size, pos);
  ASSERT_EQ(OB_SUCCESS, ret);
  ASSERT_EQ(pos, serialize_size);
  ASSERT_EQ(ctx.ls_id_, mds_ctx.ls_id_);
  ASSERT_EQ(ctx.writer_.writer_id_, mds_ctx.writer_.writer_id_);

  delete [] buffer;
}
} // namespace unittest
} // namespace oceanbase

int main(int argc, char **argv)
{
  system("rm -rf test_tablet_create_mds_ctx.log*");
  OB_LOGGER.set_file_name("test_tablet_create_mds_ctx.log", true);
  OB_LOGGER.set_log_level("INFO");
  testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
