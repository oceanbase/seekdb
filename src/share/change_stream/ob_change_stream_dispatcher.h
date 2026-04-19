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
 *
 * Change Stream Dispatcher: parsed row, execution context, and subtasks.
 * Single-threaded Dispatcher consumes committed transactions from the ring buffer,
 * parses redo, slices by heap_pk range, and pushes subtasks to Workers.
 */

#ifndef OB_CS_DISPATCHER_H_
#define OB_CS_DISPATCHER_H_

#include "lib/ob_define.h"
#include "lib/container/ob_se_array.h"
#include "lib/container/ob_iarray.h"
#include "lib/container/ob_ext_ring_buffer.h"
#include "lib/lock/ob_thread_cond.h"
#include "common/ob_tablet_id.h"
#include "common/rowkey/ob_store_rowkey.h"
#include "share/ob_thread_pool.h"
#include "storage/tx/ob_tx_seq.h"
#include "storage/blocksstable/ob_datum_row.h"     // ObDmlFlag
#include "storage/memtable/mvcc/ob_row_data.h"     // ObRowData
#include "share/change_stream/ob_change_stream_fetcher.h"
#include "lib/mysqlclient/ob_mysql_transaction.h"
#include "share/schema/ob_schema_getter_guard.h"

namespace oceanbase
{
namespace share
{

class ObCSPlugin;  // Forward declaration; defined in ob_change_stream_plugin.h
class ObCSExecCtx;

// Maximum number of plugin types.  Must match CS_PLUGIN_MAX_TYPE in
// ob_change_stream_plugin.h.  Defined here as a plain constant so that
// dispatcher.h does NOT include plugin.h (which depends on types defined here,
// creating a circular include).
static const int64_t CS_MAX_PLUGIN_COUNT = 2;

// ---------------------------------------------------------------------------
// Dispatcher: one parsed redo row, execution context, subtask
// ---------------------------------------------------------------------------

/// One parsed redo row.  Fields are populated by Dispatcher from the
/// deserialized ObMemtableMutatorRow.  new_row_ / old_row_ are zero-copy
/// pointers into the redo buffer (owned by Fetcher's ObCSTxInfo, valid
/// until release_batch pops the ring buffer entry).
struct ObCSRow
{
  common::ObTabletID tablet_id_{};                                      // Default: invalid tablet.
  int64_t  commit_version_ = 0;
  blocksstable::ObDmlFlag dml_flag_ = blocksstable::DF_NOT_EXIST;
  int64_t  heap_pk_ = 0;                                               // Heap-table hidden pk (__pk_increment), extracted while mut_row is alive.
  memtable::ObRowData new_row_{};                                       // Zero-copy into redo buffer.
  memtable::ObRowData old_row_{};                                       // Zero-copy into redo buffer.
  transaction::ObTxSEQ seq_no_{};                                       // Statement sequence number.
  int64_t  column_cnt_ = 0;
  TO_STRING_KV(K_(tablet_id), K_(commit_version),
               K_(dml_flag), K_(heap_pk), K_(new_row), K_(old_row), K_(seq_no), K_(column_cnt));
};

// ---------------------------------------------------------------------------
// Execution context and subtask (created by Dispatcher, consumed by Worker)
// ---------------------------------------------------------------------------

/// Subtask (sliced by heap_pk range; adjacent rows stay together per worker chunk).
class ObCSExecSubTask : public common::LinkTask
{
public:
  ObCSExecSubTask() : exec_ctx_(nullptr), rows_()
                      {}

  int add_row(const ObCSRow &row);

  ObCSExecCtx *get_exec_ctx() const { return exec_ctx_; }
  void set_exec_ctx(ObCSExecCtx *ctx) { exec_ctx_ = ctx; }
  common::ObSEArray<ObCSRow, 4> &get_rows() { return rows_; }
  const common::ObSEArray<ObCSRow, 4> &get_rows() const { return rows_; }

  TO_STRING_KV(K(rows_.count()));
private:
  ObCSExecCtx *exec_ctx_;
  common::ObSEArray<ObCSRow, 4> rows_;
};

/// Task execution context (shared by all subtasks: transaction handle, refresh scn, plugins).
/// Each batch creates its own plugin instances via ObCSPluginRegistry factories,
/// so plugins_ is per-batch and freed when the batch completes.
class ObCSExecCtx
{
public:
  ObCSExecCtx() : plugin_cnt_(0) { reset(); }
  ~ObCSExecCtx() { reset(); }

  void reset();
  /// Create per-batch plugin instances from registry factories.
  int init_plugins();
  /// Destroy per-batch plugin instances.
  void destroy_plugins();

  int64_t task_count_;
  int64_t task_finish_;    // Updated atomically by workers.
  int64_t task_fail_;
  int64_t row_count_;
  int64_t refresh_scn_;    // Max commit_version of tx in batch; advance after commit.
  int64_t schema_version_;
  int64_t epoch_;           // Creation epoch snapshot; mismatch with dispatcher.epoch_ → abort.

