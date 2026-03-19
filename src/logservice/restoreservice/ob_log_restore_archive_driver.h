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
#ifndef OCEANBASE_LOGSERVICE_OB_LOG_RESTORE_ARCHIVE_DRIVER_H_
#define OCEANBASE_LOGSERVICE_OB_LOG_RESTORE_ARCHIVE_DRIVER_H_

#include <cstdint>
#include "lib/utility/ob_macro_utils.h"
#include "ob_remote_fetch_log_worker.h"    // ObRemoteFetchWorker
#include "ob_log_restore_driver_base.h"    // ObLogRestoreDriverBase

namespace oceanbase
{
namespace share
{
class ObLSID;
class SCN;
}

namespace storage
{
class ObLS;
class ObLSService;
}

namespace palf
{
struct LSN;
}
namespace logservice
{
class ObLogService;
using oceanbase::share::ObLSID;
using oceanbase::storage::ObLS;
using oceanbase::storage::ObLSService;
using oceanbase::palf::LSN;

// Archive driver, controls pulling logs from the archive source
// Pulling logs from the archive source is drived with tasks, each tasks covers a range of logs to restore.
//
// Archive driver generates tasks and submits to Remote Fetch Log Worker.
// Remote Fetch Log Workers pull logs from the archive source in parallel and submit log to palf serially.
//
// Also this component controls the upper limit scn to restore, which is the tenant replayable_scn plus 3 seconds.
class ObLogRestoreArchiveDriver : public ObLogRestoreDriverBase
{
public:
  ObLogRestoreArchiveDriver();
  ~ObLogRestoreArchiveDriver();

  int init(const uint64_t tenant_id, ObLSService *ls_svr, ObLogService *log_service, ObRemoteFetchWorker *worker);
  void destroy();

private:
  int do_fetch_log_(ObLS &ls);
  int check_need_schedule_(ObLS &ls, bool &need_schedule, int64_t &proposal_id,
      int64_t &version, LSN &lsn, int64_t &last_fetch_ts, int64_t &task_count);
  int check_need_delay_(const ObLSID &id, bool &need_delay);
  int get_fetch_log_base_lsn_(ObLS &ls, const LSN &max_fetch_lsn, const int64_t last_fetch_ts, share::SCN &scn, LSN &lsn);
  int get_palf_base_lsn_scn_(ObLS &ls, LSN &lsn, share::SCN &scn);
  int submit_fetch_log_task_(ObLS &ls, const share::SCN &scn, const LSN &lsn,
      const int64_t task_count, const int64_t proposal_id, const int64_t version);
  int do_submit_fetch_log_task_(ObLS &ls, const share::SCN &scn, const LSN &lsn, const int64_t size,
      const int64_t proposal_id, const int64_t version, bool &scheduled);
private:
  ObRemoteFetchWorker *worker_;

private:
  DISALLOW_COPY_AND_ASSIGN(ObLogRestoreArchiveDriver);
};
} // namespace logservice
} // namespace oceanbase

#endif /* OCEANBASE_LOGSERVICE_OB_LOG_RESTORE_ARCHIVE_DRIVER_H_ */
