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

#define USING_LOG_PREFIX SHARE
#include "lib/oblog/ob_log_module.h"
#include "lib/thread/ob_thread_name.h"
#include "lib/allocator/ob_malloc.h"
#include "lib/utility/serialization.h"
#include "storage/memtable/ob_memtable_mutator.h"
#include "share/change_stream/ob_change_stream_dispatcher.h"
#include "share/change_stream/ob_change_stream_plugin.h"
#include "share/change_stream/ob_change_stream_worker.h"
#include "share/change_stream/ob_change_stream_mgr.h"
#include "share/schema/ob_tenant_schema_service.h"
#include "share/ob_server_struct.h"
#include "share/ob_global_stat_proxy.h"
#include "common/ob_tablet_id.h"

namespace oceanbase
{
namespace share
{

ObCSDispatcher::ObCSDispatcher()
  : share::ObThreadPool(1),
    is_inited_(false),
    refresh_scn_(0),
    next_sn_(0),
    dispatch_sn_(0),
    next_commit_sn_(0),
    epoch_(0),
    dispatcher_epoch_(0),
    active_batch_count_(0)
{
}

ObCSDispatcher::~ObCSDispatcher()
{
  destroy();
}

int ObCSDispatcher::init()
{
  int ret = common::OB_SUCCESS;
  if (is_inited_) {
    ret = common::OB_INIT_TWICE;
  } else if (OB_FAIL(tx_ring_.init(0, 256))) {
    LOG_WARN("ObCSDispatcher: init tx_ring failed", K(ret));
  } else if (OB_FAIL(dispatch_cond_.init(ObWaitEventIds::THREAD_IDLING_COND_WAIT))) {
    LOG_WARN("ObCSDispatcher: dispatch_cond init failed", K(ret));
  } else if (FALSE_IT(ObThreadPool::set_run_wrapper(MTL_CTX()))) {
  } else if (OB_FAIL(ObThreadPool::init())) {
    LOG_WARN("ObCSDispatcher: thread pool init failed", K(ret));
  } else {
    next_sn_ = 0;
    dispatch_sn_ = 0;
    next_commit_sn_ = 0;
    is_inited_ = true;
  }
  return ret;
}

int ObCSDispatcher::start()
{
  int ret = common::OB_SUCCESS;
  if (!is_inited_) {
    ret = common::OB_NOT_INIT;
    LOG_WARN("ObCSDispatcher: not inited", K(ret));
  } else {
    if (OB_FAIL(ObThreadPool::start())) {
      LOG_WARN("ObCSDispatcher: thread pool start failed", K(ret));
    } else {
      LOG_INFO("CSDispatcher: thread pool started successfully");
    }
  }
  return ret;
}

int ObCSDispatcher::init_refresh_scn_()
{
  int ret = common::OB_SUCCESS;
  int64_t schema_version = 0;
  if (!is_inited_) {
    ret = common::OB_NOT_INIT;
    LOG_WARN("ObCSDispatcher: not inited", K(ret));
  } else if (OB_ISNULL(GCTX.sql_proxy_)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("CSDispatcher: GCTX.sql_proxy_ is null", K(ret));
  } else if (GCTX.in_bootstrap_ || GCTX.start_service_time_ <= 0) {
    ret = common::OB_NOT_INIT;
    LOG_WARN("ObCSDispatcher: wait bootstrap", K(ret));
  } else if (OB_FAIL(GCTX.schema_service_->get_tenant_refreshed_schema_version(MTL_ID(), schema_version))) {
    LOG_WARN("get schema_version failed", KR(ret));
  } else if (schema_version <= 0 || !ObSchemaService::is_formal_version(schema_version)) {
    ret = OB_SCHEMA_EAGAIN;
    LOG_WARN("schema is not formal", KR(ret));
  } else {
    // Load current refresh_scn from database (row may already exist or was just inserted).
    SCN current_refresh_scn;
    if (OB_FAIL(ObGlobalStatProxy::get_change_stream_refresh_scn(
            *GCTX.sql_proxy_, MTL_ID(), false /* for_update */, current_refresh_scn))) {
      LOG_WARN("CSDispatcher: failed to load change_stream_refresh_scn, use 0", KR(ret));
    } else {
      refresh_scn_ = current_refresh_scn.get_val_for_gts();
      LOG_INFO("CSDispatcher: initialized refresh_scn successfully", K(refresh_scn_));
    }
  }
  return ret;
}

void ObCSDispatcher::stop()
{
  ObThreadPool::stop();
}

void ObCSDispatcher::wait()
{
  ObThreadPool::wait();
}

void ObCSDispatcher::destroy()
{
  if (is_inited_) {
    stop();
    wait();
    tx_ring_.destroy();
    dispatch_cond_.destroy();
    next_sn_ = 0;
    dispatch_sn_ = 0;
    next_commit_sn_ = 0;
    epoch_ = 0;
    dispatcher_epoch_ = 0;
    active_batch_count_ = 0;
    is_inited_ = false;
  }
}

int ObCSDispatcher::push(ObCSTxInfo *tx)
{
  int ret = common::OB_SUCCESS;
  if (!is_inited_ || OB_ISNULL(tx)) {
    ret = common::OB_INVALID_ARGUMENT;
    LOG_WARN("tx info is invalid", K(ret), K(is_inited_), KP(tx));
  } else {
    // Back-pressure: if in-flight entries (not yet committed) exceed the
    // threshold, spin-wait so Fetcher does not grow the ring buffer
    // unboundedly while Workers are slow.
    static const int64_t CS_RING_INFLIGHT_LIMIT = 100000;
    while (next_sn_ - tx_ring_.begin_sn() >= CS_RING_INFLIGHT_LIMIT) {
      if (has_set_stop()) {
        ret = common::OB_IN_STOP_STATE;
        return ret;
      }
      usleep(1000);  // 1ms
    }
    // Assign sn AFTER set() succeeds to avoid leaving gaps in the ring buffer.
    const int64_t sn = next_sn_;
    if (OB_FAIL(tx_ring_.set(sn, tx))) {
      LOG_WARN("ObCSDispatcher: tx_ring set failed", K(ret), K(sn));
    } else {
      next_sn_++;
      tx->in_dispatch_time_ = ObTimeUtil::current_time();
      ObThreadCondGuard cond_guard(dispatch_cond_);
      dispatch_cond_.signal();
    }
  }
  return ret;
}

// ---------------------------------------------------------------------------
// Parse one redo record buffer (may contain multiple rows).
//
// For each MUTATOR_ROW / MUTATOR_ROW_EXT_INFO entry:
//   1) deserialize ObMutatorRowHeader → tablet_id, mutator_type
//   2) deserialize ObMemtableMutatorRow → table_id, rowkey, seq_no
//      (ObRowData::deserialize is zero-copy, so this is already lightweight)
//   3) check visibility (seq_no vs rollback_list)
//   4) slice by rowkey.murmurhash(), add raw row buffer to subtask
//
// Non-data entries (table locks, etc.) are skipped by reading encrypted_len
// and advancing pos.
// ---------------------------------------------------------------------------
static int parse_redo_record(ObCSRedoRecord &redo,
                             ObCSTxInfo &tx_info,
                             ObCSExecCtx &exec_ctx,
                             int64_t &row_count)
{
  int ret = common::OB_SUCCESS;
  const char *buf = redo.redo_buf_;
  const int64_t buf_len = redo.redo_len_;
  int64_t pos = 0;
  int64_t slice_count = exec_ctx.sub_tasks_.count();
  if (OB_UNLIKELY(slice_count <= 0)) {
    ret = common::OB_ERR_UNEXPECTED;
    LOG_WARN("parse_redo_record: slice_count is zero", K(ret), K(slice_count));
    return ret;
  }

  // Skip ObMemtableMutatorMeta header (magic, crc, data_size, etc.)
  memtable::ObMemtableMutatorMeta meta;
  if (OB_FAIL(meta.deserialize(buf, buf_len, pos))) {
    LOG_WARN("parse_redo_record: deserialize mutator meta failed", K(ret), K(buf_len));
    return ret;
  }

  while (OB_SUCC(ret) && pos < buf_len) {
    // 1. Deserialize ObMutatorRowHeader → mutator_type, tablet_id.
    memtable::ObMutatorRowHeader row_header;
    if (OB_FAIL(row_header.deserialize(buf, buf_len, pos))) {
      LOG_WARN("parse_redo_record: deserialize row_header failed", K(ret), K(pos), K(buf_len));
      break;
    }

    // Record payload start (encrypted_len is the first field of all entry types).
    const int64_t row_payload_start = pos;

    // 2a. Inner-table filter: skip rows with tablet_id < OB_MAX_INNER_TABLE_ID.
    //     Read row_size (int32) and advance pos without deserializing full row.
    if (row_header.tablet_id_.id() < OB_MAX_INNER_TABLE_ID) {
      int32_t row_size = 0;
      if (OB_FAIL(common::serialization::decode_i32(buf, buf_len, pos, &row_size))) {
        LOG_WARN("parse_redo_record: decode row_size for inner-table skip", K(ret), K(pos));
        break;
      }
      pos = row_payload_start + static_cast<int64_t>(row_size);
      if (OB_UNLIKELY(pos < 0 || pos > buf_len)) {
        ret = common::OB_ERR_UNEXPECTED;
        LOG_WARN("parse_redo_record: inner-table skip overflow", K(ret), K(pos), K(buf_len), K(row_size));
        break;
      }
      continue;
    }

    // 2b. For non-data entries, skip by reading encrypted_len.
    if (row_header.mutator_type_ != memtable::MutatorType::MUTATOR_ROW &&
        row_header.mutator_type_ != memtable::MutatorType::MUTATOR_ROW_EXT_INFO) {
      int32_t entry_len = 0;
      if (OB_FAIL(common::serialization::decode_i32(buf, buf_len, pos, &entry_len))) {
        LOG_WARN("parse_redo_record: decode entry_len for skip", K(ret), K(pos));
        break;
      }
      pos = row_payload_start + static_cast<int64_t>(entry_len);
      if (OB_UNLIKELY(pos < 0 || pos > buf_len)) {
        ret = common::OB_ERR_UNEXPECTED;
        LOG_WARN("parse_redo_record: skip overflow", K(ret), K(pos), K(buf_len), K(entry_len));
      }
      continue;
    }

    // 3. Deserialize ObMemtableMutatorRow (ObRowData is zero-copy: pointer + size).
    memtable::ObMemtableMutatorRow mut_row;
    if (OB_FAIL(mut_row.deserialize(buf, buf_len, pos))) {
      LOG_WARN("parse_redo_record: deserialize mut_row failed", K(ret));
      break;
    }
    if (row_header.tablet_id_.id() > ObTabletID::MAX_USER_NORMAL_ROWID_TABLE_TABLET_ID) {
      continue;
    }
    // 4. Visibility check: skip rows rolled back within this transaction.
    if (mut_row.seq_no_.is_valid() && !is_row_visible(mut_row.seq_no_, tx_info.rollback_list_)) {
      continue;
    }

    // 6. Build ObCSRow from parsed fields and add to the subtask.
    //    rowkey_ / new_row_ / old_row_ are zero-copy into the redo buffer.
    ObCSRow cs_row;
    cs_row.tablet_id_      = row_header.tablet_id_;
    cs_row.commit_version_ = tx_info.commit_version_;
    cs_row.dml_flag_       = mut_row.dml_flag_;
    if (mut_row.rowkey_.get_obj_cnt() > 0 && OB_NOT_NULL(mut_row.rowkey_.get_obj_ptr())) {
      cs_row.heap_pk_ = mut_row.rowkey_.get_obj_ptr()[0].get_int();
    }
    cs_row.new_row_        = mut_row.new_row_;
    cs_row.old_row_        = mut_row.old_row_;
    cs_row.seq_no_         = mut_row.seq_no_;
    cs_row.column_cnt_     = mut_row.get_column_cnt();
    int64_t slice_id = cs_row.heap_pk_ % exec_ctx.sub_tasks_.count();
    if (OB_FAIL(exec_ctx.sub_tasks_.at(slice_id).add_row(cs_row))) {
      LOG_WARN("parse_redo_record: add_row failed", K(ret));
      break;
    }
    ++row_count;
  }
  return ret;
}

// ---------------------------------------------------------------------------
// Add one tx's redo records into existing subtasks.
// Iterates redo_list_, parses each redo record, dispatches rows by rowkey hash.
// Passes tx reference so parse_redo_record can check rollback visibility.
// ---------------------------------------------------------------------------
static int add_tx_redo_to_subtasks(ObCSTxInfo &tx,
                                   ObCSExecCtx &exec_ctx,
                                   int64_t &row_count)
{
  int ret = common::OB_SUCCESS;
  row_count = 0;
  for (int64_t i = 0; OB_SUCC(ret) && i < tx.redo_list_.count(); ++i) {
    ObCSRedoRecord &redo = tx.redo_list_.at(i);
    int64_t added = 0;
    if (OB_FAIL(parse_redo_record(redo, tx, exec_ctx, added))) {
      LOG_WARN("add_tx_redo_to_subtasks: parse_redo_record failed", K(ret), K(i));
    } else {
      row_count += added;
    }
  }
  return ret;
}

// ---------------------------------------------------------------------------
// run1: Dispatcher main loop.
//
// Key design: entries are read from ring buffer via get() (NOT pop).
// dispatch_sn_ tracks the read cursor; begin_sn() tracks the committed
// watermark (advanced by Worker calling release_batch after commit).
// On aggregation error, dispatch_sn_ is rolled back so entries can be
// retried in the next iteration.
// ---------------------------------------------------------------------------
void ObCSDispatcher::run1()
{
  lib::set_thread_name("CSDispatcher");

  dispatcher_epoch_ = ATOMIC_LOAD(&epoch_);
  bool init_refresh_scn = false;
  while (!has_set_stop()) {
    if (!init_refresh_scn) {
      if (OB_SUCCESS == init_refresh_scn_()) {
        init_refresh_scn = true;
      } else {
        usleep(500 * 1000);
        continue;
      }
    }
    // ① Epoch change detected → abort recovery.
    if (dispatcher_epoch_ != ATOMIC_LOAD(&epoch_)) {
      LOG_INFO("ObCSDispatcher: batch failure detected, entering recovery",
               K(dispatcher_epoch_), K(ATOMIC_LOAD(&epoch_)));
      // Wait for all in-flight batches to finish their abort/cleanup.
      while (ATOMIC_LOAD(&active_batch_count_) > 0) {
        if (has_set_stop()) { break; }
        usleep(1000);
        if (REACH_TIME_INTERVAL(1 * 1000 * 1000)) {
          LOG_INFO("ObCSDispatcher: recovery waiting for active batches",
                   K(ATOMIC_LOAD(&active_batch_count_)));
        }
      }
      if (has_set_stop()) {
        continue;  // Outer loop condition will exit.
      }
      init_refresh_scn_();
      // All batches cleaned up.  next_commit_sn_ was never advanced by the
      // failing batch, so it still points to the failure position.  Ring buffer
      // entries are intact (not popped).  Reset dispatch cursor to retry.
      dispatch_sn_ = get_next_commit_sn();
      dispatcher_epoch_ = ATOMIC_LOAD(&epoch_);
      LOG_INFO("ObCSDispatcher: recovery complete, retry from",
               K(dispatch_sn_), K(dispatcher_epoch_));
      continue;
    }

    // ② Normal dispatch.
    int ret = OB_SUCCESS;
    if (dispatch_sn_ >= tx_ring_.end_sn()) {
      // Idle: wait for Fetcher to push new data (or 100ms timeout as fallback).
      ObThreadCondGuard cond_guard(dispatch_cond_);
      if (dispatch_sn_ >= tx_ring_.end_sn()) {   // Re-check under lock to avoid missed signal.
        (void)dispatch_cond_.wait(100 * 1000);    // 100ms timeout.
      }
      if (REACH_TIME_INTERVAL(5 * 1000 * 1000)) {
        LOG_INFO("CSDispatcher: idle waiting for new transactions",
                 K(dispatch_sn_), K(tx_ring_.begin_sn()), K(tx_ring_.end_sn()),
                 K(get_next_commit_sn()), K(ATOMIC_LOAD(&active_batch_count_)));
      }
    } else if (OB_FAIL(do_dispatch_())) {
      LOG_WARN("do dispatch failed", KR(ret));
      usleep(100 * 1000);  // Deliberate backoff on error.
    }
  }
}

int ObCSDispatcher::do_dispatch_()
{
  int ret = common::OB_SUCCESS;

  // Fast-path: if epoch already changed (prior batch failed while we were
  // returning from the last do_dispatch_), skip creating a new batch and let
  // run1()'s recovery logic kick in on the next iteration.
  if (dispatcher_epoch_ != ATOMIC_LOAD(&epoch_)) {
    LOG_INFO("CSDispatcher: do_dispatch_ skipped (epoch mismatch)",
             K(dispatcher_epoch_), K(ATOMIC_LOAD(&epoch_)));
    return OB_SUCCESS;
  }

  ObChangeStreamMgr *mgr = MTL(ObChangeStreamMgr *);
  int64_t executor_count = mgr->get_worker().get_executor_count();
  ObCSExecCtx *exec_ctx = nullptr;
  bool batch_in_flight = false;  // true once any subtask is pushed to worker

  // ── Phase 1: create context, aggregate txs, init plugins ──
  if (executor_count <= 0) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("executor_count unexpected", KR(ret), K(executor_count));
  } else if (OB_ISNULL(exec_ctx = OB_NEW(ObCSExecCtx, common::ObMemAttr(MTL_ID(), "CSExecCtx")))) {
    ret = common::OB_ALLOCATE_MEMORY_FAILED;
    LOG_WARN("alloc mem failed", KR(ret));
  } else {
    exec_ctx->create_time_ = ObTimeUtil::current_time();
    exec_ctx->batch_sn_ = dispatch_sn_;
    exec_ctx->epoch_ = dispatcher_epoch_;  // Use dispatcher's local epoch, not the global atomic.
    // Rationale: if a worker just inc_epoch() due to a prior batch failure,
    // ATOMIC_LOAD(&epoch_) would give the new epoch, making workers unable
    // to detect that this batch should be aborted.  dispatcher_epoch_ is
    // only updated after recovery completes, which is the correct semantics.
    if (OB_FAIL(exec_ctx->sub_tasks_.prepare_allocate(executor_count))) {
      LOG_WARN("prepare_allocate sub_tasks failed", KR(ret), K(executor_count));
    } else {
      for (int64_t i = 0; i < executor_count; ++i) {
        exec_ctx->sub_tasks_.at(i).set_exec_ctx(exec_ctx);
      }
    }
  }
  while (OB_SUCC(ret) && exec_ctx->row_count_ < CS_AGGREGATE_ROW_THRESHOLD) {
    ObCSTxInfo *tx = nullptr;
    int64_t added = 0;
    if (OB_FAIL(tx_ring_.get(dispatch_sn_, tx))) {
      if (ret != OB_ERR_OUT_OF_UPPER_BOUND) {
        LOG_WARN("get ring unexpected", KR(ret));
      } else {
        ret = OB_SUCCESS;
        if (ObTimeUtil::current_time() - exec_ctx->create_time_ < 2 * 1000 * 1000
            && active_batch_count_ > 0) {
          ob_usleep(5 * 1000);
        } else {
          break;
        }
      }
    } else if (tx->commit_version_ <= refresh_scn_) {
      LOG_WARN("CSDispatcher: skip tx (commit_version <= refresh_scn)",
               K(exec_ctx->batch_sn_), K(dispatch_sn_), K(tx->tx_id_), K(tx->commit_version_), K(refresh_scn_));
      if (ATOMIC_BCAS(&next_commit_sn_, dispatch_sn_, dispatch_sn_ + 1)) {
        bool popped = false;
        ObCSTxInfo *pop_tx = nullptr;
        struct AlwaysTrue { bool operator()(int64_t, ObCSTxInfo *) const { return true; } } cond;
        (void)tx_ring_.pop(cond, pop_tx, popped, false);
        if (popped && OB_NOT_NULL(pop_tx)) {
          MTL(ObChangeStreamMgr *)->get_fetcher().release_committed_tx(pop_tx->tx_id_);
        }
        dispatch_sn_++;
        if (exec_ctx->tx_list_.count() == 0) {
          exec_ctx->batch_sn_ = dispatch_sn_;
        }
      } else {
        break;
      }
    } else if (exec_ctx->schema_version_ <= 0 && FALSE_IT(exec_ctx->schema_version_ = tx->schema_version_)) {
    } else if (tx->schema_version_ != exec_ctx->schema_version_) {
      LOG_WARN("CSDispatcher: schema_version mismatch",
               K(dispatch_sn_), K(tx->schema_version_), K(exec_ctx->schema_version_));
      break;
    } else if (tx->commit_version_ > exec_ctx->refresh_scn_ && FALSE_IT(exec_ctx->refresh_scn_ = tx->commit_version_)) {
    } else if (OB_FAIL(add_tx_redo_to_subtasks(*tx, *exec_ctx, added))) {
      LOG_WARN("add_tx_redo_to_subtasks failed", KR(ret));
    } else if (OB_FAIL(exec_ctx->tx_list_.push_back(tx))) {
      LOG_WARN("push_back tx ref failed", KR(ret));
    } else {
      exec_ctx->row_count_ += added;
      dispatch_sn_++;
      LOG_INFO("add tx", K(tx->tx_id_), K(added), K(exec_ctx->batch_sn_), K(dispatch_sn_),
          K(tx->commit_version_), K(tx->in_dispatch_time_), "delay", tx->in_dispatch_time_ - tx->commit_version_/1000);
    }
  }

