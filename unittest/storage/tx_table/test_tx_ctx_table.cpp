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
#define UNITTEST

#include "storage/ls/ob_freezer.h"
#include "storage/ls/ob_ls.h"
#include "storage/meta_mem/ob_storage_meta_mem_mgr.h"
#include "storage/tx/ob_tx_ctx.h"
#include "storage/tx_table/ob_tx_ctx_memtable_mgr.h"
#include "storage/tx_table/ob_tx_ctx_table.h"
#include "share/rc/ob_module_provider.h"

namespace oceanbase
{
using namespace ::testing;
using namespace transaction;
using namespace storage;
using namespace blocksstable;
using namespace share;

namespace share
{
int ObTxDataAllocator::init(const char* label)
{
  int ret = OB_SUCCESS;
  ObMemAttr mem_attr;
  if (OB_FAIL(slice_allocator_.init(
                 storage::TX_DATA_SLICE_SIZE, OB_MALLOC_NORMAL_BLOCK_SIZE, common::default_blk_alloc, mem_attr))) {
    SHARE_LOG(WARN, "init slice allocator failed", KR(ret));
  } else {
    slice_allocator_.set_nway(ALLOC_TX_DATA_MAX_CONCURRENCY);
    is_inited_ = true;
  }
  return ret;
}

void *ObTxDataAllocator::alloc(const bool enable_throttle, const int64_t abs_expire_time)
{
  void *res = slice_allocator_.alloc();
  return res;
}
};

namespace unittest
{

class ObTxCtxTableRecoverHelperUT : public ObTxCtxTableRecoverHelper
{
public:
  virtual int recover_one_tx_ctx_(transaction::ObLSTxCtxMgr* ls_tx_ctx_mgr,
                                  ObTxCtxTableInfo& ctx_info)
  {
    TRANS_LOG(INFO, "recover_one_tx_ctx_ called", K(ctx_info));
    recover_tx_id_arr_.push_back(ctx_info.tx_id_);
    return 0;
  }

  bool tx_id_recovered(transaction::ObTransID tx_id)
  {
    bool exist = false;

    for (int64_t i = 0; i < recover_tx_id_arr_.size(); ++i) {
      if (recover_tx_id_arr_[i] == tx_id) {
        exist = true;
        break;
      }
    }
    return exist;
  }

private:
  std::vector<transaction::ObTransID> recover_tx_id_arr_;
};

class TestTxCtxTable : public ::testing::Test
{
public:
  TestTxCtxTable():
      ls_(),
      tablet_id_(LS_TX_DATA_TABLET),
      ls_id_(1),
      freezer_(&ls_),
      t3m_(),
      mt_mgr_(nullptr),
      ctx_mt_mgr_(nullptr),
      runtime_state_(),
      old_server_runtime_(nullptr),
      old_mp_(nullptr)
  {
    ObLSTabletService *tablet_svr = ls_.get_tablet_svr();
    tablet_svr->init(&ls_);
  }

public:
  static ObLSTxCtxMgr ls_tx_ctx_mgr_;
  static ObLSTxCtxMgr ls_tx_ctx_mgr2_;
  ObLS ls_;
  static int64_t ref_count_;
protected:
  virtual void SetUp() override
  {
    old_server_runtime_ = share::g_server_runtime;
    old_mp_ = share::g_mp;

    ObTxPalfParam palf_param((logservice::ObLogHandler *)(0x01));
    freezer_.init(&ls_);
    EXPECT_EQ(OB_SUCCESS, t3m_.init());
    EXPECT_EQ(OB_SUCCESS,
              ls_tx_ctx_mgr_.init(
                                  &ls_.tx_table_,
                                  ls_.get_lock_table(),
                                  (ObTsMgr *)(0x01),
                                  nullptr,
                                  &palf_param,
                                  nullptr));
    EXPECT_EQ(OB_SUCCESS,
              ls_tx_ctx_mgr2_.init(
                                  &ls_.tx_table_,
                                  ls_.get_lock_table(),
                                  (ObTsMgr *)(0x01),
                                  nullptr,
                                  &palf_param,
                                  nullptr));
    ref_count_ = 0;
    ctx_mt_mgr_ = new ObTxCtxMemtableMgr();
    EXPECT_EQ(OB_SUCCESS, ctx_mt_mgr_->init(tablet_id_,
                                            &freezer_,
                                            &t3m_));
    mt_mgr_ = ctx_mt_mgr_;

    const int runtime_init_ret = runtime_state_.init();
    if (OB_SUCCESS == runtime_init_ret) {
      share::g_server_runtime = &runtime_state_;
    }
    ASSERT_EQ(OB_SUCCESS, runtime_init_ret);
    // g_mp defaults null in unittests; the TestBody's memtable
    // dec_ref() dereferences share::g_mp. Provide a stub provider (its getters
    // return nullptr, which the hit path tolerates).
    static share::ObIModuleProvider tx_ctx_table_module_provider;
    share::g_mp = &tx_ctx_table_module_provider;
  }
  virtual void TearDown() override
  {
    if (nullptr != ctx_mt_mgr_) {
      ctx_mt_mgr_->destroy();
    }
    ls_tx_ctx_mgr_.reset();
    ls_tx_ctx_mgr2_.reset();
    delete mt_mgr_;
    mt_mgr_ = NULL;
    ctx_mt_mgr_ = NULL;

    bool all_table_cleaned = false;
    const int gc_ret = t3m_.gc_tables_in_queue(all_table_cleaned);
    t3m_.destroy();

    const int64_t remaining_ref_count = ref_count_;

    share::g_mp = old_mp_;
    share::g_server_runtime = old_server_runtime_;
    runtime_state_.destroy();
    old_mp_ = nullptr;
    old_server_runtime_ = nullptr;

    EXPECT_EQ(OB_SUCCESS, gc_ret);
    EXPECT_EQ(0, remaining_ref_count);
  }
public:
  ObTabletID tablet_id_;
  ObLSID ls_id_;
  ObFreezer freezer_;
  ObStorageMetaMemMgr t3m_;
  ObIMemtableMgr *mt_mgr_;
  ObTxCtxMemtableMgr *ctx_mt_mgr_;
  ObTxDataAllocator tx_data_allocator_;
  ObTxDataOpAllocator tx_data_op_allocator_;

