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
#include <linux/futex.h>
#include <unistd.h>
#include <sys/syscall.h>

static int tc_futex(int *uaddr, int op, int val, const struct timespec *timeout, int *uaddr2, int val3) {
  return (int)syscall(SYS_futex, uaddr, op, val, timeout, uaddr2, val3);
}

static int tc_futex_wake(int *p, int val) {
  return tc_futex((int *)p, FUTEX_WAKE_PRIVATE, val, NULL, NULL, 0);
}

static struct timespec *tc_make_timespec(struct timespec *ts, int64_t us)
{
  ts->tv_sec = us / 1000000;
  ts->tv_nsec = 1000 * (us % 1000000);
  return ts;
}

static int tc_futex_wait(int *p, int val, const int64_t timeout_us) {
  int err = 0;
  struct timespec ts;
  if (0 != tc_futex((int *)p, FUTEX_WAIT_PRIVATE, val, tc_make_timespec(&ts, timeout_us), NULL, 0)) {
    err = errno;
  }
  return err;
}
