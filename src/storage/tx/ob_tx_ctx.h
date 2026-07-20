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

#ifndef OCEANBASE_TRANSACTION_OB_TX_CTX_
#define OCEANBASE_TRANSACTION_OB_TX_CTX_

#include "ob_trans_ctx.h"
#include "ob_ts_mgr.h"
#include "ob_trans_ctx_mgr.h"
#include "ob_ctx_tx_data.h"
#include "lib/list/ob_dlist.h"
#include "lib/queue/ob_link.h"
#include "logservice/palf/lsn.h"
#include "ob_tx_ctx_mds.h"
#include <cstdint>
#include "storage/multi_data_source/buffer_ctx.h"
#include "storage/tx/ob_tx_log_cb_define.h"


namespace oceanbase
{

namespace common
{
class ObAddr;
}

namespace storage
{
struct ObStoreRowLockState;
class ObDDLRedoLog;
}

namespace transaction
{
class ObITransCtxMgr;
class ObTransMsg;
class ObTxLogBlock;
enum class ObTxLogType : int64_t;
class ObTxRedoLog;
class ObTxCommitInfoLog;
class ObTxCommitLog;
class ObTxAbortLog;
class ObTxClearLog;
class ObIRetainCtxCheckFunctor;
}
namespace palf
{
class LSN;
}
namespace memtable
{
class ObIMemtableCtxFactory;
class ObIMemtableCtx;
};

namespace storage
{
class ObTxData;
};

enum
{
  USER_REQUEST_UNKNOWN = -1,
  USER_COMMIT = 0,
  USER_ABORT = 1,
};

namespace transaction
{

const static int64_t OB_TX_MAX_LOG_CBS = 15;
const static int64_t PREALLOC_LOG_CALLBACK_COUNT = 3;
const static int64_t RESERVE_LOG_CALLBACK_COUNT_FOR_FREEZING = 1;

template<typename T, typename fn>
int64_t search(const ObIArray<T> &array, fn &equal_func)
{
  int ret = OB_SUCCESS;
  int64_t search_index = -1;

  ARRAY_FOREACH_X(array, i, cnt, search_index == -1) {
    if (equal_func(array.at(i))) {
      search_index = i;
    }
  }

  return search_index;
}
template <typename>
class ObTxCtxLogOperator;

// participant transaction context
class ObTxCtx : public ObTransCtx, public common::ObLink
{
  friend class ObTransService;
  friend class IterateTxStatFunctor;
  friend class IterateTransStatForKeyFunctor;
  friend class ObTxELRHandler;
  friend class ObIRetainCtxCheckFunctor;
  friend class memtable::ObRedoLogGenerator;
  template<typename T>
  friend class ObTxCtxLogOperator;
public:
  ObTxCtx()
      : ObTransCtx(),
        common::ObLink(),
        is_inited_(false), mt_ctx_(), reserve_allocator_("PartCtx"),
        exec_info_(reserve_allocator_),
        mds_cache_(reserve_allocator_),
        has_extra_log_cb_group_(false),
        has_async_index_redo_(false),
        reserve_log_cb_group_(true/*is_reserve*/),
        extra_cb_group_list_()
  { /*reset();*/ }
  ~ObTxCtx() { destroy(); }
  void destroy();
  int init(const uint32_t session_id,
           const uint32_t associated_session_id,
           const ObTransID &trans_id,
           const int64_t trans_expired_time,
           const uint64_t cluster_version,
           ObTransService *trans_service,
           ObLSTxCtxMgr *ls_ctx_mgr,
           const bool for_replay,
           const TxCtxSource ctx_source,
           ObXATransID xid);
  void reset() { }
  int construct_context(const ObTransMsg &msg);
public:
  int start_trans();
  bool is_inited() const;
  int handle_timeout(const int64_t delay);
  int gts_callback_interrupted();
  int gts_elapse_callback(const share::SCN &gts);
  /*
   * graceful kill: wait trx finish logging
   */
  int kill(const KillTransArg &arg, ObTxCommitCallback *&cb_list);
  memtable::ObMemtableCtx *get_memtable_ctx() { return &mt_ctx_; }
  int commit(const MonotonicTs &commit_time,
             const int64_t &expire_ts,
             const common::ObString &app_trace_info,
             const int64_t &request_id);
  int abort(const int reason);
  int one_phase_commit_();
  int get_prepare_version_if_prepared(bool &is_prepared, share::SCN &prepare_version);
  const share::SCN get_commit_version() const { return ctx_tx_data_.get_commit_version(); }
  int dump_2_text(FILE *fd);
public:
  int set_trans_app_trace_id_str(const ObString &app_trace_id_str);
  const ObString &get_trans_app_trace_id_str() const { return trace_info_.get_app_trace_id(); }
  int check_modify_schema_elapsed(const ObTabletID &tablet_id,
                                  const int64_t schema_version);
  int check_modify_time_elapsed(const ObTabletID &tablet_id,
                                const int64_t timestamp);
  int iterate_tx_obj_lock_op(ObLockOpIterator &iter) const;
  int iterate_tx_lock_priority_list(ObPrioOpIterator &iter) const;
  int iterate_tx_lock_stat(ObTxLockStatIterator &iter);
  int get_memtable_key_arr(ObMemtableKeyArray &memtable_key_arr);
  uint64_t get_lock_for_read_retry_count() const { return mt_ctx_.get_lock_for_read_retry_count(); }

