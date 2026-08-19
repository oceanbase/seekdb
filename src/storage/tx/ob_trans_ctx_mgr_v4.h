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

#ifndef OCEANBASE_TRANSACTION_OB_TX_CTX_MGR
#define OCEANBASE_TRANSACTION_OB_TX_CTX_MGR

#include "ob_ls_tx_ctx_mgr_stat.h"
#include "ob_tx_stat.h"
#include "storage/tx_table/ob_tx_table_define.h"
#include "common/ob_simple_iterator.h"
#include "lib/lock/ob_qsync_lock.h"
#include "storage/tx/ob_trans_ctx.h"
#include "storage/tx/ob_tx_log_adapter.h"
#include "storage/tablelock/ob_lock_table.h"
#include "storage/tx/ob_keep_alive_ls_handler.h"

namespace oceanbase
{

namespace common
{
class SpinRWLock;
class ObTabletID;
}

namespace storage
{
class ObIMemtable;
class ObLSTxService;
class ObTransSubmitLogFunctor;
class ObTxCtxTable;
}

namespace memtable
{
class ObMemtable;
class ObIMemtableCtx;
}

namespace transaction
{
class ObLSTxCtxMgrStat;
class ObTransCtx;
class ObTxCtx;
class ObTsMgr;
class ObITxLogParam;
class ObITxLogAdapter;
class ObLSTxLogAdapter;
class ObTxLockStat;
class ObTxStat;
}

namespace unittest {
class TestTxCtxTable;
};

namespace transaction
{

// Is used to store and travserse all TxCtx's Stat information;
typedef common::ObSimpleIterator<ObTxStat,
        ObModIds::OB_TRANS_VIRTUAL_TABLE_TRANS_STAT, 16> ObTxStatIterator;

// Is used to travserse all TxCtx's lock information
typedef common::ObSimpleIterator<ObTxLockStat,
        ObModIds::OB_TRANS_VIRTUAL_TABLE_TRANS_STAT, 16> ObTxLockStatIterator;

typedef share::ObLightHashMap<ObTransID, ObTransCtx, TransCtxAlloc, common::SpinRWLock, 1 << 14 /*single LS bucket_num*/> ObLSTxCtxMap;

struct ObTxCreateArg
{
  ObTxCreateArg(const bool for_replay,
                const TxCtxSource ctx_source,
                const ObTransID &trans_id,
                const uint32_t session_id,
                const int64_t trans_expired_time,
                ObTransService *trans_service)
      : for_replay_(for_replay),
        ctx_source_(ctx_source),
        tx_id_(trans_id),
        session_id_(session_id),
        trans_expired_time_(trans_expired_time),
        trans_service_(trans_service) {}
  bool is_valid() const
  {
    return tx_id_.is_valid()
        && trans_expired_time_ > 0
        && NULL != trans_service_;
  }
  TO_STRING_KV(K_(for_replay), "ctx_source", to_str_ctx_source(ctx_source_), K_(tx_id),
                 K_(session_id),
                 K_(trans_expired_time), KP_(trans_service));
  bool for_replay_;
  TxCtxSource ctx_source_;
  ObTransID tx_id_;
  uint32_t session_id_;
  int64_t trans_expired_time_;
  ObTransService *trans_service_;
};

// Is used to store and traverse ObTxID
const static char OB_SIMPLE_ITERATOR_LABEL_FOR_TX_ID[] = "ObTxCtxMgr";
typedef common::ObSimpleIterator<ObTransID, OB_SIMPLE_ITERATOR_LABEL_FOR_TX_ID, 16> ObTxIDIterator;

// Transaction Context Manager
class ObLSTxCtxMgr
{
// ut
  friend class unittest::TestTxCtxTable;

  friend class PrintFunctor;
  friend class ObTransCtx;
  friend class ObTransCtx;
  friend class ObTransTimer;

public:
  typedef common::RWLock RWLock;
  typedef RWLock::RLockGuard RLockGuard;
  typedef RWLock::WLockGuard WLockGuard;
  typedef RWLock::WLockGuardWithRetryInterval WLockGuardWithRetryInterval;

