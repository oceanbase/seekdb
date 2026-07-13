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
#include "observer/ob_server_struct.h"
#include "storage/tablelock/ob_table_lock_rpc_struct.h"
#include "observer/ob_uniq_task_queue.h"
#include "observer/report/ob_tablet_table_updater.h"
#include "observer/ob_standby_schema_refresh_trigger.h"

namespace oceanbase
{
namespace share
{
struct ObTabletReplicaChecksumItem;
class ObTenantDagScheduler;
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
class ObRemoteLocationGetter;

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

class ObService
{
public:
  explicit ObService(const ObGlobalContext &gctx);
  virtual ~ObService();

  int init(common::ObMySQLProxy &sql_proxy,
           bool need_bootstrap);
  int start();
  void set_stop();
  void stop();
  void wait();
  int destroy();

  //fill_tablet_replica: to build a tablet replica locally
  // @params[in] tenant: tablet belongs to which tenant
  // @params[in] tablet_id: the tablet to build
  // @params[out] tablet_replica: infos about this tablet replica
  // @params[out] tablet_checksum: infos about this tablet data/column checksum
  // @params[in] need_checksum: whether to fill tablet_checksum
  // ATTENTION: If tablet does not exist, returns OB_TABLET_NOT_EXIST.
  int fill_tablet_report_info(const ObTabletID &tablet_id,
      share::ObTabletReplica &tablet_replica,
      share::ObTabletReplicaChecksumItem &tablet_checksum,
      const bool need_checksum = true);

  int update_baseline_schema_version(const int64_t schema_version);
  virtual const common::ObAddr &get_self_addr();

  ////////////////////////////////////////////////////////////////
  int check_frozen_scn(const obcall::ObCheckFrozenScnArg &arg);
  int get_min_sstable_schema_version(
      const obcall::ObGetMinSSTableSchemaVersionArg &arg,
      obcall::ObGetMinSSTableSchemaVersionRes &result);
  // ObCallSwitchSchemaP @RS DDL
  int switch_schema(const obcall::ObSwitchSchemaArg &arg, obcall::ObSwitchSchemaResult &result);
  int calc_column_checksum_request(const obcall::ObCalcColumnChecksumRequestArg &arg, obcall::ObCalcColumnChecksumRequestRes &res);
  int build_split_tablet_data_start_request(const obcall::ObTabletSplitStartArg &arg, obcall::ObTabletSplitStartResult &res);
  int build_split_tablet_data_finish_request(const obcall::ObTabletSplitFinishArg &arg, obcall::ObTabletSplitFinishResult &res);
  int fetch_split_tablet_info(const obcall::ObFetchSplitTabletInfoArg &arg, obcall::ObFetchSplitTabletInfoRes &res, const int64_t abs_timeout_us);
  int build_ddl_single_replica_request(const obcall::ObDDLBuildSingleReplicaRequestArg &arg);
  int build_ddl_single_replica_request(const obcall::ObDDLBuildSingleReplicaRequestArg &arg, obcall::ObDDLBuildSingleReplicaRequestResult &res);
  int check_and_cancel_ddl_complement_data_dag(const obcall::ObDDLBuildSingleReplicaRequestArg &arg, bool &is_dag_exist);
  int check_and_cancel_delete_lob_meta_row_dag(const obcall::ObDDLBuildSingleReplicaRequestArg &arg, bool &is_dag_exist);
  int stop_partition_write(const obcall::Int64 &switchover_timestamp, obcall::Int64 &result);
  int check_partition_log(const obcall::Int64 &switchover_timestamp, obcall::Int64 &result);
  int get_wrs_info(const obcall::ObGetWRSArg &arg, obcall::ObGetWRSResult &result);
  int broadcast_consensus_version(
      const obcall::ObBroadcastConsensusVersionArg &arg,
      obcall::ObBroadcastConsensusVersionRes &result);
  ////////////////////////////////////////////////////////////////
  int estimate_partition_rows(const obcall::ObEstPartArg &arg,
                              obcall::ObEstPartRes &res) const;
  int estimate_tablet_block_count(const obcall::ObEstBlockArg &arg,
                                  obcall::ObEstBlockRes &res) const;
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

