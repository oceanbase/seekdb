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
int ObSimpleMemLimitGetter::add_tenant(
    const int64_t lower_limit,
    const int64_t upper_limit)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(false) ||
      OB_UNLIKELY(lower_limit < 0) ||
      OB_UNLIKELY(upper_limit < 0)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", K(ret),
             K(lower_limit), K(upper_limit));
  } else {
    ObTenantInfo tenant_info(lower_limit,
                             upper_limit);
    SpinWLockGuard guard(lock_);
    if (has_tenant_()) {
      // tenant is exist do nothing
    } else {
      tenant_info_ = tenant_info;
      has_tenant_value_ = true;
    }
  }
  return ret;
}

bool ObSimpleMemLimitGetter::has_tenant() const
{
  bool found = false;
  SpinRLockGuard guard(lock_);
  found = has_tenant_();
  return found;
}

bool ObSimpleMemLimitGetter::has_tenant_() const
{
  return has_tenant_value_;
}


int ObSimpleMemLimitGetter::get_tenant_mem_limit(
    int64_t &lower_limit,
    int64_t &upper_limit) const
{
  int ret = OB_SUCCESS;
  bool found = false;
  SpinRLockGuard guard(lock_);
  if (has_tenant_value_) {
    found = true;
    lower_limit = tenant_info_.mem_lower_limit_;
    upper_limit = tenant_info_.mem_upper_limit_;
  }
  if (!found) {
    ret = OB_ENTRY_NOT_EXIST;
    LOG_ERROR("tenant is not exist", K(ret));
  }
  return ret;
}

void ObSimpleMemLimitGetter::reset()
{
  has_tenant_value_ = false;
}

}
}
