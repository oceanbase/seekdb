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
#ifndef OCEANBASE_LOG_FETCHER_LOG_HANDLER_H_
#define OCEANBASE_LOG_FETCHER_LOG_HANDLER_H_

#include "logservice/palf/log_group_entry.h"
#include "logservice/palf/lsn.h"
#include "share/ob_ls_id.h"  // ObLSID
#include "ob_log_fetcher_switch_info.h"  // KickOutInfo
#include "ob_log_fetch_stat_info.h"  // TransStatInfo

namespace oceanbase
{
namespace logfetcher
{
class ILogFetcherHandler
{
public:
  // Implementation for handling the LogGroupEntry
  //
  // @param [in]  tenant_id       Tenant ID
  // @param [in]  ls_id           LS ID
  // @param [in]  proposal_id     proposal id
  // @param [in]  group_start_lsn LogGroupEntry start LSN
  // @param [in]  group_entry     LogGroupEntry Log
  // @param [in]  buffer          Buffer of the LogGroupEntry Log
  // @param [in]  ls_fetch_ctx    LogFetcher LSCtx
  // @param [out] KickOutInfo     Set the reason for the failure, and then LogFetcher will switch the machine
  //   and try fetching log for high availability and real-time.
  // @param [out] StatInfo        Some statistics
  //
  // @retval OB_SUCCESS         Handle the GroupLogEntry successfully
  // @retval OB_NEED_RETRY      For Physical standby, return OB_NEED_RETRY uniformly when the handle group entry failed.
  // such as:
  //     OB_NOT_MASTER(the leader of LS is switched)
  //     OB_EAGAIN(the sliding window is full)
  //     OB_LOG_OUTOF_DISK_SPACE
  //
  // @retval OB_IN_STOP_STATE   Stop and exit
  // @retval Other error codes  Fail
  virtual int handle_group_entry(
      const uint64_t tenant_id,
      const share::ObLSID &ls_id,
      const int64_t proposal_id,
      const palf::LSN &group_start_lsn,
      const palf::LogGroupEntry &group_entry,
      const char *buffer,
      void *ls_fetch_ctx,
      KickOutInfo &kick_out_info,
      TransStatInfo &tsi,
      volatile bool &stop_flag) = 0;
};

} // namespace logfetcher
} // namespace oceanbase

#endif
