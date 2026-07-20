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

#ifndef OCEANBASE_PALF_LOG_SERVICE_
#define OCEANBASE_PALF_LOG_SERVICE_

#include "common/ob_role.h"
#include "lib/lock/ob_tc_rwlock.h"
#include "lib/lock/ob_spin_lock.h"
#include "lib/ob_define.h"
#include "share/scn.h"
#include "palf_callback_wrapper.h"
#include "log_engine.h"                      // LogEngine
#include "log_meta.h"
#include "log_cache.h"
#include "lsn.h"
#include "log_mode_mgr.h"
#include "log_sliding_window.h"
#include "log_state_mgr.h"
#include "log_io_task_cb_utils.h"
#include "palf_options.h"
#include "palf_iterator.h"

namespace oceanbase
{
namespace common
{
class ObILogAllocator;
}
namespace palf
{
class FlushLogCbCtx;
class LSN;
class FlushMetaCbCtx;
class LogIOFlushLogTask;
class LogIOFlushMetaTask;
class ReadBuf;
class LogWriteBuf;
class LogIOWorker;
class LogSharedQueueTh;
class IPalfEnvImpl;
class LogEngine;
class LogCache;

struct PalfStat {
  OB_UNIS_VERSION(1);
public:
  PalfStat();
  ~PalfStat() { reset(); }
  bool is_valid() const;
  void reset();

  common::ObAddr self_;
  AccessMode access_mode_;
  LSN begin_lsn_;
  share::SCN begin_scn_;
  LSN base_lsn_;
  LSN end_lsn_;
  share::SCN end_scn_;
  LSN max_lsn_;
  share::SCN max_scn_;
  TO_STRING_KV(K_(self), K_(access_mode),
      K_(begin_lsn), K_(begin_scn), K_(base_lsn), K_(end_lsn), K_(end_scn), K_(max_lsn), K_(max_scn));
};

struct PalfDiagnoseInfo {
  PalfDiagnoseInfo() { reset(); }
  ~PalfDiagnoseInfo() { reset(); }
  palf::LogState log_state_;
  void reset() {
    log_state_ = LogState::INVALID_STATE;
  }
  TO_STRING_KV(K(log_state_));
};

// The interface class of the log service, modules other than logservice are only allowed to call the interfaces of IPalfHandleImpl when using the log service
class IPalfHandleImpl
{
public:
  IPalfHandleImpl() {};
  virtual ~IPalfHandleImpl() {};
public:
  virtual bool check_can_be_used() const = 0;

  virtual int bootstrap() = 0;
  // Persist one log and invoke exactly one completion path: callback success,
  // callback failure, or replay.
  //
  // @param [in] opts, some optional parameters for submitting logs, see the definition of PalfAppendOptions for details
  // @param [in] buf, the starting pointer of the content to be persisted, buf can be released after ::submit_log function returns
  // @param [in] buf_len, the length of the content to be persisted, the valid range of size is [0, 2M]
  // @param [in] ref_scn, log corresponding time, meeting weak read requirements
  //
  // The following two values are passed out via the submit_log return parameters, rather than through on_success() and upper-layer interaction, which allows logic similar to lock_for_read to occur earlier
  // get the accurate version number information
  // @param [out] lsn, the unique identifier of the log
  //                          The main usage scenario is to record the redo log's lsn in the prepare log, used for locating historical logs when pulling back in the data link
  // @param [out] scn, submit_scn corresponding to the log, mainly used for transaction version number, for example, in the lock_for_read scenario
  //
  // @return :TODO
  virtual int submit_log(const PalfAppendOptions &opts,
                         const char *buf,
                         const int64_t buf_len,
                         const share::SCN &ref_scn,
                         LSN &lsn,
                         share::SCN &scn) = 0;
  // Set the recyclable point of the log file, log files with LSN less than or equal to lsn can be safely recycled
  //
  // @param [in] lsn, the log file position that can be recycled
  //
  // @return :TODO
  virtual int set_base_lsn(const LSN &lsn) = 0;
  // Mark the log stream as deleted.
  virtual void set_deleted() = 0;
  // @desc: query coarse lsn by scn, that means there is a LogGroupEntry in disk,
  // its lsn and scn are result_lsn and result_scn, and result_scn <= scn.
  //        result_lsn   result_scn
  //                 \   /
  //      [log 1]     [log 2][log 3] ... [log n]  [log n+1]
  //  -------------------------------------------|-------------> time
  //                                           scn
  // Note that this function may be time-consuming
  // Note that result_lsn always points to head of log file
  // @params [in] scn:
  // @params [out] result_lsn: the lower bound lsn which includes scn
  // @return
  // - OB_SUCCESS: locate_by_scn_coarsely success
  // - OB_INVALID_ARGUMENT
  // - OB_ENTRY_NOT_EXIST: there is no log in disk
  // - OB_ERR_OUT_OF_LOWER_BOUND: scn is too old, log files may have been recycled
  // - OB_NEED_RETRY: the block is being recycled or switched, need retry.
  // - others: bug
  virtual int locate_by_scn_coarsely(const share::SCN &scn, LSN &result_lsn) = 0;

