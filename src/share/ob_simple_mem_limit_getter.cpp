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

#define USING_LOG_PREFIX COMMON

#include "share/ob_simple_mem_limit_getter.h"

namespace oceanbase
{
namespace common
{
int ObSimpleMemLimitGetter::set_memory_limit(
    const int64_t lower_limit,
    const int64_t upper_limit)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(lower_limit < 0) ||
      OB_UNLIKELY(upper_limit < 0)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", K(ret),
             K(lower_limit), K(upper_limit));
  } else {
    ObMemoryLimitInfo memory_limit_info(lower_limit,
                                        upper_limit);
    SpinWLockGuard guard(lock_);
    if (has_memory_limit_()) {
      // A memory limit is already configured.
    } else {
      memory_limit_info_ = memory_limit_info;
      has_memory_limit_value_ = true;
    }
  }
  return ret;
}

bool ObSimpleMemLimitGetter::has_memory_limit() const
{
  bool found = false;
  SpinRLockGuard guard(lock_);
  found = has_memory_limit_();
  return found;
}

bool ObSimpleMemLimitGetter::has_memory_limit_() const
{
  return has_memory_limit_value_;
}


int ObSimpleMemLimitGetter::get_memory_limit(
    int64_t &lower_limit,
    int64_t &upper_limit) const
{
  int ret = OB_SUCCESS;
  bool found = false;
  SpinRLockGuard guard(lock_);
  if (has_memory_limit_value_) {
    found = true;
    lower_limit = memory_limit_info_.mem_lower_limit_;
    upper_limit = memory_limit_info_.mem_upper_limit_;
  }
  if (!found) {
    ret = OB_ENTRY_NOT_EXIST;
    LOG_ERROR("memory limit is not configured", K(ret));
  }
  return ret;
}

void ObSimpleMemLimitGetter::reset()
{
  has_memory_limit_value_ = false;
}

}
}
