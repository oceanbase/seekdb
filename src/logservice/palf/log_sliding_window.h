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

#ifndef OCEANBASE_LOGSERVICE_LOG_SLIDING_WINDOW_
#define OCEANBASE_LOGSERVICE_LOG_SLIDING_WINDOW_

#include <stdint.h>
#include "lib/hash/ob_linear_hash_map.h"
#include "lib/lock/ob_spin_lock.h"
#include "lib/thread/ob_thread_lease.h"
#include "share/scn.h"
#include "log_group_entry.h"
#include "log_group_buffer.h"
#include "log_checksum.h"
#include "lsn.h"
#include "lsn_allocator.h"
#include "log_task.h"
#include "fixed_sliding_window.h"
#include "palf_base_info.h"
#include "palf_callback_wrapper.h"

namespace oceanbase
{
namespace common
{
class ObILogAllocator;
}
namespace palf
{
class PalfFSCbWrapper;
class LogEntryHeader;
}
namespace palf
{
class LogEngine;
class FlushLogCbCtx;
class LogGroupEntryHeader;
class LogTaskHeaderInfo;
class LogStateMgr;
class LogModeMgr;
class LogTask;
class LogGroupEntry;

enum FreezeMode
{
  PERIOD_FREEZE_MODE = 0,
  FEEDBACK_FREEZE_MODE,
};

inline const char *freeze_mode_2_str(const FreezeMode mode)
{
#define EXTRACT_FREEZE_MODE(type_var) case(type_var): return #type_var
  switch(mode)
  {
    EXTRACT_FREEZE_MODE(PERIOD_FREEZE_MODE);
    EXTRACT_FREEZE_MODE(FEEDBACK_FREEZE_MODE);

    default:
      return "Invalid Mode";
  }
#undef EXTRACT_FREEZE_MODE
}

class LogSlidingWindow : public ISlidingCallBack
{
public:
  LogSlidingWindow();
  virtual ~LogSlidingWindow() { destroy(); }
public:
  virtual void destroy();
  virtual int init(const common::ObAddr &self,
                   LogStateMgr *state_mgr,
                   LogModeMgr *mode_mgr,
                   LogEngine *log_engine,
                   palf::PalfFSCbWrapper *palf_fs_cb,
                   common::ObILogAllocator *alloc_mgr,
                   const PalfBaseInfo &palf_base_info);
  virtual int sliding_cb(const int64_t sn, const FixedSlidingWindowSlot *data);
  virtual int64_t get_max_log_id() const;
  virtual const share::SCN get_max_scn() const;
  virtual LSN get_max_lsn() const;
  virtual int64_t get_start_id() const;
  virtual int get_committed_end_lsn(LSN &committed_end_lsn) const;
  virtual bool is_empty() const;
  virtual bool check_all_log_has_flushed();
  virtual bool is_all_committed_log_slided_out(LSN &prev_lsn, int64_t &prev_log_id, LSN &committed_end_lsn) const;
  // ================= log sync part begin
  virtual int submit_log(const char *buf,
                 const int64_t buf_len,
                 const share::SCN &ref_scn,
                 LSN &lsn,
                 share::SCN &scn);
  virtual int after_flush_log(const FlushLogCbCtx &flush_cb_ctx);
  virtual int after_rebuild(const LSN &lsn);
  // ================= log sync part end
  virtual int append_disk_log(const LSN &lsn, const LogGroupEntry &group_entry);
  virtual int report_log_task_trace(const int64_t log_id);
  virtual void get_max_flushed_end_lsn(LSN &end_lsn) const;
  virtual int clean_log();
  virtual int activate();
  virtual int try_advance_committed_end_lsn(const LSN &end_lsn);
  virtual int64_t get_last_submit_log_id_() const;
  virtual void get_last_submit_end_lsn_(LSN &end_lsn) const;
  virtual int get_last_slide_end_lsn(LSN &out_end_lsn) const;
  virtual const share::SCN get_last_slide_scn() const;
  virtual int check_and_switch_freeze_mode();
  virtual bool is_in_period_freeze_mode() const;
  virtual int period_freeze_last_log();
  virtual int inc_update_scn_base(const share::SCN &scn);
  virtual int advance_reuse_lsn(const LSN &flush_log_end_lsn);