  int check_tx_status();
  int remove_callback_for_uncommited_txn(const memtable::ObMemtableSet *memtable_set);
  int64_t get_trans_mem_total_size() const { return mt_ctx_.get_trans_mem_total_size(); }
  int check_with_tx_data(ObITxDataCheckFunctor &fn);
  const share::SCN get_rec_log_ts() const;
  int on_tx_ctx_table_flushed();
  int update_rec_log_ts_for_parallel_replay(const SCN &rec_log_ts);

  int64_t get_applying_log_ts() const;
  int64_t get_pending_log_size() { return mt_ctx_.get_pending_log_size(); }
  int64_t get_flushed_log_size() { return mt_ctx_.get_flushed_log_size(); }
  virtual int64_t get_part_trans_action() const override;
  inline bool has_pending_write() const { return pending_write_; }

  // Change Stream: mark this tx participant as containing redo for async-indexed tables.
  void set_has_async_index_redo() { has_async_index_redo_ = true; }

  // table lock
  void set_table_lock_killed()
  { mt_ctx_.set_table_lock_killed(); }
  bool is_table_lock_killed() const;

  uint32_t get_session_id() const { return session_id_; }

  // for elr
  bool is_can_elr() const { return can_elr_; }

public:
  // thread safe
  int64_t to_string(char* buf, const int64_t buf_len) const;
private:
  // thread unsafe
  ON_DEMAND_TO_STRING_KV_(K_(session_id),
                          K_(associated_session_id),
                          K_(part_trans_action),
                          K_(pending_write),
                          K(extra_cb_group_list_.get_size()),
                          K(busy_cbs_.get_size()),
                          K(ctx_tx_data_),
                          K(create_ctx_scn_),
                          "ctx_source",
                          ctx_source_,
                          K(replay_completeness_),
                          "target_state",
                          to_str_tx_state(target_state_),
                          K_(rec_log_ts),
                          K_(prev_rec_log_ts),
                          K_(last_request_ts),
                          KP_(block_frozen_memtable),
                          K(mt_ctx_));

public:
  static const int64_t OP_LOCAL_NUM = 16;
  static const int64_t RESERVED_MEM_SIZE = 256;
private:
  void default_init_();
  int init_memtable_ctx_();
  bool is_logging_() const;

  // force abort but not submit abort log
  bool need_force_abort_() const;
  // force abort but wait abort log_cb
  bool is_force_abort_logging_() const;

