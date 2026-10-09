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

#ifndef OCEANBASE_OBSERVER_OB_SERVICE_H_
#define OCEANBASE_OBSERVER_OB_SERVICE_H_

#include "storage/tablet/ob_batch_create_tablet_arg.h"
#include "storage/tx/ob_tx_result_struct.h"
#include "storage/ob_storage_rpc_arg.h"
#include "observer/ob_server_schema_updater.h"
#include "share/ob_rpc_struct.h"
#include "share/ob_share_util.h"
#include "share/ob_server_struct.h"
#include "storage/tablelock/ob_table_lock_rpc_struct.h"
#include "observer/ob_uniq_task_queue.h"
#include "observer/ob_tablet_runtime_meta_updater.h"
#include "query/change_stream/ob_change_stream_service.h"
#include "query/command/ob_local_command_service.h"
#include "rootserver/ob_rootserver_local_runtime.h"

namespace oceanbase
{
namespace share
{
struct ObTabletLocalChecksumItem;
class ObDagScheduler;
class ObIDag;
}
namespace storage
{
struct ObFrozenStatus;
class ObLS;
}
namespace observer
{
class ObServer;
class ObServerInstance;

class ObSchemaReleaseTimeTask: public common::ObTimerTask
{
public:
  ObSchemaReleaseTimeTask();
  virtual ~ObSchemaReleaseTimeTask() {}
  int init(ObServerSchemaUpdater &schema_updater);
  void stop();
  void wait();
  void destroy();
  virtual void runTimerTask() override;
private:
  int schedule_();
private:
  ObServerSchemaUpdater *schema_updater_;
  common::ObTimer timer_;
  bool is_inited_;
};

class TelemetryTask {
public:
  TelemetryTask() = default;
  int report();
};

class ObService : public rootserver::ObIRootserverLocalRuntime,
                  public query::ObILocalCommandService
{
public:
  ObService(
      const share::ObGlobalContext &gctx,
      query::ObIChangeStreamService &change_stream_service);
  virtual ~ObService();

  int init(common::ObMySQLProxy &sql_proxy);
  int bootstrap();
  int report_bootstrap_telemetry();
  int start();
  void set_stop();
  void stop();
  void wait();
  int destroy();

  // Build tablet report information locally.
  // @params[in] tablet_id: the tablet to build
  // @params[out] runtime_info: local runtime metadata for this tablet
  // @params[out] tablet_checksum: local tablet data/column checksum
  // ATTENTION: If ls not exist, then OB_LS_NOT_EXIST
  //            If tablet not exist on that ls, then OB_TABLET_NOT_EXIST
  int fill_tablet_runtime_info(const ObTabletID &tablet_id,
      share::ObTabletRuntimeInfo &runtime_info,
      share::ObTabletLocalChecksumItem &tablet_checksum);

  int update_baseline_schema_version(const int64_t schema_version);
  virtual const common::ObAddr &get_self_addr();

  ////////////////////////////////////////////////////////////////
  int check_frozen_scn(const obcall::ObCheckFrozenScnArg &arg);
  int calc_column_checksum_request(const obcall::ObCalcColumnChecksumRequestArg &arg, obcall::ObCalcColumnChecksumRequestRes &res);
  int build_ddl_local(const obcall::ObDDLLocalBuildArg &arg, obcall::ObDDLLocalBuildResult &res);
  int check_and_cancel_ddl_complement_data_dag(const obcall::ObDDLLocalBuildArg &arg, bool &is_dag_exist);
  int check_and_cancel_delete_lob_meta_row_dag(const obcall::ObDDLLocalBuildArg &arg, bool &is_dag_exist);
  int stop_partition_write(const obcall::Int64 &switchover_timestamp, obcall::Int64 &result);
  int check_partition_log(const obcall::Int64 &switchover_timestamp, obcall::Int64 &result);
  int get_wrs_info(const obcall::ObGetWRSArg &arg, obcall::ObGetWRSResult &result);
  ////////////////////////////////////////////////////////////////
  // ObCallMinorFreezeP @RS minor freeze
  int minor_freeze(const obcall::ObMinorFreezeArg &arg,
                   obcall::Int64 &result);
  // ObCallTabletMajorFreezeP @RS tablet major freeze
  int tablet_major_freeze(const obcall::ObTabletMajorFreezeArg &arg,
                   obcall::Int64 &result);
  // ObCallCheckSchemaVersionElapsedP @RS global index builder
  int check_schema_version_elapsed(
      const obcall::ObCheckSchemaVersionElapsedArg &arg,
      obcall::ObCheckSchemaVersionElapsedResult &result);
  // ObCallGetChecksumCalSnapshotP

  int check_modify_time_elapsed(
      const obcall::ObCheckModifyTimeElapsedArg &arg,
      obcall::ObCheckModifyTimeElapsedResult &result);

  int check_ddl_tablet_merge_status(
    const obcall::ObDDLCheckTabletMergeStatusArg &arg,
    obcall::ObDDLCheckTabletMergeStatusResult &result);
  int get_server_resource_info(share::ObServerResourceInfo &resource_info);
  static int get_build_version(share::ObBuildVersion &build_version);
  int get_build_version(char *buf, int64_t buf_len) override;
  int load_all_special_system_packages() override;
  int wait_system_package_ready(const common::ObTimeoutCtx &ctx) override;
  int load_leader_cluster_login_info();
  // ObCallSetDebugSyncActionP @RS::admin to set debug sync action
  int set_ds_action(const obcall::ObDebugSyncActionArg &arg) override;
  int refresh_stat_cache(const obcall::ObUpdateStatCacheArg &arg) override;
  int update_opt_stat_monitoring_info(
      const obcall::ObFlushOptStatArg &arg) override;
  int set_tracepoint(const obcall::ObSetTracepointParam &param) override;
  int cancel_sys_task(const share::ObTaskId &task_id) override;
  int clear_expired_deadlock_events() override;
  ////////////////////////////////////////////////////////////////
  // misc functions

  int submit_async_refresh_schema_task(const int64_t schema_version);
  int check_server_empty(bool &server_empty) override;
  int wait_until_change_stream_refreshed(
      common::ObMySQLProxy &mysql_proxy,
      int64_t timeout_us) override;

private:
  int inner_fill_tablet_info_(
      const ObTabletID &tablet_id,
      storage::ObLS *ls,
      share::ObTabletRuntimeInfo &runtime_info,
      share::ObTabletLocalChecksumItem &tablet_checksum);
  int handle_server_freeze_req_(const obcall::ObMinorFreezeArg &arg);
  int handle_tablet_freeze_req_(const common::ObTabletID &tablet_id);
  int server_freeze_();
private:
  bool inited_;
  volatile bool stopped_;

  ObServerSchemaUpdater schema_updater_;

  //lease
  const share::ObGlobalContext &gctx_;
  query::ObIChangeStreamService &change_stream_service_;
  ObSchemaReleaseTimeTask schema_release_task_;
  TelemetryTask telemetry_task_;
};

}//end namespace observer
}//end namespace oceanbase
#endif