  virtual int read_data_from_buffer(const LSN &read_begin_lsn,
                                    const int64_t in_read_size,
                                    char *buf,
                                    int64_t &out_read_size) const;
  int64_t get_last_slide_log_id() const;
  virtual int try_handle_next_submit_log();
  TO_STRING_KV(K_(self), K_(lsn_allocator), K_(group_buffer),                         \
  K_(last_submit_lsn), K_(last_submit_end_lsn), K_(last_submit_log_id),   \
  K_(max_flushed_lsn), K_(max_flushed_end_lsn), K_(committed_end_lsn),    \
  K_(last_slide_log_id), K_(last_slide_scn), K_(last_slide_lsn), K_(last_slide_end_lsn),        \
  K_(last_slide_log_accum_checksum),                                                               \
  "freeze_mode", freeze_mode_2_str(freeze_mode_), K_(has_pending_handle_submit_task), KP(this));
protected:
  virtual bool is_handle_thread_lease_expired(const int64_t thread_lease_begin_ts) const;
private:
  int do_init_mem_(const PalfBaseInfo &palf_base_info,
                   common::ObILogAllocator *alloc_mgr);
  int clean_log_();
  bool is_all_log_flushed_();
  int wait_sw_slot_ready_(const int64_t log_id);
  bool can_receive_larger_log_(const int64_t log_id) const;
  bool can_submit_larger_log_(const int64_t log_id) const;
  bool can_submit_new_log_(const int64_t valid_log_size, LSN &lsn_upper_bound);
  void get_committed_end_lsn_(LSN &out_lsn) const;
  int inc_update_max_flushed_log_info_(const LSN &lsn,
                                       const LSN &end_lsn);
  void get_last_slide_end_lsn_(LSN &out_end_lsn) const;
  int64_t get_last_slide_log_id_() const;
  void get_last_slide_log_info_(int64_t &log_id,
                                share::SCN &scn,
                                LSN &lsn,
                                LSN &end_lsn,
                                int64_t &accum_checksum) const;
  int try_update_last_slide_log_info_(const int64_t log_id,
                                      const share::SCN &scn,
                                      const LSN &lsn,
                                      const LSN &end_lsn,
                                      const int64_t accum_checksum);
  int try_advance_committed_lsn_(const LSN &end_lsn);
  void get_last_submit_log_info_(LSN &lsn,
                                 LSN &end_lsn,
                                 int64_t &log_id) const;
  int set_last_submit_log_info_(const LSN &lsn,
                                const LSN &end_lsn,
                                const int64_t log_id);
  int try_freeze_prev_log_(const int64_t next_log_id, const LSN &lsn, bool &is_need_handle);
  int feedback_freeze_last_log_();
  int try_feedback_freeze_log_task_(const int64_t expected_log_id);
  int try_freeze_last_log_task_(const int64_t expected_log_id, const LSN &expected_end_lsn, bool &is_need_handle);
  int generate_new_group_log_(const LSN &lsn,
                              const int64_t log_id,
                              const share::SCN &scn,
                              const int64_t log_body_size,
                              const LogType &log_type,
                              const char *log_data,
                              const int64_t data_len,
                              bool &is_need_handle);
  int append_to_group_log_(const LSN &lsn,
                           const int64_t log_id,
                           const share::SCN &scn,
                           const int64_t log_entry_size,
                           const char *log_data,
                           const int64_t data_len,
                           bool &is_need_handle);
  int handle_next_submit_log_(bool &is_committed_lsn_updated);
  int handle_committed_log_();
  int apply_committed_log_();
  int generate_group_entry_header_(const int64_t log_id,
                                   LogTask *log_task,
                                   LogGroupEntryHeader &header,
                                   int64_t &group_log_checksum,
                                   bool &is_accum_checksum_acquired);
  int gen_committed_end_lsn_(LSN &new_committed_end_lsn);
  int inc_ref_(LogTask *log_task, const int64_t inc_val, int64_t &result);
  int wait_group_buffer_ready_(const LSN &lsn, const int64_t data_len);
  int append_disk_log_to_sw_(const LSN &lsn, const LogGroupEntry &group_entry);
  int try_update_max_lsn_(const LSN &lsn, const LogGroupEntryHeader &header);
  int truncate_lsn_allocator_(const LSN &last_lsn, const int64_t last_log_id, const share::SCN &last_scn);
  bool is_all_committed_log_slided_out_(LSN &prev_lsn,
                                        int64_t &prev_log_id,
                                        LSN &start_lsn,
                                        LSN &committed_end_lsn) const;
  int get_min_scn_from_buf_(const LogGroupEntryHeader &group_entry_header,
                                  const char *buf,
                                  const int64_t buf_len,
                                  share::SCN &min_scn);
public:
  static const int64_t TMP_HEADER_SER_BUF_LEN = 256; // temporary buffer size for log header serialization
  static const int64_t APPEND_CNT_ARRAY_SIZE = 32;   // size of the append count statistics array
  static const uint64_t APPEND_CNT_ARRAY_MASK = APPEND_CNT_ARRAY_SIZE - 1;
  static const int64_t APPEND_CNT_LB_FOR_PERIOD_FREEZE = 140000;   // Lower bound of append count to switch to PERIOD_FREEZE_MODE
private:
  struct LogTaskGuard
  {
  public:
    explicit LogTaskGuard(LogSlidingWindow *sw): sw_(sw), log_id_(common::OB_INVALID_LOG_ID) { }
    // this function should be called only once for a guard object
    int get_log_task(const int64_t log_id, LogTask *&log_task);
    // revert log task manually, usually do not need call this function
    // destructor will revert it automatically
    void revert_log_task();

