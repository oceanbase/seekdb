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

#ifndef OCEANBASE_LOGSERVICE_OB_LOG_RPC_PROXY_
#define OCEANBASE_LOGSERVICE_OB_LOG_RPC_PROXY_

#include "ob_log_rpc_req.h"
#include "rpc/obrpc/ob_rpc_proxy.h"                             // ObRpcProxy

namespace oceanbase
{
namespace obrpc
{

class ObLogServiceRpcProxy : public obrpc::ObRpcProxy
{
public:
  DEFINE_TO(ObLogServiceRpcProxy);
  RPC_S(PR3 send_log_config_change_cmd, OB_LOG_CONFIG_CHANGE_CMD,
      (logservice::LogConfigChangeCmd), logservice::LogConfigChangeCmdResp);
  RPC_AP(PR3 send_server_probe_req, OB_LOG_ARB_PROBE_MSG,
      (logservice::LogServerProbeMsg));
  RPC_AP(PR3 send_change_access_mode_cmd, OB_LOG_CHANGE_ACCESS_MODE_CMD,
      (logservice::LogChangeAccessModeCmd));
  RPC_AP(PR3 send_log_flashback_msg, OB_LOG_FLASHBACK_CMD,
      (logservice::LogFlashbackMsg));
#ifdef OB_BUILD_SHARED_STORAGE
  RPC_AP(PR3 acquire_log_rebuild_info, OB_LOG_ACQUIRE_REBUILD_INFO,
      (logservice::LogAcquireRebuildInfoMsg));
#endif
#ifdef OB_BUILD_ARBITRATION
  RPC_S(PR5 create_arb, OB_CREATE_ARB, (obrpc::ObCreateArbArg), obrpc::ObCreateArbResult);
  RPC_S(PR5 delete_arb, OB_DELETE_ARB, (obrpc::ObDeleteArbArg), obrpc::ObDeleteArbResult);
#endif
  RPC_S(PR3 get_palf_stat, OB_LOG_GET_PALF_STAT,
      (logservice::LogGetPalfStatReq), logservice::LogGetPalfStatResp);
#ifdef OB_BUILD_ARBITRATION
  RPC_S(PR5 log_probe_rs, OB_LOG_PROBE_RS, (logservice::LogProbeRsReq), logservice::LogProbeRsResp);
#endif       
  RPC_S(PR3 get_ls_ckpt, OB_LOG_GET_LS_CKPT,
      (logservice::LogGetCkptReq), logservice::LogGetCkptResp);
  RPC_AP(PR3 sync_base_lsn, OB_LOG_SYNC_BASE_LSN_REQ,
         (logservice::LogSyncBaseLSNReq));
};

} // end namespace obrpc
} // end namespace oceanbase

#endif


