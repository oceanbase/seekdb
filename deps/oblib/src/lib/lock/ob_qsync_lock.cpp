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

#include "ob_qsync_lock.h"

namespace oceanbase
{
namespace common
{
int ObQSyncLock::init(const lib::ObMemAttr &mem_attr)
{
  UNUSED(mem_attr);
  return OB_SUCCESS;
}

void ObQSyncLock::destroy()
{
  // do nothing
}

int ObQSyncLock::rdlock()
{
  int ret = common::OB_SUCCESS;

  do {
    if (common::OB_EAGAIN == (ret = try_rdlock())) {
      sched_yield();
    }
  } while (common::OB_EAGAIN == ret);

  return ret;
}

void ObQSyncLock::rdunlock()
{
  qsync_.release_ref();
}

int ObQSyncLock::wrlock()
{
  do {
    if (!ATOMIC_BCAS(&write_flag_, 0, 1)) {
      sched_yield();
    } else {
      bool sync_success = false;
      for (int64_t i = 0; !sync_success && i < TRY_SYNC_COUNT; i++) {
        sync_success = qsync_.try_sync();
      }
      if (sync_success) {
        break;
      } else {
        ATOMIC_STORE(&write_flag_, 0);
        sched_yield();
      }
    }
  } while (true);
  return common::OB_SUCCESS;
}

void ObQSyncLock::wrunlock()
{
  ATOMIC_STORE(&write_flag_, 0);
}

int ObQSyncLock::try_rdlock()
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(0 != ATOMIC_LOAD(&write_flag_))) {
    ret = OB_EAGAIN;
  } else {
    const int64_t idx = qsync_.acquire_ref();
    if (OB_UNLIKELY(0 != ATOMIC_LOAD(&write_flag_))) {
      qsync_.release_ref(idx);
      ret = OB_EAGAIN;
    } else {
      // success, do nothing
    }
  }
  return ret;
}

int ObQSyncLock::try_wrlock()
{
  int ret = OB_SUCCESS;
  if (!ATOMIC_BCAS(&write_flag_, 0, 1)) {
    ret = OB_EAGAIN;
  } else {
    bool sync_success = false;
    for (int64_t i = 0; !sync_success && i < TRY_SYNC_COUNT; i++) {
      sync_success = qsync_.try_sync();
    }
    if (!sync_success) {
      ATOMIC_STORE(&write_flag_, 0);
      ret = OB_EAGAIN;
    }
  }
  return ret;
}
}
}
