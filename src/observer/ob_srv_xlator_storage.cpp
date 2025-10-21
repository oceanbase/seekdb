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
#define USING_LOG_PREFIX SERVER

#include "observer/ob_srv_xlator.h"


#include "src/observer/table/ob_table_filter.h"

using namespace oceanbase;
using namespace oceanbase::observer;
using namespace oceanbase::lib;
using namespace oceanbase::rpc;
using namespace oceanbase::sql;
using namespace oceanbase::common;
using namespace oceanbase::transaction;
using namespace oceanbase::obrpc;
using namespace oceanbase::obmysql;

void oceanbase::observer::init_srv_xlator_for_storage(ObSrvRpcXlator *xlator) {
    // oceanbase service provied
    RPC_PROCESSOR(ObGetWRSInfoP, gctx_);
    RPC_PROCESSOR(ObStopPartitionWriteP, gctx_);
    RPC_PROCESSOR(ObCheckPartitionLogP, gctx_);
    RPC_PROCESSOR(ObRpcFetchSysLSP, gctx_);
    //RPC_PROCESSOR(ObRpcRemoveReplicaP, gctx_);
    RPC_PROCESSOR(ObRpcMinorFreezeP, gctx_);
    RPC_PROCESSOR(ObRpcCheckSchemaVersionElapsedP, gctx_);
    RPC_PROCESSOR(ObRpcCheckMemtableCntP, gctx_);
    RPC_PROCESSOR(ObRpcCheckMediumCompactionInfoListP, gctx_);
    RPC_PROCESSOR(ObRpcCheckCtxCreateTimestampElapsedP, gctx_);
    RPC_PROCESSOR(ObRpcUpdateBaselineSchemaVersionP, gctx_);
    RPC_PROCESSOR(ObRpcSwitchLeaderP, gctx_);
    RPC_PROCESSOR(ObRpcBatchSwitchRsLeaderP, gctx_);
    RPC_PROCESSOR(ObRpcGetPartitionCountP, gctx_);
    RPC_PROCESSOR(ObRpcSwitchSchemaP, gctx_);
    RPC_PROCESSOR(ObRpcRefreshMemStatP, gctx_);
    RPC_PROCESSOR(ObRpcWashMemFragmentationP, gctx_);
    RPC_PROCESSOR(ObRpcCheckServerEmptyP, gctx_);
    RPC_PROCESSOR(ObRpcCheckServerEmptyWithResultP, gctx_);
    RPC_PROCESSOR(ObRpcPrepareServerForAddingServerP, gctx_);
    RPC_PROCESSOR(ObRpcCheckServerMachineStatusP, gctx_);
    RPC_PROCESSOR(ObAllServerTracerP, gctx_);
    RPC_PROCESSOR(ObRpcCheckDeploymentModeP, gctx_);
    RPC_PROCESSOR(ObRpcSyncAutoincValueP, gctx_);
    RPC_PROCESSOR(ObRpcClearAutoincCacheP, gctx_);
    RPC_PROCESSOR(ObReportReplicaP, gctx_);
    RPC_PROCESSOR(ObRecycleReplicaP, gctx_);
    RPC_PROCESSOR(ObClearLocationCacheP, gctx_);
    RPC_PROCESSOR(ObSetDSActionP, gctx_);
    RPC_PROCESSOR(ObRefreshIOCalibrationP, gctx_);
    RPC_PROCESSOR(ObExecuteIOBenchmarkP, gctx_);
    RPC_PROCESSOR(ObSyncPartitionTableP, gctx_);
    RPC_PROCESSOR(ObCancelSysTaskP, gctx_);
    RPC_PROCESSOR(ObCalcColumnChecksumRequestP, gctx_);
    RPC_PROCESSOR(ObRpcBackupLSDataP, gctx_);
    RPC_PROCESSOR(ObRpcBackupLSComplLOGP, gctx_);
    RPC_PROCESSOR(ObRpcBackupBuildIndexP, gctx_);
    RPC_PROCESSOR(ObRpcBackupLSCleanP, gctx_);
    RPC_PROCESSOR(ObRpcBackupMetaP, gctx_);
    RPC_PROCESSOR(ObRpcBackupFuseTabletMetaP, gctx_);
    RPC_PROCESSOR(ObRpcBackupLSDataResP, gctx_);
    RPC_PROCESSOR(ObRpcBackupCleanLSResP, gctx_);

    RPC_PROCESSOR(ObRpcCheckBackupTaskExistP, gctx_);
    RPC_PROCESSOR(ObRenewInZoneHbP, gctx_);
    RPC_PROCESSOR(ObPreProcessServerP, gctx_);
    RPC_PROCESSOR(ObRpcBroadcastRsListP, gctx_);
    RPC_PROCESSOR(ObRpcBuildDDLSingleReplicaRequestP, gctx_);
    RPC_PROCESSOR(ObRpcBuildSplitTabletDataStartRequestP, gctx_);
    RPC_PROCESSOR(ObRpcBuildSplitTabletDataFinishRequestP, gctx_);
    RPC_PROCESSOR(ObRpcFreezeSplitSrcTabletP, gctx_);
    RPC_PROCESSOR(ObRpcFetchSplitTabletInfoP, gctx_);
    RPC_PROCESSOR(ObRpcFetchTabletAutoincSeqCacheP, gctx_);
    RPC_PROCESSOR(ObRpcBatchGetTabletAutoincSeqP, gctx_);
    RPC_PROCESSOR(ObRpcBatchSetTabletAutoincSeqP, gctx_);
    RPC_PROCESSOR(ObRpcClearTabletAutoincSeqCacheP, gctx_);
    RPC_PROCESSOR(ObRpcBatchGetTabletBindingP, gctx_);
    RPC_PROCESSOR(ObRpcBatchGetTabletSplitP, gctx_);
    RPC_PROCESSOR(ObRpcRemoteWriteDDLRedoLogP, gctx_);
    RPC_PROCESSOR(ObRpcRemoteWriteDDLCommitLogP, gctx_);
    RPC_PROCESSOR(ObRpcSetTabletAutoincSeqP, gctx_);
    RPC_PROCESSOR(ObRpcRemoteWriteDDLIncCommitLogP, gctx_);
    RPC_PROCESSOR(ObRpcDRTaskReplyToMetaP, gctx_);
    RPC_PROCESSOR(ObRpcLSCancelReplicaP, gctx_);
    RPC_PROCESSOR(ObRpcLSMigrateReplicaP, gctx_);
    RPC_PROCESSOR(ObRpcLSAddReplicaP, gctx_);
    RPC_PROCESSOR(ObRpcLSTypeTransformP, gctx_);
    RPC_PROCESSOR(ObRpcLSRemovePaxosReplicaP, gctx_);
    RPC_PROCESSOR(ObRpcLSRemoveNonPaxosReplicaP, gctx_);
    RPC_PROCESSOR(ObRpcLSModifyPaxosReplicaNumberP, gctx_);
    RPC_PROCESSOR(ObRpcLSCheckDRTaskExistP, gctx_);
    RPC_PROCESSOR(ObAdminDRTaskP, gctx_);
    RPC_PROCESSOR(ObRpcCreateTenantUserLSP, gctx_);
    RPC_PROCESSOR(ObRpcLoadTenantTableSchemaP, gctx_);
    RPC_PROCESSOR(ObRpcGenUniqueIDP, gctx_);
    RPC_PROCESSOR(ObRpcStartTransferTaskP, gctx_);
#ifdef OB_BUILD_ARBITRATION
    RPC_PROCESSOR(ObRpcAddArbP, gctx_);
    RPC_PROCESSOR(ObRpcRemoveArbP, gctx_);
#endif
    RPC_PROCESSOR(ObRpcDDLCheckTabletMergeStatusP, gctx_);
    RPC_PROCESSOR(ObRpcCreateDuplicateLSP, gctx_);
    RPC_PROCESSOR(ObRpcNotifyTenantSnapshotSchedulerP, gctx_);
    RPC_PROCESSOR(ObRpcInnerCreateTenantSnapshotP, gctx_);
    RPC_PROCESSOR(ObRpcInnerDropTenantSnapshotP, gctx_);
    RPC_PROCESSOR(ObRpcNotifyCloneSchedulerP, gctx_);
    RPC_PROCESSOR(ObRpcNotifyTenantThreadP, gctx_);
    RPC_PROCESSOR(ObRpcTabletMajorFreezeP, gctx_);
    RPC_PROCESSOR(ObRpcDetectSessionAliveP, gctx_);
    RPC_PROCESSOR(ObCancelGatherStatsP, gctx_);
    RPC_PROCESSOR(ObCollectMvMergeInfoP, gctx_);
    RPC_PROCESSOR(ObFetchStableMemberListP, gctx_);
    RPC_PROCESSOR(ObRpcPrepareTabletSplitTaskRangesP, gctx_);
    RPC_PROCESSOR(ObRpcCheckStorageOperationStatusP, gctx_);
#ifdef OB_BUILD_SHARED_STORAGE
    RPC_PROCESSOR(ObRpcRemoteWriteDDLFinishLogP, gctx_);
    RPC_PROCESSOR(ObFetchReplicaPrewarmMicroBlockP, gctx_.bandwidth_throttle_);
    RPC_PROCESSOR(ObGetSSMacroBlockP, gctx_);
    RPC_PROCESSOR(ObGetSSPhyBlockInfoP, gctx_);
    RPC_PROCESSOR(ObGetSSMicroBlockMetaP, gctx_);
    RPC_PROCESSOR(ObRpcSyncHotMicroKeyP, gctx_);
    RPC_PROCESSOR(ObGetSSMacroBlockByURIP, gctx_);
    RPC_PROCESSOR(ObDelSSTabletMetaP, gctx_);
    RPC_PROCESSOR(ObEnableSSMicroCacheP, gctx_);
    RPC_PROCESSOR(ObGetSSMicroCacheInfoP, gctx_);
    RPC_PROCESSOR(ObRpcClearSSMicroCacheP, gctx_);
    RPC_PROCESSOR(ObDelSSLocalTmpFileP, gctx_);
    RPC_PROCESSOR(ObDelSSLocalMajorP, gctx_);
    RPC_PROCESSOR(ObCalibrateSSDiskSpaceP, gctx_);
    RPC_PROCESSOR(ObDelSSTabletMicroP, gctx_);
    RPC_PROCESSOR(ObSetSSCkptCompressorP, gctx_);
    RPC_PROCESSOR(ObSetSSCacheSizeRatioP, gctx_);
    RPC_PROCESSOR(ObTriggerStorageCacheP, gctx_);
#endif
    RPC_PROCESSOR(ObNotifySharedStorageInfoP, gctx_);
    RPC_PROCESSOR(ObRpcBroadcastConfigVersionP, gctx_);
    RPC_PROCESSOR(ObRpcNotifyLSRestoreFinishP, gctx_);
#ifdef OB_BUILD_ARBITRATION
    RPC_PROCESSOR(ObFetchArbMemberP, gctx_);
#endif
    RPC_PROCESSOR(ObRpcStartArchiveP, gctx_);
}