  bool need_record_log_() const;
  void reset_redo_lsns_();
  void set_prev_record_lsn_(const LogOffSet &prev_record_lsn);
  int trans_clear_(const share::SCN log_ts);
  int trans_kill_();
  int tx_end_(const bool commit);
  int trans_replay_commit_(const share::SCN &commit_version,
                           const share::SCN &final_log_ts,
                           const uint64_t log_cluster_version,
                           const uint64_t checksum);
  int trans_replay_abort_(const share::SCN &final_log_ts);
  int update_publish_version_(const share::SCN &publish_version, const bool for_replay);
  bool can_be_recycled_();
  bool need_to_check_tx_status_();
  int gc_ctx_();

  int common_on_success_(ObTxLogCb * log_cb);
  int on_success_ops_(ObTxLogCb * log_cb);
  void check_and_register_timeout_task_();

public:
  // ========================================================
  // newly added for 4.0

  bool is_decided() const { return ctx_tx_data_.is_decided(); }
  // check data is rollbacked either partially or totally
  // in which case the reader should be failed
  bool is_data_rollbacked() const { return mt_ctx_.is_tx_rollbacked(); }
  int on_success(ObTxLogCb *log_cb);
  int on_failure(ObTxLogCb *log_cb);

  int try_submit_next_log();
  // for instant logging and freezing
  int submit_redo_after_write(const bool force, const ObTxSEQ &write_seq_no);
  int submit_redo_log_for_freeze(const uint32_t freeze_clock);
  int return_redo_log_cb(ObTxLogCb *log_cb);
  int push_replaying_log_ts(const share::SCN log_ts_ns, const int64_t log_entry_no);
  int push_replayed_log_ts(const share::SCN log_ts_ns,
                           const palf::LSN &offset,
                           const int64_t log_entry_no);
  int iter_next_log_for_replay(ObTxLogBlock &log_block,
                               ObTxLogHeader &log_header,
                               const share::SCN log_scn);
  int replay_one_part_of_big_segment(const palf::LSN &offset,
                                     const share::SCN &timestamp,
                                     const int64_t &part_log_no);

  int replay_redo_in_ctx(const ObTxRedoLog &redo_log,
                         const palf::LSN &offset,
                         const share::SCN &timestamp,
                         const int64_t &part_log_no,
                         const bool is_tx_log_queue,
                         const bool serial_final,
                         const ObTxSEQ &max_seq_no);
  int replay_rollback_to(const ObTxRollbackToLog &log,
                         const palf::LSN &offset,
                         const share::SCN &timestamp,
                         const int64_t &part_log_no,
                         const bool is_tx_log_queue,
                         const bool pre_barrier);
  int replay_commit_info(const ObTxCommitInfoLog &commit_info_log,
                         const palf::LSN &offset,
                         const share::SCN &timestamp,
                         const int64_t &part_log_no,
                         const bool pre_barrier);
  int replay_commit(const ObTxCommitLog &commit_log,
                    const palf::LSN &offset,
                    const share::SCN &timestamp,
                    const int64_t &part_log_no,
                    const share::SCN &replay_compact_version);
  int replay_clear(const ObTxClearLog &clear_log,
                   const palf::LSN &offset,
                   const share::SCN &timestamp,
                   const int64_t &part_log_no);
  int replay_abort(const ObTxAbortLog &abort_log,
                   const palf::LSN &offset,
                   const share::SCN &timestamp,
                   const int64_t &part_log_no);

  int replay_multi_data_source(const ObTxMultiDataSourceLog &log,
                               const palf::LSN &lsn,
                               const share::SCN &timestamp,
                               const int64_t &part_log_no);

  int replay_record(const ObTxRecordLog &log,
                    const palf::LSN &lsn,
                    const share::SCN &timestamp,
                    const int64_t &part_log_no);

  void force_no_need_replay_checksum(const bool parallel_replay, const share::SCN &log_ts);

  void check_no_need_replay_checksum(const share::SCN &log_ts, const int index);
  bool is_replay_complete_unknown() const { return replay_completeness_.is_unknown(); }
  int set_replay_incomplete(const share::SCN log_ts);
  // return the min log ts of those logs which are submitted but
  // not callbacked yet, if there is no such log return INT64_MAX
  const share::SCN get_min_undecided_log_ts() const;