  ObLSTxCtxMgr()
      : tx_log_adapter_(&log_adapter_def_), rwlock_(ObLatchIds::DEFAULT_SPIN_RWLOCK)

  {
    reset();
  }

  virtual ~ObLSTxCtxMgr() { destroy(); }

  // @param [in] ls_id, associated ls_id;
  // @param [in] ts_mgr: used to get gts, see: update_max_replay_commit_version function;
  // @param [in] txs: transaction service which hold the ObTxCtxMgr;
  // @param [in] log_param: the params which is used to init ObLSTxCtxMgr's log_adapter_def_;
  int init(ObTxTable *tx_table,
           ObLockTable *lock_table,
           ObTsMgr *ts_mgr,
           ObTransService *txs,
           ObITxLogParam *log_param,
           ObITxLogAdapter * log_adapter);

  // Destroy the ObLSTxCtxMgr
  void destroy();

  // Reset the ObLSTxCtxMgr
  void reset();

  // Offline the in-memory state of the ObLSTxCtxMgr
  int offline();

public:
  // Create a TxCtx whose tx_id is specified
  // @param [in] tx_id: transaction ID
  // @param [in] for_replay: Identifies whether the TxCtx is created for replay processing;
  // @param [out] existed: if it's true, means that an existing TxCtx with the same tx_id has
  //        been found, and the found TxCtx will be returned through the outgoing parameter tx_ctx;
  // TODO insert_and_get return existed object ptr;
  // @param [out] tx_ctx: newly allocated or already exsited transaction context
  // @return OB_SUCCESS, if the tx_ctx newly allocated or already existed
  int create_tx_ctx(const ObTxCreateArg &arg,
                    bool& existed,
                    ObTxCtx *&tx_ctx);

  // Find specified TxCtx from the ObLSTxCtxMgr;
  // @param [in] tx_id: transaction ID
  // @param [in] for_replay: Identifies whether the TxCtx is used by replay processing;
  // @param [out] tx_ctx: context found through ObLSTxCtxMgr's hash table
  // @return OB_TRANS_CTX_NOT_EXIST, if the specified TxCtx is not found;
  int get_tx_ctx(const ObTransID &tx_id, const bool for_replay, ObTxCtx *&tx_ctx);

  int get_tx_ctx_with_timeout(const ObTransID &tx_id,
                              const bool for_replay,
                              ObTxCtx *&tx_ctx,
                              const int64_t lock_timeout);

  // Find specified TxCtx directly from the ObLSTxCtxMgr's hash_map
  // @param [in] tx_id: transaction ID
  // @param [out] tx_ctx: context found through ObLSTxCtxMgr's hash table
  // @return OB_TRANS_CTX_NOT_EXIST, if the specified TxCtx is not found;
  int get_tx_ctx_directly_from_hash_map(const ObTransID &tx_id, ObTxCtx *&ctx);

  // Decrease the specified tx_ctx's reference count
  // @param [in] tx_ctx: the TxCtx will be revert
  int revert_tx_ctx(ObTxCtx *tx_ctx);

  // Decrease the specified tx_ctx's reference count
  // @param [in] tx_ctx: the TxCtx will be revert
  int revert_tx_ctx(ObTransCtx *tx_ctx);

  // Decrease the specified tx_ctx's reference count without acquiring ObLSTxCtxMgr's lock
  // @param [in] tx_ctx: the TxCtx will be revert
  int revert_tx_ctx_without_lock(ObTransCtx *ctx);
  int del_tx_ctx(ObTransCtx *ctx);

  // Freeze process needs to traverse TxCtx to submit log
  // @param[in] freeze_clock, the freeze clock after which will not be traversaled
  int traverse_tx_to_submit_redo_log(ObTransID &fail_tx_id, const uint32_t freeze_clock = UINT32_MAX);
  int traverse_tx_to_submit_next_log();

