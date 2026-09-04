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
 * Change Stream Fetcher: data structures for consuming CLOG by transaction.
 * Single-threaded Fetcher maintains ObCSTxInfo per tx_id and pushes committed
 * transactions into the Dispatcher ring buffer.
 */

#ifndef OB_CS_FETCHER_H_
#define OB_CS_FETCHER_H_

#include "lib/ob_define.h"
#include "lib/container/ob_se_array.h"
#include "lib/hash/ob_hashmap.h"
#include "common/ob_tablet_id.h"
#include "share/scn.h"
#include "share/ob_thread_pool.h"
#include "lib/lock/ob_thread_cond.h"
#include "lib/atomic/ob_atomic.h"
#include "share/log/palf/lsn.h"
#include "logservice/palf/log_entry.h"
#include "logservice/palf/palf_iterator.h"
#include "storage/tx/ob_tx_seq.h"
#include "storage/tx/ob_trans_define.h"

namespace oceanbase
{
namespace logservice
{
class ObILogStorage;
}
namespace share
{
namespace schema
{
class ObSchemaPublishSignal;
}

/// Interval for refreshing the in-memory min_dep_lsn (us).
static constexpr int64_t CS_FETCHER_MIN_DEP_LSN_UPDATE_INTERVAL_US = 5 * 1000 * 1000;
/// Interval for schema version check and mode switching (us).
/// Only calls check_has_async_index_tables_() when version changes (DDL), so 10ms is essentially free.
static constexpr int64_t CS_FETCHER_SCHEMA_CHECK_INTERVAL_US = 10 * 1000;
/// Interval for progress log (us).
static constexpr int64_t CS_FETCHER_PROGRESS_LOG_INTERVAL_US = 10 * 1000 * 1000;
/// Sleep when init checks (sql_proxy not ready, etc.).
static constexpr int64_t CS_FETCHER_INIT_RETRY_SLEEP_US = 500 * 1000;
/// Sleep when init_consumption_position_ fails.
static constexpr int64_t CS_FETCHER_INIT_FAIL_SLEEP_US = 1000 * 1000;
/// Cond wait timeout in IDLE mode. Keep it no longer than the min_dep_lsn refresh interval.
static constexpr int64_t CS_FETCHER_IDLE_COND_WAIT_MS = 5 * 1000;
/// Sleep when caught up (OB_ITER_END).
static constexpr int64_t CS_FETCHER_ITER_END_SLEEP_US = 200 * 1000;
/// Sleep when iter.next/get_entry fails.
static constexpr int64_t CS_FETCHER_ITER_ERR_SLEEP_US = 100 * 1000;
/// tx_info_ hash bucket count.
static constexpr int64_t CS_FETCHER_TX_INFO_BUCKET_CNT = 1024;
/// Max redo mutator size (64MB), sanity check to avoid OOM from corrupted data.
static constexpr int64_t CS_FETCHER_MAX_REDO_MUTATOR_SIZE = 64 * 1024 * 1024;

class ObCSDispatcher;

/// Buffer for a single redo record (Fetcher maintains these per transaction).
struct ObCSRedoRecord
{
  char   *redo_buf_ = nullptr;
  int64_t redo_len_ = 0;

  void reset()
  {
    redo_buf_ = nullptr;
    redo_len_ = 0;
  }
  TO_STRING_KV(K_(redo_buf), K_(redo_len));
};

/// Rollback range [to_seq, from_seq). Rows with stmt seq_no in this range are not visible.
using ObCSRollbackRange = std::pair<transaction::ObTxSEQ, transaction::ObTxSEQ>;

/// Per-transaction info (Fetcher keyed by tx_id; pushed to Dispatcher ring buffer on commit).
struct ObCSTxInfo
{
  int64_t   tx_id_ = 0;
  int64_t   commit_version_ = 0;   // Used for refresh_scn and filtering.
  palf::LSN start_lsn_;           // Used by get_min_dep_lsn for log reclaim and init restart.
  int64_t   schema_version_ = 0;  // Assigned from Fetcher's current_schema_version_ at commit time.
  bool      is_ddl_ = false;      // True if redo contains __all_ddl_operation rows.
  common::ObSEArray<ObCSRollbackRange, 1> rollback_list_;
  common::ObSEArray<ObCSRedoRecord, 1>    redo_list_;
  int64_t in_dispatch_time_ = 0;

  void reset()
  {
    tx_id_ = 0;
    commit_version_ = 0;
    start_lsn_.reset();
    schema_version_ = 0;
    is_ddl_ = false;
    rollback_list_.reset();
    redo_list_.reset();
    in_dispatch_time_ = 0;
  }

  /// Free redo buffers and reset; caller then frees the ObCSTxInfo itself.
  void destroy();

  TO_STRING_KV(K_(tx_id), K_(commit_version), K_(start_lsn), K_(schema_version), K_(is_ddl), K_(in_dispatch_time));
};

/// A versioned proof published by the single Fetcher thread.
/// ready_schema_version is meaningful only together with has_async_index and
/// error_code from the same snapshot.
struct ObCSAsyncSchemaState
{
  ObCSAsyncSchemaState()
    : ready_schema_version_(0),
      last_no_async_index_drained_schema_version_(0),
      has_async_index_(false),
      error_code_(OB_SUCCESS)
  {}

