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

#ifndef OCEANBASE_TRANSACTION_TEST_BASIC_FAKE_DEFINE_
#define OCEANBASE_TRANSACTION_TEST_BASIC_FAKE_DEFINE_

#define protected public
#define private public

#include "storage/tx/ob_trans_define.h"
#include "storage/tx_table/ob_tx_table.h"
#include "lib/utility/ob_defer.h"
#include "storage/tx/ob_tx_log_adapter.h"
#include "storage/tx/ob_ts_mgr.h"
#include "storage/tx/ob_gti_source.h"
#include "storage/tx/ob_tx_replay_executor.h"
#include "storage/tx/ob_tx_ctx.h"
#include "share/rc/ob_server_runtime.h"

namespace oceanbase {
using namespace share;
using namespace memtable;


namespace transaction {

class ObFakeTxDataTable : public ObTxDataTable {
public:
  ObSliceAlloc slice_allocator_;
  ObTxDataAllocator __FAKE_ALLOCATOR_OBJ;
  ObTxDataAllocator *FAKE_ALLOCATOR = &__FAKE_ALLOCATOR_OBJ;
  ObTxDataOpAllocator __FAKE_ALLOCATOR_OBJ2;
  ObTxDataOpAllocator *FAKE_ALLOCATOR2 = &__FAKE_ALLOCATOR_OBJ2;

public:
  ObFakeTxDataTable() : arena_allocator_(), map_(arena_allocator_, 1 << 20 /*2097152*/)
  {
    IGNORE_RETURN map_.init();
    ObMemAttr mem_attr;
    mem_attr.label_ = "TX_DATA_TABLE";
    mem_attr.ctx_id_ = ObCtxIds::DEFAULT_CTX_ID;
    ObMemtableMgrHandle memtable_mgr_handle;
    OB_ASSERT(OB_SUCCESS == slice_allocator_.init(
                                sizeof(ObTxData), OB_MALLOC_NORMAL_BLOCK_SIZE, common::default_blk_alloc, mem_attr));
    slice_allocator_.set_nway(32);
    FAKE_ALLOCATOR->init("FAKE_A");
    FAKE_ALLOCATOR2->init();
    tx_data_allocator_ = FAKE_ALLOCATOR;
    is_inited_ = true;
  }
  virtual int init(ObLS *ls, ObTxCtxTable *tx_ctx_table) override
  {
    return OB_SUCCESS;
  }
  virtual int start() override { return OB_SUCCESS; }
  virtual void stop() override {}
  virtual void reset() override {}
  virtual void destroy() override {}
  virtual int alloc_tx_data(ObTxDataGuard &tx_data_guard,
                            const bool enable_throttle,
                            const int64_t abs_expire_time)
  {
    ObMemAttr attr;
    void *ptr = ob_malloc(TX_DATA_SLICE_SIZE, attr);
    ObTxData *tx_data = new (ptr) ObTxData();
    tx_data->ref_cnt_ = 100;
    tx_data->tx_data_allocator_ = FAKE_ALLOCATOR;
    tx_data->op_allocator_ = FAKE_ALLOCATOR2;
    tx_data_guard.init(tx_data);
    return OB_ISNULL(tx_data) ? OB_ALLOCATE_MEMORY_FAILED : OB_SUCCESS;
  }
  virtual int deep_copy_tx_data(const ObTxDataGuard &from_guard, ObTxDataGuard &to_guard) override
  {
    int ret = OB_SUCCESS;
    void *ptr = slice_allocator_.alloc();
    ObTxData *to = new (ptr) ObTxData();
    ObTxData *from = (ObTxData*)from_guard.tx_data();
    to->ref_cnt_ = 100;
    to->tx_data_allocator_ = FAKE_ALLOCATOR;
    to_guard.init(to);
    OX (*to = *from);
    return ret;
  }
  virtual void free_tx_data(ObTxData *tx_data)
  {
  }
  virtual int alloc_undo_status_node(ObUndoStatusNode *&undo_status_node) override
  {
    void *ptr = ob_malloc(TX_DATA_SLICE_SIZE, ObNewModIds::TEST);
    undo_status_node = new (ptr) ObUndoStatusNode();
    return OB_SUCCESS;
  }
  virtual int free_undo_status_node(ObUndoStatusNode *&undo_status_node) override
  {
    return OB_SUCCESS;
  }
  virtual int insert(ObTxData *&tx_data) override
  {
    int ret = OB_SUCCESS;
    OZ (map_.insert(tx_data->tx_id_, tx_data));
    return ret;
  }
  virtual int check_with_tx_data(const ObTransID tx_id,
                                 ObITxDataCheckFunctor &fn,
                                 ObTxDataGuard &tx_data_guard,
                                 share::SCN &recycled_scn) override
  {
    int ret = OB_SUCCESS;
    OZ (map_.get(tx_id, tx_data_guard));
    OZ (fn(*tx_data_guard.tx_data()));
    if (OB_ENTRY_NOT_EXIST == ret) { ret = OB_TRANS_CTX_NOT_EXIST; }
    return ret;
  }
  ObArenaAllocator arena_allocator_;
  ObTxDataHashMap map_;
};

class ObFakeTxTable : public ObTxTable {
public:
  ObFakeTxTable() : ObTxTable(tx_data_table_) {}
public:
  ObFakeTxDataTable tx_data_table_;
};

class ObFakeGtiSource : public ObIGtiSource
{
  int get_trans_id(int64_t &trans_id) {
    trans_id = ATOMIC_AAF(&tx_id, 1);
    return OB_SUCCESS;
  }
  int64_t tx_id = 66;
};

class ObFakeTsMgr : public ObTsMgr
{
private:
  int get_gts_error_ = OB_SUCCESS;
public:
  void inject_get_gts_error(int error) {
    get_gts_error_ = error;
  }
  void repair_get_gts_error() {
    get_gts_error_ = OB_SUCCESS;
  }
  void reset() {
    get_gts_error_ = OB_SUCCESS;
  }
  int get_gts(const MonotonicTs stc,
              share::SCN &scn,
              MonotonicTs &receive_gts_ts) override
  {
    UNUSED(stc);
    int ret = get_gts_error_;
    if (OB_SUCC(ret)) {
      const int64_t gts = ATOMIC_AAF(&gts_, 1);
      ret = scn.convert_for_gts(gts);
      receive_gts_ts = MonotonicTs::current_time();
    }
    return ret;
  }