  // Get the min prepare version of transaction module of current observer for slave read
  // @param [out] min_prepare_version: MIN(uncommitted tx_ctx's prepare version | ObLSTxCtxMgr)
  int get_min_uncommit_tx_prepare_version(share::SCN &min_prepare_version);

  // Check the ObLSTxCtxMgr's status; if it's stopped, the new transaction ctx will not be created;
  // otherwise, the tx_ctx is created normally
  bool is_stopped() const { return is_stopped_(); }

  // Stop the ObLSTxCtxMgr and kill all local transactions.
  // @param [in] graceful: indicate the kill behavior
  int stop(const bool graceful);

  // Kill all the tx in this ObLSTxCtxMgr
  // @param [in] graceful: indicate the kill behavior
  // @param [out] is_all_tx_cleaned_up: set it to true, when all tx are killed
  int kill_all_tx(const bool graceful, bool &is_all_tx_cleaned_up);

  // Block this ObLSTxCtxMgr, it can no longer create new tx_ctx;
  // @param [out] is_all_tx_cleaned_up: set it to true, when all transactions are cleaned up;
  int block_tx(bool &is_all_tx_cleaned_up);
  int block_all(bool &is_all_tx_cleaned_up);

  // Block this ObLSTxCtxMgr, it can no longer create normal tx_ctx;
  // Allow create special tx_ctx, exp: mds_trans;
  // @param [out] is_all_tx_cleaned_up: set it to true, when all transactions are cleaned up;
  int block_normal(bool &is_all_tx_cleaned_up);
  int unblock_normal();
  int online();

  // Get the TxCtx count in this ObLSTxCtxMgr;
  int64_t get_tx_ctx_count() const { return get_tx_ctx_count_(); }

  // Get the count of active transactions which have not been committed or aborted
  int64_t get_active_tx_count() const { return ATOMIC_LOAD(&active_tx_count_); }

  // Check all active and not "for_replay" tx_ctx in this ObLSTxCtxMgr
  // whether all the transactions that modify the specified tablet before
  // a schema version are finished.
  // @param [in] schema_version: the schema_version to check
  // @param [out] block_tx_id: a running transaction that modify the tablet before schema version.
  // Return Values That Need Attention:
  // @return OB_EAGAIN: Some TxCtx that has modify the tablet before schema
  // version is running;
  int check_modify_schema_elapsed(const common::ObTabletID &tablet_id,
                                  const int64_t schema_version,
                                  ObTransID &block_tx_id);

  // Check all active and not "for_replay" tx_ctx in this ObLSTxCtxMgr
  // whether all the transactions that modify the specified tablet before
  // a timestamp are finished.
  // @param [in] timestamp: the timestamp to check
  // @param [out] block_tx_id: a running transaction that modify the tablet before timestamp.
  // Return Values That Need Attention:
  // @return OB_EAGAIN: Some TxCtx that has modify the tablet before timestamp
  // is running;
  int check_modify_time_elapsed(const common::ObTabletID &tablet_id,
                                const int64_t timestamp,
                                ObTransID &block_tx_id);

  // check schduler status for tx gc
  int check_tx_status(share::SCN &min_start_scn, MinStartScnStatus &status);

  ObITxLogAdapter *get_ls_log_adapter() { return tx_log_adapter_; }

  // Get the tx_table of this LogStream
  int get_tx_table_guard(ObTxTableGuard &guard) {
    return tx_table_->get_tx_table_guard(guard);
  }

  ObTxTable *get_tx_table() {
    return tx_table_;
  }

  // Get the LockTable of this LogStream
  int get_lock_memtable(ObTableHandleV2 &handle) {
    return lock_table_->get_lock_memtable(handle);
  }
  // dump a single tx data to text
  // @param [in] tx_id
  // @param [in] fd
  int dump_single_tx_data_2_text(const int64_t tx_id, FILE *fd);
  int start_readonly_request();
  int end_readonly_request();

public:
  // Increase this ObLSTxCtxMgr's total_tx_ctx_count
  void inc_total_tx_ctx_count() { (void)ATOMIC_AAF(&total_tx_ctx_count_, 1); }

