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

#ifndef OCEANBASE_STORAGE_BLOCKSSTABLE_OB_IO_BENCH_CONTROLLER_H_
#define OCEANBASE_STORAGE_BLOCKSSTABLE_OB_IO_BENCH_CONTROLLER_H_

#include "lib/lock/ob_mutex.h"
#include "lib/thread/threads.h"
#include "share/io/ob_i_io_bench_controller.h"

namespace oceanbase
{
namespace storage
{

class ObIOBenchController final
    : public common::ObIIOBenchController,
      public lib::Threads
{
public:
  ObIOBenchController();
  ~ObIOBenchController() override;
  int start_io_bench() override;
  int get_benchmark_status(
      int64_t &start_ts,
      int64_t &finish_ts,
      int &ret_code) const override;
  void run1() override;

private:
  bool thread_inited_;
  lib::ObMutex running_mutex_;
  int64_t start_ts_;
  int64_t finish_ts_;
  int ret_code_;
};

}  // namespace storage
}  // namespace oceanbase

#endif  // OCEANBASE_STORAGE_BLOCKSSTABLE_OB_IO_BENCH_CONTROLLER_H_