  // Guard: if no transactions were aggregated, skip the rest.
  if (OB_SUCC(ret) && exec_ctx->tx_list_.count() == 0) {
    OB_DELETE(ObCSExecCtx, "CSExecCtx", exec_ctx);
    exec_ctx = nullptr;
    return OB_SUCCESS;
  }

  LOG_INFO("CSDispatcher: batch",
           K(exec_ctx->batch_sn_), K(exec_ctx->tx_list_.count()),
           K(exec_ctx->row_count_), K(exec_ctx->refresh_scn_),
           K(exec_ctx->schema_version_), K(executor_count));

  ObMultiVersionSchemaService *schema_service = nullptr;
  bool trans_started = false;
  if (OB_FAIL(ret)) {
  } else if (OB_FAIL(exec_ctx->init_plugins())) {
    LOG_WARN("init plugins failed", KR(ret));
  } else if (OB_ISNULL(schema_service = MTL(ObTenantSchemaService *)->get_schema_service())) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("schema service is null", KR(ret));
  } else if (OB_FAIL(exec_ctx->trans_.start(GCTX.sql_proxy_, MTL_ID()))) {
    LOG_WARN("trans start failed", KR(ret));
  } else {
    trans_started = true;
  }

  // ── Phase 2: push subtasks to workers ──
  // task_count_ is set once and NEVER modified afterwards; this avoids a
  // race where workers compare task_finish_ against a stale task_count_.
  // active_batch_count_ MUST be incremented before any push, so that even
  // if the last-worker fires immediately, the count is already > 0.
  if (OB_SUCC(ret)) {
    exec_ctx->task_count_ = exec_ctx->sub_tasks_.count();
    ATOMIC_INC(&active_batch_count_);

    int64_t pushed = 0;
    for (int64_t i = 0; i < exec_ctx->sub_tasks_.count(); ++i) {
      if (OB_FAIL(mgr->get_worker().push_subtask(i, &exec_ctx->sub_tasks_.at(i)))) {
        LOG_WARN("push_subtask failed", KR(ret), K(i));
        break;
      }
      pushed++;
    }

    if (pushed > 0) {
      batch_in_flight = true;
    }

    LOG_INFO("CSDispatcher: subtasks pushed to workers",
             K(exec_ctx->batch_sn_), K(pushed), K(exec_ctx->task_count_), K(exec_ctx->sub_tasks_));

    const int64_t unpushed = exec_ctx->sub_tasks_.count() - pushed;
    if (unpushed > 0) {
      // Push failure (partial or total).  Instead of adjusting task_count_
      // (which races with workers' last-worker check), we bump task_finish_
      // for the unpushed tasks and let ATOMIC_AAF determine the last finisher.
      ATOMIC_AAF(&exec_ctx->task_fail_, 1);
      const int64_t finished = ATOMIC_AAF(&exec_ctx->task_finish_, unpushed);
      if (finished == exec_ctx->task_count_) {
        // All pushed workers already finished — dispatcher is last finisher.
        if (trans_started) {
          (void)exec_ctx->trans_.end(false);
        }
        inc_epoch();  // Signal subsequent batches to abort.
        dec_active_batch_count();
        OB_DELETE(ObCSExecCtx, "CSExecCtx", exec_ctx);
        exec_ctx = nullptr;
        batch_in_flight = false;
      }
      ret = OB_SUCCESS;  // Workers / dispatcher already handling cleanup.
    }
  }