    ~LogTaskGuard()
    {
      revert_log_task();
    }
  private:
    LogSlidingWindow *sw_;
    int64_t log_id_;
  };
private:
  common::ObAddr self_;
  FixedSlidingWindow<LogTask> sw_;
  LogChecksum checksum_;
  LogStateMgr *state_mgr_;
  LogModeMgr *mode_mgr_;
  LogEngine *log_engine_;
  palf::PalfFSCbWrapper *palf_fs_cb_;
  LSNAllocator lsn_allocator_;
  LogGroupBuffer group_buffer_;
  // Record the last submit log info.
  // It is used to submit logs sequentially, for restarting, set it as last_replay_log_id.
  mutable common::ObSpinLock last_submit_info_lock_;
  LSN last_submit_lsn_;
  LSN last_submit_end_lsn_;
  int64_t last_submit_log_id_;
  // Record the max flushed log info.
  // max_flushed_lsn_: start lsn of max flushed log, it can be used as prev_lsn for fetching log.
  // max_flushed_end_lsn_: end lsn of max flushed log, it can be used as start_lsn for fetching log.
  mutable common::ObSpinLock max_flushed_info_lock_;
  LSN max_flushed_lsn_;
  LSN max_flushed_end_lsn_;
  // Record committed end lsn.
  mutable common::ObSpinLock committed_info_lock_;
  LSN committed_end_lsn_;
  // Record the last log which has slided out.
  // last_slide_lsn_: it is used as prev_lsn for fetching log
  // last_slide_end_lsn_: it is used for checking all committed log slided out when fetching log.
  mutable common::ObSpinLock last_slide_info_lock_;
  int64_t last_slide_log_id_;   // used by clean log
  share::SCN last_slide_scn_;
  LSN last_slide_lsn_;
  LSN last_slide_end_lsn_;
  int64_t last_slide_log_accum_checksum_;
  mutable int64_t cannot_freeze_log_warn_time_;
  mutable int64_t larger_log_warn_time_;
  mutable int64_t log_life_long_warn_time_;
  common::ObThreadLease commit_log_handling_lease_;  // thread lease for handling committed logs
  common::ObThreadLease submit_log_handling_lease_;  // thread lease for handling committed logs
  int64_t end_lsn_stat_time_us_;
  LSN last_record_end_lsn_;
  ObMiniStat::ObStatItem fs_cb_cost_stat_;
  ObMiniStat::ObStatItem log_life_time_stat_;
  int64_t accum_slide_log_cnt_;
  int64_t accum_log_gen_to_freeze_cost_;
  int64_t accum_log_gen_to_submit_cost_;
  int64_t accum_log_submit_to_flush_cost_;
  int64_t accum_log_submit_to_first_ack_cost_;
  int64_t accum_log_submit_to_commit_cost_;
  int64_t accum_log_submit_to_slide_cost_;
  mutable int64_t log_slide_stat_time_;
  int64_t group_log_stat_time_us_;
  int64_t accum_log_cnt_;
  int64_t accum_group_log_size_;
  int64_t last_record_group_log_id_;
  int64_t append_cnt_array_[APPEND_CNT_ARRAY_SIZE];
  FreezeMode freeze_mode_;
  bool has_pending_handle_submit_task_;
  bool is_inited_;
private:
  DISALLOW_COPY_AND_ASSIGN(LogSlidingWindow);
};

} // namespace palf
} // namespace oceanbase
#endif // OCEANBASE_LOGSERVICE_LOG_SLIDING_WINDOW_
