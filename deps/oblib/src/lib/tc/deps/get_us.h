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

// static int64_t tc_get_us()
// {
//   struct timespec tp;
//   clock_gettime(CLOCK_REALTIME_COARSE, &tp);
//   //clock_gettime(CLOCK_REALTIME, &tp);
//   return tp.tv_sec * 1000000 + tp.tv_nsec/1000;
// }
#ifdef _WIN32
#include <windows.h>
#include <stdint.h>

static int clock_gettime_realtime(struct timespec *tp) {
    FILETIME ft;
    GetSystemTimePreciseAsFileTime(&ft);

    unsigned long long t = ((unsigned long long)ft.dwHighDateTime << 32) | ft.dwLowDateTime;
    t -= 116444736000000000ULL;
    tp->tv_sec  = t / 10000000ULL;
    tp->tv_nsec = (t % 10000000ULL) * 100;
    return 0;
}

#define CLOCK_REALTIME 0
#define clock_gettime(a, b) clock_gettime_realtime(b)
#endif

static int64_t tc_get_ns()
{
  struct timespec tp;
  clock_gettime(CLOCK_REALTIME, &tp);
  return tp.tv_sec * 1000000000 + tp.tv_nsec;
}
