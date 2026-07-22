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

#ifndef OCEABASE_STORAGE_MEMSTORE_FREEZER_
#define OCEABASE_STORAGE_MEMSTORE_FREEZER_

#include "lib/atomic/ob_atomic.h"
#include "lib/list/ob_list.h"
#include "lib/literals/ob_literals.h"
#include "lib/lock/ob_tc_rwlock.h"
#include "lib/task/ob_timer.h"
#include "share/ob_occam_timer.h"
#include "storage/multi_data_source/runtime_utility/mds_factory.h"
#include "storage/compaction/ob_compaction_util.h"
#include "storage/ls/ob_freezer_define.h"
#include "storage/tx_storage/ob_memstore_freezer_common.h"

namespace oceanbase
{
namespace storage
{
class ObMemstoreFreezer;
class ObTxDataFreezeGuard;

class ObMemstoreFreezerStat
{
public:
  static const int64_t MAX_FREEZER_MERGE_TYPE = 3;
  enum ObFreezerMergeType
  {
    UNNECESSARY_TYPE = -1,
    MINI_MERGE       = 0,
    MINOR_MERGE      = 1,
    MAJOR_MERGE      = 2,
    MAX_MERGE_TYPE   = 3
  };
  ObMemstoreFreezerStat() { reset(); }
  ~ObMemstoreFreezerStat() {}
public:
  int64_t last_captured_timestamp_;
  // captured data size from last captured time
  int64_t captured_data_size_;
  int64_t captured_freeze_times_;

  int64_t captured_merge_time_cost_[ObFreezerMergeType::MAX_MERGE_TYPE];
  int64_t captured_merge_times_[ObFreezerMergeType::MAX_MERGE_TYPE];
  int64_t captured_source_times_[MAX_FREEZE_SOURCE_TYPE_COUNT];

  int64_t last_captured_retire_clock_;

  ObFreezerMergeType switch_to_freezer_merge_type(const compaction::ObMergeType type);

  const char *freezer_merge_type_to_str(const ObFreezerMergeType merge_type);

  bool is_useful_freezer_merge_type(const ObFreezerMergeType merge_type);

  void reset(int64_t retire_clock = 0);

  void refresh();

  void add_freeze_event();

  void add_merge_event(const compaction::ObMergeType type, const int64_t cost);

  void print_activity_metrics();

  void assign(const ObMemstoreFreezerStat stat);

  TO_STRING_KV(K_(last_captured_timestamp),
               K_(captured_data_size),
               K_(captured_freeze_times),
               K_(last_captured_retire_clock));
};

class ObMemstoreFreezerStatHistory
{
public:
  // 5(day in working week) * 24(hour in day) * 2(half of an hour in an hour)
  static const int64_t MAX_HISTORY_LENGTH = 5 * 24 * 2;
  ObMemstoreFreezerStatHistory(): start_(0), length_(0) {}

  void add_activity_metric(const ObMemstoreFreezerStat stat);

  void reset();
public:
  int64_t start_;
  int64_t length_;
  ObMemstoreFreezerStat history_[MAX_HISTORY_LENGTH];
};

// this is used for global freeze, all the freeze task should call the function of this unit.
class ObMemstoreFreezer
{
friend ObTxDataFreezeGuard;
friend class ObFreezer;
struct PeriodicalUpdateValueCache {
  PeriodicalUpdateValueCache() : value_(false), update_ts_(0) {}
  void reset()
  {
    value_ = false;
    update_ts_ = 0;
  }
  bool value_;
  int64_t update_ts_;
};

public:
  const static int64_t TIME_WHEEL_PRECISION = 100_ms;
  const static int64_t SLOW_FREEZE_INTERVAL = 30_s;
  const static int FREEZE_TRIGGER_THREAD_NUM= 1;
  const static int FREEZE_THREAD_NUM= 1;
  const static int64_t FREEZE_TRIGGER_INTERVAL = 2_s;
  const static int64_t UPDATE_INTERVAL = 100_ms;
  const static int64_t MAX_FREEZE_TIMEOUT_US = 1800 * 1000 * 1000; // 30 min
  // replay use 1G/s
  const static int64_t REPLAY_RESERVE_MEMSTORE_BYTES = 100 * 1024 * 1024; // 100 MB
  const static int64_t MEMSTORE_USED_CACHE_REFRESH_INTERVAL = 100_ms;
  const static int64_t FREEZE_RETRY_TIME_US = 600LL * 1000LL * 1000LL; // 10 minutes
  static double MDS_TABLE_FREEZE_TRIGGER_PERCENTAGE;

public:
  ObMemstoreFreezer();
  ~ObMemstoreFreezer();
  static int server_module_init(ObMemstoreFreezer* &m);
  int init();
  void destroy();
  int start();
  int stop();
  void wait();

  // freeze all local checkpoint units.
  int freeze_all(const ObFreezeSourceFlag source);