  // ── Error cleanup (Phase 1 errors, or Phase 2 with zero pushed) ──
  if (OB_FAIL(ret) && !batch_in_flight && OB_NOT_NULL(exec_ctx)) {
    dispatch_sn_ = exec_ctx->batch_sn_;
    if (trans_started) {
      (void)exec_ctx->trans_.end(false);
    }
    // destroy_plugins() is called by ~ObCSExecCtx() -> reset().
    OB_DELETE(ObCSExecCtx, "CSExecCtx", exec_ctx);
    exec_ctx = nullptr;
  }
  return ret;
}

void ObCSExecCtx::reset()
{
  task_count_ = 0;
  task_finish_ = 0;
  task_fail_ = 0;
  row_count_ = 0;
  refresh_scn_ = 0;
  schema_version_ = 0;
  epoch_ = 0;
  batch_sn_ = 0;
  tx_list_.reset();
  sub_tasks_.reset();
  destroy_plugins();
  MEMSET(plugins_, 0, sizeof(plugins_));
  plugin_cnt_ = 0;
  process_cnt_ =0;
  create_time_ = 0;
  process_time_ = 0;
}

int ObCSExecCtx::init_plugins()
{
  int ret = common::OB_SUCCESS;
  static_assert(CS_PLUGIN_MAX_TYPE <= CS_MAX_PLUGIN_COUNT,
                "plugins_ array in ObCSExecCtx is too small for CS_PLUGIN_MAX_TYPE");
  ObCSPluginRegistry &registry = ObCSPluginRegistry::get_instance();
  plugin_cnt_ = CS_MAX_PLUGIN_COUNT;
  for (int64_t i = 0; OB_SUCC(ret) && i < CS_MAX_PLUGIN_COUNT; ++i) {
    ObCSPluginFactoryFunc factory = registry.get_factory(static_cast<CS_PLUGIN_TYPE>(i));
    if (factory != nullptr) {
      plugins_[i] = factory();
      if (OB_ISNULL(plugins_[i])) {
        ret = common::OB_ALLOCATE_MEMORY_FAILED;
        LOG_WARN("ObCSExecCtx: plugin factory returned null", K(ret), K(i));
      } else if (OB_FAIL(plugins_[i]->init())) {
        LOG_WARN("ObCSExecCtx: plugin init failed", K(ret), K(i));
      } else {
        plugins_[i]->set_plugin_type(static_cast<CS_PLUGIN_TYPE>(i));
      }
    }
  }
  if (OB_FAIL(ret)) {
    destroy_plugins();
  }
  return ret;
}

