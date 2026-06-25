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
#include "logrpc/ob_log_rpc_req.h"
#include "logrpc/ob_log_service_rpc_shell.h"
#include "palf/log_block_pool_interface.h"             // ILogBlockPool
#include "palf/log_define.h"
#include "rcservice/ob_role_change_service.h"
#include "replayservice/ob_log_replay_service.h"
#include "ob_net_keepalive_adapter.h"
#include "ob_ls_adapter.h"
#include "ob_location_adapter.h"
#include "ob_log_flashback_service.h"                    // ObLogFlashbackService
#include "ob_log_handler.h"
#include "ob_log_monitor.h"

namespace oceanbase
{
namespace common
{
class ObAddr;
class ObILogAllocator;
class ObMySQLProxy;
}

namespace rpc
{
namespace frame
{
class ObReqTransport;
}
}

namespace share
{
class ObLSID;
class ObLocationService;
class SCN;
}

namespace palf
{
class PalfHandleGuard;
class PalfRoleChangeCb;
class PalfDiskOptions;
class PalfEnv;
}
namespace storage
{
class ObLSService;
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
  static palf::AccessMode get_palf_access_mode(const share::ObTenantRole &tenant_role);
  int init(const palf::PalfOptions &options,
           const char *base_dir,
           const common::ObAddr &self,
           common::ObILogAllocator *alloc_mgr,
           storage::ObLSService *ls_service,
           share::ObLocationService *location_service,
           palf::ILogBlockPool *log_block_pool,
           common::ObMySQLProxy *sql_proxy,
           IObNetKeepAliveAdapter *net_keepalive_adapter);
  //--Log stream related interfaces--
  //New log stream interface, this interface will create the corresponding directory for the log stream and create a new log stream with PalfBaseInfo as the log base point.
  //This includes generating and initializing the corresponding ObReplayStatus structure
  // @param [in] id, log stream identifier
  // @param [in] replica_type, the replica type of the log stream
  // @param [in] tenant_role, tenant role, this decides the Palf usage mode (APPEND/RAW_WRITE)
  // @param [in] palf_base_info, log synchronization base point information
  // @param [out] log_handler, new log stream returned in the form of ObLogHandler, ensuring the lifecycle of the log stream when used by upper layers
  int create_ls(const share::ObLSID &id,
                const common::ObReplicaType &replica_type,
                const share::ObTenantRole &tenant_role,
                const palf::PalfBaseInfo &palf_base_info,
                const bool allow_log_sync,
                ObLogHandler &log_handler);
  //Delete log stream interface: After the outer call to create_ls(), if subsequent processes fail, remove_ls() needs to be called
  int remove_ls(const share::ObLSID &id,
                ObLogHandler &log_handler);

  int check_palf_exist(const share::ObLSID &id, bool &exist) const;
  //Downtime restart recovery log stream interface, including generating and initializing the corresponding ObReplayStatus structure
  // @param [in] id, log stream identifier
  // @param [out] log_handler, new log stream returned in the form of ObLogHandler, ensuring the lifecycle of the log stream when used by upper layers
  int add_ls(const share::ObLSID &id,
             ObLogHandler &log_handler);

  int open_palf(const share::ObLSID &id,
                palf::PalfHandleGuard &palf_handle);

  // get role of current palf replica.
  // NB: distinguish the difference from get_role of log_handler
  // In general, get the replica role to do migration/blance/report, use this interface,
  // to write log, use get_role of log_handler
  int get_palf_role(const share::ObLSID &id,
                    common::ObRole &role,
                    int64_t &proposal_id);

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
  int iterate_palf(const ObFunction<int(const palf::PalfHandle&)> &func);
  int iterate_apply(const ObFunction<int(const ObApplyStatus&)> &func);
  int iterate_replay(const ObFunction<int(const ObReplayStatus&)> &func);

  // @desc: flashback all log_stream's redo log of tenant 'tenant'
  // @params [in] const uint64_t tenant: id of tenant which should be flashbacked
  // @params [in] const SCN &flashback_scn: flashback point
  // @params [in] const int64_t timeout_us: timeout time (us)
  // @return
  //   - OB_SUCCESS
  //   - OB_INVALID_ARGUEMENT: invalid tenant or flashback_scn
  //   - OB_NOT_SUPPORTED: meta tenant or sys tenant can't be flashbacked
  //   - OB_EAGAIN: another flashback operation is doing
  //   - OB_TIMEOUT: timeout
  int flashback(const share::SCN &flashback_scn, const int64_t timeout_us);

  int diagnose_role_change(RCDiagnoseInfo &diagnose_info);
  int diagnose_replay(const share::ObLSID &id, ReplayDiagnoseInfo &diagnose_info);
  int diagnose_apply(const share::ObLSID &id, ApplyDiagnoseInfo &diagnose_info);
  int get_io_start_time(int64_t &last_working_time);
  int check_disk_space_enough(bool &is_disk_enough);

  palf::PalfEnv *get_palf_env() { return palf_env_; }
  ObLogReplayService *get_log_replay_service()  { return &replay_service_; }
  ObLogApplyService *get_log_apply_service()  { return &apply_service_; }
  obcall::ObLogServiceRpcProxy *get_rpc_proxy() { return &rpc_proxy_; }
  ObLogFlashbackService *get_flashback_service() { return &flashback_service_; }
  // Get restore net driver for standby log sync
  // Returns the net driver from restore service if available
  class ObLogRestoreNetDriver;
  ObLogRestoreNetDriver *get_restore_net_driver();
  // Get CDC service for log fetcher (standby log sync server side)
  int check_need_do_checkpoint(bool &need_do_checkpoint);

private:
  int create_ls_(const share::ObLSID &id,
                 const common::ObReplicaType &replica_type,
                 const share::ObTenantRole &tenant_role,
                 const palf::PalfBaseInfo &palf_base_info,
                 const bool allow_log_sync,
                 ObLogHandler &log_handler);
  struct GetUnrecycableLogDiskSizeFunctor {
    GetUnrecycableLogDiskSizeFunctor() : unrecycable_log_disk_size_(0) {}
    ~GetUnrecycableLogDiskSizeFunctor() { unrecycable_log_disk_size_ = 0; }
    int operator()(ObLS *ls);
    int64_t unrecycable_log_disk_size_;
  };
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
  ObRoleChangeService role_change_service_;
  ObLocationAdapter location_adapter_;
  ObLSAdapter ls_adapter_;
  obcall::ObLogServiceRpcProxy rpc_proxy_;
  ObLogFlashbackService flashback_service_;
  ObLogMonitor monitor_;
  ObSpinLock update_palf_opts_lock_;
  // Restore service for standby log sync
  // CDC service for log fetcher (standby log sync server side)
private:
  DISALLOW_COPY_AND_ASSIGN(ObLogService);
};

} // end namespace logservice
} // end namespace oceanbase
#endif // OCEANBASE_LOGSERVICE_OB_LOG_SERVICE_
