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

#include "lib/signal/ob_signal_utils.h"
#include <time.h>
#include <sys/time.h>
#include "lib/ob_errno.h"
#include "lib/utility/ob_macro_utils.h"
#include "lib/charset/ob_mysql_global.h"
#include "lib/signal/ob_libunwind.h"
#include "lib/ash/ob_active_session_guard.h"

extern "C" {
extern int64_t get_rel_offset_c(int64_t addr);
};

namespace oceanbase
{
namespace common
{
/* From mongodb */
_RLOCAL(ObJumpBuf *, g_jmp);
_RLOCAL(ByteBuf<256>, crash_restore_buffer);

void crash_restore_handler(int sig, siginfo_t *s, void *p)
{
  if (SIGSEGV == sig || SIGABRT == sig ||
      SIGBUS == sig || SIGFPE == sig) {
    int64_t len = 0;
#ifdef __x86_64__
    safe_backtrace(crash_restore_buffer, 255, &len);
#endif
    crash_restore_buffer[len++] = '\0';
    siglongjmp(*g_jmp, 1);
  } else {
    ob_signal_handler(sig, s, p);
  }
}

int64_t safe_parray(char *buf, int64_t len, int64_t *array, int size)
{
  int64_t pos = 0;
  if (NULL != buf && len > 0 && NULL != array) {
    int64_t count = 0;
    for (int64_t i = 0; i < size; i++) {
      int64_t addr = get_rel_offset_c(array[i]);
      if (0 == i) {
        count = lnprintf(buf + pos, len - pos, "0x%lx", addr);
      } else {
        count = lnprintf(buf + pos, len - pos, " 0x%lx", addr);
      }
      if (count >= 0 && pos + count < len) {
        pos += count;
      } else {
        // buf not enough
        break;
      }
    }
    buf[pos] = 0;
  }
  return pos;
}

} // namespace common
} // namespace oceanbase

extern "C" {
  int64_t safe_parray_c(char *buf, int64_t len, int64_t *array, int size)
  {
    return oceanbase::common::safe_parray(buf, len, array, size);
  }
}
