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

#define USING_LOG_PREFIX STORAGE

#include "ob_lob_retry.h"
#include "share/interrupt/ob_global_interrupt_call.h"
#include "storage/lob/ob_lob_location.h"

namespace oceanbase
{
namespace storage
{

int ObLobRetryUtil::check_need_retry(ObLobAccessParam &param, const int error_code, const int retry_cnt, bool &need_retry)
{
  int ret = OB_SUCCESS;
  if (param.from_rpc_ && ! param.enable_remote_retry_) {
    need_retry = false;
    LOG_WARN("can not retry because from rpc", K(ret), KR(ret), K(error_code), KR(error_code), K(retry_cnt), K(need_retry), K(param));
  } else if (! is_remote_ret_can_retry(error_code)) {
    LOG_WARN("can not retry error code", K(ret), KR(ret), K(error_code), KR(error_code), K(retry_cnt), K(need_retry), K(param));
  } else if (ObTimeUtility::current_time() > param.timeout_) {
    need_retry = false;
    ret = OB_TIMEOUT;
    int64_t cur_time = ObTimeUtility::current_time();
    LOG_WARN("[LOB RETRY] query timeout", K(cur_time), K(param.timeout_), K(ret));
  } else if (IS_INTERRUPTED()) {
    need_retry = false;
    LOG_INFO("[LOB RETRY] Retry is interrupted by worker interrupt signal", KR(ret), K(error_code), KR(error_code), K(retry_cnt), K(need_retry));
  } else if (lib::Worker::WS_OUT_OF_THROTTLE == THIS_WORKER.check_wait()) {
    need_retry = false;
    ret = OB_KILLED_BY_THROTTLING;
    LOG_INFO("[LOB RETRY] Retry is interrupted by worker check wait", K(ret), KR(ret), K(error_code), KR(error_code), K(retry_cnt), K(need_retry));
  } else {
    need_retry = true;
    switch (error_code) {
      case  OB_LS_NOT_EXIST: // single tenant never drops tenant; fall through to refresh location
      case	OB_REPLICA_NOT_READABLE:
      case  OB_RPC_CONNECT_ERROR:
      case  OB_RPC_SEND_ERROR:
      case  OB_RPC_POST_ERROR:
      case  OB_NOT_MASTER:
      case  OB_NO_READABLE_REPLICA:
      case  OB_TABLET_NOT_EXIST:
      case  OB_LS_OFFLINE: {
        if (!need_retry) {
        } else if (OB_FAIL(ObLobLocationUtil::lob_refresh_location(param, error_code, retry_cnt))) {
          LOG_WARN("fail to do refresh location", K(ret), K(error_code), K(retry_cnt), K(param));
          need_retry = false;
        }
        LOG_INFO("retry again", K(ret), KR(ret), K(error_code), KR(error_code), K(retry_cnt), K(need_retry), K(param));
        break;
      }
      default: {
        need_retry = false;
        LOG_WARN("unknow retry error_code, not retry", K(ret), KR(ret), K(error_code), KR(error_code), K(retry_cnt), K(need_retry));
      }
    }
  }
  return ret;
}

bool ObLobRetryUtil::is_remote_ret_can_retry(int ret)
{
  return (ret == OB_REPLICA_NOT_READABLE) || 
         (ret == OB_RPC_CONNECT_ERROR) ||
         (ret == OB_RPC_SEND_ERROR) || 
         (ret == OB_RPC_POST_ERROR) || 
         (ret == OB_NOT_MASTER) || 
         (ret == OB_NO_READABLE_REPLICA) || 
         (ret == OB_TABLET_NOT_EXIST) || 
         (ret == OB_LS_NOT_EXIST) ||
         (ret == OB_LS_OFFLINE);
}

} // storage
} // oceanbase