  // Decrease this ObLSTxCtxMgr's total_tx_ctx_count
  void dec_total_tx_ctx_count() { (void)ATOMIC_AAF(&total_tx_ctx_count_, -1); }

  // Increase active trx count in this ls
  void inc_active_tx_count() { (void)ATOMIC_AAF(&active_tx_count_, 1); }

  // Decrease active trx count in this ls
  void dec_active_tx_count() { (void)ATOMIC_AAF(&active_tx_count_, -1); }

  void inc_total_active_readonly_request_count()
  {
    const int64_t count = ATOMIC_AAF(&total_active_readonly_request_count_, 1);
  }
  void dec_total_active_readonly_request_count()
  {
    int ret = common::OB_ERR_UNEXPECTED;
    const int64_t count = ATOMIC_AAF(&total_active_readonly_request_count_, -1);
    if (OB_UNLIKELY(count < 0)) {
      TRANS_LOG(ERROR, "unexpected total_active_readonly_request_count", KP(this), K(count));
    }
  }
  int64_t get_total_active_readonly_request_count() { return ATOMIC_LOAD(&total_active_readonly_request_count_); }

  // Get all tx obj lock information in this ObLSTxCtxMgr
  // @param [out] iter: all tx obj lock op information
  int iterate_tx_obj_lock_op(ObLockOpIterator &iter);

  // Get all tx lock information in this ObLSTxCtxMgr
  // @param [out] trans_lock_stat_iter: all tx lock information
  int iterate_tx_lock_stat(ObTxLockStatIterator &tx_lock_stat_iter);

  // Get all tx_ctx stat information in this ObLSTxCtxMgr
  // @param [out] tx_stat_iter: all TxCtx stat information
  int iterate_tx_ctx_stat(ObTxStatIterator &tx_stat_iter);

  // Get all tx_ids in one hash bucket from the hashtable of this ObLSTxCtxMgr, and store them in
  // ObTxIDIterator for subsequent iterative access;
  // @param [in] iter: tx_id information;
  // @param [in] bucket_pos: the specified bucket pos;
  int iterator_tx_id_in_one_bucket(ObTxIDIterator& iter, int bucket_pos);

  // Get all tx_ids from the hashtable of this ObLSTxCtxMgr, and store them in
  // ObTxIDIterator for subsequent iterative access;
  // @param [in] iter: tx_id information;

  // Get the buckets cnt of ObLSTxCtxMgr'hashtable, which is a constant value;
  static int64_t get_tx_ctx_map_buckets_cnt()
  { return ObLSTxCtxMap::get_buckets_cnt(); }

  // Get the minimum value of rec_scn of TxCtx in this ObLSTxCtxMgr;
  // This value is used as the starting point of the redo replay required by the ObTxCtxTable file;
  // @param [out] rec_scnMIN(TxCtx's rec_scn in ObLSTxCtxMgr)
  int get_rec_scn(share::SCN &rec_scn);

  // Notify that the ObTxCtxTable corresponding to this ObLSTxCtxMgr has completed the flush operation;
  // Traverse ObLSTxCtxMgr and set the prev_rec_scn of each ObTxCtx to OB_INVALID_TIMESTAMP;
  int on_tx_ctx_table_flushed();

  // Print all transaction information in this ObLSTxCtxMgr to the log;
  // @param [in] max_print: The Max Count of TxCtx to Print
  // @param [in] verbose: if it set to true, print the TxCtx's Detailed information
  void print_all_tx_ctx(const int64_t max_print, const bool verbose);

  // Before Deleting the memtable, remove its associated callback from the callback list of all the
  // active transactions;
  // When the memtable has mini-merged, the commit_version of its associated
  // transaction may be undecided;
  // @param [in] mt: the memtable point which is going to be deleted;
  int remove_callback_for_uncommited_tx(
    const memtable::ObMemtableSet *memtable_set);