  // freeze a tablet
  int tablet_freeze(const common::ObTabletID &tablet_id,
                    const bool is_sync,
                    const int64_t max_retry_time,
                    const bool need_rewrite_tablet_meta,
                    const ObFreezeSourceFlag source);
  // check if this runtime's memstore is out of range, and trigger minor/major freeze.
  int check_and_do_freeze();

  // do freezer diagnose info
  int do_freeze_diagnose();

  // record freeze source history
  void record_freezer_source_event(const ObFreezeSourceFlag source);

  // report freeze source history
  void report_freezer_source_events();

  // used for replay to check whether can enqueue another replay task
  bool is_replay_pending_log_too_large(const int64_t pending_size);
  // If the runtime's freeze process is slowed, we will only freeze one time every
  // SLOW_FREEZE_INTERVAL.
  // set the global freeze process slowed. used while the tablet's max memtable
  // number meet.
  // @param[in] tablet_id, which tablet slow the freeze process.
  // @param[in] retire_clock, the memtable's retire clock.
  int set_slow_freeze(const common::ObTabletID &tablet_id,
                             const int64_t retire_clock);
  // uset the slow freeze flag.
  // if the global freeze process is slowed by this tablet, then unset it.
  // @param[in] tablet_id, the tablet who want to unset the slow freeze flag.
  //                       unset success if the tablet is the one who slow the runtime.
  //                       else do nothing.
  int unset_slow_freeze(const common::ObTabletID &tablet_id);
  // check whether the runtime mem limit, memstore limit has been changed.
  // @param[in] curr_lower_limit, the new lower limit
  // @param[in] curr_upper_limit, the new upper limit
  bool is_memory_limit_changed(const int64_t curr_lower_limit,
                             const int64_t curr_upper_limit) const;
  // set memory limit, both for min and max memory limit.
  // @param[in] lower_limit, the min memory limit will be set.
  // @param[in] upper_limit, the max memory limit will be set.
  int set_memory_limit(const int64_t lower_limit,
                           const int64_t upper_limit);
  // get the runtime mem limit, both min and max memory limit.
  // @param[out] lower_limit, the min memory limit set now.
  // @param[out] upper_limit, the max memory limit set now.
  int get_server_mem_limit(int64_t &lower_limit,
                           int64_t &upper_limit) const;
  // get the memstore info.
  int get_memstore_condition(int64_t &active_memstore_used,
                               int64_t &total_memstore_used,
                               int64_t &memstore_freeze_trigger,
                               int64_t &memstore_limit,
                               int64_t &freeze_cnt,
                               const bool force_refresh = true);
  // get the memstore used
  // get the memstore limit.
  int get_memstore_limit(int64_t &mem_limit);
  // get the memstore limit percentage
  static int64_t get_memstore_limit_percentage();
  // this is used to check if the runtime's memstore is out at user side.
  int check_memstore_full(bool &is_out_of_mem);
  // this is used for internal check rather than user side.
  int check_memstore_full_internal(bool &is_out_of_mem);
  // this check if a major freeze is needed
  bool need_major_freeze();
  // used to print a log.
  // update the memstore limit use sysconf.
  int reload_config();
  // print the memory usage info into print_buf.
  // @param[out] print_buf, the buf is used to print.
  // @param[in] buf_len, the buf length.
  // @param[in/out] pos, from which position to print and return the print position.
  int print_memory_usage(char *print_buf,
                         int64_t buf_len,
                         int64_t &pos);
  // if major freeze is failed and need retry, set the major freeze into at retry_major_info_.
  const ObRetryMajorInfo &get_retry_major_info() const { return retry_major_info_; }
  void record_freeze_failed_tablet(const ObTabletID &tablet_id);
  void erase_freeze_failed_tablet(const ObTabletID &tablet_id);
  void set_retry_major_info(const ObRetryMajorInfo &retry_major_info)
  {
    retry_major_info_ = retry_major_info;
  }
  static int64_t get_freeze_trigger_interval() { return FREEZE_TRIGGER_INTERVAL; }
  bool exist_ls_freezing();
  bool exist_ls_throttle_is_skipping();
  bool memstore_remain_memory_is_exhausting();

  // freezer stat collector and generator
  void add_merge_event(const compaction::ObMergeType type, const int64_t cost)
  {
    freezer_stat_.add_merge_event(type, cost);
  }

  void get_freezer_stat_history_snapshot(int64_t &length);

  void get_freezer_stat_from_history(int64_t pos, ObMemstoreFreezerStat& stat);

  // Process-wide monotonic id for checkpoint/freeze batch log correlation (not persisted).

  // record major frozen scn and reset freeze cnt
  int update_frozen_scn(const int64_t frozen_scn);

