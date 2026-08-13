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

#ifndef OCEANBASE_QUERY_DDL_OB_DDL_EXECUTION_GUARD_H_
#define OCEANBASE_QUERY_DDL_OB_DDL_EXECUTION_GUARD_H_

#include "lib/ob_errno.h"

namespace oceanbase
{
namespace query
{

class ObIDdlExecutionLimiter
{
public:
  virtual ~ObIDdlExecutionLimiter() = default;
  virtual int try_acquire_ddl_execution(int64_t cpu_quota_concurrency) = 0;
  virtual void release_ddl_execution() = 0;
};

class ObDdlExecutionGuard
{
public:
  explicit ObDdlExecutionGuard(ObIDdlExecutionLimiter &limiter)
      : limiter_(&limiter), acquired_(false)
  {}
  explicit ObDdlExecutionGuard(ObIDdlExecutionLimiter *limiter)
      : limiter_(limiter), acquired_(false)
  {}
  ObDdlExecutionGuard(const ObDdlExecutionGuard &) = delete;
  ObDdlExecutionGuard &operator=(const ObDdlExecutionGuard &) = delete;

  ~ObDdlExecutionGuard()
  {
    if (acquired_ && nullptr != limiter_) {
      limiter_->release_ddl_execution();
    }
  }

  int try_acquire(const int64_t cpu_quota_concurrency)
  {
    int ret = common::OB_NOT_INIT;
    if (acquired_) {
      ret = common::OB_INIT_TWICE;
    } else if (nullptr == limiter_) {
      ret = common::OB_NOT_INIT;
    } else {
      ret = limiter_->try_acquire_ddl_execution(cpu_quota_concurrency);
      acquired_ = common::OB_SUCCESS == ret;
    }
    return ret;
  }

private:
  ObIDdlExecutionLimiter *limiter_;
  bool acquired_;
};

} // namespace query
} // namespace oceanbase

#endif // OCEANBASE_QUERY_DDL_OB_DDL_EXECUTION_GUARD_H_