  // @desc: query coarse scn by lsn, that means there is a LogGroupEntry in disk,
  // its lsn and scn are result_lsn and result_scn, and result_lsn <= lsn.
  //  result_lsn    result_scn
  //           \    /
  //    [log 1][log 2][log 3][log 4][log 5]...[log n][log n+1]
  //  --------------------------------------------|-------------> lsn
  //                                             lsn
  // Note that this function may be time-consuming
  // @params [in] lsn: lsn
  // @params [out] result_scn: the lower bound scn which includes lsn
  // - OB_SUCCESS; locate_by_lsn_coarsely success
  // - OB_INVALID_ARGUMENT
  // - OB_ERR_OUT_OF_LOWER_BOUND: lsn is too small, log files may have been recycled
  // - OB_NEED_RETRY: the block is being recycled or switched, need retry.
  // - others: bug
  virtual int locate_by_lsn_coarsely(const LSN &lsn, share::SCN &result_scn) = 0;
  virtual int get_begin_lsn(LSN &lsn) const = 0;
  virtual int get_begin_scn(share::SCN &scn) = 0;
  virtual int get_base_lsn(LSN &lsn) const = 0;
  virtual int get_base_info(const LSN &base_lsn, PalfBaseInfo &base_info) = 0;

  virtual int get_min_block_id_for_gc(block_id_t &min_block_id) = 0;
  virtual int get_min_block_info_for_gc(block_id_t &min_block_id, share::SCN &max_scn) = 0;
  //begin lsn                          base lsn                                end lsn
  //   │                                │                                         │
  //   │                                │                                         │
  //   │                                │                                         │
  //   │                                │                                         │
  //   ▼                                ▼                                         ▼
  //   ┌─────────────────────────────────────────────────────────────────────────────────┐
  //   │                                                                                 │
  //   │                                                                                 │
  //   │                                                                                 │
  //   └─────────────────────────────────────────────────────────────────────────────────┘
  //
  // return the block length which the previous data was committed
  virtual const LSN get_end_lsn() const = 0;
  virtual LSN get_max_lsn() const = 0;
  virtual const share::SCN get_max_scn() const = 0;
  virtual const share::SCN get_end_scn() const = 0;
  virtual const LSN get_readable_end_lsn() const = 0;
  virtual int get_total_used_disk_space(int64_t &total_used_disk_space, int64_t &unrecyclable_disk_space) const = 0;
  virtual const LSN &get_base_lsn_used_for_block_gc() const = 0;
  virtual int delete_block(const block_id_t &block_id) = 0;
  virtual int inner_after_flush_log(const FlushLogCbCtx &flush_log_cb_ctx) = 0;
  virtual int inner_after_flush_meta(const FlushMetaCbCtx &flush_meta_cb_ctx) = 0;
  virtual int inner_after_truncate_prefix_blocks(const TruncatePrefixBlocksCbCtx &truncate_prefix_cb_ctx) = 0;
  virtual int advance_reuse_lsn(const LSN &flush_log_end_lsn) = 0;
  virtual int inner_append_log(const LSN &lsn,
                               const LogWriteBuf &write_buf,
                               const share::SCN &scn) = 0;
  virtual int inner_append_log(const LSNArray &lsn_array,
                               const LogWriteBufArray &write_buf_array,
                               const SCNArray &scn_array) = 0;
  virtual int inner_append_meta(const char *buf,
                                const int64_t buf_len) = 0;
  virtual int inner_truncate_prefix_blocks(const LSN &lsn) = 0;
  virtual int check_and_switch_state() = 0;
  virtual int check_and_switch_freeze_mode() = 0;
  virtual bool is_in_period_freeze_mode() const = 0;
  virtual int period_freeze_last_log() = 0;
  virtual int set_scan_disk_log_finished() = 0;
  virtual int get_access_mode_ref_scn(AccessMode &access_mode,
                                      SCN &ref_scn) const = 0;
  // ===================== Iterator start =======================
  virtual int alloc_palf_buffer_iterator(const LSN &offset,
                                         PalfBufferIterator &iterator) = 0;
  virtual int alloc_palf_buffer_iterator(const SCN &scn,
                                         PalfBufferIterator &iterator) = 0;
  virtual int alloc_palf_group_buffer_iterator(const LSN &offset,
                                               PalfGroupBufferIterator &iterator) = 0;
  virtual int alloc_palf_group_buffer_iterator(const share::SCN &scn,
                                               PalfGroupBufferIterator &iterator) = 0;
  // ===================== Iterator end =======================

