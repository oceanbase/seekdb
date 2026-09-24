/*
 * Copyright (c) 2026 OceanBase.
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

#include "lib/wait_event/ob_wait_event.h"
#include "storage/ddl/ob_table_fork_info.h"
#include "storage/ddl/ob_tablet_fork_task.h"
#include "storage/tmp_file/ob_tmp_file_io_ctx.h"
#include "storage/tx_table/ob_tx_data_cache.h"

namespace oceanbase
{
namespace storage
{
using namespace common;

TEST(StorageAssertCond, mismatched_fork_arrays_remain_recoverable)
{
  ObTableForkInfo info;
  info.table_id_ = 1001;
  info.schema_version_ = 1;
  info.task_id_ = 1;
  info.fork_snapshot_version_ = 1;
  info.data_format_version_ = 1;
  ASSERT_EQ(OB_SUCCESS, info.source_tablet_ids_.push_back(ObTabletID(1001)));
  ASSERT_EQ(OB_SUCCESS, info.source_tablet_ids_.push_back(ObTabletID(1002)));
  ASSERT_EQ(OB_SUCCESS, info.dest_tablet_ids_.push_back(ObTabletID(2001)));

  ObSEArray<ObTabletForkParam, 2> params;
  ObTabletForkParam param;
  EXPECT_EQ(OB_INVALID_ARGUMENT, info.generate_fork_params(params));
  EXPECT_EQ(OB_INVALID_ARGUMENT, info.get_tablet_fork_param(ObTabletID(1001), param));
  EXPECT_EQ(0, params.count());

  ASSERT_EQ(OB_SUCCESS, info.dest_tablet_ids_.push_back(ObTabletID(2002)));
  ASSERT_EQ(OB_SUCCESS, info.generate_fork_params(params));
  ASSERT_EQ(2, params.count());
  EXPECT_EQ(ObTabletID(1001), params.at(0).source_tablet_id_);
  EXPECT_EQ(ObTabletID(2002), params.at(1).dest_tablet_id_);
  ASSERT_EQ(OB_SUCCESS, info.get_tablet_fork_param(ObTabletID(1002), param));
  EXPECT_EQ(ObTabletID(2002), param.dest_tablet_id_);
  EXPECT_EQ(OB_ENTRY_NOT_EXIST, info.get_tablet_fork_param(ObTabletID(1003), param));
}

TEST(StorageAssertCond, tmp_file_io_preserves_public_validation)
{
  tmp_file::ObTmpFileIOCtx ctx;
  char buffer[32] = {};
  EXPECT_EQ(OB_NOT_INIT, ctx.wait());
  EXPECT_EQ(OB_NOT_INIT, ctx.prepare_read(buffer, sizeof(buffer)));

  ObIOFlag flag;
  flag.set_read();
  flag.set_wait_event(ObWaitEventIds::DB_FILE_DATA_READ);
  ASSERT_EQ(OB_SUCCESS, ctx.init(1, 1, true, flag, 1000000, false, false, false));
  EXPECT_EQ(OB_INVALID_ARGUMENT, ctx.prepare_read(nullptr, sizeof(buffer)));
  EXPECT_EQ(OB_INVALID_ARGUMENT, ctx.prepare_read(buffer, 0));
  ASSERT_EQ(OB_SUCCESS, ctx.prepare_read(buffer, sizeof(buffer), 0));
  EXPECT_EQ(OB_INVALID_ARGUMENT, ctx.update_data_size(sizeof(buffer) + 1));
  ASSERT_EQ(OB_SUCCESS, ctx.update_data_size(12));
  ASSERT_EQ(OB_SUCCESS, ctx.update_data_size(20));
  EXPECT_EQ(32, ctx.get_done_size());
  EXPECT_EQ(0, ctx.get_todo_size());
  EXPECT_EQ(OB_SUCCESS, ctx.wait());

  ASSERT_EQ(OB_SUCCESS, ctx.prepare_write(buffer, sizeof(buffer)));
  EXPECT_EQ(OB_SUCCESS, ctx.wait());
}

TEST(StorageAssertCond, tx_cache_copy_rejects_invalid_buffers)
{
  ObTxDataCacheKey source(transaction::ObTransID(42));
  alignas(ObTxDataCacheKey) char buffer[sizeof(ObTxDataCacheKey)];
  ObIKVCacheKey *copy = nullptr;
  EXPECT_EQ(OB_INVALID_ARGUMENT, source.deep_copy(nullptr, sizeof(buffer), copy));
  EXPECT_EQ(OB_INVALID_ARGUMENT, source.deep_copy(buffer, sizeof(buffer) - 1, copy));
  EXPECT_EQ(nullptr, copy);
  ASSERT_EQ(OB_SUCCESS, source.deep_copy(buffer, sizeof(buffer), copy));
  ASSERT_NE(nullptr, copy);
  EXPECT_TRUE(source == *copy);
  static_cast<ObTxDataCacheKey *>(copy)->~ObTxDataCacheKey();
}

} // namespace storage
} // namespace oceanbase
