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
// Process-local serialization for DDL operations that must not run in parallel.
#ifndef OCEANBASE_ROOTSERVER_OB_LOCAL_DDL_SERIAL_CALL_H_
#define OCEANBASE_ROOTSERVER_OB_LOCAL_DDL_SERIAL_CALL_H_

#include "lib/lock/ob_mutex.h"
#include "lib/worker.h"          // THIS_WORKER
#include "lib/ob_errno.h"
#include "lib/oblog/ob_log_module.h"

namespace oceanbase {
namespace rootserver {

// Acquisition is bounded by THIS_WORKER's deadline so callers never wait forever.
inline lib::ObMutex &local_ddl_serial_mutex()
{
  static lib::ObMutex mutex;
  return mutex;
}

// Acquire the process-local DDL lock, run fn, then release the lock. A timed-out
// acquisition returns the lock error. fn must return an int ret code.
template <typename F>
inline int local_ddl_serial_call(F &&fn)
{
  int ret = local_ddl_serial_mutex().lock(THIS_WORKER.get_timeout_ts());
  if (OB_SUCCESS != ret) {
  } else {
    struct Unlock { ~Unlock() { local_ddl_serial_mutex().unlock(); } } guard;
    ret = fn();
  }
  return ret;
}

} // namespace rootserver
} // namespace oceanbase
#endif // OCEANBASE_ROOTSERVER_OB_LOCAL_DDL_SERIAL_CALL_H_
