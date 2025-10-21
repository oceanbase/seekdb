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

#ifndef OB_RETRY_QUEUE_H
#define OB_RETRY_QUEUE_H


#include "lib/queue/ob_link_queue.h"
#include "rpc/ob_request.h"

namespace oceanbase
{
namespace omt
{
const uint64_t RETRY_QUEUE_TIMESTEP = 10 * 1000L;
class ObRetryQueue {
public:
  ObRetryQueue()
  {
    last_timestamp_ = common::ObTimeUtility::current_time();
  }
  enum { RETRY_QUEUE_SIZE = 256 };
  int push(rpc::ObRequest &req, const uint64_t timestamp);
  int pop(common::ObLink *&task, bool need_clear = false);

private:

  common::ObSpLinkQueue queue_[RETRY_QUEUE_SIZE];
  uint64_t last_timestamp_;
};

}  // omt
}  // oceanbase


#endif