  // dump and recover tx ctx table using the following functions.
  // get_tx_ctx_table_info returns OB_TRANS_CTX_NOT_EXIST if the tx ctx table need not to be
  // dumped.
  int get_tx_ctx_table_info(ObTxCtxTableInfo &info);
  int serialize_tx_ctx_to_buffer(ObTxLocalBuffer &buffer, int64_t &serialize_size);
  int recover_tx_ctx_table_info(ObTxCtxTableInfo &ctx_info);

  int correct_cluster_version_(uint64_t cluster_version_in_log);

  bool need_commit_callback_();
  int supplement_tx_op_if_exist_(const bool for_replay, const share::SCN replay_scn);
  int recover_tx_ctx_from_tx_op_(ObTxOpVector &tx_op_list, const share::SCN replay_scn);

  int register_multi_data_source(const ObTxDataSourceType type,
                                 const char *buf,
                                 const int64_t len,
                                 const bool try_lock,
                                 const ObTxSEQ seq_no,
                                 const ObRegisterMdsFlag &register_flag);

  const share::SCN get_start_log_ts()
  {
    return ctx_tx_data_.get_start_log_ts();
  }

  const share::SCN get_tx_end_log_ts() const
  {
    return ctx_tx_data_.get_end_log_ts();
  }

  void set_retain_cause(RetainCause cause)
  {
    ATOMIC_CAS(&retain_cause_, static_cast<int16_t>(RetainCause::UNKOWN),
               static_cast<int16_t>(cause));
  }
  RetainCause get_retain_cause() const { return static_cast<RetainCause>(ATOMIC_LOAD(&retain_cause_)); };

  int del_retain_ctx();

  // ========================================================
private:
  // ========================================================
  // newly added for 4.0
  int submit_log_impl_(const ObTxLogType log_type);
  void handle_submit_log_err_(const ObTxLogType log_type, int &ret);
  typedef logservice::ObReplayBarrierType ObReplayBarrierType;
  int submit_log_block_out_(ObTxLogBlock &block,
                            const share::SCN &base_scn,
                            ObTxLogCb *&log_cb,
                            const int64_t replay_hint = 0,
                            const ObReplayBarrierType barrier = ObReplayBarrierType::NO_NEED_BARRIER,
                            const int64_t retry_timeout_us = 1000);
  int after_submit_log_(ObTxLogBlock &log_block,
                        ObTxLogCb *log_cb,
                        memtable::ObRedoLogSubmitHelper *redo_helper);
  int submit_commit_log_();
  int submit_abort_log_();
  int submit_clear_log_();
  int submit_record_log_();
  int submit_redo_commit_info_log_();
  int submit_redo_if_serial_logging_(ObTxLogBlock &log_block,
                                     bool &has_redo,
                                     memtable::ObRedoLogSubmitHelper &helper);
  int submit_redo_if_parallel_logging_();
  int submit_redo_commit_info_log_(ObTxLogBlock &log_block,
                                   bool &has_redo,
                                   memtable::ObRedoLogSubmitHelper &helper,
                                   logservice::ObReplayBarrierType &barrier);
  int submit_pending_log_block_(ObTxLogBlock &log_block,
                                memtable::ObRedoLogSubmitHelper &helper,
                                const logservice::ObReplayBarrierType &barrier);
  bool should_switch_to_parallel_logging_();
  int switch_to_parallel_logging_(const share::SCN serial_final_scn,
                                  const ObTxSEQ max_seq_no);
  bool has_replay_serial_final_() const;
  void recovery_parallel_logging_();
  int check_can_submit_redo_();
  void force_no_need_replay_checksum_(const bool parallel_replay, const share::SCN &log_ts);
  int serial_submit_redo_after_write_(int &submitted_cnt);
  int submit_big_segment_log_();
  int prepare_big_segment_submit_(ObTxLogCb *segment_cb,
                                  const share::SCN &base_scn,
                                  logservice::ObReplayBarrierType barrier_type,
                                  const ObTxLogType & segment_log_type);
  int add_unsynced_segment_cb_(ObTxLogCb *log_cb);
  int remove_unsynced_segment_cb_(const share::SCN &remove_scn);
  share::SCN get_min_unsyncd_segment_scn_();
  int init_log_block_(ObTxLogBlock &log_block,
                      const int64_t suggested_buf_size = ObTxAdaptiveLogBuf::NORMAL_LOG_BUF_SIZE,
                      const bool serial_final = false);
  int reuse_log_block_(ObTxLogBlock &log_block);
  int compensate_abort_log_();
  int validate_commit_info_log_(const ObTxCommitInfoLog &commit_info_log);

