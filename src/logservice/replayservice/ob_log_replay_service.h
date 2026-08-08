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

#ifndef OCEANBASE_LOGSERVICE_OB_LOG_REPLAY_SERVICE_
#define OCEANBASE_LOGSERVICE_OB_LOG_REPLAY_SERVICE_

#include "ob_replay_status.h"
#include "share/log/ob_log_base_header.h"
#include "lib/task/ob_timer.h"
#include "lib/thread/ob_simple_thread_pool.h"
#include "lib/lock/ob_qsync_lock.h"
#include "share/scn.h"

namespace oceanbase
{
namespace share
{
class SCN;
}
namespace palf
{
class PalfEnv;
}
namespace logservice
{
class ObILogStorage;
class ReplayProcessStat : public common::ObTimerTask
{
public:
  ReplayProcessStat();
  virtual ~ReplayProcessStat();
public:
  int init(ObLogReplayService *rp_sv);
  int start();
  void stop();
  void wait();
  void destroy();
  virtual void runTimerTask();
private:
  static const int64_t SCAN_TIMER_INTERVAL = 10 * 1000 * 1000; //10s
  //Total replay log volume at the last poll
  int64_t last_replayed_log_size_;
  int64_t last_submitted_log_size_;
  ObLogReplayService *rp_sv_;
  common::ObTimer timer_;
  bool is_inited_;
};

class ObILogReplayService
{
public:
  virtual int is_replay_done(const palf::LSN &end_lsn, bool &is_done) = 0;
  virtual int is_submit_task_clear(bool &is_clear) = 0;
  virtual int enable_local_replay(const palf::LSN &begin_lsn) = 0;
  virtual int disable_local_replay() = 0;
};
/*
TODO(yaoying.yyy): memory management of replayservice needs to be documented
*/

class ObLogReplayService: public common::ObLinkQueueThreadPool, public ObILogReplayService
{
public:
  ObLogReplayService();
  virtual ~ObLogReplayService();
  int init(palf::PalfEnv *palf_env,
           ObILogStorage *log_storage,
           ObILogAllocator *allocator,
           const int64_t replay_thread_quota);
public:
  int start();
  void stop();
  void wait();
  void destroy();
public:
  void handle(common::LinkTask *task);
  int create_status();
  int remove_status();
  int enable(const palf::LSN &base_lsn,
             const share::SCN &base_scn);
  int disable();
  int is_enabled(bool &is_enabled);
  int block_submit_log();
  int unblock_submit_log();
  int disable_local_replay();
  int enable_local_replay(const palf::LSN &begin_lsn);
  int is_replay_done(const palf::LSN &end_lsn,
                     bool &is_done);
  int is_submit_task_clear(bool &is_clear);
  int get_max_replayed_scn(share::SCN &scn);
  int get_min_unreplayed_scn(SCN &scn);
  int submit_task(ObReplayServiceTask *task);
  int update_replayable_point(const share::SCN &replayable_scn);
  int get_replayable_point(share::SCN &replayable_scn);
  int stat(LSReplayStat &replay_stat);
  int stat_replay_process(int64_t &submitted_log_size,
                          int64_t &unsubmitted_log_size,
                          int64_t &replayed_log_size,
                          int64_t &unreplayed_log_size);
  int diagnose(ReplayDiagnoseInfo &diagnose_info);
  void inc_pending_task_size(const int64_t log_size);
  void dec_pending_task_size(const int64_t log_size);
  int64_t get_pending_task_size() const;
  void *alloc_replay_task(const int64_t size);
  void free_replay_task(ObLogReplayTask *task);
  void free_replay_task_log_buf(ObLogReplayTask *task);
  int has_fatal_error(bool &bool_ret);
private:
  int get_replay_status_(ObReplayStatusGuard &guard);
  int pre_check_(ObReplayStatus &replay_status,
                 ObReplayServiceTask &task);
  void process_replay_ret_code_(const int ret_code,
                                ObReplayStatus &replay_status,
                                ObReplayServiceReplayTask &task_queue,
                                ObLogReplayTask &replay_task);
  void revert_replay_status_(ObReplayStatus *replay_status);
  int try_submit_remained_log_replay_task_(ObReplayServiceSubmitTask *submit_task);
  int fetch_and_submit_single_log_(ObReplayStatus &replay_status,
                                   ObReplayServiceSubmitTask *submit_task,
                                   palf::LSN &cur_lsn,
                                   share::SCN &cur_log_submit_scn,
                                   int64_t &log_size);
  int fetch_pre_barrier_log_(ObReplayStatus &replay_status,
                             ObReplayServiceSubmitTask *submit_task,
                             ObLogReplayTask *&replay_task,
                             const ObLogBaseHeader &header,
                             const char *log_buf,
                             const palf::LSN &cur_lsn,
                             const share::SCN &cur_log_submit_scn,
                             const int64_t log_size);
  bool is_tenant_out_of_memory_() const;
  int handle_submit_task_(ObReplayServiceSubmitTask *submit_task,
                          bool &is_timeslice_run_out);
  int handle_replay_task_(ObReplayServiceReplayTask *task_queue,
                          bool &is_timeslice_run_out);
  int check_can_submit_log_replay_task_(ObLogReplayTask *replay_task,
                                        ObReplayStatus *replay_status);
  int do_replay_task_(ObLogReplayTask *replay_task,
                      ObReplayStatus *replay_status,
                      const int64_t replay_queue_idx);
  int submit_log_replay_task_(ObLogReplayTask &replay_task,
                              ObReplayStatus &replay_status);
  int statistics_replay_cost_(const int64_t init_task_time,
                              const int64_t first_handle_time);
  void on_replay_error_(ObLogReplayTask &replay_task, int ret);
  void on_replay_error_();
  void free_replay_log_buf_(ObLogReplayBuffer *&replay_buf);

