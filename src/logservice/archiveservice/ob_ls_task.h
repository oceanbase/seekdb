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

#ifndef OCEANBASE_ARCHIVE_OB_LOG_STREAM_TASK_H_
#define OCEANBASE_ARCHIVE_OB_LOG_STREAM_TASK_H_

#include "lib/container/ob_se_array.h"
#include "lib/hash/ob_link_hashmap.h"          // LinkHashNode
#include "lib/lock/ob_spin_rwlock.h"           // SpinRWLock
#include "lib/queue/ob_link_queue.h"           // ObSpLinkQueue
#include "ob_archive_define.h"                 // ArchiveWorkStation LogFileTuple
#include "share/ob_ls_id.h"                    // ObLSID
#include "logservice/palf/lsn.h"               // LSN
#include "share/scn.h"               // SCN
#include "lib/utility/ob_print_utils.h"        // print
#include <cstdint>

namespace oceanbase
{
namespace share
{
class ObLSID;
class ObLSArchivePersistInfo;
}

namespace palf
{
struct LSN;
}

namespace archive
{
class ObArchiveSendTask;
class ObArchiveLogFetchTask;
class ObArchiveLSMgr;
class StartArchiveHelper;
class ObArchiveAllocator;
class ObArchiveWorker;
class ObArchiveTaskStatus;
struct LSArchiveStat;

using oceanbase::palf::LSN;
using oceanbase::share::ObLSID;
using oceanbase::share::ObLSArchivePersistInfo;

typedef common::LinkHashValue<ObLSID> LSArchiveTaskValue;
class ObLSArchiveTask : public LSArchiveTaskValue
{
public:
  ObLSArchiveTask();
  ~ObLSArchiveTask();

public:
  int init(const StartArchiveHelper &helper, ObArchiveAllocator *allocator);
  // Update log stream archiving task
  int update_ls_task(const StartArchiveHelper &helper);
  // Check if the archive task matches the current leader
  bool check_task_valid(const ArchiveWorkStation &station);
  // Destroy the archiving task, release resources
  void destroy();
  // fetcher will add the clog read to the pending send queue, this task queue is linked in the order of log offsets in the clog file
  int push_fetch_log(ObArchiveLogFetchTask &task);
  // submit send task
  int push_send_task(ObArchiveSendTask &task, ObArchiveWorker &worker);
  // Get the progress of the sequencer module
  int get_sequencer_progress(const ArchiveKey &key,
                             ArchiveWorkStation &station,
                             LSN &offset);
  // Update sequencer module progress
  int update_sequencer_progress(const ArchiveWorkStation &station,
                                const int64_t size,
                                const LSN &offset);
  // Get the sorted data that can be submitted for sending
  // NB: If there is no continuous task with the current archive progress, then return NULL
  int get_sorted_fetch_log(ObArchiveLogFetchTask *&task);
  // Update fetcher archive progress
  int update_fetcher_progress(const ArchiveWorkStation &station, const LogFileTuple &tuple);
  // Get fetch progress
  int get_fetcher_progress(const ArchiveWorkStation &station,
                         palf::LSN &offset,
                         share::SCN &scn,
                         int64_t &last_fetch_timestamp);

  int compensate_piece(const ArchiveWorkStation &station,
                       const int64_t next_compensate_piece_id);
  // Update log stream archive progress
  int update_archive_progress(const ArchiveWorkStation &station,
                              const int64_t file_id,
                              const int64_t file_offset,
                              const LogFileTuple &tuple);
  // Get the log stream archive progress
  int get_archive_progress(const ArchiveWorkStation &station,
                           int64_t &file_id,
                           int64_t &file_offset,
                           LogFileTuple &tuple);

  // get send task count in send_task_status
  // @param[in] station, the archive work station of ls
  // @param[out] count, the count of send_tasks in task_status
  int get_send_task_count(const ArchiveWorkStation &station, int64_t &count);
  // Get archive parameters
  int get_archive_send_arg(const ArchiveWorkStation &station,
                           ObArchiveSendDestArg &arg);

  int get_max_archive_info(const ArchiveKey &key,
                           ObLSArchivePersistInfo &info);

  int get_max_no_limit_lsn(const ArchiveWorkStation &station, LSN &lsn);

  // support flush all logs to archvie dest
  int update_no_limit_lsn(const palf::LSN &lsn);

  int mark_error(const ArchiveKey &key);

  int print_self();

  void stat(LSArchiveStat &ls_stat) const;

  void mock_init(const ObLSID &id, ObArchiveAllocator *allocotr);
  TO_STRING_KV(K_(id),
               K_(station),
               K_(round_start_scn),
               K_(dest));

private:
  typedef common::SpinRWLock RWLock;
  typedef common::SpinRLockGuard  RLockGuard;
  typedef common::SpinWLockGuard  WLockGuard;

  class ArchiveDest
  {
    friend ObLSArchiveTask;
  public:
    ArchiveDest();
    ~ArchiveDest();

