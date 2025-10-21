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

#include <iostream>
#include "lib/thread_local/ob_tsi_factory.h"

using namespace oceanbase::common;
using namespace oceanbase::lib;
using namespace std;

static constexpr auto CNT = 1000L*1000L*100L;

void bench(string name, void (*func)())
{
  auto start_ts = ObTimeUtility::current_time();
  func();
  auto end_ts = ObTimeUtility::current_time();
  auto elapsed = end_ts - start_ts;
  if (end_ts > start_ts) {
    cout << name << ": " <<   CNT / elapsed << "Mps" << endl;
  }
}

void bench_thread_local()
{
  static __thread auto var = 0L;
  bench(__FUNCTION__, [] {
    for (int64_t i = 0; i < CNT; i++) {
      var += i << 1;
    }
  });
}

void bench_co_local()
{
  RLOCAL(int64_t, var);
  bench(__FUNCTION__, [] {
    for (int64_t i = 0; i < CNT; i++) {
      var += i << 1;
    }
  });
}

void bench_tsi()
{
  bench(__FUNCTION__, [] {
    for (int64_t i = 0; i < CNT; i++) {
      *GET_TSI0(int64_t) += i << 1;
    }
  });
}

void bench_csi()
{
  bench(__FUNCTION__, [] {
    for (int64_t i = 0; i < CNT; i++) {
      *GET_TSI(int64_t) += i << 1;
    }
  });
}

void bench_result()
{
  bench_co_local();
  bench_thread_local();
  bench_csi();
  bench_tsi();
  cout << endl;
}

int main()
{
  for (int i = 0; i < 5; i++) {
    bench_result();
  }
  return 0;
}
