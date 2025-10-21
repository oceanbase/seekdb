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

#ifndef OCEANBASE_LOGSERVICE_OB_REPLAY_HANDLER_
#define OCEANBASE_LOGSERVICE_OB_REPLAY_HANDLER_

#include "lib/lock/ob_tc_rwlock.h"
#include "logservice/ob_log_base_type.h"
#include "logservice/palf/lsn.h"
#include "share/ob_ls_id.h"

namespace oceanbase
{
namespace storage
{
class ObLS;
}
namespace share
{
class SCN;
}
namespace logservice
{
class ObLogService;
class ObReplayHandler
{
public:
  ObReplayHandler(storage::ObLS *ls_);
  ~ObReplayHandler();
  void reset();
public:
  int register_handler(const ObLogBaseType &type,
                       ObIReplaySubHandler *handler);
  void unregister_handler(const ObLogBaseType &type);

  int replay(const ObLogBaseType &type,
             const void *buffer,
             const int64_t nbytes,
             const palf::LSN &lsn,
             const share::SCN &scn);
private:
  typedef common::RWLock::WLockGuard WLockGuard;
  typedef common::RWLock::RLockGuard RLockGuard;
private:
  storage::ObLS *ls_;
  common::RWLock lock_;
  ObIReplaySubHandler *handlers_[ObLogBaseType::MAX_LOG_BASE_TYPE];
private:
  DISALLOW_COPY_AND_ASSIGN(ObReplayHandler);
};

} // namespace logservice
} // namespace oceanbase

#endif // OCEANBASE_LOGSERVICE_OB_REPLAY_HANDLER_
