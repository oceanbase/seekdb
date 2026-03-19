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
#ifndef OCEANBASE_LOGSERVICE_OB_REMOTE_LOG_WRITER_H_
#define OCEANBASE_LOGSERVICE_OB_REMOTE_LOG_WRITER_H_

#include "lib/utility/ob_macro_utils.h"
#include "share/ob_ls_id.h"
#include "share/ob_thread_pool.h"                           // ObThreadPool
#include "ob_log_restore_define.h"

namespace oceanbase
{
namespace storage
{
class ObLSService;
}
namespace share
{
class ObLSID;
class SCN;
}
namespace palf
{
class LSN;
}
namespace logservice
{
class ObFetchLogTask;
class ObRemoteFetchWorker;
class ObLogRestoreService;
class ObRemoteLogWriter : public share::ObThreadPool
{
public:
  ObRemoteLogWriter();
  virtual ~ObRemoteLogWriter();

public:
  int init(const uint64_t tenant_id,
      storage::ObLSService *ls_svr,
      ObLogRestoreService *restore_service,
      ObRemoteFetchWorker *worker);
  void destroy();
  int start();
  void stop();
  void wait();

private:
  void run1();
  void do_thread_task_();
  int foreach_ls_(const share::ObLSID &id);
  int submit_entries_(ObFetchLogTask &task);
  int submit_log_(const share::ObLSID &id, const int64_t proposal_id, const palf::LSN &lsn,
      const share::SCN &scn, const char *buf, const int64_t buf_size);
  int update_max_fetch_info_(const share::ObLSID &id, const int64_t proposal_id,
      const palf::LSN &lsn, const share::SCN &scn);
  int try_retire_(ObFetchLogTask *&task);
  void inner_free_task_(ObFetchLogTask &task);
  void report_error_(const share::ObLSID &id,
                     const int ret_code,
                     const palf::LSN &lsn,
                     const ObLogRestoreErrorContext::ErrorType &error_type);

private:
  bool inited_;
  uint64_t tenant_id_;
  storage::ObLSService *ls_svr_;
  ObLogRestoreService *restore_service_;
  ObRemoteFetchWorker *worker_;

private:
  DISALLOW_COPY_AND_ASSIGN(ObRemoteLogWriter);
};
} // namespace logservice
} // namespace oceanbase
#endif /* OCEANBASE_LOGSERVICE_OB_REMOTE_LOG_WRITER_H_ */