void ObCSExecCtx::destroy_plugins()
{
  for (int64_t i = 0; i < plugin_cnt_; ++i) {
    if (plugins_[i] != nullptr) {
      plugins_[i]->destroy();
      OB_DELETE(ObCSPlugin, "CSPlugin", plugins_[i]);
      plugins_[i] = nullptr;
    }
  }
}

int ObCSExecSubTask::add_row(const ObCSRow &row)
{
  return rows_.push_back(row);
}

// ---------------------------------------------------------------------------
// release_batch: called by the last Worker of a batch after commit/rollback,
// while this batch still holds the serial-commit "lock" (next_commit_sn_ ==
// batch_sn_).  Pops num_tx entries from the ring buffer (advancing begin_sn_)
// and calls Fetcher::release_committed_tx() to remove from tx_info_ map,
// free redo buffers, and free the ObCSTxInfo object.
// ---------------------------------------------------------------------------
void ObCSDispatcher::release_batch(ObCSExecCtx *ctx)
{
  int ret = common::OB_SUCCESS;
  if (OB_ISNULL(ctx)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_ERROR("ObCSDispatcher: release_batch ctx is null", KR(ret), KP(ctx));
    return;
  }
  ObCSFetcher &fetcher = MTL(ObChangeStreamMgr *)->get_fetcher();

  struct AlwaysTrue {
    bool operator()(int64_t /*sn*/, ObCSTxInfo *) const { return true; }
  } cond;

  const int64_t num_tx = ctx->tx_list_.count();
  for (int64_t i = 0; i < num_tx; ++i) {
    bool popped = false;
    ObCSTxInfo *tx = nullptr;
    if (OB_FAIL(tx_ring_.pop(cond, tx, popped, false)) || !popped) {
      // Ring buffer pop should never fail here: these entries were successfully
      // set() by Fetcher and are in [begin_sn, end_sn).  A failure indicates
      // ring buffer corruption or a programming error.  Continuing would leave
      // next_commit_sn_ inconsistent with begin_sn, causing cascading failures.
      LOG_ERROR("FATAL: release_batch pop failed — aborting to prevent data corruption",
                K(ret), K(popped), K(i), K(num_tx),
                K(ctx->batch_sn_), K(tx_ring_.begin_sn()), K(tx_ring_.end_sn()));
      ob_abort();
    }
    if (OB_NOT_NULL(tx)) {
      (void)fetcher.release_committed_tx(tx->tx_id_);
    }
  }

  // Advance serial commit cursor so next batch can proceed.
  set_next_commit_sn(ctx->batch_sn_ + num_tx);

  // Update in-memory refresh_scn.
  if (ctx->refresh_scn_ > refresh_scn_) {
    refresh_scn_ = ctx->refresh_scn_;
  }
}



}  // namespace share
}  // namespace oceanbase