  int get_gts(share::SCN &scn) override
  {
    int ret = get_gts_error_;
    if (OB_SUCC(ret)) {
      ret = scn.convert_for_gts(ATOMIC_AAF(&gts_, 1));
    }
    return ret;
  }

  int get_gts_sync(const int64_t timeout_us, share::SCN &scn) override
  {
    UNUSED(timeout_us);
    return get_gts(scn);
  }

  int wait_gts_elapse(const share::SCN &scn) override
  {
    int ret = get_gts_error_;
    if (OB_SUCC(ret)) {
      update_fake_gts(scn.get_val_for_gts() + 1);
    }
    return ret;
  }

  void update_fake_gts(const int64_t gts)
  {
    int64_t old_gts = ATOMIC_LOAD(&gts_);
    while (old_gts < gts && !ATOMIC_BCAS(&gts_, old_gts, gts)) {
      old_gts = ATOMIC_LOAD(&gts_);
    }
  }

  int64_t gts_ = 100;
};

class ObFakeTxLogAdapter : public ObITxLogAdapter, public share::ObThreadPool
{
public:
  virtual int start() {
    int ret = OB_SUCCESS;
    ObThreadPool::set_run_wrapper(share::server_runtime());
    ret = ObThreadPool::start();
    stop_ = false;
    TRANS_LOG(INFO, "start.FakeTxLogAdapter", KP(this));
    return ret;
  }

  void stop() {
    stop_ = true;
    cond_.signal();
    TRANS_LOG(INFO, "stop.FakeTxLogAdapter", KP(this));
    ObThreadPool::stop();
  }

  struct ApplyCbTask : public common::ObLink
  {
    int64_t replay_hint_;
    logservice::AppendCb *cb_;
  };

  struct ReplayCbTask : public common::ObLink
  {
    int64_t replay_hint_;
    char* log_buf_;
    int64_t log_size_;
    palf::LSN lsn_;
    int64_t log_ts_;
  };

  const static int64_t TASK_QUEUE_CNT = 128;
  ObSpScLinkQueue apply_task_queue_arr[TASK_QUEUE_CNT];
  ObSpScLinkQueue replay_task_queue_arr[TASK_QUEUE_CNT];
  share::SCN max_submit_scn_ =  share::SCN::invalid_scn();
  share::SCN max_committed_scn_ =  share::SCN::invalid_scn();

