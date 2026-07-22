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
// RS-layer serialization for operations converted from the former priority-10
// single-threaded RPC queue.
#ifndef OCEANBASE_ROOTSERVER_OB_RS_SERIAL_CALL_H_
#define OCEANBASE_ROOTSERVER_OB_RS_SERIAL_CALL_H_

#include "lib/lock/ob_mutex.h"
#include "lib/worker.h"          // THIS_WORKER
#include "lib/ob_errno.h"
#include "lib/oblog/ob_log_module.h"

namespace oceanbase {
namespace rootserver {

// One process-global lock preserves serialization for priority-10
// non-parallel RS operations without relying on ddl_epoch. Lock and timeout are orthogonal,
// but acquisition is bounded by THIS_WORKER's deadline so it never waits forever.
inline lib::ObMutex &rs_serial_mutex()
{
  static lib::ObMutex mutex;
  return mutex;
}

// serial_call: acquire the global RS serial lock (timed, bounded by the caller's
// deadline), run fn under it, release. On lock-acquire timeout returns the lock
// error (OB_TIMEOUT). fn must return an int ret code.
template <typename F>
inline int serial_call(F &&fn)
{
  int ret = rs_serial_mutex().lock(THIS_WORKER.get_timeout_ts());
  if (OB_SUCCESS != ret) {
    RS_LOG(WARN, "fail to acquire RS serial lock before its deadline", K(ret));
  } else {
    struct Unlock { ~Unlock() { rs_serial_mutex().unlock(); } } guard;
    ret = fn();
  }
  return ret;
}

} // namespace rootserver
} // namespace oceanbase
#endif // OCEANBASE_ROOTSERVER_OB_RS_SERIAL_CALL_H_
