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

#ifndef OB_MULTI_LEVEL_QUEUE_H
#define OB_MULTI_LEVEL_QUEUE_H

#include "lib/queue/ob_priority_queue.h"
#include "rpc/ob_request.h"
/// TODO remove multi level queue
#define MULTI_LEVEL_QUEUE_SIZE (1)
#define MULTI_LEVEL_THRESHOLD (2)
#define GROUP_MULTI_LEVEL_THRESHOLD (1)

namespace oceanbase
{
namespace omt
{


class ObMultiLevelQueue {
public:
  void set_limit(const int64_t limit);
  int push(rpc::ObRequest &req, const int32_t level, const int32_t prio);
  int pop(common::ObLink *&task, const int32_t level, const int64_t timeout_us);
  int pop_timeup(common::ObLink *&task, const int32_t level, const int64_t timeout_us);
  int try_pop(common::ObLink *&task, const int32_t level);
  int64_t get_size(const int32_t level) const;
  int64_t get_total_size() const;
  common::ObPriorityQueue<1>* get_pq_queue(const int32_t level) { return &queue_[level]; }
  int64_t to_string(char *buf, const int64_t buf_len) const
  {
    int64_t pos = 0;
    common::databuff_printf(buf, buf_len, pos, "total_size=%ld ", get_total_size());
    for(int i = 0; i < MULTI_LEVEL_QUEUE_SIZE; i++) {
      common::databuff_printf(buf, buf_len, pos, "queue[%d]=%ld ", i, queue_[i].size());
    }
    return pos;
  }
private:
  common::ObPriorityQueue<1> queue_[MULTI_LEVEL_QUEUE_SIZE];
};

}  // omt
}  // oceanbase

#endif