  int64_t get_redo_log_no_() const;
  bool has_persisted_log_() const;

  int update_replaying_log_no_(const share::SCN &log_ts_ns, int64_t part_log_no);
  int check_and_merge_redo_lsns_(const palf::LSN &offset);
  int try_submit_next_log_(const bool for_freeze = false);
  // redo lsns is stored when submit log, when log fails to majority
  // and is callbacked via on_failure, redo lsns should be fixed
  int fix_redo_lsns_(const ObTxLogCb *log_cb);

  int do_local_tx_end_(TxEndAction tx_end_action);
  // int on_local_tx_end_(TxEndAction tx_end_action);
  int do_local_commit_tx_();
  int do_local_abort_tx_();
  int do_force_kill_tx_();
  int on_local_commit_tx_();
  int after_local_commit_succ_();
  int on_local_abort_tx_();

  // int local_tx_abort_();
  int abort_(int reason);

  int update_max_commit_version_();

  int generate_prepare_version_();
  int generate_commit_version_();

  bool is_committing_() const;
  // int insert_to_tx_table_(ObTxData *tx_data);
  int replay_update_tx_data_(const bool commit,
                             const share::SCN &log_ts,
                             const share::SCN &commit_version);
  int replace_tx_data_with_backup_(const ObTxDataBackup &backup, share::SCN log_ts_ns);
  void set_durable_state_(const ObTxState state)
  { exec_info_.state_ = state; }
  ObTxState get_durable_state_() const
  { return exec_info_.state_; }

  int notify_table_lock_(const SCN &log_ts,
                         const bool for_replay,
                         const ObTxBufferNodeArray &notify_array,
                         const bool is_force_kill);
  int notify_data_source_(const NotifyType type,
                          const share::SCN &log_ts,
                          const bool for_replay,
                          const ObTxBufferNodeArray &notify_array,
                          const bool willing_to_commit = true,
                          const bool is_force_kill = false);
  int gen_total_mds_array_(ObTxBufferNodeArray &mds_array);
  int deep_copy_mds_array_(const ObTxBufferNodeArray &mds_array,
                           ObTxBufferNodeArray &incremental_array,
                           bool need_replace = false);
  int prepare_mds_tx_op_(const ObTxBufferNodeArray &mds_array,
                         share::SCN op_scn,
                         share::ObTenantTxDataOpAllocator &tx_op_allocator,
                         ObTxOpArray &tx_op_list,
                         bool is_replay);
  int replay_mds_to_tx_table_(const ObTxBufferNodeArray &mds_node_array, const share::SCN op_scn);
  int insert_mds_to_tx_table_(ObTxLogCb &log_cb);
  int insert_undo_action_to_tx_table_(ObUndoAction &undo_action,
                                      ObTxDataGuard &new_tx_data_guard,
                                      storage::ObUndoStatusNode *&undo_node,
                                      const share::SCN op_scn);
  int replay_undo_action_to_tx_table_(ObUndoAction &undo_action, const share::SCN op_scn);
  int decide_state_log_barrier_type_(const ObTxLogType &state_log_type,
                                     logservice::ObReplayBarrierType &final_barrier_type);
  bool is_contain_mds_type_(const ObTxDataSourceType target_type);
  int submit_multi_data_source_();
  int submit_multi_data_source_(ObTxLogBlock &log_block);
  void clean_retain_cause_()
  {
    ATOMIC_STORE(&retain_cause_, static_cast<int16_t>(RetainCause::UNKOWN));
  }