  ObServerRuntimeState runtime_state_;
  ObServerRuntimeState *old_server_runtime_;
  ObIModuleProvider *old_mp_;
};

ObLSTxCtxMgr TestTxCtxTable::ls_tx_ctx_mgr_;
ObLSTxCtxMgr TestTxCtxTable::ls_tx_ctx_mgr2_;
int64_t TestTxCtxTable::ref_count_;

TEST_F(TestTxCtxTable, test_tx_ctx_memtable_mgr)
{
  EXPECT_EQ(0, TestTxCtxTable::ref_count_);
  CreateMemtableArg arg;
  EXPECT_EQ(OB_SUCCESS, mt_mgr_->create_memtable(arg));

  EXPECT_EQ(1, TestTxCtxTable::ref_count_);

  ObTableHandleV2 handle1;
  ObTxCtxMemtable *memtable1 = NULL;
  EXPECT_EQ(OB_SUCCESS, mt_mgr_->get_active_memtable(handle1));
  EXPECT_EQ(true, handle1.get_table()->is_tx_ctx_memtable());
  EXPECT_EQ(OB_SUCCESS, handle1.get_tx_ctx_memtable(memtable1));

  ObSEArray<ObTableHandleV2, 5> handles2;
  ObTableHandleV2 handle2;
  ObTxCtxMemtable *memtable2 = NULL;
  EXPECT_EQ(OB_SUCCESS, mt_mgr_->get_all_memtables(handles2));
  EXPECT_EQ(1, handles2.count());
  handle2 = handles2[0];
  EXPECT_EQ(true, handle2.get_table()->is_tx_ctx_memtable());
  EXPECT_EQ(OB_SUCCESS, handle2.get_tx_ctx_memtable(memtable2));

  EXPECT_EQ(memtable1, memtable2);
  handle2.reset();
  handles2.reset();
  EXPECT_EQ(1, TestTxCtxTable::ref_count_);

  TRANS_LOG(INFO, "[TX_CTX_TABLE] tx ctx memtable mgr test successfully", KPC(ctx_mt_mgr_), K(TestTxCtxTable::ref_count_));

  ObArenaAllocator allocator;
  ObTableReadInfo read_info;
  ObTableIterParam param;
  ObArray<ObColDesc> columns_;
  columns_.reset();
  ObColDesc col_desc;
  columns_.push_back(col_desc);
  param.reset();
  read_info.init(allocator, 16000, 1, columns_, nullptr/*storage_cols_index*/);
  param.tablet_id_ = tablet_id_;
  param.read_info_ = &read_info;
  param.is_multi_version_minor_merge_ = true;

  ObTableAccessContext context;
  ObStoreCtx store_ctx;
  context.reset();
  context.store_ctx_ = &store_ctx;
  context.allocator_ = &allocator;
  context.stmt_allocator_ = &allocator;
  context.merge_scn_.convert_from_ts(996);
  context.is_inited_ = true;

  ObDatumRange key_range;
  ObStoreRowIterator *row_iter = NULL;
  const ObDatumRow *row = NULL;
  ObDatumRow row_copy;

  EXPECT_EQ(OB_SUCCESS, memtable1->scan(param,
                                        context,
                                        key_range,
                                        row_iter));

  EXPECT_EQ(OB_ITER_END, row_iter->get_next_row(row));

  row_iter->reset();
  row_iter = NULL;
  row = NULL;

  ObTransID id1(1);
  static ObTxCtx ctx1;
  ctx1.trans_id_ = id1;
  ctx1.is_inited_ = true;
  ctx1.exec_info_.max_applying_log_ts_.convert_from_ts(1);
  ctx1.replay_completeness_.set(true);
  ctx1.rec_log_ts_.convert_from_ts(996);
  ObTxData data1;
  // ctx1.tx_data_ = &data1;
  ctx1.ctx_tx_data_.test_init(data1, &ls_tx_ctx_mgr_);

  ObTransID id2(2);
  static ObTxCtx ctx2;
  ctx2.trans_id_ = id2;
  ctx2.is_inited_ = true;
  ctx2.exec_info_.max_applying_log_ts_.convert_from_ts(2);
  ctx2.replay_completeness_.set(true);
  ctx2.rec_log_ts_.convert_from_ts(996);
  ObTxData data2;
  // ctx2.tx_data_ = &data2;
  ctx2.ctx_tx_data_.test_init(data2, &ls_tx_ctx_mgr_);
  ObTransCtx *ctx = NULL;
  EXPECT_EQ(OB_SUCCESS, ls_tx_ctx_mgr_.ls_tx_ctx_map_.insert_and_get(id1, &ctx1, &ctx));
  EXPECT_EQ(OB_SUCCESS, ls_tx_ctx_mgr_.ls_tx_ctx_map_.insert_and_get(id2, &ctx2, &ctx));

  int64_t idx = 0;
  // ObTransSSTableDurableCtxInfo ctx_info;
  ObTxCtxTableInfo ctx_info;
  ObSliceAlloc slice_allocator;
  ObTxDataTable tx_data_table;
  ObMemAttr attr;
  tx_data_allocator_.init("test");
  tx_data_table.tx_data_allocator_ = &tx_data_allocator_;
  tx_data_op_allocator_.init();

  ObTxPalfParam palf_param((logservice::ObLogHandler *)(0x01));

  ObTxCtxTableRecoverHelperUT recover_helper;
  ObLSTxCtxMgr* ls_tx_ctx_mgr_recover = &unittest::TestTxCtxTable::ls_tx_ctx_mgr2_;

  EXPECT_EQ(OB_SUCCESS, memtable1->scan(param,
                                        context,
                                        key_range,
                                        row_iter));

  recover_helper.reset();
  for (int64_t ctx_idx = 0; ctx_idx < 2; ++ctx_idx) {
    ls_tx_ctx_mgr_recover->reset();
    EXPECT_EQ(OB_SUCCESS,
              ls_tx_ctx_mgr_recover->init(
                                          &ls_.tx_table_,
                                          &ls_.lock_table_,
                                          (ObTsMgr *)(0x01),
                                          nullptr,
                                          &palf_param,
                                          nullptr));
    ObTxCtxMemtableScanIterator* tx_ctx_memtable_iter = dynamic_cast<ObTxCtxMemtableScanIterator*>(row_iter);
    ASSERT_EQ(OB_SUCCESS, tx_ctx_memtable_iter->TEST_set_max_value_length(32));
    do {
      EXPECT_EQ(OB_SUCCESS, row_iter->get_next_row(row));
      TRANS_LOG(INFO, "row_info", K(*row));

      int64_t meta_col = TX_CTX_TABLE_META_COLUMN +
        ObMultiVersionRowkeyHelpper::get_extra_rowkey_col_cnt();
      int64_t value_col = meta_col + 1;
      row_copy.reset();
      row_copy.init(allocator, 8);
      row_copy.deep_copy(*row, allocator);
      row_copy.storage_datums_[TX_CTX_TABLE_META_COLUMN] = row_copy.storage_datums_[meta_col];
      row_copy.storage_datums_[TX_CTX_TABLE_VAL_COLUMN] = row_copy.storage_datums_[value_col];
      TRANS_LOG(INFO, "row_info projected", K(row_copy));
      ASSERT_EQ(OB_SUCCESS, recover_helper.recover(row_copy, tx_data_table, ls_tx_ctx_mgr_recover));
    } while (tx_ctx_memtable_iter->has_unmerged_buf_);
  }
  EXPECT_EQ(true, recover_helper.tx_id_recovered(id1));
  EXPECT_EQ(true, recover_helper.tx_id_recovered(id2));
  TRANS_LOG(INFO, "[TX_CTX_TABLE] successfully recover");
/*
  TRANS_LOG(INFO, "[TX_CTX_TABLE] get next row return", KPC(row));
  EXPECT_EQ(OB_SUCCESS, ObTxCtxTable::TEST_recover(*row, idx, ctx_info, slice_allocator));
  EXPECT_EQ(0, idx);
  EXPECT_EQ(id1, ctx_info.tx_id_);
  TRANS_LOG(INFO, "[TX_CTX_TABLE] successfully recover", K(ctx_info));

  EXPECT_EQ(OB_SUCCESS, row_iter->get_next_row(row));
  TRANS_LOG(INFO, "[TX_CTX_TABLE] get next row return", KPC(row));
  EXPECT_EQ(OB_SUCCESS, ObTxCtxTable::TEST_recover(*row, idx, ctx_info, slice_allocator));
  EXPECT_EQ(1, idx);
  EXPECT_EQ(id2, ctx_info.tx_id_);
  TRANS_LOG(INFO, "[TX_CTX_TABLE] successfully recover", K(ctx_info));

  EXPECT_EQ(OB_ITER_END, row_iter->get_next_row(row));
  EXPECT_EQ(1, TestTxCtxTable::ref_count_);
*/
  TRANS_LOG(INFO, "[TX_CTX_TABLE] tx ctx memtable test successfully", KPC(memtable1), K(TestTxCtxTable::ref_count_));
  ctx1.is_inited_ = false;
  ctx2.is_inited_ = false;
}
} // namspace unittest

namespace storage
{
int ObTxCtxTable::acquire_ref_()
{
  int ret = OB_SUCCESS;

  ls_tx_ctx_mgr_ = &unittest::TestTxCtxTable::ls_tx_ctx_mgr_;
  unittest::TestTxCtxTable::ref_count_++;
  TRANS_LOG(INFO, "[TX_CTX_TABLE] tx ctx table acquire ref", K(unittest::TestTxCtxTable::ref_count_), K(this));

  return ret;
}

int ObTxCtxTable::release_ref_()
{
  int ret = OB_SUCCESS;

  ls_tx_ctx_mgr_ = NULL;
  unittest::TestTxCtxTable::ref_count_--;
  TRANS_LOG(INFO, "[TX_CTX_TABLE] tx ctx table release ref", K(unittest::TestTxCtxTable::ref_count_), K(this));

  return ret;
}

void ObTxData::dec_ref()
{
  return;
}

} // namespace storage

namespace transaction
{
int ObLSTxCtxMgr::init(ObTxTable *tx_table,
                       ObLockTable *lock_table,
                       ObTsMgr *ts_mgr,
                       ObTransService *txs,
                       ObITxLogParam *param,
                       ObITxLogAdapter *log_adapter)
{
  int ret = OB_SUCCESS;

  UNUSED(log_adapter);
  if (is_inited_) {
    TRANS_LOG(WARN, "ObLSTxCtxMgr inited twice");
    ret = OB_INIT_TWICE;
  } else {
    if (OB_FAIL(ls_tx_ctx_map_.init(lib::ObMemAttr("LSTxCtxMgr")))) {
      TRANS_LOG(WARN, "ls_tx_ctx_map_ init fail", KR(ret));
    } else {
      is_inited_ = true;
      stopped_ = false;
      block_tx_ = false;
      block_normal_tx_ = false;
      block_all_ = false;
      tx_table_ = tx_table;
      lock_table_ = lock_table,
      txs_ = txs;
      ts_mgr_ = ts_mgr;
      TRANS_LOG(INFO, "ObLSTxCtxMgr inited success", KP(this));
    }
  }

  return ret;
}
}
} // namespace oceanbase

int main(int argc, char **argv)
{
  system("rm -rf test_tx_ctx_table.log*");
  OB_LOGGER.set_file_name("test_tx_ctx_table.log");
  OB_LOGGER.set_log_level("INFO");
  STORAGE_LOG(INFO, "begin unittest: test tx ctx table");
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
