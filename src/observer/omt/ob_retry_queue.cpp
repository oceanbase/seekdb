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

#define USING_LOG_PREFIX SERVER_OMT
#include "share/ob_define.h"
#include "ob_retry_queue.h"


using namespace oceanbase::common;
using namespace oceanbase::omt;
using namespace oceanbase::rpc;


int ObRetryQueue::push(ObRequest &req, const uint64_t timestamp)
{
  uint64_t idx = max(timestamp / RETRY_QUEUE_TIMESTEP, last_timestamp_ / RETRY_QUEUE_TIMESTEP + 2);
  int queue_idx = idx & (RETRY_QUEUE_SIZE - 1);
  return queue_[queue_idx].push(&req);
}

int ObRetryQueue::pop(ObLink *&task, bool need_clear)
{
  int ret = OB_ENTRY_NOT_EXIST;
  uint64_t curr_timestamp = ObTimeUtility::current_time();
  uint64_t idx = last_timestamp_ / RETRY_QUEUE_TIMESTEP;
  if (!need_clear) {
    int queue_idx = idx & (RETRY_QUEUE_SIZE - 1);
    while (last_timestamp_ <= curr_timestamp && OB_FAIL(queue_[queue_idx].pop(task))) {
      ATOMIC_FAA(&last_timestamp_, RETRY_QUEUE_TIMESTEP);
      queue_idx = (++idx) & (RETRY_QUEUE_SIZE - 1);
    }
  } else {
    int queue_idx = 0;
    while (queue_idx < RETRY_QUEUE_SIZE && OB_FAIL(queue_[queue_idx].pop(task))) {
      queue_idx++;
    }
  }
  return ret;
}

