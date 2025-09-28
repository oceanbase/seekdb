/**
 * Copyright (c) 2021 OceanBase
 * OceanBase CE is licensed under Mulan PubL v2.
 * You can use this software according to the terms and conditions of the Mulan PubL v2.
 * You may obtain a copy of Mulan PubL v2 at:
 *          http://license.coscl.org.cn/MulanPubL-2.0
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND,
 * EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
 * MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
 * See the Mulan PubL v2 for more details.
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
