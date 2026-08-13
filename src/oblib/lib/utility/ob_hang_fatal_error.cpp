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
#include <cstdlib>
#include "lib/profile/ob_trace_id.h"
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
int64_t g_fatal_error_thread_id = -1;

int64_t get_fatal_error_thread_id()
{
  return g_fatal_error_thread_id;
}
void set_fatal_error_thread_id(int64_t thread_id)
{
  g_fatal_error_thread_id = thread_id;
}

void right_to_die_or_duty_to_live()
{
  const ObFatalErrExtraInfoGuard *extra_info = ObFatalErrExtraInfoGuard::get_thd_local_val_ptr();
  set_fatal_error_thread_id(GETTID());
  ObCStringHelper helper;
  const char *info = (NULL == extra_info) ? NULL : helper.convert(*extra_info);
  LOG_DBA_ERROR_V2(OB_SERVER_THREAD_PANIC, OB_ERR_THREAD_PANIC,
                   "Fatal invariant failed, info= ", info, ", lbt= ", lbt());
  std::abort();
}

} //common
} //oceanbase
