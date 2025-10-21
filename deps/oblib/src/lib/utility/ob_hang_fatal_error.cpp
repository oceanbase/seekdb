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

#include "lib/utility/ob_hang_fatal_error.h"
#include "common/ob_clock_generator.h"
#include "lib/utility/utility.h"

extern "C" {
void right_to_die_or_duty_to_live_c()
{
  ::oceanbase::common::right_to_die_or_duty_to_live();
}
}

namespace oceanbase
{
namespace common
{
_RLOCAL(bool, in_try_stmt);
int64_t g_fatal_error_thread_id = -1;

int64_t get_fatal_error_thread_id()
{
  return g_fatal_error_thread_id;
}
void set_fatal_error_thread_id(int64_t thread_id)
{
  g_fatal_error_thread_id = thread_id;
}

// To die or to live, it's a problem.
void right_to_die_or_duty_to_live()
{
  const ObFatalErrExtraInfoGuard *extra_info = ObFatalErrExtraInfoGuard::get_thd_local_val_ptr();
  set_fatal_error_thread_id(GETTID());
  while (true) {
    ObCStringHelper helper;
    const char *info = (NULL == extra_info) ? NULL : helper.convert(*extra_info);
    LOG_DBA_ERROR_V2(OB_SERVER_THREAD_PANIC, OB_ERR_THREAD_PANIC, "Trying so hard to die, info= ", info, ", lbt= ", lbt());
  #ifndef FATAL_ERROR_HANG
    if (in_try_stmt) {
      throw OB_EXCEPTION<OB_ERR_UNEXPECTED>();
    }
  #endif
    ob_usleep(60 * 1000 * 1000); // sleep 60s
  }
}

} //common
} //oceanbase
