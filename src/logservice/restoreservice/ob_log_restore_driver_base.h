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
#ifndef OCEANBASE_LOGSERVICE_OB_LOG_RESTORE_DRIVER_H_
#define OCEANBASE_LOGSERVICE_OB_LOG_RESTORE_DRIVER_H_

#include "share/scn.h"
#include "storage/ls/ob_ls.h"
#include <cstdint>

namespace oceanbase
{
namespace storage
{
class ObLS;
class ObLSService;
}
namespace logservice
{
class ObLogService;
class ObLogRestoreDriverBase
{
  const int64_t FETCH_LOG_AHEAD_THRESHOLD_NS = 6 * 1000 * 1000 *1000L;  // 6s
public:
  ObLogRestoreDriverBase();
  virtual ~ObLogRestoreDriverBase();

  int init(const uint64_t tenant_id, ObLSService *ls_svr, ObLogService *log_service);
  void destroy();
  int do_schedule();
  int set_global_recovery_scn(const share::SCN &recovery_scn);
protected:
  virtual int do_fetch_log_(ObLS &ls) = 0;
  int check_replica_status_(storage::ObLS &ls, bool &can_fetch_log);
  int get_upper_resotore_scn(share::SCN &scn);
private:
  int check_fetch_log_unlimited_(bool &limit);
protected:
  bool inited_;
  uint64_t tenant_id_;
  storage::ObLSService *ls_svr_;
  ObLogService *log_service_;
  share::SCN global_recovery_scn_;
};
} // namespace logservice
} // namespace oceanbase
#endif /* OCEANBASE_LOGSERVICE_OB_LOG_RESTORE_DRIVER_H_ */
