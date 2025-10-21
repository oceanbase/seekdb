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

#ifndef OCEABASE_MALLOC_TIME_MONITOR_H_
#define OCEABASE_MALLOC_TIME_MONITOR_H_
#include "lib/alloc/alloc_struct.h"
#include "lib/oblog/ob_log.h"
namespace oceanbase
{
namespace lib
{
class ObMallocTimeMonitor
{
public:
  static const int64_t WARN_THRESHOLD = 10000000;
  ObMallocTimeMonitor()
  {
    MEMSET(this, 0, sizeof(*this));
  }

  static ObMallocTimeMonitor &get_instance()
  {
    static ObMallocTimeMonitor instance;
    return instance;
  }

  void record_malloc_time(ObBasicTimeGuard& time_guard, const int64_t size, const ObMemAttr& attr)
  {
    const int64_t cost_time = time_guard.get_diff();
    if (OB_UNLIKELY(cost_time > WARN_THRESHOLD)) {
      const int64_t buf_len = 1024;
      char buf[buf_len] = {'\0'};
      int64_t pos = attr.to_string(buf, buf_len);
      (void)logdata_printf(buf, buf_len, pos, ", size=%ld, ", size);
      pos += time_guard.to_string(buf + pos, buf_len - pos);
      int64_t tid = GETTID();
      fprintf(stderr, "[%ld]OB_MALLOC COST TOO MUCH TIME, cost_time=%ld, %.*s\n", tid, cost_time, static_cast<int>(pos), buf);
    }
  }
  void print();
};
} // end of namespace lib
} // end of namespace oceanbase

#endif