  public:
    int init(const LSN &max_no_limit_lsn,
        const LSN &piece_min_lsn, const LSN &lsn, const int64_t file_id,
        const int64_t file_offset, const share::ObArchivePiece &piece,
        const share::SCN &max_archived_scn, const bool is_log_gap_exist,
        ObArchiveAllocator *allocator);
    void destroy();
    void get_sequencer_progress(LSN &offset) const;
    int update_sequencer_progress(const int64_t size, const LSN &offset);
    void get_fetcher_progress(LogFileTuple &tuple, int64_t &last_fetch_timestamp) const;
    int update_fetcher_progress(const share::SCN &round_start_scn, const LogFileTuple &tuple);
    int push_fetch_log(ObArchiveLogFetchTask &task);
    int push_send_task(ObArchiveSendTask &task, ObArchiveWorker &worker);
    int get_top_fetch_log(ObArchiveLogFetchTask *&task);
    int pop_fetch_log(ObArchiveLogFetchTask *&task);
    int compensate_piece(const int64_t piece_id);
    void get_max_archive_progress(LSN &piece_min_lsn, LSN &lsn, share::SCN &scn, ObArchivePiece &piece,
        int64_t &file_id, int64_t &file_offset, bool &error_exist);
    int update_archive_progress(const share::SCN &round_start_scn, const int64_t file_id, const int64_t file_offset, const LogFileTuple &tuple);
    void get_archive_progress(int64_t &file_id, int64_t &file_offset, LogFileTuple &tuple);
    void get_send_task_count(int64_t &count);
    void get_archive_send_arg(ObArchiveSendDestArg &arg);
    void get_max_no_limit_lsn(LSN &lsn);
    void update_no_limit_lsn(const palf::LSN &lsn);
    void mark_error();
    void print_tasks_();
    int64_t to_string(char *buf, const int64_t buf_len) const;

  private:
    void free_send_task_status_();
    void free_fetch_log_tasks_();

  private:
    bool               has_encount_error_;
    bool               is_worm_;
    // archive_lag_target with noneffective for logs whose lsn smaller than this lsn
    palf::LSN          max_no_limit_lsn_;
    palf::LSN          piece_min_lsn_;
    // archived log description
    LogFileTuple       max_archived_info_;
    int64_t archive_file_id_;
    int64_t archive_file_offset_;
    bool piece_dir_exist_;

    LSN       max_seq_log_offset_;
    LogFileTuple       max_fetch_info_;
    int64_t last_fetch_timestamp_;
    ObArchiveLogFetchTask *wait_send_task_array_[MAX_FETCH_TASK_NUM];
    int64_t             wait_send_task_count_;
    ObArchiveTaskStatus *send_task_queue_;

    ObArchiveAllocator *allocator_;

  private:
    DISALLOW_COPY_AND_ASSIGN(ArchiveDest);
  };

private:
  bool is_task_stale_(const ArchiveWorkStation &station) const;
  int update_unlock_(const StartArchiveHelper &helper, ObArchiveAllocator *allocator);

private:
  ObLSID id_;
  uint64_t tenant_id_;
  ArchiveWorkStation station_;
  share::SCN round_start_scn_;
  ArchiveDest dest_;
  ObArchiveAllocator *allocator_;
  mutable RWLock rwlock_;
};

struct LSArchiveStat
{
  uint64_t tenant_id_;
  int64_t ls_id_;
  int64_t dest_id_;
  int64_t incarnation_;
  int64_t round_;
  // dest_type dest_value
  int64_t lease_id_;
  share::SCN round_start_scn_;
  int64_t max_issued_log_lsn_;
  int64_t issued_task_count_;
  int64_t issued_task_size_;
  int64_t max_prepared_piece_id_;
  int64_t max_prepared_lsn_;
  share::SCN max_prepared_scn_;
  int64_t wait_send_task_count_;
  int64_t archive_piece_id_;
  int64_t archive_lsn_;
  share::SCN archive_scn_;
  int64_t archive_file_id_;
  int64_t archive_file_offset_;
  TO_STRING_KV(K_(tenant_id), K_(ls_id), K_(dest_id), K_(incarnation), K_(round),
      K_(lease_id), K_(round_start_scn), K_(max_issued_log_lsn), K_(issued_task_count),
      K_(issued_task_size), K_(max_prepared_piece_id), K_(max_prepared_lsn),
      K_(max_prepared_scn), K_(wait_send_task_count), K_(archive_piece_id), K_(archive_lsn),
      K_(archive_scn), K_(archive_file_id), K_(archive_file_offset));
};

class ObArchiveLSGuard final
{
public:
  explicit ObArchiveLSGuard(ObArchiveLSMgr *ls_mgr);
  ~ObArchiveLSGuard();

public:
  void set_ls_task(ObLSArchiveTask *task);
  ObLSArchiveTask * get_ls_task();

  TO_STRING_KV(KPC(ls_task_));
private:
  void revert_ls_task_();

private:
  ObLSArchiveTask          *ls_task_;
  ObArchiveLSMgr           *ls_mgr_;

private:
  DISALLOW_COPY_AND_ASSIGN(ObArchiveLSGuard);
};

} // namespace archive
} // namespace oceanbase

#endif /* OCEANBASE_ARCHIVE_OB_LOG_STREAM_TASK_H_ */