  void run_timer_task();

private:
  int get_memstore_condition_(int64_t &active_memstore_used,
                                int64_t &total_memstore_used,
                                int64_t &memstore_freeze_trigger,
                                int64_t &memstore_limit,
                                int64_t &freeze_cnt,
                                const bool force_refresh = true);
  int check_memstore_full_(bool &last_result,
                           int64_t &last_check_timestamp,
                           bool &is_out_of_mem,
                           const bool from_user = true);
  static int ls_freeze_data_(ObLS *ls);
  static int ls_freeze_data_(ObLS *ls, const bool is_sync, const int64_t abs_timeout_ts);
  static int ls_freeze_all_unit_(
    ObLS *ls,
    const int64_t abs_timeout_ts = INT64_MAX,
    const ObFreezeSourceFlag source = ObFreezeSourceFlag::INVALID_SOURCE);
  // freeze all the ls of this runtime.
  // return the first failed code.
  int freeze_all_data_();
  // we can only deal with freeze one by one.
  // set global freezing will prevent a new freeze.
  int set_freezing_();
  // unset global freezing flag.
  // @param[in] rollback_freeze_cnt, reduce the runtime's freeze count by 1, if true.
  int unset_freezing_(const bool rollback_freeze_cnt);
  static int64_t get_freeze_trigger_percentage_();
  static int64_t get_memstore_limit_percentage_();
  int async_freeze_(const ObMemstoreFreezeArg &arg);
  int post_freeze_request_(const storage::ObFreezeType freeze_type,
                           const int64_t try_frozen_version);
  int retry_failed_major_freeze_(bool &triggered);
  int get_global_frozen_scn_(int64_t &frozen_version);
  int post_tx_data_freeze_request_();
  int post_mds_table_freeze_request_();
  int get_memory_usage_(ObMemstoreFreezeCtx &ctx);
  int get_memory_stat_(ObMemstoreStatistic &stat);
  static int get_freeze_trigger_(ObMemstoreFreezeCtx &ctx);
  bool need_freeze_(const ObMemstoreFreezeCtx &ctx);
  bool is_major_freeze_turn_();
  int do_major_if_need_(const bool need_freeze);
  int do_minor_freeze_data_(const ObMemstoreFreezeCtx &ctx);
  int do_major_freeze_(const int64_t try_frozen_scn);
  void log_frozen_memstore_info_if_need_(const ObMemstoreFreezeCtx &ctx);
  int check_and_freeze_normal_data_(ObMemstoreFreezeCtx &ctx);
  int check_and_freeze_tx_data_();
  int check_and_freeze_mds_table_();

  int get_tx_data_info_for_freeze_(int64_t &tx_data_frozen_mem_used,
                                   int64_t &tx_data_active_mem_used,
                                   bool &need_re_freeze,
                                   bool for_statistic_print = false);

  int get_ls_tx_data_memory_info_(ObLS *ls,
                                  int64_t &ls_tx_data_frozen_mem_used,
                                  int64_t &ls_tx_data_active_mem_used,
                                  bool for_statistic_print = false);

private:
  class TimerTask : public common::ObTimerTask
  {
  public:
    TimerTask(ObMemstoreFreezer &freezer) : freezer_(freezer) {}
    virtual ~TimerTask() = default;
    void runTimerTask() override { freezer_.run_timer_task(); }
  private:
    ObMemstoreFreezer &freezer_;
  };
private:
  bool is_inited_;
  bool is_freezing_tx_data_;
  ObMemstoreInfo memstore_info_;                  // store the mem limit, memstore limit and etc.
ObAddr self_;
  ObRetryMajorInfo retry_major_info_;

  common::ObTimer freeze_trigger_timer_;
  TimerTask freeze_trigger_timer_task_;
  common::ObOccamThreadPool freeze_thread_pool_;
  ObSpinLock freeze_thread_pool_lock_;

  // diagnose only, we capture the freeze stats every 30 minutes
  ObMemstoreFreezerStat freezer_stat_;
  // diagnose only, we capture the freeze history in one monthes
  ObMemstoreFreezerStatHistory freezer_history_;
  PeriodicalUpdateValueCache throttle_is_skipping_cache_;
  PeriodicalUpdateValueCache memstore_remain_memory_is_exhausting_cache_;
};

class ObTxDataFreezeGuard
{
public:
  ObTxDataFreezeGuard() : can_freeze_(false), memstore_freezer_(nullptr) {}
  ~ObTxDataFreezeGuard() { reset(); }

  int init(ObMemstoreFreezer *memstore_freezer)
  {
    int ret = OB_SUCCESS;
    reset();
    if (OB_ISNULL(memstore_freezer)) {
      ret = OB_INVALID_ARGUMENT;
      STORAGE_LOG(WARN, "invalid tx data table", KR(ret));
    } else {
      can_freeze_ = (false == ATOMIC_CAS(&(memstore_freezer->is_freezing_tx_data_), false, true));
      if (can_freeze_) {
        memstore_freezer_ = memstore_freezer;
      }
    }
    return ret;
  }

  void reset()
  {
    can_freeze_ = false;
    if (OB_NOT_NULL(memstore_freezer_)) {
      ATOMIC_STORE(&(memstore_freezer_->is_freezing_tx_data_), false);
      memstore_freezer_ = nullptr;
    }
  }

  bool can_freeze() { return can_freeze_; }

private:
  bool can_freeze_;
  ObMemstoreFreezer *memstore_freezer_;
};

}  // namespace storage
}  // namespace oceanbase

#endif