  // ==================== Callback start ======================
  virtual int register_file_size_cb(palf::PalfFSCbNode *fs_cb) = 0;
  virtual int unregister_file_size_cb(palf::PalfFSCbNode *fs_cb) = 0;
  // ==================== Callback end ========================
  virtual int stat(PalfStat &palf_stat) = 0;
  virtual int get_palf_epoch(int64_t &palf_epoch) const = 0;
  virtual int diagnose(PalfDiagnoseInfo &diagnose_info) const = 0;
  virtual int read_data_from_buffer(const LSN &read_begin_lsn,
                                    const int64_t in_read_size,
                                    char *buf,
                                    int64_t &out_read_size) const = 0;
  // =================== Callback end ===========================

  virtual int raw_read(const palf::LSN &lsn,
                       char *read_buf,
                       const int64_t nbytes,
                       int64_t &read_size,
                       LogIOContext &io_ctx) = 0;
  virtual int try_handle_next_submit_log() = 0;
  DECLARE_PURE_VIRTUAL_TO_STRING;
};

class PalfHandleImpl : public IPalfHandleImpl
{
public:
  PalfHandleImpl();
  ~PalfHandleImpl() override;
  int init(const AccessMode &access_mode,
           const PalfBaseInfo &palf_base_info,
           const char *log_dir,
           ObILogAllocator *alloc_mgr,
           ILogBlockPool *log_block_pool,
           LogIOWorker *log_io_worker,
           LogSharedQueueTh *log_shared_queue_th,
           IPalfEnvImpl *palf_env_impl,
           const common::ObAddr &self,
           const int64_t palf_epoch,
           LogIOAdapter *io_adapter);
  bool check_can_be_used() const override final;
  // Restart interface
  // 1. Generate iterator, locate the end of meta_storage and log_storage;
  // 2. Read the latest data from meta storage, initialize dio_aligned_buf;
  // 3. Initialize dio_aligned_buf in log_storage;
  // 4. Initialize other fields of palf_handle_impl.
  int load(const char *log_dir,
           ObILogAllocator *alloc_mgr,
           ILogBlockPool *log_block_pool,
           LogIOWorker*log_io_worker,
           LogSharedQueueTh *log_shared_queue_th,
           IPalfEnvImpl *palf_env_impl,
           const common::ObAddr &self,
           const int64_t palf_epoch,
           LogIOAdapter *io_adapter,
           bool &is_integrity);
  void destroy();
  int start();
  int bootstrap() override final;
  int submit_log(const PalfAppendOptions &opts,
                 const char *buf,
                 const int64_t buf_len,
                 const share::SCN &ref_scn,
                 LSN &lsn,
                 share::SCN &scn) override final;

