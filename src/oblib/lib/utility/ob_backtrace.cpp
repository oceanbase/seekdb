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
#ifdef __EMSCRIPTEN__
// The pinned Emscripten SDK uses these same entry points for sanitizer stack
// traces (system/lib/libc/emscripten_internal.h). Wasm has no native FP chain.
extern "C" uintptr_t emscripten_stack_snapshot(void);
extern "C" uint32_t emscripten_stack_unwind_buffer(uintptr_t pc, uintptr_t *buffer, uint32_t depth);
#endif

namespace oceanbase
{
namespace common
{
int64_t get_rel_offset(int64_t addr)
{
  // seekdb is built as a non-PIE (ET_EXEC) executable; backtrace addresses are
  // link-time VMAs that addr2line accepts directly.
  return addr;
}

bool g_enable_backtrace = true;

#ifdef __EMSCRIPTEN__
int _ob_backtrace(void **buffer, int size)
{
  if (!g_enable_backtrace || buffer == nullptr || size <= 0) { return 0; }
  const uintptr_t pc = emscripten_stack_snapshot();
  return static_cast<int>(emscripten_stack_unwind_buffer(
      pc, reinterpret_cast<uintptr_t *>(buffer), static_cast<uint32_t>(size)));
}
#elif defined(_WIN32)
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
  int64_t offsets[MAX_ADDRS_COUNT];
  for (int i = 0; i < size; ++i) { offsets[i] = reinterpret_cast<uintptr_t>(addrs[i]); }
  return parray(*&buffer, LBT_BUFFER_LENGTH, offsets, size);
}

char *lbt(char *buf, int32_t len)
{
  void *addrs[MAX_ADDRS_COUNT];
  int size = ob_backtrace(addrs, MAX_ADDRS_COUNT);
  int64_t offsets[MAX_ADDRS_COUNT];
  for (int i = 0; i < size; ++i) { offsets[i] = reinterpret_cast<uintptr_t>(addrs[i]); }
  return parray(buf, len, offsets, size);
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
        count = snprintf(buf + pos, len - pos, "0x%" PRIx64, static_cast<uint64_t>(addr));
      } else {
        count = snprintf(buf + pos, len - pos, " 0x%" PRIx64, static_cast<uint64_t>(addr));
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

} // end namespace common
} // end namespace oceanbase
