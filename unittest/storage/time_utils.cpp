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

#include "time_utils.h"
#include <stdlib.h>             /* exit */
#include <stdio.h>              /* printf */
#include <sched.h>              /* sched_**** */
#include <unistd.h>
#include <stdint.h>
#include <iostream>
#include <limits.h>
#include <time.h>

uint64_t get_cycle_count()
{
  unsigned int lo, hi;
  __asm__ __volatile__ ("rdtsc" : "=a" (lo), "=d" (hi));
  return ((uint64_t)hi << 32) | lo;
}


uint64_t get_time(int type)
{
  uint64_t ret = 0;
  struct timespec time;
  clock_gettime(type, &time);
  ret = time.tv_nsec + static_cast<uint64_t>(time.tv_sec) * 1000000000;
  return ret;
}

uint64_t my_get_time(TIME_TYPE type)
{
  uint64_t ret = 0;
  switch (type)
  {
  case CLOCK_CIRCLE: {
    ret = get_cycle_count();
    break;
  }
  case REAL: {
    ret = get_time(CLOCK_REALTIME);
    break;
  }
  case PROCESS: {
    ret = get_time(CLOCK_PROCESS_CPUTIME_ID);
    break;
  }
  case THREAD: {
    ret = get_time(CLOCK_THREAD_CPUTIME_ID);
    break;
  }
  default:
    break;
  }
  return ret;
}