  int set_base_lsn(const LSN &lsn) override final;
  void set_deleted() override final;
  int locate_by_scn_coarsely(const share::SCN &scn, LSN &result_lsn) override final;
  int locate_by_lsn_coarsely(const LSN &lsn, share::SCN &result_scn) override final;
  int read_data_from_buffer(const LSN &read_begin_lsn,
                            const int64_t in_read_size,
                            char *buf,
                            int64_t &out_read_size) const;
  int raw_read(const palf::LSN &lsn,
               char *buffer,
               const int64_t nbytes,
               int64_t &read_size,
               LogIOContext &io_ctx) override;
  int try_handle_next_submit_log();
public:
  int delete_block(const block_id_t &block_id) override final;
  int set_scan_disk_log_finished() override;
  int get_access_mode_ref_scn(AccessMode &access_mode,
                              SCN &ref_scn) const override final;
  // =========================== Iterator start ============================
  int alloc_palf_buffer_iterator(const LSN &offset, PalfBufferIterator &iterator) override final;
  int alloc_palf_buffer_iterator(const SCN &scn, PalfBufferIterator &iterator) override final;
  int alloc_palf_group_buffer_iterator(const LSN &offset, PalfGroupBufferIterator &iterator) override final;
  int alloc_palf_group_buffer_iterator(const share::SCN &scn, PalfGroupBufferIterator &iterator) override final;
  // =========================== Iterator end ============================

  // ==================== Callback start ======================
  int register_file_size_cb(palf::PalfFSCbNode *fs_cb) override final;
  int unregister_file_size_cb(palf::PalfFSCbNode *fs_cb) override final;
  int set_monitor_cb(PalfMonitorCb *monitor_cb);
  int reset_monitor_cb();
  // ==================== Callback end ========================
public:
  int get_begin_lsn(LSN &lsn) const override final;
  int get_begin_scn(share::SCN &scn)  override final;
  int get_base_lsn(LSN &lsn) const override final;
  int get_base_info(const LSN &base_lsn, PalfBaseInfo &base_info) override final;
  int get_min_block_id_for_gc(block_id_t &min_block_id) override final;
  int get_min_block_info_for_gc(block_id_t &min_block_id, share::SCN &max_scn) override final;
  // return the block length which the previous data was committed
  const LSN get_end_lsn() const override final
  {
    LSN committed_end_lsn;
    sw_.get_committed_end_lsn(committed_end_lsn);
    LSN max_flushed_end_lsn;
    (void)sw_.get_max_flushed_end_lsn(max_flushed_end_lsn);
    return MIN(max_flushed_end_lsn, committed_end_lsn);
  }

  LSN get_max_lsn() const override final
  {
    return sw_.get_max_lsn();
  }

  const share::SCN get_max_scn() const override final
  {
    return sw_.get_max_scn();
  }

  const share::SCN get_end_scn() const override final
  {
    return sw_.get_last_slide_scn();
  }
  const LSN get_readable_end_lsn() const override final
  {
    LSN committed_end_lsn;
    sw_.get_committed_end_lsn(committed_end_lsn);
    LSN max_flushed_end_lsn;
    sw_.get_max_flushed_end_lsn(max_flushed_end_lsn);
    return MIN(committed_end_lsn, max_flushed_end_lsn);
  }
  int get_total_used_disk_space(int64_t &total_used_disk_space, int64_t &unrecyclable_disk_space) const;
  // return the smallest recycable lsn
  const LSN &get_base_lsn_used_for_block_gc() const override final
  {
    return log_engine_.get_base_lsn_used_for_block_gc();
  }
  // =====================  LogIOTask start ==========================
  int inner_after_flush_log(const FlushLogCbCtx &flush_log_cb_ctx) override final;
  int inner_after_flush_meta(const FlushMetaCbCtx &flush_meta_cb_ctx) override final;
  int inner_after_truncate_prefix_blocks(const TruncatePrefixBlocksCbCtx &truncate_prefix_cb_ctx) override final;
  int advance_reuse_lsn(const LSN &flush_log_end_lsn);
  int inner_append_log(const LSN &lsn,
                       const LogWriteBuf &write_buf,
                       const share::SCN &scn) override final;
  int inner_append_log(const LSNArray &lsn_array,
                       const LogWriteBufArray &write_buf_array,
                       const SCNArray &scn_array);
  int inner_append_meta(const char *buf,
                        const int64_t buf_len) override final;
  int inner_truncate_prefix_blocks(const LSN &lsn) override final;
  // ==================================================================
  int check_and_switch_state() override final;
  int check_and_switch_freeze_mode() override final;
  bool is_in_period_freeze_mode() const override final;
  int period_freeze_last_log() override final;
  int stat(PalfStat &palf_stat) override final;
  int get_palf_epoch(int64_t &palf_epoch) const override final;

