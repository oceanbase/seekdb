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

#include "observer/ob_server_schema_updater.h"
#include "observer/ob_rpc_processor_simple.h"
#include "observer/ob_uniq_task_queue.h"
#include "observer/report/ob_tablet_table_updater.h"
#include "src/share/backup/ob_log_restore_struct.h"
#include "src/share/schema/ob_standby_schema_refresh_trigger.h"

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
  int init(ObServerSchemaUpdater &schema_updater, int tg_id);
  virtual void runTimerTask() override;
private:
  int schedule_();
private:
  ObServerSchemaUpdater *schema_updater_;
  bool is_inited_;
};

class TelemetryTask : public common::ObTimerTask {
public:
  TelemetryTask(bool embed_mode);
  virtual void runTimerTask() override;
  bool embed_mode_;
};

class ObService
{
public:
  explicit ObService(const ObGlobalContext &gctx);
  virtual ~ObService();

  int init(common::ObMySQLProxy &sql_proxy,
           bool need_bootstrap);
  int start(bool embed_mode);
  void set_stop();
  void stop();
  void wait();
  int destroy();

  //fill_tablet_replica: to build a tablet replica locally
  // @params[in] tenant_id: tablet belongs to which tenant
  // @params[in] ls_id: tablet belongs to which log stream
  // @params[in] tablet_id: the tablet to build
  // @params[out] tablet_replica: infos about this tablet replica
  // @params[out] tablet_checksum: infos about this tablet data/column checksum
  // @params[in] need_checksum: whether to fill tablet_checksum
  // ATTENTION: If ls not exist, then OB_LS_NOT_EXIST
  //            If tablet not exist on that ls, then OB_TABLET_NOT_EXIST
  int fill_tablet_report_info(
      const uint64_t tenant_id,
      const share::ObLSID &ls_id,
      const ObTabletID &tablet_id,
      share::ObTabletReplica &tablet_replica,
      share::ObTabletReplicaChecksumItem &tablet_checksum,
      const bool need_checksum = true);

  int update_baseline_schema_version(const int64_t schema_version);
  virtual const common::ObAddr &get_self_addr();

  ////////////////////////////////////////////////////////////////
  int check_frozen_scn(const obrpc::ObCheckFrozenScnArg &arg);
  int get_min_sstable_schema_version(
      const obrpc::ObGetMinSSTableSchemaVersionArg &arg,
      obrpc::ObGetMinSSTableSchemaVersionRes &result);
  // ObRpcSwitchSchemaP @RS DDL
  int switch_schema(const obrpc::ObSwitchSchemaArg &arg, obrpc::ObSwitchSchemaResult &result);
  int calc_column_checksum_request(const obrpc::ObCalcColumnChecksumRequestArg &arg, obrpc::ObCalcColumnChecksumRequestRes &res);
  int build_split_tablet_data_start_request(const obrpc::ObTabletSplitStartArg &arg, obrpc::ObTabletSplitStartResult &res);
  int build_split_tablet_data_finish_request(const obrpc::ObTabletSplitFinishArg &arg, obrpc::ObTabletSplitFinishResult &res);
  int freeze_split_src_tablet(const obrpc::ObFreezeSplitSrcTabletArg &arg, obrpc::ObFreezeSplitSrcTabletRes &res, const int64_t abs_timeout_us);
  int fetch_split_tablet_info(const obrpc::ObFetchSplitTabletInfoArg &arg, obrpc::ObFetchSplitTabletInfoRes &res, const int64_t abs_timeout_us);
  int build_ddl_single_replica_request(const obrpc::ObDDLBuildSingleReplicaRequestArg &arg);
  int build_ddl_single_replica_request(const obrpc::ObDDLBuildSingleReplicaRequestArg &arg, obrpc::ObDDLBuildSingleReplicaRequestResult &res);
  int check_and_cancel_ddl_complement_data_dag(const obrpc::ObDDLBuildSingleReplicaRequestArg &arg, bool &is_dag_exist);
  int check_and_cancel_delete_lob_meta_row_dag(const obrpc::ObDDLBuildSingleReplicaRequestArg &arg, bool &is_dag_exist);
  int stop_partition_write(const obrpc::Int64 &switchover_timestamp, obrpc::Int64 &result);
  int check_partition_log(const obrpc::Int64 &switchover_timestamp, obrpc::Int64 &result);
  int get_wrs_info(const obrpc::ObGetWRSArg &arg, obrpc::ObGetWRSResult &result);
  int broadcast_consensus_version(
      const obrpc::ObBroadcastConsensusVersionArg &arg,
      obrpc::ObBroadcastConsensusVersionRes &result);
  ////////////////////////////////////////////////////////////////
  int backup_ls_data(const obrpc::ObBackupDataArg &arg);
  int backup_completing_log(const obrpc::ObBackupComplLogArg &arg);
  int backup_build_index(const obrpc::ObBackupBuildIdxArg &arg);
  int backup_fuse_tablet_meta(const obrpc::ObBackupFuseTabletMetaArg &arg);
  int check_backup_dest_connectivity(const obrpc::ObCheckBackupConnectivityArg &arg);
  int backup_meta(const obrpc::ObBackupMetaArg &arg);
  int check_backup_task_exist(const obrpc::ObBackupCheckTaskArg &arg, bool &res);
  int delete_backup_ls_task(const obrpc::ObLSBackupCleanArg &arg);
  int notify_archive(const obrpc::ObNotifyArchiveArg &arg);
  int report_backup_over(const obrpc::ObBackupTaskRes &res);
  int report_backup_clean_over(const obrpc::ObBackupTaskRes &res);

