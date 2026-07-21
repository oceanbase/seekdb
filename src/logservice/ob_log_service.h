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

#ifndef OCEANBASE_LOGSERVICE_OB_LOG_SERVICE_
#define OCEANBASE_LOGSERVICE_OB_LOG_SERVICE_

#include "common/ob_role.h"
#include "lib/ob_define.h"
#include "applyservice/ob_log_apply_service.h"
#include "palf/log_block_pool_interface.h"             // ILogBlockPool
#include "palf/log_define.h"
#include "localservice/ob_local_log_handler_set.h"
#include "replayservice/ob_log_replay_service.h"
#include "ob_net_keepalive_adapter.h"
#include "ob_ls_adapter.h"
#include "ob_log_handler.h"
#include "ob_log_monitor.h"

namespace oceanbase
{
namespace common
{
class ObAddr;
class ObILogAllocator;
}

namespace share
{
class SCN;
}

namespace storage
{
class ObLSService;
}

namespace palf
{
class PalfHandleGuard;
class PalfDiskOptions;
class PalfEnv;
}

namespace logservice
{

class ObLogService
{
public:
  ObLogService();
  virtual ~ObLogService();
  static int mtl_init(ObLogService* &logservice);
  static void mtl_destroy(ObLogService* &logservice);
  int start();
  void stop();
  void wait();
  void destroy();
public:
  int init(const palf::PalfOptions &options,
           const char *base_dir,
           const common::ObAddr &self,
           common::ObILogAllocator *alloc_mgr,
           storage::ObLSService *ls_service,
           palf::ILogBlockPool *log_block_pool,
           IObNetKeepAliveAdapter *net_keepalive_adapter);
  // Create the unique log stream from its persisted base point.
  int create_ls(const palf::PalfBaseInfo &palf_base_info,
                ObLogHandler &log_handler);
  //Delete log stream interface: After the outer call to create_ls(), if subsequent processes fail, remove_ls() needs to be called
  int remove_ls(ObLogHandler &log_handler);

  int check_palf_exist(bool &exist) const;
  // Restart recovery interface for the single log stream.
  int add_ls(ObLogHandler &log_handler);

  int open_palf(palf::PalfHandleGuard &palf_handle);

  int update_replayable_point(const share::SCN &replayable_point);
  int get_replayable_point(share::SCN &replayable_point);

  // @brief get palf disk usage
  // @param [out] used_size_byte
  // @param [out] total_size_byte, if in shrinking status, total_size_byte is the value after shrinking.
  // NB: total_size_byte may be smaller than used_size_byte.
  int get_palf_disk_usage(int64_t &used_size_byte, int64_t &total_size_byte);

  // @brief get palf disk usage
  // @param [out] used_size_byte
  // @param [out] total_size_byte, if in shrinking status, total_size_byte is the value before shrinking.
  int get_palf_stable_disk_usage(int64_t &used_size_byte, int64_t &total_size_byte);
  // why we need update 'log_disk_size_' and 'log_disk_util_threshold' separately.
  //
  // 'log_disk_size' is a member of unit config.
  // 'log_disk_util_threshold' and 'log_disk_util_limit_threshold' are members of tenant parameters.
  // If we just only provide 'update_disk_options', the correctness of PalfDiskOptions can not be guaranteed.
  // for example, original PalfDiskOptions is that
  // {
  //   log_disk_size = 100G,
  //   log_disk_util_limit_threshold = 95,
  //   log_disk_util_threshold = 80
  // }
  //
  // 1. thread1 update 'log_disk_size' with 50G, and it will used 'update_disk_options' with PalfDiskOptions
  // {
  //   log_disk_size = 50G,
  //   log_disk_util_limit_threshold = 95,
  //   log_disk_util_threshold = 80
  // }
  // 2. thread2 updaet 'log_disk_util_limit_threshold' with 85, and it will used 'update_disk_options' with PalfDiskOptions
  // {
  //   log_disk_size = 100G,
  //   log_disk_util_limit_threshold = 85,
  //   log_disk_util_threshold = 80
  // }.
  int update_palf_options_except_disk_usage_limit_size();
  int update_log_disk_usage_limit_size(const int64_t log_disk_usage_limit_size);
  int get_palf_options(palf::PalfOptions &options);
  int stat_palf(palf::PalfStat &palf_stat);
  int stat_apply(LSApplyStat &apply_stat);
  int stat_replay(LSReplayStat &replay_stat);

  int diagnose_replay(ReplayDiagnoseInfo &diagnose_info);
  int diagnose_apply(ApplyDiagnoseInfo &diagnose_info);
  int get_io_start_time(int64_t &last_working_time);
  int check_disk_space_enough(bool &is_disk_enough);

  palf::PalfEnv *get_palf_env() { return palf_env_; }
  ObLogReplayService *get_log_replay_service()  { return &replay_service_; }
  ObLogApplyService *get_log_apply_service()  { return &apply_service_; }
  // Get restore net driver for standby log sync
  // Returns the net driver from restore service if available
  class ObLogRestoreNetDriver;
  ObLogRestoreNetDriver *get_restore_net_driver();
  int check_need_do_checkpoint(bool &need_do_checkpoint);

private:
  int create_ls_(const palf::PalfBaseInfo &palf_base_info,
                 ObLogHandler &log_handler);
  int get_unrecyclable_log_disk_size(int64_t &unrecyclable_log_disk_size);
private:
  bool is_inited_;
  bool is_running_;
  bool enable_shared_storage_;

  common::ObAddr self_;
  palf::PalfEnv *palf_env_;
  IObNetKeepAliveAdapter *net_keepalive_adapter_;
  common::ObILogAllocator *alloc_mgr_;

  ObLogApplyService apply_service_;
  ObLogReplayService replay_service_;
  ObLSAdapter ls_adapter_;
  ObLogMonitor monitor_;
  ObSpinLock update_palf_opts_lock_;
  // Restore service for standby log sync
private:
  DISALLOW_COPY_AND_ASSIGN(ObLogService);
};

} // end namespace logservice
} // end namespace oceanbase
namespace oceanbase
{
namespace logservice
{
// demoted from share::ObShareUtil(checks whether the clog disk is full or hung, through MTL ObLogService)
int check_clog_disk_full_or_hang(bool &clog_disk_is_full, bool &clog_disk_is_hang);
}
}

#endif // OCEANBASE_LOGSERVICE_OB_LOG_SERVICE_