  // Find the TxCtx and execute the check functor with the tx_data retained on the TxCtx;
  // Called by the ObTxCtxTable
  // @param [in] tx_id: transaction ID
  // @param [in] fn: the check functor
  int check_with_tx_data(const ObTransID &tx_id, ObITxDataCheckFunctor &fn);

  // TODO Remove
  int get_min_undecided_scn(share::SCN &scn);

  // Iterate all tx ctx in this ls and get the min_start_scn
  int get_min_start_scn(share::SCN &min_start_scn);

  // Get the trans_service corresponding to this ObLSTxCtxMgr;
  transaction::ObTransService *get_trans_service() { return txs_; }

  // check is blocked
  bool is_tx_blocked() const { return is_tx_blocked_(); }

  // check all blocked
  bool is_all_blocked() const { return is_all_blocked_(); }
  bool is_normal_tx_blocked() const { return is_normal_blocked_(); }

  // Switch the prev_aggre_log_ts and aggre_log_ts during dump starts
  int refresh_aggre_rec_scn();

  // Update aggre_log_ts without lock, because we canot lock using the order of
  // ObTxCtx -> ObLSTxCtxMgr, It will be a deadlock with normal order.
  int update_aggre_log_ts_wo_lock(share::SCN rec_log_ts);

  int get_max_decided_scn(share::SCN & scn);

  TO_STRING_KV(KP(this),
               K_(stopped),
               K_(block_tx),
               K_(block_normal_tx),
               K_(block_all),
               K_(total_tx_ctx_count),
               K_(active_tx_count),
               K_(aggre_rec_scn),
               K_(prev_aggre_rec_scn));
private:
  DISALLOW_COPY_AND_ASSIGN(ObLSTxCtxMgr);

private:
  static const int64_t OB_TRANS_STATISTICS_INTERVAL = 60 * 1000 * 1000;
  static const int64_t OB_PARTITION_AUDIT_LOCAL_STORAGE_COUNT = 4;
  static const int64_t TRY_THRESOLD_US = 1 * 1000 *1000;
  static const int64_t RETRY_INTERVAL_US = 10 *1000;

private:
  int process_callback_(ObTxCommitCallback *&cb_list) const;
  void print_all_tx_ctx_(const int64_t max_print, const bool verbose);
  int64_t get_tx_ctx_count_() const { return ATOMIC_LOAD(&total_tx_ctx_count_); }
  int create_tx_ctx_(const ObTxCreateArg &arg,
                     bool &existed,
                     ObTxCtx *&ctx);
  int get_tx_ctx_(const ObTransID &tx_id, const bool for_replay, ObTxCtx *&tx_ctx);
  share::SCN get_aggre_rec_scn_();
public:
  static const int64_t MAX_HASH_ITEM_PRINT = 16;
  static const int64_t WAIT_READONLY_REQUEST_TIME = 10 * 1000 * 1000;

private:
  inline bool is_tx_blocked_() const
  { return ATOMIC_LOAD(&block_tx_); }

  inline bool is_normal_blocked_() const
  { return ATOMIC_LOAD(&block_normal_tx_); }

  inline bool is_all_blocked_() const
  { return ATOMIC_LOAD(&block_all_); }

  inline bool is_stopped_() const
  { return ATOMIC_LOAD(&stopped_); }

private:
  // Identifies this ObLSTxCtxMgr is inited or not;
  bool is_inited_;

  bool stopped_;
  bool block_tx_;
  bool block_normal_tx_;
  bool block_all_;

  // A thread-safe hashmap, used to find and traverse TxCtx in this ObLSTxCtxMgr
  ObLSTxCtxMap ls_tx_ctx_map_;


  // The tx table associated with this ObLSTxCtxMgr
  ObTxTable *tx_table_;
  // The Lock Table associate with this ObLSTxCtxMgr
  ObLockTable *lock_table_;

