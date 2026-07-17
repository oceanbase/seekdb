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

#include <thread>
#include <gtest/gtest.h>
#define private public
#define protected public
#define USING_LOG_PREFIX TRANS
#include "tx_node.h"
#include "../mock_utils/async_util.h"
#include "test_tx_dsl.h"

namespace oceanbase
{
using namespace ::testing;
using namespace transaction;
using namespace share;


static ObSharedMemAllocMgr MTL_MEM_ALLOC_MGR;
static FakeModuleProvider G_TEST_MODULE_PROVIDER;

namespace share {

ObTxDataThrottleGuard::~ObTxDataThrottleGuard() {}

int ObTenantTxDataAllocator::init(const char *label)
{
  int ret = OB_SUCCESS;
  ObMemAttr mem_attr;
  throttle_tool_ = &(MTL_MEM_ALLOC_MGR.share_resource_throttle_tool());
  if (OB_FAIL(slice_allocator_.init(
                 storage::TX_DATA_SLICE_SIZE, OB_MALLOC_NORMAL_BLOCK_SIZE, block_alloc_, mem_attr))) {
    SHARE_LOG(WARN, "init slice allocator failed", KR(ret));
  } else {
    slice_allocator_.set_nway(ObTenantTxDataAllocator::ALLOC_TX_DATA_MAX_CONCURRENCY);
    is_inited_ = true;
  }
  return ret;
}
int ObMemstoreAllocator::init()
{
  throttle_tool_ = &MTL_MEM_ALLOC_MGR.share_resource_throttle_tool();
  return arena_.init();
}
int ObMemstoreAllocator::AllocHandle::init()
{
  int ret = OB_SUCCESS;
  uint64_t tenant_id = 1;
  ObSharedMemAllocMgr *mtl_alloc_mgr = &MTL_MEM_ALLOC_MGR;
  ObMemstoreAllocator &host = mtl_alloc_mgr->memstore_allocator();
  (void)host.init_handle(*this);
  return ret;
}
};  // namespace share

namespace concurrent_control
{
int check_sequence_set_violation(const concurrent_control::ObWriteFlag ,
                                 const int64_t ,
                                 const ObTransID ,
                                 const blocksstable::ObDmlFlag ,
                                 const int64_t ,
                                 const ObTransID ,
                                 const blocksstable::ObDmlFlag ,
                                 const int64_t )
{
  return OB_SUCCESS;
}
}
class ObTestTx : public ::testing::Test
{
public:
  virtual void SetUp() override
  {
    oceanbase::ObClusterVersion::get_instance().update_data_version(DATA_CURRENT_VERSION);
    ObMallocAllocator::get_instance()->create_and_add_tenant_allocator();
    ObAddr ip_port(ObAddr::VER::IPV4, "119.119.0.1",2023);
    ObCurTraceId::init(ip_port);
    ObClockGenerator::init();
    const testing::TestInfo* const test_info =
      testing::UnitTest::GetInstance()->current_test_info();
    // publish the fake module set as the process-global provider (see tx_node.h).
    publish_test_module_provider(G_TEST_MODULE_PROVIDER, MTL_MEM_ALLOC_MGR);
    auto test_name = test_info->name();
    _TRANS_LOG(INFO, ">>>> starting test : %s", test_name);
  }
  virtual void TearDown() override
  {
    const testing::TestInfo* const test_info =
      testing::UnitTest::GetInstance()->current_test_info();
    auto test_name = test_info->name();
    _TRANS_LOG(INFO, ">>>> tearDown test : %s", test_name);
    ObClockGenerator::destroy();
    ObMallocAllocator::get_instance()->recycle_tenant_allocator();
  }
  MsgBus bus_;
};

TEST_F(ObTestTx, start_trans_expired)
{
  auto n1 = new ObTxNode(ObAddr(ObAddr::VER::IPV4, "127.0.0.1", 8888), bus_);

  DEFER(delete(n1));

  ASSERT_EQ(OB_SUCCESS, n1->start());
  auto guard = n1->get_tx_guard();
  ObTxDesc &tx = guard.get_tx_desc();
  ObTxParam tx_param;
  tx_param.timeout_us_ = 1000; // 1ms
  tx_param.access_mode_ = ObTxAccessMode::RW;
  tx_param.isolation_ = ObTxIsolationLevel::RC;
  tx_param.cluster_id_ = 100;
  ASSERT_EQ(OB_SUCCESS, n1->start_tx(tx, tx_param));
  usleep(100000); // 100ms
  // create tx ctx failed caused by trans_timeout
  ASSERT_EQ(OB_TRANS_TIMEOUT, n1->write(tx, 100, 112));
  ASSERT_EQ(OB_SUCCESS, n1->rollback_tx(tx));
  ASSERT_EQ(OB_SUCCESS, n1->wait_all_tx_ctx_is_destoryed());
}

TEST_F(ObTestTx, replay_basic)
{
  START_TX_REPLAY_PAIR(n1, n2);
  ObLSTxCtxMgr &n1_ctx_mgr = n1->txs_.tx_ctx_mgr_.get_tx_ctx_manager();

  {
    PREPARE_TX(n1, tx);
    PREPARE_TX_PARAM(tx_param);
    ASSERT_EQ(OB_SUCCESS, n1->start_tx(tx, tx_param));
    ASSERT_EQ(OB_SUCCESS, n1->write(tx, 100, 112));
    ASSERT_EQ(OB_SUCCESS, n1->commit_tx(tx, n1->ts_after_ms(500)));
  }
  n1->wait_all_redolog_applied();
  ASSERT_EQ(OB_SUCCESS, n1->wait_all_tx_ctx_is_destoryed());
  ASSERT_EQ(0, n1_ctx_mgr.get_tx_ctx_count());

  auto replay_to_n2 = [n2](const void *buffer,
                           const int64_t nbytes,
                           const palf::LSN &lsn,
                           const int64_t ts_ns) {
    return n2->replay(buffer, nbytes, lsn, ts_ns);
  };
  ASSERT_EQ(OB_SUCCESS, n2->fake_tx_log_adapter_->replay_all(replay_to_n2));

  ObLSTxCtxMgr &n2_ctx_mgr = n2->txs_.tx_ctx_mgr_.get_tx_ctx_manager();
  {
    PREPARE_TX(n2, tx);
    ObTxReadSnapshot snapshot;
    ASSERT_EQ(OB_SUCCESS,
              n2->get_read_snapshot(tx,
                                    ObTxIsolationLevel::RC,
                                    n2->ts_after_ms(100),
                                    snapshot));
    int64_t value = 0;
    ASSERT_EQ(OB_SUCCESS, n2->read(snapshot, 100, value));
    ASSERT_EQ(112, value);
  }
  ASSERT_EQ(OB_SUCCESS, n2->wait_all_tx_ctx_is_destoryed());
  ASSERT_EQ(0, n2_ctx_mgr.get_tx_ctx_count());
}

TEST_F(ObTestTx, rollback_with_branch_savepoint)
{
  START_ONE_TX_NODE(n1);
  PREPARE_TX(n1, tx);
  PREPARE_TX_PARAM(tx_param);
  CREATE_IMPLICIT_SAVEPOINT(n1, tx, tx_param, global_sp1);
  CREATE_BRANCH_SAVEPOINT(n1, tx, 100, sp_b100_1);
  ASSERT_EQ(OB_SUCCESS, n1->write(tx, 100, 111, 100));
  CREATE_BRANCH_SAVEPOINT(n1, tx, 200, sp_b200_1);
  ASSERT_EQ(OB_SUCCESS, n1->write(tx, 200, 211, 200));
  ASSERT_EQ(OB_SUCCESS, n1->write(tx, 101, 112, 100));
  ASSERT_EQ(OB_SUCCESS, n1->write(tx, 500, 505)); // global write
  ASSERT_EQ(OB_SUCCESS, n1->write(tx, 201, 212, 200));
  // rollback branch 200
  ASSERT_EQ(OB_SUCCESS, ROLLBACK_TO_IMPLICIT_SAVEPOINT(n1, tx, sp_b200_1, 2000*1000));
  // check branch 100 is readable
  int64_t val = 0;
  ASSERT_EQ(OB_SUCCESS, n1->read(tx, 101, val));
  ASSERT_EQ(val, 112);
  // check global write is readable
  ASSERT_EQ(OB_SUCCESS, n1->read(tx, 500, val));
  ASSERT_EQ(val, 505);
  // check branch 200 is un-readable
  ASSERT_EQ(OB_ENTRY_NOT_EXIST, n1->read(tx, 200, val));
  ASSERT_EQ(OB_ENTRY_NOT_EXIST, n1->read(tx, 201, val));
  // write with branch 200
  ASSERT_EQ(OB_SUCCESS, n1->write(tx, 206, 602, 200));
  // rollback branch 100
  ASSERT_EQ(OB_SUCCESS, ROLLBACK_TO_IMPLICIT_SAVEPOINT(n1, tx, sp_b100_1, 2000*1000));
  // check global write is readable
  ASSERT_EQ(OB_SUCCESS, n1->read(tx, 500, val));
  ASSERT_EQ(val, 505);
  // check branch 200 is readable
  ASSERT_EQ(OB_SUCCESS, n1->read(tx, 206, val));
  ASSERT_EQ(val, 602);
  // check branch 100 is un-readable
  ASSERT_EQ(OB_ENTRY_NOT_EXIST, n1->read(tx, 100, val));
  ASSERT_EQ(OB_ENTRY_NOT_EXIST, n1->read(tx, 101, val));
  // rollback global
  ASSERT_EQ(OB_SUCCESS, ROLLBACK_TO_IMPLICIT_SAVEPOINT(n1, tx, global_sp1, 2000 * 1000));
  // check global and branch 200 is un-readable
  ASSERT_EQ(OB_ENTRY_NOT_EXIST, n1->read(tx, 500, val));
  ASSERT_EQ(OB_ENTRY_NOT_EXIST, n1->read(tx, 206, val));
  ROLLBACK_TX(n1, tx);
}


#define TEST_MARK_ABORT_AND_COMMIT(FLG)                         \
  TEST_F(ObTestTx, commit_tx_sanity_check_flag_ ## FLG)         \
  {                                                             \
    START_ONE_TX_NODE(n1);                                      \
    PREPARE_TX(n1, tx);                                         \
    PREPARE_TX_PARAM(tx_param);                                 \
    CREATE_IMPLICIT_SAVEPOINT(n1, tx, tx_param, global_sp1);    \
    ASSERT_EQ(n1->write(tx, 1, 1), OB_SUCCESS);                 \
    ASSERT_EQ(tx.state_, ObTxDesc::State::IMPLICIT_ACTIVE);     \
    tx.flags_.FLG = true;                                       \
    const int commit_ret = COMMIT_TX(n1, tx, 50000);            \
    EXPECT_EQ(commit_ret, OB_TRANS_ROLLBACKED);                 \
  }
TEST_MARK_ABORT_AND_COMMIT(WRITE_STATE_ABORTED_)
TEST_MARK_ABORT_AND_COMMIT(WRITE_STATE_INCOMPLETE_)
#undef _MARK_ABORT_AND_COMMIT

////
/// APPEND NEW TEST HERE, USE PRE DEFINED MACRO IN FILE `test_tx.dsl`
/// SEE EXAMPLE: TEST_F(ObTestTx, rollback_savepoint_timeout)
///

} // oceanbase

int main(int argc, char **argv)
{
  uint64_t checksum = 1100101;
  uint64_t c = 0;
  uint64_t checksum1 = ob_crc64(checksum, (void*)&c, sizeof(uint64_t));
  uint64_t checksum2 = ob_crc64(c, (void*)&checksum, sizeof(uint64_t));
  int64_t tx_id = 21533427;
  uint64_t h = murmurhash(&tx_id, sizeof(tx_id), 0);
  system("rm -rf test_tx.log*");
  ObLogger &logger = ObLogger::get_logger();
  logger.set_file_name("test_tx.log", true); // audit
  logger.set_log_level(OB_LOG_LEVEL_DEBUG);
  ::testing::InitGoogleTest(&argc, argv);
  TRANS_LOG(INFO, "mmhash:", K(h), K(checksum1), K(checksum2));
  return RUN_ALL_TESTS();
}
