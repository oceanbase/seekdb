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

#ifndef OCEANBASE_COMMON_OB_SIMPLE_MEM_LIMIT_GETTER_H_
#define OCEANBASE_COMMON_OB_SIMPLE_MEM_LIMIT_GETTER_H_

#include "lib/container/ob_se_array.h"
#include "lib/lock/ob_spin_rwlock.h"
#include "share/ob_i_tenant_mem_limit_getter.h"

namespace oceanbase
{
namespace common
{
class ObSimpleMemLimitGetter : public ObITenantMemLimitGetter
{
public:
  ObSimpleMemLimitGetter() : lock_(ObLatchIds::DEFAULT_SPIN_RWLOCK), has_tenant_value_(false) {}
  virtual ~ObSimpleMemLimitGetter() {}
  int add_tenant(const int64_t lower_limit,
                 const int64_t upper_limit);
  bool has_tenant() const override;
  int get_tenant_mem_limit(int64_t &lower_limit,
                           int64_t &upper_limit) const override;
  void reset();
private:
  bool has_tenant_() const;
private:
  struct ObTenantInfo
  {
    ObTenantInfo()
      : mem_lower_limit_(-1),
        mem_upper_limit_(-1) {}

    ObTenantInfo(int64_t lower_limit,
                 int64_t upper_limit)
      : mem_lower_limit_(lower_limit),
        mem_upper_limit_(upper_limit) {}

    TO_STRING_KV(K_(mem_lower_limit), K_(mem_upper_limit));

    int64_t mem_lower_limit_;
    int64_t mem_upper_limit_;
  };

private:
  SpinRWLock lock_;
  ObTenantInfo tenant_info_;
  bool has_tenant_value_;
};

} // common
} // oceanbase

#endif
