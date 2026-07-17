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

#ifndef OB_FTS_PX_COORDINATOR_H_
#define OB_FTS_PX_COORDINATOR_H_

#include "lib/ob_errno.h"

namespace oceanbase
{
namespace storage
{

class ObThreadCond;

class ObFtsPxCoordinator
{
public:
  ObFtsPxCoordinator()
    : is_inited_(false), sample_finished_(false), cond_(nullptr)
  {
  }

  int init()
  {
    int ret = OB_SUCCESS;
    is_inited_ = true;
    return ret;
  }

  int wait_sample_finish(int64_t timeout_us)
  {
    int ret = OB_SUCCESS;
    return ret;
  }

  int notify_sample_finished()
  {
    int ret = OB_SUCCESS;
    sample_finished_ = true;
    return ret;
  }

  OB_INLINE bool is_sample_finished() const { return sample_finished_; }

  void destroy()
  {
    is_inited_ = false;
  }

private:
  bool is_inited_;
  bool sample_finished_;
  ObThreadCond *cond_;
};

} // end namespace storage
} // end namespace oceanbase

#endif /* OB_FTS_PX_COORDINATOR_H_ */
