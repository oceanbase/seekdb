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

#ifndef OCEANBASE_SQL_OB_MEMORY_TRACKER_H
#define OCEANBASE_SQL_OB_MEMORY_TRACKER_H

#include "lib/alloc/alloc_func.h"

namespace oceanbase
{
namespace lib
{
class MemoryContext;

struct ObMemTracker
{
  ObMemTracker() :
    cache_mem_limit_(0), check_status_times_(0), try_check_tick_(0), mem_context_(nullptr)
  {}
  void reset()
  {
    cache_mem_limit_ = 0;
    check_status_times_ = 0;
    try_check_tick_ = 0;
    mem_context_ = nullptr;
  }

  int64_t cache_mem_limit_;
  uint16_t check_status_times_;
  uint16_t try_check_tick_;
  lib::MemoryContext *mem_context_;
};

class ObMemTrackerGuard
{
public:
  const static uint64_t DEFAULT_CHECK_STATUS_TRY_TIMES = 1024;
  const static uint64_t UPDATE_MEM_LIMIT_THRESHOLD = 512;
  ObMemTrackerGuard(lib::MemoryContext &mem_context)
  {
    mem_tracker_.reset();
    mem_tracker_.mem_context_ = &mem_context;
  }
  ~ObMemTrackerGuard()
  {
    mem_tracker_.reset();
  }
  static void update_mem_limit();
  static int check_status();
  static int try_check_status(int64_t check_try_times = DEFAULT_CHECK_STATUS_TRY_TIMES);

private:
  static thread_local ObMemTracker mem_tracker_;
};

} // end namespace lib
} // end namespace oceanbase

#define MEM_TRACKER_GUARD(mem_context)                                      \
oceanbase::lib::ObMemTrackerGuard mem_tracker_guard(mem_context);
#define RESET_TRY_CHECK_TICK                                                \
oceanbase::lib::ObMemTrackerGuard::reset_try_check_tick();
#define CHECK_MEM_STATUS()                                                  \
oceanbase::lib::ObMemTrackerGuard::check_status()
#define TRY_CHECK_MEM_STATUS(check_try_times)                               \
oceanbase::lib::ObMemTrackerGuard::try_check_status(check_try_times)

#endif /* OCEANBASE_SQL_OB_MEMORY_TRACKER_H */
