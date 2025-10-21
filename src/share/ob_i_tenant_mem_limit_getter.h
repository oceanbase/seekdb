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

#ifndef OCEABASE_SHARE_OB_I_TENANT_MEM_LIMIT_GETTER_H_
#define OCEABASE_SHARE_OB_I_TENANT_MEM_LIMIT_GETTER_H_

#include "share/ob_define.h"
#include "lib/container/ob_iarray.h"

namespace oceanbase
{
namespace common
{

class ObITenantMemLimitGetter
{
public:
  virtual bool has_tenant(const uint64_t tenant_id) const = 0;
  virtual int get_all_tenant_id(ObIArray<uint64_t> &key) const = 0;
  virtual int get_tenant_mem_limit(const uint64_t tenant_id,
                                   int64_t &lower_limit,
                                   int64_t &upper_limit) const = 0;
};

} // common
} // oceanbase

#endif // OCEABASE_SHARE_OB_I_TENANT_MEM_LIMIT_MGR_H_