  void run1() {
    while(true) {
      int64_t process_cnt = 0;
      bool stop = stop_;
      if (!ATOMIC_LOAD(&pause_)) {
        for (int64_t i = 0; i < TASK_QUEUE_CNT; ++i) {
          while(true) {
            ObLink *task = apply_task_queue_arr[i].pop();
            if (task) {
              ++process_cnt;
              max_committed_scn_ = static_cast<ApplyCbTask*>(task)->cb_->__get_scn();
              static_cast<ApplyCbTask*>(task)->cb_->on_success();
              delete task;
              ATOMIC_DEC(&inflight_cnt_);
            } else {
              break;
            }
          }
        }
      }
      if (!pause_ && 0 == process_cnt && stop) {
        break;
      } else if (0 == process_cnt && ATOMIC_BCAS(&is_sleeping_, false, true)) {
        auto key = cond_.get_key();
        cond_.wait(key, 10 * 1000);
      }
    }
  }
  void wakeup() { if (ATOMIC_BCAS(&is_sleeping_, true, false)) { cond_.signal(); }}
  template <typename Function> int replay_all(Function& fn)
  {
    LOG_INFO("replay all begin");
    int ret = OB_SUCCESS;

    for (int64_t i = 0; i < TASK_QUEUE_CNT; ++i) {
      while(OB_SUCC(ret)) {
        ReplayCbTask *task = static_cast<ReplayCbTask*>(replay_task_queue_arr[i].pop());
        if (task) {
          ret = fn(task->log_buf_, task->log_size_, task->lsn_, task->log_ts_);
          delete task->log_buf_;
          delete task;
          ATOMIC_DEC(&unreplay_cnt_);
        } else {
          break;
        }
      }
    }
    LOG_INFO("replay all end", K(ret));
    return ret;
  }

  int submit_log(const char *buf,
                 const int64_t size,
                 const share::SCN &base_ts,
                 ObTxBaseLogCb *cb,
                 const bool need_nonblock,
                 const int64_t retry_timeout_us)
  {
    int ret = OB_SUCCESS;
    logservice::ObLogBaseHeader base_header;
    int64_t tmp_pos = 0;
    if (OB_FAIL(base_header.deserialize(buf, size, tmp_pos))) {
      LOG_WARN("log base header deserialize error", K(ret));
    } else {
      const int64_t replay_hint = base_header.get_replay_hint();
      const int64_t ts = MAX(base_ts.get_val_for_gts(), ts_) + 1;
      share::SCN scn;
      scn.convert_for_gts(ts);
      int64_t queue_idx = replay_hint % TASK_QUEUE_CNT;
      if (!ATOMIC_LOAD(&log_drop_)) {
        const palf::LSN lsn = palf::LSN(++lsn_);
        cb->set_log_ts(scn);
        cb->set_lsn(lsn);
        cb->set_submit_ts(ObTimeUtility::current_time());
        ts_ = ts;
        ApplyCbTask *apply_task = new ApplyCbTask();
        apply_task->replay_hint_ = replay_hint;
        apply_task->cb_ = cb;
        max_submit_scn_ = share::SCN::max(max_submit_scn_, scn);

        apply_task_queue_arr[queue_idx].push(apply_task);
        ATOMIC_INC(&inflight_cnt_);
        wakeup();

        ReplayCbTask *replay_task = new ReplayCbTask();
        replay_task->replay_hint_ = replay_hint;
        replay_task->log_buf_ = new char[size];
        memcpy(replay_task->log_buf_, buf, size);
        replay_task->log_size_ = size;
        replay_task->lsn_ = lsn;
        replay_task->log_ts_ = ts;
        replay_task_queue_arr[queue_idx].push(replay_task);
        ATOMIC_INC(&unreplay_cnt_);

        TRANS_LOG(INFO, "submit_log",
            K(replay_hint), K(inflight_cnt_), K(unreplay_cnt_), K(queue_idx));
        hex_dump(buf, size, true, OB_LOG_LEVEL_INFO);
      } else {
        TRANS_LOG(INFO, "drop_log",
            K(replay_hint), K(inflight_cnt_), K(unreplay_cnt_), K(queue_idx));
        hex_dump(buf, size, true, OB_LOG_LEVEL_INFO);
      }
    }
    return ret;
  }