  int get_ls_sync_scn(const obrpc::ObGetLSSyncScnArg &arg,
                           obrpc::ObGetLSSyncScnRes &result);
  int force_set_ls_as_single_replica(const obrpc::ObForceSetLSAsSingleReplicaArg &arg);
  int force_set_server_list(const obrpc::ObForceSetServerListArg &arg, obrpc::ObForceSetServerListResult &result);
  int estimate_partition_rows(const obrpc::ObEstPartArg &arg,
                              obrpc::ObEstPartRes &res) const;
  int estimate_tablet_block_count(const obrpc::ObEstBlockArg &arg,
                                  obrpc::ObEstBlockRes &res) const;
  int estimate_skip_rate(const obrpc::ObEstSkipRateArg &arg,
                         obrpc::ObEstSkipRateRes &res) const;
  ////////////////////////////////////////////////////////////////
  // ObRpcMinorFreezeP @RS minor freeze
  int minor_freeze(const obrpc::ObMinorFreezeArg &arg,
                   obrpc::Int64 &result);
  // ObRpcTabletMajorFreezeP @RS tablet major freeze
  int tablet_major_freeze(const obrpc::ObTabletMajorFreezeArg &arg,
                   obrpc::Int64 &result);
  // ObRpcCheckSchemaVersionElapsedP @RS global index builder
  int check_schema_version_elapsed(
      const obrpc::ObCheckSchemaVersionElapsedArg &arg,
      obrpc::ObCheckSchemaVersionElapsedResult &result);
  // ObRpcGetChecksumCalSnapshotP

  // ObRpcCheckMemtableCntP
  int check_memtable_cnt(
      const obrpc::ObCheckMemtableCntArg &arg,
      obrpc::ObCheckMemtableCntResult &result);
  // ObRpcCheckMediumCompactionInfoListP
  int check_medium_compaction_info_list_cnt(
      const obrpc::ObCheckMediumCompactionInfoListArg &arg,
      obrpc::ObCheckMediumCompactionInfoListResult &result);
  int prepare_tablet_split_task_ranges(
      const obrpc::ObPrepareSplitRangesArg &arg,
      obrpc::ObPrepareSplitRangesRes &result);

  int check_modify_time_elapsed(
      const obrpc::ObCheckModifyTimeElapsedArg &arg,
      obrpc::ObCheckModifyTimeElapsedResult &result);

