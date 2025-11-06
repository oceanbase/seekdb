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

#ifndef OCEANBASE_LOGSERVICE_OB_LOG_REQUEST_HANDLER_
#define OCEANBASE_LOGSERVICE_OB_LOG_REQUEST_HANDLER_

#include "lib/ob_errno.h"                           // OB_SUCCESS...
#include "logservice/ob_reporter_adapter.h"         // ObLogReporterAdapter
#include "logservice/palf_handle_guard.h"           // palf::PalfHandleGuard
#include "ob_log_rpc_req.h"                         // Req...

namespace oceanbase
{

namespace palf
{
class PalfEnv;
}

namespace obrpc
{
class ObLogServiceRpcProxy;
}

namespace logservice
{
#ifdef OB_BUILD_ARBITRATION
class ObArbitrationService;
#endif
class ObLogFlashbackService;
class ObLogHandler;
class ObLogReplayService;

class LogRequestHandler
{
public:
  LogRequestHandler();
  ~LogRequestHandler();
  template <typename ReqType, typename RespType>
  int handle_sync_request(const ReqType &req, RespType &resp);
  template <typename ReqType>
  int handle_request(const ReqType &req);
private:
  int get_palf_handle_guard_(const int64_t palf_id, palf::PalfHandleGuard &palf_handle_guard) const;
#ifdef OB_BUILD_ARBITRATION
  int get_arb_service_(ObArbitrationService *&arb_service) const;
#endif
  int get_self_addr_(common::ObAddr &self) const;
  int get_rpc_proxy_(obrpc::ObLogServiceRpcProxy *&rpc_proxy) const;
  int get_flashback_service_(ObLogFlashbackService *&flashback_srv) const;
  int get_replay_service_(ObLogReplayService *&replay_srv) const;
  int get_log_handler_(const int64_t palf_id,
                       storage::ObLSHandle &ls_handle,
                       logservice::ObLogHandler *&log_handler) const;
  int change_access_mode_(const LogChangeAccessModeCmd &req);
#ifdef OB_BUILD_SHARED_STORAGE
  int handle_acquire_log_rebuild_info_msg_(const LogAcquireRebuildInfoMsg &req);
#endif
};

class ConfigChangeCmdHandler{
public:
  explicit ConfigChangeCmdHandler(palf::PalfHandle *palf_handle)
  {
    if (NULL != palf_handle) {
      palf_handle_ = palf_handle;
    }
  }
  ~ConfigChangeCmdHandler()
  {
    palf_handle_ = NULL;
  }
  int handle_config_change_cmd(const LogConfigChangeCmd &req,
                               LogConfigChangeCmdResp &resp) const;
private:
  int get_reporter_(ObLogReporterAdapter *&reporter) const;
private:
  palf::PalfHandle *palf_handle_;
};
} // end namespace logservice
} // end namespace oceanbase

#endif