  int try_alloc_retain_ctx_func_();
  int try_gc_retain_ctx_func_();
  int insert_into_retain_ctx_mgr_(RetainCause cause,
                                  const share::SCN &log_ts,
                                  palf::LSN lsn,
                                  bool for_replay);

  int prepare_mul_data_source_tx_end_(bool is_commit);

  bool is_support_parallel_replay_() const;
  int set_replay_completeness_(const bool complete, const share::SCN replay_scn);
  int errsim_notify_mds_();
  bool is_support_tx_op_() const;
protected:
  virtual int get_gts_(share::SCN &gts);
  virtual int wait_gts_elapse_commit_version_(bool &need_wait);
  virtual int get_local_max_read_version_(share::SCN &local_max_read_version);
  virtual int update_local_max_commit_version_(const share::SCN &commit_version);
private:

  int init_log_cbs_(const ObTransID &tx_id);
  int extend_log_cbs_(ObTxLogCb *&log_cb);
  int extend_log_cb_group_();
  void reset_log_cb_list_(common::ObDList<ObTxLogCb> &cb_list);
  void reset_log_cbs_();
  int prepare_log_cb_(const bool need_final_cb, ObTxLogCb *&log_cb);
  int get_log_cb_(const bool need_final_cb, ObTxLogCb *&log_cb);
  int return_log_cb_(ObTxLogCb *log_cb, bool release_final_cb = false);
  int get_max_submitting_log_info_(palf::LSN &lsn, share::SCN &log_ts);
  int get_prev_log_lsn_(const ObTxLogBlock &log_block, ObTxPrevLogType &prev_log_type, palf::LSN &lsn);
  int set_start_scn_in_commit_log_(ObTxCommitLog &commit_log);



  int check_replay_avaliable_(const palf::LSN &offset,
                              const share::SCN &timestamp,
                              const int64_t &part_log_no,
                              bool &need_replay);
  int update_rec_log_ts_(bool for_replay, const share::SCN &rec_log_ts);
  int refresh_rec_log_ts_();
  int get_tx_ctx_table_info_(ObTxCtxTableInfo &info);
  const share::SCN get_rec_log_ts_() const;

  int check_is_aborted_in_tx_data_(const ObTransID tx_id,
                                   bool &is_aborted);

  // ========================================================

private:
  int set_commit_request_id_(const int64_t request_id);
  int post_tx_commit_resp_(const int status);

  int get_stat_for_virtual_table(bool &has_write_state, int &busy_cbs_cnt);

  int submit_log_if_allow(const char *buf,
                          const int64_t size,
                          const share::SCN &base_ts,
                          ObTxBaseLogCb *cb,
                          const bool need_nonblock,
                          const ObTxCbArgArray &cb_arg_array);

private:
  // int tx_end_(const bool commit, const int64_t commit_version);
  int restart_commit_retry_timer_();
  // ============================ TX COMMITTER END ============================
public:
  /*
   * check_status - check txn ctx is ready to acccept access
   *
   * the preconditions include an active, writable local context.
   */
  int check_status();
  /*
   * start_access - start txn protected resources access
   * @data_seq: the sequence_no of current access will be alloced
   *            new created data will marked with this seq no
   * @branch: branch id of this access
   * @is_delete_insert: tag for delete_insert table
   */
  int start_access(const ObTxDesc &tx_desc,
                   ObTxSEQ &data_seq,
                   const int16_t branch,
                   const concurrent_control::ObWriteFlag &write_flag);
  /*
   * end_access - end of txn protected resources access
   */
  int end_access();
  int check_pending_log_overflow(const int64_t stmt_timeout);
  int rollback_to_savepoint(const int64_t op_sn,
                            ObTxSEQ from_seq,
                            const ObTxSEQ to_seq,
                            const int64_t seq_base);
  bool is_xa_trans() const { return !exec_info_.xid_.empty(); }
  int handle_tx_keepalive_response(const int64_t status);
private:
  bool fast_check_need_submit_redo_for_freeze_() const;
  int check_status_();
  int tx_keepalive_response_(const int64_t status);
  void report_write_ctx_status_(const int status, const bool check_tx_status);
  void notify_tx_killed_(const int kill_reason);
  int rollback_to_savepoint_(const ObTxSEQ from_scn,
                             const ObTxSEQ to_scn,
                             const share::SCN replay_scn = share::SCN::invalid_scn());
  int submit_rollback_to_log_(const ObTxSEQ from_scn,
                              const ObTxSEQ to_scn);
  int submit_redo_log_for_freeze_(bool &try_submit, const uint32_t freeze_clock);
  void print_first_mvcc_callback_();

