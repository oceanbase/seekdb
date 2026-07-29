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
#define USING_LOG_PREFIX TRANS
#include "test_tx_dsl.h"
#include "tx_node.h"
namespace oceanbase
{
using namespace ::testing;
using namespace transaction;
using namespace share;

static ObSharedMemAllocMgr SHARED_MEM_ALLOC_MGR;
static FakeModuleProvider G_TEST_MODULE_PROVIDER;

namespace share {

ObMdsThrottleGuard::~ObMdsThrottleGuard() {}
ObTxDataThrottleGuard::~ObTxDataThrottleGuard() {}

int ObTxDataAllocator::init(const char *label)
{
  int ret = OB_SUCCESS;
  ObMemAttr mem_attr;
  throttle_tool_ = &(SHARED_MEM_ALLOC_MGR.share_resource_throttle_tool());
  if (OB_FAIL(slice_allocator_.init(
                 storage::TX_DATA_SLICE_SIZE, OB_MALLOC_NORMAL_BLOCK_SIZE, block_alloc_, mem_attr))) {
    SHARE_LOG(WARN, "init slice allocator failed", KR(ret));
  } else {
    slice_allocator_.set_nway(ObTxDataAllocator::ALLOC_TX_DATA_MAX_CONCURRENCY);
    is_inited_ = true;
  }
  return ret;
}
int ObMemstoreAllocator::init()
{
  throttle_tool_ = &SHARED_MEM_ALLOC_MGR.share_resource_throttle_tool();
  return arena_.init();
}
int ObMemstoreAllocator::AllocHandle::init()
{
  int ret = OB_SUCCESS;
  ObSharedMemAllocMgr *mtl_alloc_mgr = &SHARED_MEM_ALLOC_MGR;
  ObMemstoreAllocator &host = mtl_alloc_mgr->memstore_allocator();
  (void)host.init_handle(*this);
  return ret;
}
};  // namespace share

namespace concurrent_control
{
int check_sequence_set_violation(const concurrent_control::ObWriteFlag,
                                 const int64_t,
                                 const ObTransID,
                                 const blocksstable::ObDmlFlag,
                                 const int64_t,
                                 const ObTransID,
                                 const blocksstable::ObDmlFlag,
                                 const int64_t)
{
  return OB_SUCCESS;
}
} // namespace concurrent_control

class ReplayLogEntryFunctor
{
public:
  ReplayLogEntryFunctor(ObTxNode *n) : n_(n) {}

