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

#ifndef OCEANBASE_SHARE_IO_OB_I_IO_BENCH_CONTROLLER_H_
#define OCEANBASE_SHARE_IO_OB_I_IO_BENCH_CONTROLLER_H_

#include <stdint.h>

namespace oceanbase
{
namespace common
{

// Demand-owned seam for the Storage-backed IO benchmark.
class ObIIOBenchController
{
public:
  virtual ~ObIIOBenchController() = default;
  virtual int start_io_bench() = 0;
  virtual int get_benchmark_status(
      int64_t &start_ts,
      int64_t &finish_ts,
      int &ret_code) const = 0;
};

}  // namespace common
}  // namespace oceanbase

#endif  // OCEANBASE_SHARE_IO_OB_I_IO_BENCH_CONTROLLER_H_