  void reset()
  {
    ready_schema_version_ = 0;
    last_no_async_index_drained_schema_version_ = 0;
    has_async_index_ = false;
    error_code_ = OB_SUCCESS;
  }

  int64_t ready_schema_version_;
  int64_t last_no_async_index_drained_schema_version_;
  bool has_async_index_;
  int error_code_;

  TO_STRING_KV(K_(ready_schema_version),
               K_(last_no_async_index_drained_schema_version),
               K_(has_async_index),
               K_(error_code));
};

// ---------------------------------------------------------------------------
// ObCSFetcher: single-threaded CLOG consumer, assembles by transaction, pushes on commit.
// ---------------------------------------------------------------------------

class ObCSFetcher : public share::ObThreadPool
{
public:
  ObCSFetcher();
  virtual ~ObCSFetcher();

  int init(
      ObCSDispatcher *dispatcher,
      logservice::ObILogStorage &log_storage,
      schema::ObSchemaPublishSignal &schema_publish_signal,
      lib::IRunWrapper *run_wrapper);
  int start();
  void stop();
  void wait();
  void destroy();

  /// Return the latest Fetcher-published minimum depended LSN.
  /// This is an atomic cached read and never scans Fetcher transaction state.
  int get_min_dep_lsn(palf::LSN &min_lsn) const;

  /// Get one internally consistent copy of the Fetcher-owned schema state.
  int get_async_schema_state(ObCSAsyncSchemaState &state);
  /// Wait until Fetcher has prepared exactly schema_version.  If runtime schema
  /// changes first, returns OB_EAGAIN so the caller can recapture its target.
  int wait_async_schema_ready(const int64_t schema_version,
                              const int64_t abs_timeout_us,
                              ObCSAsyncSchemaState &state);
  /// Recovery invalidates prior ready proofs; Fetcher will rebuild the state.
  void reset_async_schema_state();

  /// End LSN of the last log entry successfully processed or safely skipped.
  int64_t get_processed_end_lsn() const { return ATOMIC_LOAD(&processed_end_lsn_); }

  transaction::ObTransID get_current_processing_tx_id() const;
  int64_t get_current_processing_tx_count() const;
  palf::LSN get_current_lsn() const;
  SCN get_current_scn() const;

  /// Called by Dispatcher drain loop after Worker commits a batch.
  /// Removes the transaction from tx_info_ map, calls tx->destroy() to free
  /// redo buffers, and frees the ObCSTxInfo object.
  /// Thread-safe: ObHashMap uses bucket-level locking.
  int release_committed_tx(int64_t tx_id);

protected:
  void run1() override;

private:
  enum RunningMode { IDLE = 0, ACTIVE = 1 };

  int init_consumption_position_();
  int calc_min_dep_lsn_(palf::LSN &min_lsn);
  void try_update_min_dep_lsn_();
  void publish_min_dep_lsn_(const palf::LSN &min_lsn);
  int get_runtime_schema_version_(int64_t &schema_version) const;
  /// Check exactly the requested runtime schema version.
  int check_has_async_index_tables_(const int64_t schema_version, bool &has_async);
  int refresh_async_schema_state_(bool &iter_ready);
  int drain_to_idle_(const int64_t schema_version);
  void publish_schema_state_(const int64_t schema_version,
                             const bool has_async_index,
                             const bool advance_drained_version);
  void publish_schema_error_(const int error_code);
  int handle_redo_log_(const transaction::ObTransID &tx_id,
                       const char *mutator_buf,
                       int64_t mutator_size,
                       const palf::LSN &lsn);
  int handle_commit_log_(const transaction::ObTransID &tx_id,
                         const SCN &commit_version);
  int handle_abort_log_(const transaction::ObTransID &tx_id);
  int handle_rollback_to_log_(const transaction::ObTransID &tx_id,
                              const transaction::ObTxSEQ &from,
                              const transaction::ObTxSEQ &to);
  int extract_ddl_schema_version_(ObCSTxInfo *tx, int64_t &schema_version);
  /// Get or create tx in tx_info_; used by handle_redo_log_ and MDS DDL branch.
  int get_or_create_tx_info_(int64_t tid, const palf::LSN &lsn, ObCSTxInfo *&tx);

  bool is_inited_;
  ObCSDispatcher *dispatcher_;
  logservice::ObILogStorage *log_storage_;
  palf::PalfBufferIterator iter_;
  palf::LSN current_lsn_;
  palf::LSN min_dep_lsn_;          // Atomically published; only Fetcher writes.
  int64_t processed_end_lsn_;
  SCN current_scn_;
  int64_t current_schema_version_;
  common::hash::ObHashMap<int64_t, ObCSTxInfo *> tx_info_; // tx_id -> ObCSTxInfo
  int64_t total_tx_committed_;
  RunningMode running_mode_;           // IDLE: no async-index tables; ACTIVE: consuming logs.
  int64_t pending_ready_schema_version_;
  ObCSAsyncSchemaState async_schema_state_;
  transaction::ObTransID current_processing_tx_id_;
  schema::ObSchemaPublishSignal *schema_publish_signal_;
  int64_t observed_schema_publish_epoch_;
  common::ObThreadCond schema_state_cond_; // Protects async_schema_state_ and wakes refresh waiters.
};

}  // namespace share
}  // namespace oceanbase

#endif  // OB_CS_FETCHER_H_