  share::SCN inner_get_replayable_point_() const;
private:
  const int64_t MAX_REPLAY_TIME_PER_ROUND = 10 * 1000; //10ms
  const int64_t MAX_SUBMIT_TIME_PER_ROUND = 100 * 1000; //100ms
  const int64_t TASK_QUEUE_WAIT_IN_GLOBAL_QUEUE_TIME_THRESHOLD = 5 * 1000 * 1000; //5s
  const int64_t PENDING_TASK_MEMORY_LIMIT = 128 * (1LL << 20); //128MB
  // Accumulate replay tasks to the threshold before submitting a batch.
  static const int64_t BATCH_PUSH_REPLAY_TASK_COUNT_THRESOLD = 1024;
  static const int64_t BATCH_PUSH_REPLAY_TASK_SIZE_THRESOLD = 16 * (1LL << 20); //16MB
  // params of adaptive thread pool
  const int64_t LEAST_THREAD_NUM = 8;
  const int64_t ESTIMATE_TS = 200000;
  const int64_t EXPAND_RATE = 90;
  const int64_t SHRINK_RATE = 75;
private:
  bool is_inited_;
  bool is_running_;
  ReplayProcessStat replay_stat_;
  ObILogStorage *log_storage_;
  palf::PalfEnv *palf_env_;
  ObILogAllocator *allocator_;
  share::SCN replayable_point_;
  ObReplayStatus *replay_status_;
  mutable common::ObQSyncLock lock_;
  int64_t pending_replay_log_size_;
  ObMiniStat::ObStatItem wait_cost_stat_;
  ObMiniStat::ObStatItem replay_cost_stat_;
  DISALLOW_COPY_AND_ASSIGN(ObLogReplayService);
};

} // namespace replayservice
} // namespace oceanbase

#endif // OCEANBASE_LOGSERVICE_OB_LOG_REPLAY_SERVICE_
