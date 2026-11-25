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
#pragma once

#include "lib/lock/ob_mutex.h"
#include "lib/worker.h"

namespace oceanbase
{
namespace share
{

inline lib::ObMutex &local_ddl_serial_mutex()
{
  static lib::ObMutex mutex;
  return mutex;
}

// Use the same lock across all callers of serialized local DDL commands.
template <typename F>
inline int local_ddl_serial_call(F &&fn)
{
  auto &mutex = local_ddl_serial_mutex();
  int ret = mutex.lock(THIS_WORKER.get_timeout_ts());
  if (OB_SUCCESS == ret) {
    struct UnlockGuard
    {
      lib::ObMutex &mutex_;
      ~UnlockGuard() { mutex_.unlock(); }
    } guard{mutex};
    ret = fn();
  }
  return ret;
}

} // namespace share
} // namespace oceanbase
