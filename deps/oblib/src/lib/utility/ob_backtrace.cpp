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

#define USING_LOG_PREFIX LIB

#ifdef _WIN32
// Include Windows headers first, before any headers that might undefine OPTIONAL
#include <windows.h>
// Clang may not define OPTIONAL from specstrings.h; dbghelp.h requires it
#ifndef OPTIONAL
#define OPTIONAL
#endif
#include <dbghelp.h>
#pragma comment(lib, "dbghelp.lib")
#endif

#include "ob_backtrace.h"
#include "lib/utility/utility.h"

namespace oceanbase
{
namespace common
{
int light_backtrace(void **buffer, int size)
{
#ifdef _WIN32
  // The frame-pointer walking implementation below assumes the System V
  // x86_64 / AArch64 ABI in which RBP / X29 is a real frame pointer and each
  // frame is laid out as [saved_fp][return_addr][...]. On Windows x64 (MS
  // ABI) RBP is just an ordinary non-volatile register and can hold any
  // value the compiler wants (often a base address into a stack object), so
  // dereferencing it as a saved-FP chain reads arbitrary memory and crashes
  // with AccessViolation as soon as the chased value lands outside the
  // mapped stack region. Use the unwind-table based RtlCaptureStackBackTrace
  // (the kernel-mode-safe Win32 API) instead.
  if (OB_UNLIKELY(buffer == nullptr || size <= 0)) {
    return 0;
  }
  USHORT frames = ::CaptureStackBackTrace(/*FramesToSkip=*/1,
                                          static_cast<ULONG>(size),
                                          buffer,
                                          /*BackTraceHash=*/nullptr);
  return static_cast<int>(frames);
#else
  int64_t rbp = 0;
#if defined(__x86_64__)
  asm("mov %%rbp, %0" : "=r"(rbp));
#elif defined(__aarch64__)
  asm("mov %0, x29" : "=r"(rbp));
#endif
  return light_backtrace(buffer, size, rbp);
#endif
}

int light_backtrace(void **buffer, int size, int64_t rbp)
{
#ifdef _WIN32
  // The provided rbp is meaningless under the Windows x64 ABI; fall back to
  // the same unwind-table based capture as the 2-arg overload. This keeps
  // the signal-handler call site (which only fires on POSIX) compiling on
  // Windows without exposing it to the broken frame-pointer walk.
  (void)rbp;
  return light_backtrace(buffer, size);
#else
  int rv = 0;
  if (rv < size) {
    int (*fp)(void**, int, int64_t) = light_backtrace;
    buffer[rv++] = (void*)fp;
  }
  void *stack_addr = nullptr;
  size_t stack_size = 0;
  if (OB_LIKELY(OB_SUCCESS == get_stackattr(stack_addr, stack_size))) {
#define addr_in_stack(addr) (addr >= (int64_t)stack_addr && addr < (int64_t)stack_addr + stack_size)
    while (rbp != 0 && rv < size) {
      // Validate rbp itself before dereferencing it -- otherwise a bogus
      // saved-FP value silently chained from the previous iteration can
      // point at unmapped memory and the deref below would crash before
      // the addr_in_stack check on its loaded value can run.
      if (!addr_in_stack(rbp)) {
        break;
      }
#if defined(__aarch64__)
      if (!addr_in_stack(*(int64_t*)rbp) &&
          !FALSE_IT(rbp += 16) &&
          !addr_in_stack(*(int64_t*)rbp)) {
#else
      if (!addr_in_stack(*(int64_t*)rbp)) {
#endif
        break;
      } else {
        int64_t return_addr = rbp + 8;
        buffer[rv++] = (void*)*(int64_t*)return_addr;
        rbp = *(int64_t*)rbp;
      }
    }
#undef addr_in_stack
  }
  return rv;
#endif
}

int64_t get_rel_offset(int64_t addr)
{
  // seekdb is built as a non-PIE (ET_EXEC) executable; backtrace addresses are
  // link-time VMAs that addr2line accepts directly.
  return addr;
}

bool g_enable_backtrace = true;

#ifdef _WIN32
// Windows implementation of ob_backtrace using CaptureStackBackTrace
int _ob_backtrace(void** buffer, int size)
{
  if (!g_enable_backtrace) {
    return 0;
  }
  USHORT frames = CaptureStackBackTrace(0, size, buffer, NULL);
  return (int)frames;
}
#endif

constexpr int MAX_ADDRS_COUNT = 100;
RLOCAL(ByteBuf<LBT_BUFFER_LENGTH>, buffer);

char *lbt()
{
  void *addrs[MAX_ADDRS_COUNT];
  int size = ob_backtrace(addrs, MAX_ADDRS_COUNT);
  return parray(*&buffer, LBT_BUFFER_LENGTH, (int64_t *)addrs, size);
}

char *lbt(char *buf, int32_t len)
{
  void *addrs[MAX_ADDRS_COUNT];
  int size = ob_backtrace(addrs, MAX_ADDRS_COUNT);
  return parray(buf, len, (int64_t *)addrs, size);
}

char *parray(int64_t *array, int size)
{
  return parray(buffer, LBT_BUFFER_LENGTH, array, size);
}

char *parray(char *buf, int64_t len, int64_t *array, int size)
{
  //As used in lbt, and lbt used when print error log.
  //Can not print error log this function.
  if (NULL != buf && len > 0 && NULL != array) {
    int64_t pos = 0;
    int64_t count = 0;
    for (int64_t i = 0; i < size; i++) {
      int64_t addr = get_rel_offset(array[i]);
      if (0 == i) {
        count = snprintf(buf + pos, len - pos, "0x%lx", addr);
      } else {
        count = snprintf(buf + pos, len - pos, " 0x%lx", addr);
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
  return buf;
}

void addrs_to_offsets(void **buffer, int size)
{
  for (int64_t i = 0; i < size; i++) {
    buffer[i] = (void*)get_rel_offset((int64_t)buffer[i]);
  }
}

EXTERN_C_BEGIN
int ob_backtrace_c(void **buffer, int size)
{
  return ob_backtrace(buffer, size);
}
char *parray_c(char *buf, int64_t len, int64_t *array, int size)
{
  return parray(buf, len, array, size);
}
int64_t get_rel_offset_c(int64_t addr)
{
  return get_rel_offset(addr);
}
char *lbt_c()
{
  return lbt();
}
EXTERN_C_END

} // end namespace common
} // end namespace oceanbase
