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

#ifndef OCEANBASE_ARCHIVE_OB_ARCHIVE_SERVICE_H_
#define OCEANBASE_ARCHIVE_OB_ARCHIVE_SERVICE_H_

#include "share/ob_thread_pool.h"       // ObThreadPool
#include "common/ob_queue_thread.h"     // ObCond
#include "ob_archive_allocator.h"       // ObArchiveAllocator
#include "ob_archive_sequencer.h"        // ObArchiveSequencer
#include "ob_archive_fetcher.h"         // ObArchiveFetcher
#include "ob_archive_sender.h"          // ObArchiveSender
#include "ob_archive_persist_mgr.h"     // ObArchivePersistMgr
#include "ob_archive_round_mgr.h"       // ObArchiveRoundMgr
#include "ob_ls_mgr.h"          // ObArchiveLSMgr
#include "ob_archive_scheduler.h"       // ObArchiveScheduler
#include "ob_archive_timer.h"           // ObArchiveTimer
#include "ob_ls_meta_recorder.h"        // ObLSMetaRecorder

namespace oceanbase
{
namespace share
{
class ObTenantArchiveRoundAttr;
}
namespace logservice
{
class ObLogService;
}

namespace palf
{
struct LSN;
}

namespace storage
{
class ObLSService;
}

namespace archive
{
using oceanbase::logservice::ObLogService;
using oceanbase::storage::ObLSService;
using oceanbase::palf::LSN;
using oceanbase::share::ObTenantArchiveRoundAttr;
class ObLSArchiveTask;

/*
 * Tenant Archive Service
 * */
class ObArchiveService : public share::ObThreadPool
{
public:
  ObArchiveService();
  virtual ~ObArchiveService();

public:
  static int mtl_init(ObArchiveService *&archive_svr);
  int init(ObLogService *log_service, ObLSService *ls_svr, const uint64_t tenant_id);
  int start();
  void stop();
  void wait();
  void destroy();
public:
  // Get the log stream archive progress, for reference by migration/copy/GC archive progress
  //
  // @param [in], id log stream ID
  // @param [out], lsn log stream archive progress lsn
  // @param [out], scn log stream archive progress scn
  // @param [out], force_wait The calling module needs to strictly refer to the archiving progress, and block the calling module when archiving lags behind
  // @param [out], ignore archive progress
  //
  // @retval OB_SUCCESS Get archive progress successfully
  // @retval OB_EGAIN caller needs to retry
  int get_ls_archive_progress(const ObLSID &id, LSN &lsn, share::SCN &scn, bool &force_wait, bool &ignore);
  // Real-time SQL query table to get whether the tenant is in archive mode, do not call this interface if not necessary (currently only GC needs)
  // @param [out], in_archive tenant is in archive mode
  //
  // @retval OB_SUCCESS confirmation successful
  // @retval other_code get failed
  int check_tenant_in_archive(bool &in_archive);
  // Get the log stream archiving speed
  //
  // @param [in], id log stream ID
  // @param [out], speed log archive speed, unit: Bytes/s
  // @param [out], force_wait The calling module needs to strictly refer to the archiving progress, and block the calling module when archiving lags behind
  // @param [out], ignore archive progress
  //
  // @retval OB_SUCCESS Get archive speed successfully
  // @retval other_code get failed
  //
  // @note: Log data volume archived in a unit of time, if no logs are archived within the unit of time, the speed obtained is 0
  int get_ls_archive_speed(const ObLSID &id, int64_t &speed, bool &force_wait, bool &ignore);

  ///////////// RPC process functions /////////////////
  void wakeup();

  // Flush all logs to all archive destinations, in this interface a flush all flag is set and the flush action will be done in background.
  // The flush flag is a temporary state on the ls, so only ls in archive progress in this server will be affected.
  //
  // New ls after the function call and ls migrated to other servers are immune.
  void flush_all();

  int iterate_ls(const std::function<int(const ObLSArchiveTask&)> &func);

private:
  enum class ArchiveRoundOp
  {
    NONE = 0,
    START = 1,
    STOP = 2,
    FORCE_STOP = 3,
    MARK_INTERRUPT = 4,
    SUSPEND = 5,
  };
private:
  void run1();
  void do_thread_task_();
  bool need_check_switch_archive_() const;
  bool need_check_switch_stop_status_() const;
  bool need_print_archive_status_() const;
  // ============= Enable/Disable Archiving ========== //
  void do_check_switch_archive_();
  // 1. Get tenant-level archive configuration item information
  int load_archive_round_attr_(ObTenantArchiveRoundAttr &attr);
  // 2. Check if archive status switch is needed
  int check_if_need_switch_log_archive_(const ObTenantArchiveRoundAttr &attr, ArchiveRoundOp &op);
  // 3. Handle archive start, state push to DOING
  int start_archive_(const ObTenantArchiveRoundAttr &attr);
  // 3.1 Set archive information
  int set_log_archive_info_(const ObTenantArchiveRoundAttr &attr);
  // 3.2 Notify all modules to start archiving
  void notify_start_();
  // 4. Handle archive closure, state push to STOPPING
  void stop_archive_();
  // 4.1 Check residual task cleanup completion
  void check_and_set_archive_stop_();
  // 4.1.1 Cleanup log stream archive task
  int clear_ls_task_();
  // 4.1.2 Clean up archive information for each module
  void clear_archive_info_();
  // 4.1.3 Check archive task clear
  bool check_archive_task_empty_() const;
  // 4.2 Set STOP
  int set_log_archive_stop_status_(const ArchiveKey &key);
  // 5. Archive round lags behind and tenant is in closed archive state, force stop, status to STOP
  int force_stop_archive(const int64_t incarnation, const int64_t round);

  // 6. set archive suspend
  int suspend_archive_(const ObTenantArchiveRoundAttr &attr);
  // ====== Print archive status =========== //
  void print_archive_status_();

private:
  const int64_t THREAD_RUN_INTERVAL = 5 * 1000 * 1000L;
private:
  bool inited_;
  uint64_t tenant_id_;
  ObArchiveAllocator allocator_;
  ObArchiveRoundMgr archive_round_mgr_;
  ObArchiveLSMgr ls_mgr_;
  ObArchiveSequencer sequencer_;
  ObArchiveFetcher fetcher_;
  ObArchiveSender sender_;
  ObArchivePersistMgr persist_mgr_;
  ObArchiveScheduler scheduler_;
  ObLSMetaRecorder ls_meta_recorder_;
  ObArchiveTimer timer_;
  logservice::ObLogService *log_service_;
  ObLSService *ls_svr_;
  common::ObCond cond_;
};

}
}

#endif /* OCEANBASE_ARCHIVE_OB_ARCHIVE_SERVICE_H_ */