  void set_target_state(ObTxState state) { target_state_ = state; }

public:
  int prepare_for_submit_redo(ObTxLogCb *&log_cb,
                              ObTxLogBlock &log_block,
                              const bool serial_final = false);
  int submit_redo_log_out(ObTxLogBlock &log_block,
                          ObTxLogCb *&log_cb,
                          memtable::ObRedoLogSubmitHelper &helper,
                          const int64_t replay_hint,
                          const bool has_hold_ctx_lock,
                          share::SCN &submitted_scn);
  bool is_parallel_logging() const;

  ObTxState get_target_state() const { return target_state_; }
  ObTxState get_downstream_state() const { return exec_info_.state_; }

private:
  DISALLOW_COPY_AND_ASSIGN(ObTxCtx);
private:
  //0x0078746374726170 means reset partctx
  static const int64_t PART_CTX_MAGIC_NUM = 0x0078746374726170;
  static const int64_t REPLAY_PRINT_TRACE_THRESHOLD = 10 * 1000; // 10 ms
  static const int64_t REDO_SYNC_TASK_RETRY_INTERVAL_US = 10 * 1000; // 10ms
  static const int64_t END_STMT_SLEEP_US = 10 * 1000; // 10ms
  static const int64_t MAX_END_STMT_RETRY_TIMES = 100;
  static const uint64_t MAX_PREV_LOG_IDS_COUNT = 1024;
  static const bool NEED_FINAL_CB = true;
private:
  bool is_inited_;
  memtable::ObMemtableCtx mt_ctx_;
  share::SCN end_log_ts_;
  int64_t stmt_expired_time_;

  int64_t last_check_tx_status_ts_;
  int64_t cur_query_start_time_;
  // when cluster_version is unknown at ctx created time, will choice
  // CLUSTER_CURRENT_VERSION, which may not the real cluster_version
  // of this transaction
  // this can only happen when create ctx for replay and create ctx
  // for recovery before v.4.3
  bool cluster_version_accurate_;
  /*
   * used during txn protected data access
   */
  // number of in-progress access
  int pending_write_;
  // latest operation sequence no, used to detect duplicate operation
  int64_t last_op_sn_;
  // data sequence no of latest access
  ObTxSEQ last_scn_;
  // data sequence no of first access
  ObTxSEQ first_scn_;
private:
  TransModulePageAllocator reserve_allocator_;
  // ========================================================
  // newly added for 4.0
  // persistent state
  ObTxExecInfo exec_info_;
  ObCtxTxData ctx_tx_data_;
  // when multi source data is registered, it is stored in the array below,
  // it is moved to exec_info_.multi_source_data_ when corresponding
  // redo log callbacked.
  ObTxMDSCache mds_cache_;
  ObIRetainCtxCheckFunctor *retain_ctx_func_ptr_;
  // runtime_state_ is volatile
  ObTxRuntimeState runtime_state_;

  bool has_extra_log_cb_group_;
  // Set when DML writes to a table that has async indexes.
  // Propagated to ObTxLogBlockHeader::HAS_ASYNC_INDEX for Change Stream fast filtering.
  bool has_async_index_redo_;
  ObTxLogCbGroup reserve_log_cb_group_;
  TxLogCbGroupList extra_cb_group_list_;
  common::ObDList<ObTxLogCb> free_cbs_;
  common::ObDList<ObTxLogCb> busy_cbs_;