  ObMySQLTransaction trans_;

  int64_t batch_sn_ = 0;            // Ring position of this batch; commit only when head.
  common::ObSEArray<ObCSTxInfo *, 4> tx_list_;  // References to tx in this batch (owned by Fetcher map).
  common::ObSEArray<ObCSExecSubTask, 16> sub_tasks_;

  ObCSPlugin *plugins_[CS_MAX_PLUGIN_COUNT];  // Per-batch plugin instances created by factory.
  int64_t plugin_cnt_;
  int64_t process_cnt_;
  int64_t create_time_;
  int64_t process_time_;

  DISALLOW_COPY_AND_ASSIGN(ObCSExecCtx);
};

// ---------------------------------------------------------------------------
// Row visibility (design doc 3.2)
// ---------------------------------------------------------------------------

/// Returns true if stmt_seq_no is not covered by any rollback to savepoint range.
inline bool is_row_visible(
    const transaction::ObTxSEQ &stmt_seq_no,
    const common::ObIArray<ObCSRollbackRange> &rollback_list)
{
  for (int64_t i = 0; i < rollback_list.count(); ++i) {
    const ObCSRollbackRange &r = rollback_list.at(i);
    if (r.first <= stmt_seq_no && stmt_seq_no < r.second) {
      return false;  // Inside a rollback range.
    }
  }
  return true;
}

// ---------------------------------------------------------------------------
// ObCSDispatcher: single-threaded consumer of committed tx, slices and pushes to Workers.
// All sibling module access (Worker, Plugins) goes through MTL(ObChangeStreamMgr*).
// ---------------------------------------------------------------------------

class ObCSDispatcher : public share::ObThreadPool
{
public:
  ObCSDispatcher();
  virtual ~ObCSDispatcher();

  int init();
  int start();
  /// Initialize change_stream_refresh_scn entry in __all_global_stat.
  /// Should be called after start() but before run1() begins processing.
  int init_refresh_scn();
  void stop();
  void wait();
  void destroy();

  /// Fetcher calls this when a transaction commits.
  int push(ObCSTxInfo *tx);

  int64_t get_refresh_scn() const { return refresh_scn_; }

  int64_t get_next_commit_sn() const { return ATOMIC_LOAD(&next_commit_sn_); }
  void set_next_commit_sn(int64_t sn) { ATOMIC_STORE(&next_commit_sn_, sn); }

  /// Global abort epoch — incremented by last-worker on batch failure.
  /// Workers compare ctx->epoch_ with this to detect abort.
  int64_t get_epoch() const { return ATOMIC_LOAD(&epoch_); }
  void inc_epoch() { ATOMIC_INC(&epoch_); }

  /// Active in-flight batch count.  Incremented by Dispatcher before pushing
  /// subtasks, decremented by last-worker in cleanup.  Dispatcher waits for
  /// this to reach 0 during recovery.
  void dec_active_batch_count() { ATOMIC_DEC(&active_batch_count_); }

  /// Called by the last Worker of a batch on SUCCESS path.
  /// Pops tx entries from ring buffer, releases ObCSTxInfo via Fetcher,
  /// advances next_commit_sn_, and updates in-memory refresh_scn.
  void release_batch(ObCSExecCtx *ctx);

protected:
  void run1() override;
  int do_dispatch_();
  int init_refresh_scn_();

private:
  static const int64_t CS_AGGREGATE_ROW_THRESHOLD = 1000;  // Aggregate small tx until >= 1000 rows or queue empty.

  bool is_inited_;
  common::ObExtendibleRingBuffer<ObCSTxInfo> tx_ring_;  // Stores ObCSTxInfo*
  int64_t refresh_scn_;                // Current refresh_scn loaded from __all_global_stat.
  int64_t next_sn_;           // Serial number for ring buffer set (Fetcher side)
  int64_t dispatch_sn_;       // Next position to read from ring buffer (dispatch cursor).
                              // Always: begin_sn() <= dispatch_sn_ <= end_sn().
                              // begin_sn() = committed watermark (advanced by release_batch after Worker commit).
                              // dispatch_sn_ = dispatched watermark (advanced by get() during aggregation).
  int64_t next_commit_sn_;    // Serial commit: next batch_sn allowed to commit (Worker spins until head).

  // Epoch-based abort signaling.
  int64_t epoch_;              // Global abort epoch (atomic).  Incremented on batch failure.
  int64_t dispatcher_epoch_;   // Dispatcher's local epoch cache; compared with epoch_ to detect failure.
  int64_t active_batch_count_; // Atomic count of in-flight batches.

  // Condition variable: Fetcher signals after push(), Dispatcher waits when idle.
  common::ObThreadCond dispatch_cond_;
};

}  // namespace share
}  // namespace oceanbase

#endif  // OB_CS_DISPATCHER_H_