  // ObCallCheckMemtableCntP
  int check_memtable_cnt(
      const obcall::ObCheckMemtableCntArg &arg,
      obcall::ObCheckMemtableCntResult &result);
  // ObCallCheckMediumCompactionInfoListP
  int check_medium_compaction_info_list_cnt(
      const obcall::ObCheckMediumCompactionInfoListArg &arg,
      obcall::ObCheckMediumCompactionInfoListResult &result);
  int prepare_tablet_split_task_ranges(
      const obcall::ObPrepareSplitRangesArg &arg,
      obcall::ObPrepareSplitRangesRes &result);

  int check_modify_time_elapsed(
      const obcall::ObCheckModifyTimeElapsedArg &arg,
      obcall::ObCheckModifyTimeElapsedResult &result);

  int check_ddl_tablet_merge_status(
    const obcall::ObDDLCheckTabletMergeStatusArg &arg,
    obcall::ObDDLCheckTabletMergeStatusResult &result);
  ////////////////////////////////////////////////////////////////
  // ObCallBatchSwitchRsLeaderP @RS leader coordinator & admin
  int batch_switch_rs_leader(const ObAddr &arg);
  // ObCallGetPartitionCountP @RS leader coordinator
  int get_partition_count(obcall::ObGetPartitionCountResult &result);

  ////////////////////////////////////////////////////////////////

  // ObCallGetServerStatusP @RS
  int get_server_resource_info(const obcall::ObGetServerResourceInfoArg &arg, obcall::ObGetServerResourceInfoResult &result);
  int get_server_resource_info(share::ObServerResourceInfo &resource_info);
  static int get_build_version(share::ObBuildVersion &build_version);
  int check_server_empty(const obcall::ObCheckServerEmptyArg &arg, obcall::Bool &is_empty);
  int check_server_empty_with_result(const obcall::ObCheckServerEmptyArg &arg, obcall::ObCheckServerEmptyResult &result);
  // ObCallIsEmptyServerP @RS bootstrap

  ////////////////////////////////////////////////////////////////
  int load_leader_cluster_login_info();
  // ObCallSetDebugSyncActionP @RS::admin to set debug sync action
  int set_ds_action(const obcall::ObDebugSyncActionArg &arg);
  // ObSyncPartitionTableP @RS empty_server_checker
  int sync_partition_table(const obcall::Int64 &arg);
  // ObCallSetTPP @RS::admin to set tracepoint
  int set_tracepoint(const obcall::ObAdminSetTPArg &arg);
  int cancel_sys_task(const share::ObTaskId &task_id);
  int refresh_memory_stat();
  ////////////////////////////////////////////////////////////////
  // misc functions

  int get_tenant_refreshed_schema_version(
      const obcall::ObGetTenantSchemaVersionArg &arg,
      obcall::ObGetTenantSchemaVersionResult &result);
  int submit_async_refresh_schema_task(const int64_t schema_version);
  int init_tenant_config(
      const obcall::ObInitTenantConfigArg &arg,
      obcall::ObInitTenantConfigRes &result);
  int check_server_empty(bool &server_empty);
  int change_external_storage_dest(obcall::ObAdminSetConfigArg &arg);

private:
  int bootstrap();
  int inner_fill_tablet_info_(
      const ObTabletID &tablet_id,
      storage::ObLS *ls,
      share::ObTabletReplica &tablet_replica,
      share::ObTabletReplicaChecksumItem &tablet_checksum,
      const bool need_checksum);
  int set_server_id_(const int64_t server_id);

  int handle_tenant_freeze_req_();
  int handle_tablet_freeze_req_(const common::ObTabletID &tablet_id);
  int tenant_freeze_();
private:
  bool inited_;
  volatile bool stopped_;

  ObServerSchemaUpdater schema_updater_;

  //lease
  const ObGlobalContext &gctx_;
  ObSchemaReleaseTimeTask schema_release_task_;
  TelemetryTask telemetry_task_;
  share::schema::ObStandbySchemaRefreshTrigger standby_schema_refresh_trigger_;
  bool need_bootstrap_;
};

}//end namespace observer
}//end namespace oceanbase
#endif
