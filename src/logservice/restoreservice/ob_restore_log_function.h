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
#ifndef OCEANBASE_LOGSERVICE_OB_RESTORE_LOG_FUNCTION_H_
#define OCEANBASE_LOGSERVICE_OB_RESTORE_LOG_FUNCTION_H_

#include "logservice/logfetcher/ob_log_fetch_stat_info.h"   // TransStatInfo
#include "logservice/palf/lsn.h"    // LSN
#include "logservice/palf/log_group_entry.h"   // LogGroupEntry
#include "logservice/logfetcher/ob_log_handler.h"   // ILogFetcherHandler
#include <cstdint>
namespace oceanbase
{
namespace share
{
struct ObLSID;
}
namespace storage
{
class ObLSService;
}
namespace logservice
{
class ObRestoreLogFunction : public logfetcher::ILogFetcherHandler
{
public:
  ObRestoreLogFunction();
  virtual ~ObRestoreLogFunction();
public:
  int init(const uint64_t tenant_id, storage::ObLSService *ls_svr);
  void destroy();
  void reset();

  virtual int handle_group_entry(
      const uint64_t tenant_id,
      const share::ObLSID &id,
      const int64_t proposal_id,
      const palf::LSN &group_start_lsn,
      const palf::LogGroupEntry &group_entry,
      const char *buffer,
      void *ls_fetch_ctx,
      logfetcher::KickOutInfo &kick_out_info,
      logfetcher::TransStatInfo &tsi,
      volatile bool &stop_flag) override final;

private:
  int process_(const share::ObLSID &id,
      const int64_t proposal_id,
      const palf::LSN &lsn,
      const share::SCN &scn,
      const char *buf,
      const int64_t buf_size,
      volatile bool &stop_flag);

private:
  bool inited_;
  uint64_t tenant_id_;
  storage::ObLSService *ls_svr_;
};
} // namespace logservice
} // namespace oceanbase
#endif /* OCEANBASE_LOGSERVICE_OB_RESTORE_LOG_FUNCTION_H_ */
