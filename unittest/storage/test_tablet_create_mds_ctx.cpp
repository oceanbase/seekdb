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

#include "lib/allocator/page_arena.h"
#include "lib/ob_errno.h"
#include "share/rc/ob_server_runtime.h"
#include "storage/multi_data_source/compile_utility/compile_mapper.h"
#include "storage/multi_data_source/mds_ctx.h"
#include "storage/multi_data_source/ob_tablet_create_mds_ctx.h"
#include "storage/multi_data_source/runtime_utility/mds_factory.h"
#include "storage/tx/ob_trans_define.h"
#undef protected
#undef private

using namespace oceanbase::common;
using namespace oceanbase::share;
using namespace oceanbase::storage;
using namespace oceanbase::unittest;

namespace oceanbase
{
namespace storage
{
class ObMemstoreFreezer;
}
namespace unittest
{
class TestTabletCreateMdsCtx : public ::testing::Test
{
public:
  TestTabletCreateMdsCtx() = default;
  virtual ~TestTabletCreateMdsCtx() = default;

  void SetUp() override
  {
    old_memstore_freezer_ = share::server_service<storage::ObMemstoreFreezer>();
    // MdsFactory only checks whether the service has been bound before copying.
    share::bind_server_service<storage::ObMemstoreFreezer>(
        reinterpret_cast<storage::ObMemstoreFreezer *>(this));
  }

  void TearDown() override
  {
    share::bind_server_service<storage::ObMemstoreFreezer>(old_memstore_freezer_);
  }

protected:
  storage::ObMemstoreFreezer *old_memstore_freezer_ = nullptr;
};

TEST_F(TestTabletCreateMdsCtx, start_mds_ctx)
{
  int ret = OB_SUCCESS;

  mds::ObTabletCreateMdsCtx mds_ctx{mds::MdsWriter{transaction::ObTransID{123}}};

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
  ASSERT_EQ(ctx.writer_.writer_id_, mds_ctx.writer_.writer_id_);

  delete [] buffer;
}

TEST_F(TestTabletCreateMdsCtx, deep_copy_mds_ctx_with_sparse_binding_id)
{
  static_assert(mds::BufferCtxBindingTypeId<mds::MdsCtx>::value == 0);
  static_assert(mds::TupleTypeIdx<mds::BufferCtxTupleHelper, mds::MdsCtx>::value == 1);

  const transaction::ObTransID tx_id{123};
  mds::MdsCtx source_ctx{mds::MdsWriter{tx_id}};
  source_ctx.set_binding_type_id(mds::BufferCtxBindingTypeId<mds::MdsCtx>::value);
  ObArenaAllocator allocator{ObModIds::TEST};
  mds::BufferCtx *copied_ctx = nullptr;

  ASSERT_EQ(OB_SUCCESS,
            mds::MdsFactory::deep_copy_buffer_ctx(tx_id, source_ctx, copied_ctx, allocator));
  ASSERT_NE(nullptr, copied_ctx);
  mds::MdsCtx *copied_mds_ctx = dynamic_cast<mds::MdsCtx *>(copied_ctx);
  ASSERT_NE(nullptr, copied_mds_ctx);
  EXPECT_EQ(source_ctx.get_binding_type_id(), copied_mds_ctx->get_binding_type_id());
  EXPECT_EQ(source_ctx.get_writer().writer_type_, copied_mds_ctx->get_writer().writer_type_);
  EXPECT_EQ(source_ctx.get_writer().writer_id_, copied_mds_ctx->get_writer().writer_id_);

  copied_mds_ctx->~MdsCtx();
  allocator.free(copied_ctx);
}
} // namespace unittest
} // namespace oceanbase
