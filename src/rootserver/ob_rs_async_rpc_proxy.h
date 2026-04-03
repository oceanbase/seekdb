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

#ifndef OCEANBASE_ROOTSERVER_OB_RS_ASYNC_RPC_PROXY_H_
#define OCEANBASE_ROOTSERVER_OB_RS_ASYNC_RPC_PROXY_H_

#include "share/ob_rpc_struct.h"
#include "share/ob_srv_rpc_proxy.h"
#include "share/ob_common_rpc_proxy.h"
#include "share/rpc/ob_async_rpc_proxy.h"
#include "rpc/obrpc/ob_rpc_packet.h"
#include "rpc/obrpc/ob_rpc_result_code.h"
#include "rpc/obrpc/ob_rpc_proxy.h"

namespace oceanbase
{
namespace rootserver
{

RPC_F(obrpc::OB_MINOR_FREEZE, obrpc::ObMinorFreezeArg,
      obrpc::Int64, ObMinorFreezeProxy);
RPC_F(obrpc::OB_GET_WRS_INFO, obrpc::ObGetWRSArg,
    obrpc::ObSrvRpcProxy::ObRpc<obrpc::OB_GET_WRS_INFO>::Response, ObGetWRSProxy);
RPC_F(obrpc::OB_CHECK_SCHEMA_VERSION_ELAPSED, obrpc::ObCheckSchemaVersionElapsedArg,
    obrpc::ObCheckSchemaVersionElapsedResult, ObCheckSchemaVersionElapsedProxy);
RPC_F(obrpc::OB_CHECK_MEMTABLE_CNT, obrpc::ObCheckMemtableCntArg,
      obrpc::ObCheckMemtableCntResult, ObCheckMemtableCntProxy);
RPC_F(obrpc::OB_CHECK_MEDIUM_INFO_LIST_CNT, obrpc::ObCheckMediumCompactionInfoListArg,
      obrpc::ObCheckMediumCompactionInfoListResult, ObCheckMediumCompactionInfoListProxy);
RPC_F(obrpc::OB_DDL_BUILD_SINGLE_REPLICA_REQUEST, obrpc::ObDDLBuildSingleReplicaRequestArg,
    obrpc::ObDDLBuildSingleReplicaRequestResult, ObDDLBuildSingleReplicaRequestProxy);
RPC_F(obrpc::OB_CHECK_MODIFY_TIME_ELAPSED, obrpc::ObCheckModifyTimeElapsedArg,
    obrpc::ObSrvRpcProxy::ObRpc<obrpc::OB_CHECK_MODIFY_TIME_ELAPSED>::Response, ObCheckCtxCreateTimestampElapsedProxy);
RPC_F(obrpc::OB_PARTITION_STOP_WRITE, obrpc::Int64,
    obrpc::ObSrvRpcProxy::ObRpc<obrpc::OB_PARTITION_STOP_WRITE>::Response, ObRpcStopWriteProxy);
RPC_F(obrpc::OB_PARTITION_CHECK_LOG, obrpc::Int64,
    obrpc::ObSrvRpcProxy::ObRpc<obrpc::OB_PARTITION_CHECK_LOG>::Response, ObRpcCheckLogProxy);
RPC_F(obrpc::OB_CHECK_FROZEN_SCN, obrpc::ObCheckFrozenScnArg,
    obrpc::ObSrvRpcProxy::ObRpc<obrpc::OB_CHECK_FROZEN_SCN>::Response, ObCheckFrozenScnProxy);
RPC_F(obrpc::OB_GET_MIN_SSTABLE_SCHEMA_VERSION, obrpc::ObGetMinSSTableSchemaVersionArg,
    obrpc::ObGetMinSSTableSchemaVersionRes, ObGetMinSSTableSchemaVersionProxy);
RPC_F(obrpc::OB_CREATE_TABLET, obrpc::ObBatchCreateTabletArg, obrpc::ObCreateTabletBatchRes, ObTabletCreatorProxy);
RPC_F(obrpc::OB_DROP_TABLET, obrpc::ObBatchRemoveTabletArg, obrpc::ObRemoveTabletRes, ObTabletDropProxy);
RPC_F(obrpc::OB_SWITCH_SCHEMA, obrpc::ObSwitchSchemaArg, obrpc::ObSwitchSchemaResult, ObSwitchSchemaProxy);
RPC_F(obrpc::OB_BATCH_GET_TABLET_AUTOINC_SEQ, obrpc::ObBatchGetTabletAutoincSeqArg,
    obrpc::ObBatchGetTabletAutoincSeqRes, ObBatchGetTabletAutoincSeqProxy);
RPC_F(obrpc::OB_BATCH_SET_TABLET_AUTOINC_SEQ, obrpc::ObBatchSetTabletAutoincSeqArg,
    obrpc::ObBatchSetTabletAutoincSeqRes, ObBatchSetTabletAutoincSeqProxy);
RPC_F(obrpc::OB_CLEAR_TABLET_AUTOINC_SEQ_CACHE, obrpc::ObClearTabletAutoincSeqCacheArg,
    obrpc::ObSrvRpcProxy::ObRpc<obrpc::OB_CLEAR_TABLET_AUTOINC_SEQ_CACHE>::Response, ObClearTabletAutoincSeqCacheProxy);
RPC_F(obrpc::OB_GET_LS_SYNC_SCN, obrpc::ObGetLSSyncScnArg, obrpc::ObGetLSSyncScnRes, ObGetLSSyncScnProxy);
RPC_F(obrpc::OB_INIT_TENANT_CONFIG, obrpc::ObInitTenantConfigArg,
    obrpc::ObInitTenantConfigRes, ObInitTenantConfigProxy);
RPC_F(obrpc::OB_DDL_CHECK_TABLET_MERGE_STATUS, obrpc::ObDDLCheckTabletMergeStatusArg,
    obrpc::ObDDLCheckTabletMergeStatusResult, ObCheckTabletMergeStatusProxy);
RPC_F(obrpc::OB_BROADCAST_CONSENSUS_VERSION, obrpc::ObBroadcastConsensusVersionArg, obrpc::ObBroadcastConsensusVersionRes, ObBroadcstConsensusVersionProxy);
RPC_F(obrpc::OB_KILL_CLIENT_SESSION, obrpc::ObKillClientSessionArg, obrpc::ObKillClientSessionRes, ObKillClientSessionProxy);
RPC_F(obrpc::OB_FLUSH_LS_ARCHIVE, obrpc::ObFlushLSArchiveArg, obrpc::Int64, ObFlushLSArchiveProxy);
RPC_F(obrpc::OB_KILL_QUERY_CLIENT_SESSION, obrpc::ObKillQueryClientSessionArg, obrpc::Int64, ObKillQueryClientSessionProxy);
RPC_F(obrpc::OB_LOAD_TENANT_TABLE_SCHEMA, obrpc::ObLoadTenantTableSchemaArg, obrpc::ObRpcProxy::NoneT, ObLoadTenantTableSchemaProxy);

}//end namespace rootserver
}//end namespace oceanbase

#endif //OCEANBASE_ROOTSERVER_OB_RS_ASYNC_RPC_PROXY_H_