  //config change lock related function


  int diagnose(PalfDiagnoseInfo &diagnose_info) const;
  TO_STRING_KV(K_(self), K_(has_set_deleted));

private:
  int do_init_mem_(const PalfBaseInfo &palf_base_info,
                   const LogMeta &log_meta,
                   const char *log_dir,
                   const common::ObAddr &self,
                   ObILogAllocator *alloc_mgr,
                   IPalfEnvImpl *palf_env_impl);
  int after_flush_snapshot_meta_(const LSN &lsn);
  int get_prev_log_info_(const LSN &lsn, LogInfo &log_info);
  int construct_palf_base_info_(const LSN &max_committed_lsn,
                                PalfBaseInfo &palf_base_info);
  int append_disk_log_to_sw_(const LSN &start_lsn);
  int get_binary_search_range_(const share::SCN &scn,
                               block_id_t &min_block_id,
                               block_id_t &max_block_id,
                               block_id_t &result_block_id);
  int get_block_id_by_scn_(const share::SCN &scn, block_id_t &result_block_id);
  void inc_update_last_locate_block_scn_(const block_id_t &block_id, const share::SCN &scn);

  // ======================= report event begin =======================================
  // ======================= report event end =======================================
  template<typename LogEntryType>
  int alloc_iterator_from_scn_(const SCN &scn,
                               PalfIterator<LogEntryType> &iterator);
private:
  typedef common::RWLock RWLock;
  typedef RWLock::RLockGuard RLockGuard;
  typedef RWLock::WLockGuard WLockGuard;
  typedef common::ObSpinLock SpinLock;
  typedef common::ObSpinLockGuard SpinLockGuard;
  typedef common::RWLock::WLockGuardWithTimeout WLockGuardWithTimeout;
private:
  mutable RWLock lock_;
  char log_dir_[common::MAX_PATH_SIZE];
  LogSlidingWindow sw_;
  LogModeMgr mode_mgr_;
  LogStateMgr state_mgr_;
  LogEngine log_engine_;
  LogCache log_cache_;
  common::ObILogAllocator *allocator_;
  common::ObAddr self_;
  palf::PalfFSCbWrapper fs_cb_wrapper_;
  LogPlugins plugins_;
  // ======optimization for locate_by_scn_coarsely=========
  mutable SpinLock last_locate_lock_;
  share::SCN last_locate_scn_;
  block_id_t last_locate_block_;
  // ======optimization for locate_by_scn_coarsely=========
  int64_t cannot_recv_log_warn_time_;
  int64_t log_disk_full_warn_time_;
  int64_t wait_slide_print_time_us_;
  int64_t append_size_stat_time_us_;
  LSN last_record_append_lsn_;
  // NB: only set has_set_deleted_ to true when this palf_handle has been deleted.
  bool has_set_deleted_;
  IPalfEnvImpl *palf_env_impl_;
  bool diskspace_enough_;
  ObMiniStat::ObStatItem append_cost_stat_;
  ObMiniStat::ObStatItem flush_cb_cost_stat_;
  ObMiniStat::ObStatItem handle_submit_log_cost_stat_;
  int64_t last_accum_write_statistic_time_;
  int64_t accum_write_log_size_;  // the accum size of written logs
  int64_t last_dump_info_time_us_;
  bool is_inited_;
};
} // end namespace palf
} // end namespace oceanbase
#endif // OCEANBASE_LOGSERVICE_LOG_SERVICE_
