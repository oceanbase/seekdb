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

#define USING_LOG_PREFIX COMMON

#include "lib/signal/ob_signal_struct.h"
#include <new>
#include "lib/atomic/ob_atomic.h"
#include "lib/coro/co_var.h"
#include "lib/utility/ob_defer.h"

namespace oceanbase
{
namespace common
{
const int SIG_STACK_SIZE = 16L<<10;
uint64_t g_rlimit_core = 0;

thread_local ObSqlInfo ObSqlInfoGuard::tl_sql_info;

} // namespace common
} // namespace oceanbase