  ObSpinLock log_cb_lock_;
  ObTxLogBigSegmentInfo big_segment_info_;
  // flag if the first callback is linked to a logging_block memtable
  // to prevent unnecessary submit_log actions for freeze
  memtable::ObMemtable *block_frozen_memtable_;
  // The semantic of the rec_log_ts means the log ts of the first state change
  // after the previous checkpoint. So we use the current strategy to maintain
  // the rec_log_ts:
  //
  // 1. Txn ctx maintians two variables named rec_log_ts and prev_rec_log_ts
  // 2. When the txn submits the log, if rec_log_ts is the default value,
  //    update it
  // 3. Each time the tx ctx table is checkpointed, set prev_rec_log_ts to be
  //    the rec_log_ts if unset and rec_log_ts to be default value
  // 4. When the checkpoint is succeed, we set prev_rec_log_ts to be the default value
  // 5. Get rec_log_ts: if prev_rec_log_ts exists, return prev_rec_log_ts,
  //    otherwise return rec_log_ts
  // 6. NB(TODO(handora.qc): Should we maintain it): Requirement is neccessary
  //    that the merge of the tx ctx table must be one at a time
  share::SCN rec_log_ts_;
  share::SCN prev_rec_log_ts_;
  bool is_ctx_table_merged_;
  // +-------------------+                   +---------------------------------+                      +-------+     +-----------------+     +----------------------+
  // | tx_ctx A exiting  |                   |                                 |                      |       |     |   replay from   |     |                      |
  // | start_log_ts = n  |  recover_ts = n   | remove from tx_ctx_table & dump |  recover_ts = n+10   | crash |     | min_ckpt_ts n+m |     | tx_ctx is incomplete |
  // | end_log_ts = n+10 | ----------------> |                                 | -------------------> |       | --> |    (0<m<10)     | --> |                      |
  // +-------------------+                   +---------------------------------+                      +-------+     +-----------------+     +----------------------+
  struct ReplayCompleteness {
    ReplayCompleteness(): complete_(C::UNKNOWN) {}
    void reset() { complete_ = C::UNKNOWN; }
    enum class C : int { COMPLETE = 1, INCOMPLETE = 0, UNKNOWN = -1 } complete_;
    void set(const bool complete) { complete_ = complete ? C::COMPLETE : C::INCOMPLETE; }
    bool is_unknown() const { return complete_ == C::UNKNOWN; }
    bool is_complete() const { return complete_ == C::COMPLETE; }
    bool is_incomplete() const { return complete_ == C::INCOMPLETE; }
    DECLARE_TO_STRING { int64_t pos = 0; BUF_PRINTF("%d", complete_); return pos; };
  } replay_completeness_;
  // set true when submitting redo log for freezing and reset after freezing
  bool is_submitting_redo_log_for_freeze_;
  share::SCN create_ctx_scn_; // replay or recover debug
  TxCtxSource ctx_source_; // transaction context creation source

  int16_t retain_cause_;

  ObTxState target_state_;
  // this is used to denote the time of last request including start_access, commit, rollback
  // this is a tempoary variable which is set to now by default
  int64_t last_request_ts_;

  // ========================================================
};

// reserve log callback for freezing and other two log callbacks for normal
STATIC_ASSERT(OB_TX_MAX_LOG_CBS >= PREALLOC_LOG_CALLBACK_COUNT &&
    PREALLOC_LOG_CALLBACK_COUNT >= (RESERVE_LOG_CALLBACK_COUNT_FOR_FREEZING + 2), "log callback is not enough");

#if defined(__x86_64__)
/* uncomment this block to error messaging real size
template<int s> struct size_of_xxx_;
static size_of_xxx_<sizeof(ObTxCtx)> _x;
*/
// orinally 11264 -> 10000 -> 11264(for 32 log_cbs)
//STATIC_ASSERT(sizeof(ObTxCtx) < 15000, "ObTxCtx is too big ");
#endif

} // transaction
} // oceanbase

#endif // OCEANBASE_TRANSACTION_OB_TX_CTX_