  int get_max_decided_scn(share::SCN &scn) {
    int ret = OB_SUCCESS;
    share::SCN min_unreplayed_scn;
    share::SCN min_unapplyed_scn;
    min_unreplayed_scn.invalid_scn();
    min_unapplyed_scn.invalid_scn();

    for (int64_t i = 0; i < TASK_QUEUE_CNT; ++i) {
      if (!replay_task_queue_arr[i].empty()) {
        share::SCN tmp_scn;
        tmp_scn.convert_for_gts(
            static_cast<ReplayCbTask *>(replay_task_queue_arr[i].top())->log_ts_);
        if (min_unreplayed_scn.is_valid() && tmp_scn.is_valid()) {
          min_unreplayed_scn = share::SCN::min(tmp_scn, min_unreplayed_scn);
        } else if (tmp_scn.is_valid()) {
          min_unreplayed_scn = tmp_scn;
        }
      }
    }

    for (int64_t i = 0; i < TASK_QUEUE_CNT; ++i) {
      if (!apply_task_queue_arr[i].empty()) {
        share::SCN tmp_scn;
        tmp_scn = (static_cast<ObTxBaseLogCb *>(
                       (static_cast<ApplyCbTask *>(apply_task_queue_arr[i].top()))
                           ->cb_))
                      ->get_log_ts();
        if (min_unapplyed_scn.is_valid() && tmp_scn.is_valid()) {
          min_unapplyed_scn = share::SCN::min(tmp_scn, min_unapplyed_scn);
        } else if (tmp_scn.is_valid()) {
          min_unapplyed_scn = tmp_scn;
        }
      }
    }

    if (min_unapplyed_scn.is_valid() && min_unapplyed_scn.is_valid()) {
      scn = share::SCN::max(min_unapplyed_scn, min_unapplyed_scn);
    } else {
      scn = max_submit_scn_;
    }
    if (scn.is_valid()) {
      share::SCN::minus(scn, 1);
    }
    return OB_SUCCESS;
  }
  int get_append_mode_initial_scn(share::SCN &ref_scn) {
    int ret = OB_SUCCESS;
    ref_scn = share::SCN::invalid_scn();
    return ret;
  }

  int get_inflight_cnt() {
    return ATOMIC_LOAD(&inflight_cnt_);
  }

  int get_unreplay_cnt() {
    return ATOMIC_LOAD(&unreplay_cnt_);
  }

  void set_pause() {
    ATOMIC_SET(&pause_, true);
  }

  void clear_pause() {
    ATOMIC_SET(&pause_, false);
  }

  void set_log_drop() {
    ATOMIC_SET(&log_drop_, true);
  }

  void clear_log_drop() {
    ATOMIC_SET(&log_drop_, false);
  }

  bool pause_ = false;
  bool log_drop_ = false;
  int64_t ts_ = 1;
  int64_t lsn_ = 1;
  int64_t inflight_cnt_ = 0;
  int64_t unreplay_cnt_ = 0;
  bool is_sleeping_ = false;
  common::SimpleCond cond_;
};

class ObFakeTxReplayExecutor : public ObTxReplayExecutor
{
public:
  ObFakeTxReplayExecutor(storage::ObLS *ls,
                         storage::ObLSTxService *ls_tx_srv,
                         const palf::LSN &lsn,
                         const share::SCN &log_timestamp,
                         const logservice::ObLogBaseHeader &base_header)
    : ObTxReplayExecutor(ls, ls_tx_srv, lsn, log_timestamp, base_header)
  { memtable_ = nullptr; }
  ~ObFakeTxReplayExecutor() { }
  int set_memtable(memtable::ObMemtable* memtable)
  {
    memtable_ = memtable;
    return OB_SUCCESS;
  }
  int execute(const char *buf,
              const int64_t size,
              const int skip_pos)
  {
    return do_replay_(buf, size, skip_pos);
  }
  int replay_one_row_in_memtable_(memtable::ObMutatorRowHeader& row_head,
                                  memtable::ObMemtableMutatorIterator *mmi_ptr) override
  {
    int ret = OB_SUCCESS;
    storage::ObStoreCtx storeCtx;
    storeCtx.ls_ = ls_;
    storeCtx.mvcc_acc_ctx_.tx_ctx_ = ctx_;
    storeCtx.mvcc_acc_ctx_.mem_ctx_ = mt_ctx_;
    storeCtx.tablet_id_ = row_head.tablet_id_;
    storeCtx.mvcc_acc_ctx_.tx_id_ = ctx_->get_trans_id();

    switch (row_head.mutator_type_) {
    case memtable::MutatorType::MUTATOR_ROW: {
      if (OB_FAIL(memtable_->replay_row(storeCtx, log_ts_ns_, mmi_ptr_))) {
        TRANS_LOG(WARN, "[Replay Tx] replay row error", K(ret));
      } else {
        TRANS_LOG(INFO, "[Replay Tx] replay row in memtable success");
      }
      break;
    }
    default: {
      ret = OB_ERR_UNEXPECTED;
      TRANS_LOG(ERROR, "[Replay Tx] Unknown mutator_type", K(row_head.mutator_type_));
    } // default
    } // switch

    return ret;
  }

public:
  memtable::ObMemtable* memtable_;
};

class ObFakeLSTxService : public ObLSTxService
{
public:
  ObFakeLSTxService(ObLS *parent) : ObLSTxService(parent) { }
  ~ObFakeLSTxService() {}
  virtual share::SCN get_ls_weak_read_ts() override {
    return share::SCN::min_scn();
  }
};

} // transaction
} // oceanbase

#endif //OCEANBASE_TRANSACTION_TEST_BASIC_FAKE_DEFINE_