  ObITxLogAdapter *tx_log_adapter_;
  ObLSTxLogAdapter log_adapter_def_;

  mutable RWLock rwlock_;
  // Total TxCtx count in this ObLSTxCtxMgr
  int64_t total_tx_ctx_count_ CACHE_ALIGNED;

  int64_t total_active_readonly_request_count_ CACHE_ALIGNED;

  int64_t active_tx_count_;

  // Transaction service which hold the ObTxCtxMgr;
  ObTransService *txs_;

  // The time source
  ObTsMgr *ts_mgr_;

  // Recover log timestamp for aggregated tx ctx. For the purpose of Durability,
  // we need a space to store rec_log_ts for checkpoint[1]. Although tx ctx
  // itself can store rec_log_ts, it may release its ctx when txn itself
  // finishes. So we decide to store all tx ctx, whose rec_log_ts should be
  // remembered while must be released soon, in the ctx_mgr.
  //
  // [1]: It you are interested in rec_log_ts, you can see ARIES paper.
  share::SCN aggre_rec_scn_;
  share::SCN prev_aggre_rec_scn_;

  // Online timestamp for ObLSTxCtxMgr
  int64_t online_ts_;
};

// Used to iteratively access TxCtx in ObLSTxCtxMgr;
//
// ObLSTxCtxIterator holds an ObTxIDIterator; ObLSTxCtxIterator will batch out all tx_ids in a
// single hash_bucket from the hashtable of the ObLSTxCtxMgr and store them in the ObTxIDIterator
// it holds;
// In ObLSTxCtxIterator::get_next_tx_ctx function, take out the next tx_id from ObTxIDIterator,
// and then obtain TxCtx by searching ObLSTxCtxMgr; if there is a concurrent delete
// TxCtx request from generating tx_id_array to searching for TxCtx, it is normal that TxCtx
// cannot be found;
// When the tx_id in ObTxIDIterator is exhausted, go to the next hash bucket to batch out all tx_ids;
//
// In this way, the additional memory can be controlled on the number of tx_id of a single bucket;
// the current hashmap is divided into 16384 buckets; it can achieve a small additional memory footprint;
class ObLSTxCtxIterator
{
public:
  ObLSTxCtxIterator() : BUCKETS_CNT_(ObLSTxCtxMgr::get_tx_ctx_map_buckets_cnt()) { reset(); }
  ~ObLSTxCtxIterator() {}
  void reset();

  int set_ready(ObLSTxCtxMgr* ls_tx_ctx_mgr);
  bool is_ready() const {return is_ready_; }

  int get_next_tx_ctx(ObTxCtx *&tx_ctx);
  int revert_tx_ctx(ObTxCtx *tx_ctx);

private:
  int get_next_tx_id_(ObTransID& tx_id);

private:
  bool is_ready_;
  int64_t current_bucket_pos_;
  const int64_t BUCKETS_CNT_;
  ObLSTxCtxMgr* ls_tx_ctx_mgr_;
  ObTxIDIterator tx_id_iter_;
};

class ObTxCtxMgr
{
public:
  ObTxCtxMgr()
      : is_inited_(false),
        is_running_(false),
        tx_ctx_mgr_(NULL),
        ts_mgr_(NULL),
        txs_(NULL)
  {
    reset();
  }
  ~ObTxCtxMgr() { destroy(); }
  // @param [in] ts_mgr: used to get gts, see: update_max_replay_commit_version function;
  // @param [in] txs: transaction service which hold the ObTxCtxMgr;
  int init(ObTsMgr *ts_mgr, ObTransService *txs);

  // Mark ObTxCtxMgr as running
  int start();

  // Stop the transaction context manager and mark ObTxCtxMgr not running.
  int stop();

  // Destroy the ObTxCtxMgr;
  void destroy();

  // Wait until the transaction context manager has stopped and holds no TxCtx.
  int wait();

  // Reset the ObTxCtxMgr;
  void reset();
public:

