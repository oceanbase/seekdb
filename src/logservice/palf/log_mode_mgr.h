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

#ifndef OCEANBASE_LOGSERVICE_LOG_MODE_MGR_
#define OCEANBASE_LOGSERVICE_LOG_MODE_MGR_

#include "lib/lock/ob_spin_lock.h"              // SpinRWLock
#include "share/log/palf/log_define.h"                         // utils
#include "log_meta_info.h"                      // LogMembershipMeta

namespace oceanbase
{
namespace palf
{
class LogStateMgr;
class LogEngine;
class LogSlidingWindow;

class LogModeMgr
{
public:
  LogModeMgr();
  virtual ~LogModeMgr() { destroy(); }
  int init(const common::ObAddr &self,
           const LogModeMeta &log_mode_meta);
  virtual void destroy();
  virtual int get_access_mode(AccessMode &access_mode) const;
  virtual int get_access_mode_ref_scn(AccessMode &access_mode,
                                      share::SCN &ref_scn) const;
  bool can_append() const;
  int64_t to_string(char* buf, const int64_t buf_len) const
  {
    int64_t pos = 0;
    J_OBJ_START();
    J_KV(K_(self), K_(applied_mode_meta));
    J_OBJ_END();
    return pos;
  }

private:
  bool is_inited_;
  common::ObAddr self_;
  LogModeMeta applied_mode_meta_;
};
} // namespace palf
} // namespace oceanbase

#endif // OCEANBASE_LOGSERVICE_LOG_MODE_MGR_
