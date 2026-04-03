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

#ifndef OCEANBASE_ARCHIVE_OB_ARCHIVE_SENDER_H_
#define OCEANBASE_ARCHIVE_OB_ARCHIVE_SENDER_H_

#include "common/ob_queue_thread.h"
#include "src/logservice/archiveservice/ob_archive_define.h"
#include "src/logservice/archiveservice/ob_archive_round_mgr.h"
#include "share/backup/ob_backup_struct.h"  // ObBackupPathString
#include "share/ob_thread_pool.h"           // ObThreadPool
#include "ob_archive_task.h"                // ObArchiveSendTask
#include "ob_archive_worker.h"              // ObArchiveWorker
#include "lib/queue/ob_lighty_queue.h"      // ObLightyQueue
#include <cstdint>

namespace oceanbase
{
namespace share
{
class ObLSID;
class ObArchivePiece;
}
namespace backup
{
class ObBackupPath;
}
namespace archive
{
class ObArchiveAllocator;
class ObArchiveLSMgr;
class ObArchivePersistMgr;
class ObArchiveRoundMgr;
class ObArchiveSendTask;
class ObLSArchiveTask;
using oceanbase::share::ObLSID;
/*
 * ObArchiveSender calls the underlying storage interface, ultimately writing the clog file to the backup medium
 * In the current implementation, the sender module serially archives data for a single log stream, with the underlying storage interface ensuring concurrent writes
 * The sender module is multithreaded, with each thread consuming SendTask in a blocking upload manner and advancing the log stream archive progress
 * */
class ObArchiveSender : public share::ObThreadPool, public ObArchiveWorker
{
  static const int64_t MAX_SEND_NUM = 10;
  static const int64_t MAX_ARCHIVE_TASK_STATUS_POP_TIMEOUT = 5 * 1000 * 1000L;
  static const int64_t ARCHIVE_DBA_ERROR_LOG_PRINT_INTERVAL = 10 * 1000 * 1000L; // dba error log print interval
public:
  ObArchiveSender();
  virtual ~ObArchiveSender();

public:
  int start();
  void stop();
  void wait();
  int init(const uint64_t tenant_id,
      ObArchiveAllocator *allocator,
      ObArchiveLSMgr *ls_mgr,
      ObArchivePersistMgr *persist_mgr,
      ObArchiveRoundMgr *round_mgr);
  void destroy();
  void release_send_task(ObArchiveSendTask *task);
  int submit_send_task(ObArchiveSendTask *task);
  int push_task_status(ObArchiveTaskStatus *task_status);
  int64_t get_send_task_status_count() const;

  int modify_thread_count(const int64_t thread_count);
private:
  enum class DestSendOperator
  {
    SEND = 1,
    WAIT = 2,
    COMPENSATE = 3,
  };

  enum class TaskConsumeStatus
  {
    INVALID = 0,
    DONE = 1,
    STALE_TASK = 2,
    NEED_RETRY = 3,
  };
private:
  int submit_send_task_(ObArchiveSendTask *task);
  void run1();
  void do_thread_task_();

  int try_consume_send_task_();

  int do_consume_send_task_();
  // consume task status, for log stream level send_task queue, currently single-threaded consuming a single log stream
  int get_send_task_(ObArchiveSendTask *&task, bool &exist);

  void handle(ObArchiveSendTask &task, TaskConsumeStatus &consume_status);
  // 1. Check server archive status
  bool in_normal_status_(const ArchiveKey &key) const;
  // 2. Check if this send_task can be archived
  int check_can_send_task_(const ObArchiveSendTask &task,
      const LogFileTuple &tuple);
  // 2.1 Check if piece is continuous
  int check_piece_continuous_(const ObArchiveSendTask &task,
      const LogFileTuple &tuple,
      int64_t &next_piece_id,
      DestSendOperator &operation);
  int do_compensate_piece_(const share::ObLSID &id,
      const int64_t next_piece_id,
      const ArchiveWorkStation &station,
      const share::ObBackupDest &backup_dest,
      ObLSArchiveTask &ls_archive_task);
  // 3. Execute archive
  int archive_log_(const share::ObBackupDest &backup_dest,
      const int64_t backup_dest_id,
      const ObArchiveSendDestArg &arg,
      ObArchiveSendTask &task,
      ObLSArchiveTask &ls_archive_task);

  // 3.1 decide archive file
  int decide_archive_file_(const ObArchiveSendTask &task,
      const int64_t pre_file_id,
      const int64_t pre_file_offset,
      const ObArchivePiece &pre_piece,
      int64_t &file_id,
      int64_t &file_offset);

  // 3.2 build archive dir if needed
  int build_archive_prefix_if_needed_(const ObLSID &id,
      const ArchiveWorkStation &station,
      const bool piece_dir_exist,
      const ObArchivePiece &pre_piece,
      const ObArchivePiece &cur_piece,
      const share::ObBackupDest &backup_dest);

  // 3.3 build archive path
  int build_archive_path_(const ObLSID &id,
      const int64_t file_id,
      const ArchiveWorkStation &station,
      const ObArchivePiece &piece,
      const share::ObBackupDest &backup_dest,
      share::ObBackupPath &path);

  // 3.4 fill file header
  //
  int fill_file_header_if_needed_(const ObArchiveSendTask &task,
      char *&filled_data,
      int64_t &filled_data_len);

  // 3.5 push log
  int push_log_(const share::ObLSID &id,
      const ObString &uri,
      const share::ObBackupStorageInfo *storage_info,
      const int64_t backup_dest_id,
      const bool is_full_file,
      const bool is_can_seal,
      const int64_t offset,
      char *data,
      const int64_t data_len);
  // 3.6 Execute archive callback
  void update_archive_progress_(ObArchiveSendTask &task);

  // retire task status
  int try_retire_task_status_(ObArchiveTaskStatus &status);

  // free residual send_tasks when sender destroy
  int free_residual_task_();

  void handle_archive_ret_code_(const ObLSID &id,
      const ArchiveKey &key,
      const int ret_code);

  bool is_retry_ret_code_(const int ret_code) const;
  bool is_ignore_ret_code_(const int ret_code) const;

  void statistic(const int64_t log_size, const int64_t buf_size, const int64_t cost_ts);

  int try_free_send_task_();
  int do_free_send_task_();
private:
  bool                  inited_;
  uint64_t              tenant_id_;
  ObArchiveAllocator    *allocator_;
  ObArchiveLSMgr        *ls_mgr_;
  ObArchivePersistMgr   *persist_mgr_;
  ObArchiveRoundMgr     *round_mgr_;

  common::ObLightyQueue task_queue_;            // queue to store ObArchiveTaskStatus
  common::ObCond        send_cond_;
};

} // namespace archive
} // namespace oceanbase
#endif /* OCEANBASE_ARCHIVE_OB_ARCHIVE_SENDER_H_ */