  int check_ddl_tablet_merge_status(
    const obrpc::ObDDLCheckTabletMergeStatusArg &arg,
    obrpc::ObDDLCheckTabletMergeStatusResult &result);
  ////////////////////////////////////////////////////////////////
  // ObRpcBatchSwitchRsLeaderP @RS leader coordinator & admin
  int batch_switch_rs_leader(const ObAddr &arg);
  // ObRpcGetPartitionCountP @RS leader coordinator
  int get_partition_count(obrpc::ObGetPartitionCountResult &result);

  ////////////////////////////////////////////////////////////////

  // ObRpcGetServerStatusP @RS
  int get_server_resource_info(const obrpc::ObGetServerResourceInfoArg &arg, obrpc::ObGetServerResourceInfoResult &result);
  int get_server_resource_info(share::ObServerResourceInfo &resource_info);
  static int get_build_version(share::ObBuildVersion &build_version);
  int check_server_empty(const obrpc::ObCheckServerEmptyArg &arg, obrpc::Bool &is_empty);
  int check_server_empty_with_result(const obrpc::ObCheckServerEmptyArg &arg, obrpc::ObCheckServerEmptyResult &result);
  // ObRpcIsEmptyServerP @RS bootstrap

  ////////////////////////////////////////////////////////////////
  int load_leader_cluster_login_info();
  // ObDropReplicaP @RS::admin to drop replica
  int set_ds_action(const obrpc::ObDebugSyncActionArg &arg);
  int report_replica(const obrpc::ObReportSingleReplicaArg &arg);
  // ObSyncPartitionTableP @RS empty_server_checker
  int sync_partition_table(const obrpc::Int64 &arg);
  // ObRpcSetTPP @RS::admin to set tracepoint
  int set_tracepoint(const obrpc::ObAdminSetTPArg &arg);
  int cancel_sys_task(const share::ObTaskId &task_id);
  int refresh_memory_stat();
  int wash_memory_fragmentation();
  ////////////////////////////////////////////////////////////////
  // misc functions

  int get_tenant_refreshed_schema_version(
      const obrpc::ObGetTenantSchemaVersionArg &arg,
      obrpc::ObGetTenantSchemaVersionResult &result);
  int submit_async_refresh_schema_task(const uint64_t tenant_id, const int64_t schema_version);
  int init_tenant_config(
      const obrpc::ObInitTenantConfigArg &arg,
      obrpc::ObInitTenantConfigRes &result);
  int check_server_empty(bool &server_empty);
  int change_external_storage_dest(obrpc::ObAdminSetConfigArg &arg);

private:
  int bootstrap();
  int bootstrap_standby();
  int build_restore_source_attr(const common::ObAddr &primary_addr,
                                 share::ObRestoreSourceServiceAttr &source_attr);
  int schedule_standby_restore_task();
  int create_sys_ls();
  int init_tenant_merge_info_(const uint64_t tenant_id);
  int inner_fill_tablet_info_(
      const int64_t tenant_id,
      const ObTabletID &tablet_id,
      storage::ObLS *ls,
      share::ObTabletReplica &tablet_replica,
      share::ObTabletReplicaChecksumItem &tablet_checksum,
      const bool need_checksum);
  int set_server_id_(const int64_t server_id);

  int handle_server_freeze_req_(const obrpc::ObMinorFreezeArg &arg);
  int handle_tenant_freeze_req_(const obrpc::ObMinorFreezeArg &arg);
  int handle_ls_freeze_req_(const obrpc::ObMinorFreezeArg &arg);
  int tenant_freeze_(const uint64_t tenant_id);
  int handle_ls_freeze_req_(const uint64_t tenant_id, const share::ObLSID &ls_id, const common::ObTabletID &tablet_id);
private:
  bool inited_;
  volatile bool stopped_;

  ObServerSchemaUpdater schema_updater_;

  //lease
  const ObGlobalContext &gctx_;
  ObSchemaReleaseTimeTask schema_release_task_;
  TelemetryTask telemetry_task_;
  // Schema refresh trigger is now managed by MTL framework
  bool need_bootstrap_;
};

}//end namespace observer
}//end namespace oceanbase
#endif