  int operator()(const void *buffer,
                 const int64_t nbytes,
                 const palf::LSN &lsn,
                 const int64_t ts_ns)
  {
    return n_->replay(buffer, nbytes, lsn, ts_ns);
  }

private:
  ObTxNode *n_;
};


OB_NOINLINE int ObTransService::acquire_local_snapshot_(SCN &snapshot)
{
  int ret = OB_SUCCESS;
  snapshot = tx_version_mgr_.get_max_commit_ts(false);
  return ret;
}

bool NOTIFY_MDS_ERRSIM = false;

OB_NOINLINE int ObTxCtx::errsim_notify_mds_()
{
  int ret = OB_SUCCESS;

  if (NOTIFY_MDS_ERRSIM) {
    ret = OB_ERR_UNEXPECTED;
  }

  if (OB_FAIL(ret)) {
    TRANS_LOG(WARN, "errsim notify mds", K(ret), K(NOTIFY_MDS_ERRSIM));
  }

  return ret;
}

class ObTestRegisterMDS : public ::testing::Test
{
public:
  virtual void SetUp() override
  {
    const uint64_t tv = ObTimeUtility::current_time();
    ObCurTraceId::set(&tv);
    ObClockGenerator::init();
    const testing::TestInfo *const test_info =
        testing::UnitTest::GetInstance()->current_test_info();
    auto test_name = test_info->name();
    // publish the fake module set as the process-global provider (see tx_node.h).
    publish_test_module_provider(G_TEST_MODULE_PROVIDER, SHARED_MEM_ALLOC_MGR);
    _TRANS_LOG(INFO, ">>>> starting test : %s", test_name);
    LOG_INFO(">>>>>>starting>>>>>>>>", K(test_name));
  }
  virtual void TearDown() override
  {
    // Test-body guards have destroyed all ObTxNode instances by now. Restore
    // long-lived globals before any other teardown work can observe them.
    share::g_mp = &G_TEST_MODULE_PROVIDER;
    share::g_server_runtime = &share::g_bootstrap_server_runtime;
    const testing::TestInfo *const test_info =
        testing::UnitTest::GetInstance()->current_test_info();
    auto test_name = test_info->name();
    _TRANS_LOG(INFO, ">>>> tearDown test : %s", test_name);
    ObClockGenerator::destroy();
    LOG_INFO(">>>>>teardown>>>>>>>>", K(test_name));
  }
  MsgBus bus_;
};

TEST_F(ObTestRegisterMDS, basic)
{
  START_TX_REPLAY_PAIR(n1, n2);
  PREPARE_TX(n1, tx);
  PREPARE_TX_PARAM(tx_param);
  const char mds_marker = '\0';

  ASSERT_EQ(OB_SUCCESS, n1->start_tx(tx, tx_param));
  ASSERT_EQ(OB_SUCCESS, n1->txs_.register_mds_into_tx(tx, ObTxDataSourceType::DDL_TRANS,
                                                      &mds_marker, sizeof(mds_marker)));
  n2->wait_all_redolog_applied();
  ASSERT_EQ(OB_SUCCESS, n1->commit_tx(tx, n1->ts_after_ms(500)));

  ReplayLogEntryFunctor functor(n2);
  ASSERT_EQ(OB_SUCCESS, n2->fake_tx_log_adapter_->replay_all(functor));

  ASSERT_EQ(OB_SUCCESS, n1->wait_all_tx_ctx_is_destoryed());

  ASSERT_EQ(OB_SUCCESS, n2->wait_all_tx_ctx_is_destoryed());
}

TEST_F(ObTestRegisterMDS, oversized_mds_rejected)
{
  START_TX_REPLAY_PAIR(n1, n2);
  PREPARE_TX(n1, tx);
  PREPARE_TX_PARAM(tx_param);
  tx_param.timeout_us_ = 1000 * 1000 * 1000;
  const int64_t char_count = 2 * ObTxMultiDataSourceLog::MAX_MDS_LOG_SIZE;
  ObArenaAllocator allocator;
  char *mds_str = static_cast<char *>(allocator.alloc(char_count));
  ASSERT_NE(nullptr, mds_str);
  MEMSET(mds_str, 'M', char_count);

  ASSERT_EQ(OB_SUCCESS, n1->start_tx(tx, tx_param));
  ASSERT_EQ(OB_LOG_TOO_LARGE,
            n1->txs_.register_mds_into_tx(
                tx, ObTxDataSourceType::DDL_TRANS, mds_str, char_count));

  // The rejected registration rolls back its implicit savepoint. The transaction
  // must remain usable for the one-byte marker emitted by the local Change Stream.
  const char mds_marker = '\0';
  ASSERT_EQ(OB_SUCCESS,
            n1->txs_.register_mds_into_tx(
                tx, ObTxDataSourceType::DDL_TRANS, &mds_marker, sizeof(mds_marker)));
  n1->wait_all_redolog_applied();
  ASSERT_EQ(OB_SUCCESS, n1->commit_tx(tx, n1->ts_after_ms(100 * 1000)));

  ReplayLogEntryFunctor functor(n2);
  ASSERT_EQ(OB_SUCCESS, n2->fake_tx_log_adapter_->replay_all(functor));

  ASSERT_EQ(OB_SUCCESS, n1->wait_all_tx_ctx_is_destoryed());

  ASSERT_EQ(OB_SUCCESS, n2->wait_all_tx_ctx_is_destoryed());
}

TEST_F(ObTestRegisterMDS, notify_mds_error)
{
  START_TX_REPLAY_PAIR(n1, n2);
  PREPARE_TX(n1, tx);
  PREPARE_TX_PARAM(tx_param);
  const char mds_marker = '\0';

  ASSERT_EQ(OB_SUCCESS, n1->start_tx(tx, tx_param));

  NOTIFY_MDS_ERRSIM = true;
  DEFER(NOTIFY_MDS_ERRSIM = false);
  ASSERT_EQ(OB_ERR_UNEXPECTED, n1->txs_.register_mds_into_tx(tx, ObTxDataSourceType::DDL_TRANS,
                                                      &mds_marker, sizeof(mds_marker)));
  NOTIFY_MDS_ERRSIM = false;

  n2->wait_all_redolog_applied();
  ASSERT_EQ(OB_SUCCESS, n1->commit_tx(tx, n1->ts_after_ms(500)));

  ReplayLogEntryFunctor functor(n2);
  ASSERT_EQ(OB_SUCCESS, n2->fake_tx_log_adapter_->replay_all(functor));

  ASSERT_EQ(OB_SUCCESS, n1->wait_all_tx_ctx_is_destoryed());

  ASSERT_EQ(OB_SUCCESS, n2->wait_all_tx_ctx_is_destoryed());
}
} // namespace oceanbase

int main(int argc, char **argv)
{
  int64_t tx_id = 21533427;
  uint64_t h = murmurhash(&tx_id, sizeof(tx_id), 0);
  system("rm -rf test_register_mds.log*");
  ObLogger &logger = ObLogger::get_logger();
  logger.set_file_name("test_register_mds.log", true); // audit
  logger.set_log_level(OB_LOG_LEVEL_DEBUG);
  ::testing::InitGoogleTest(&argc, argv);
  TRANS_LOG(INFO, "mmhash:", K(h));
  return RUN_ALL_TESTS();
}
