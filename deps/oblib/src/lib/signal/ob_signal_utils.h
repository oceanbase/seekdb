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

#ifndef OCEANBASE_SIGNAL_UTILS_H_
#define OCEANBASE_SIGNAL_UTILS_H_

#include <stdio.h>
#include <setjmp.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <poll.h>
#include <sys/syscall.h>
#include <fcntl.h>
#include "lib/coro/co_var.h"
#include "lib/signal/ob_signal_struct.h"
#include "lib/utility/ob_macro_utils.h"
#include "lib/ob_errno.h"
#include "lib/utility/ob_defer.h"
#include "lib/ob_abort.h"
#include "util/easy_string.h"

namespace oceanbase
{
namespace common
{
typedef sigjmp_buf ObJumpBuf;
RLOCAL_EXTERN(ObJumpBuf *, g_jmp);
RLOCAL_EXTERN(ByteBuf<256>, crash_restore_buffer);

extern void crash_restore_handler(int, siginfo_t*, void*);

template<typename Function>
void do_with_crash_restore(Function &&func, bool &has_crash)
{
  has_crash = false;

  signal_handler_t handler_bak = get_signal_handler();
  ObJumpBuf *g_jmp_bak = g_jmp;
  ObJumpBuf jmp;
  g_jmp = &jmp;
  int js = sigsetjmp(*g_jmp, 1);
  if (0 == js) {
    get_signal_handler() = crash_restore_handler;
    func();
  } else if (1 == js) {
    has_crash = true;
  } else {
    // unexpected
    ob_abort();
  }
  g_jmp = g_jmp_bak;
  get_signal_handler() = handler_bak;
}

template<typename Function>
void do_with_crash_restore(Function &&func, bool &has_crash, decltype(func()) &return_value)
{
  do_with_crash_restore([&]() { return_value = func(); }, has_crash);
}

int64_t safe_parray(char *buf, int64_t len, int64_t *array, int size);

} // namespace common
} // namespace oceanbase

extern "C" {
  int64_t safe_parray_c(char *buf, int64_t len, int64_t *array, int size);
}

#endif // OCEANBASE_SIGNAL_UTILS_H_