  // Find a specified TxCtx.
  // @param [in] tx_id: transaction ID
  // @param [in] for_replay: Identifies whether the TxCtx is used by replay processing
  // @param [out] tx_ctx: context found through ls transaction context manager's hash table
  // @return OB_SUCCESS, if found the transaction tx_ctx
  int get_tx_ctx(const ObTransID &tx_id,
                 const bool for_replay,
                 ObTxCtx *&tx_ctx);

  // Create a TxCtx with the specified transaction ID.
  // @param [in] tx_id: transaction ID
  // @param [in] for_replay: Identifies whether the TxCtx is created for replay processing;
  // @param [out] existed:
  //              if it's true, means that an existing TxCtx with the same tx_id has been found;
  // TODO insert_and_get return existed object ptr;
  // @param [out] tx_ctx: newly allocated or already exsited transaction context
  // @return OB_SUCCESS, if the transaction tx_ctx newly allocated or already existed
  int create_tx_ctx(const ObTxCreateArg &arg,
                    bool& existed,
                    ObTxCtx *&tx_ctx);

  // Decrease the specified tx_ctx's reference count
  // @param [in] tx_ctx: the TxCtx will be revert
  int revert_tx_ctx(ObTxCtx *tx_ctx);

  int create_context_manager(ObTxTable *tx_table,
                             ObLockTable *lock_table,
                             storage::ObLSTxService &ls_tx_svr,
                             ObITxLogParam *param,
                             ObITxLogAdapter *log_adapter);

  // Delete the context manager after all active TxCtx objects are stopped.
  int remove_context_manager(const bool graceful);

  // Block the context manager, so that it can no longer create a new TxCtx,
  // and returns whether there are still active transactions through out parameters;
  // @param [out] is_all_tx_cleaned_up, set it to true, when all transactions are cleaned up;
  int block_tx(bool &is_all_tx_cleaned_up);
  // block tx and readonly request
  int block_all(bool &is_all_tx_cleaned_up);

  // Get the min prepare version of transaction module of current observer for slave read
  // @param [out] min_prepare_version: MIN(uncommitted transaction ctx's prepare version)
  int get_min_uncommit_tx_prepare_version(share::SCN &min_prepare_version);

  // Get transaction context manager statistics.
  // @param [in] addr: the address of the observer;
  // @param [out] tx_ctx_mgr_stat: the collected context manager statistics;
  int get_tx_ctx_mgr_stat(const ObAddr &addr,
                          ObLSTxCtxMgrStat &tx_ctx_mgr_stat);

  // Get all transaction information at the server level
  // @param [out] tx_stat_iter: Used to return all the collected TxCtx's Stat information,
  //             and then used to iteratively output to the virtual table;
  int iterate_all_observer_tx_stat(ObTxStatIterator &tx_stat_iter);

  // Before Deleting the memtable, remove its associated callback from the callback list of all the
  // active transactions; When the memtable has mini-merged, the commit_version of its associated
  // transaction may be undecided;
  // @param [in] mt: the memtable point which will be deleted;
  int remove_callback_for_uncommited_tx(const memtable::ObMemtableSet *memtable_set);

  TO_STRING_KV(K(is_inited_), KP(this));


  ObLSTxCtxMgr &get_tx_ctx_manager() { return *tx_ctx_mgr_; }


private:
  int stop_context_manager_(const bool graceful);

  int wait_context_manager_();

  int remove_context_manager_();

  int print_tx_ctx_();

  void release_tx_ctx_mgr_();
private:
  bool is_inited_;

  bool is_running_;

  ObLSTxCtxMgr *tx_ctx_mgr_;

  // The time source
  ObTsMgr *ts_mgr_;

  // Transaction service which hold the ObTxCtxMgr;
  ObTransService *txs_;
private:
  DISALLOW_COPY_AND_ASSIGN(ObTxCtxMgr);
};

} // transaction
} // oceanbase

#endif // OCEANBASE_TRANSACTION_OB_TX_CTX_MGR
