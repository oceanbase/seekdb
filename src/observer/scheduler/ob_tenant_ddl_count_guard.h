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

#ifndef OCEANBASE_SHARE_SCHEDULER_OB_TENANT_DDL_COUNT_GUARD_H_
#define OCEANBASE_SHARE_SCHEDULER_OB_TENANT_DDL_COUNT_GUARD_H_

#include <stdint.h>
#include "lib/utility/ob_macro_utils.h"

namespace oceanbase
{
namespace share
{
namespace schema
{

// RAII guard: inc/dec a tenant's in-flight DDL count via observer omt MultiTenant.
// Lives in L9 (share/scheduler) because its definitions legally depend down on
// observer/omt (GCTX.omt_->inc/dec_tenant_ddl_count).
class ObTenantDDLCountGuard
{
public:
  ObTenantDDLCountGuard () : had_inc_ddl_(false) {}
  int try_inc_ddl_count(const int64_t cpu_quota_concurrency);
  ~ObTenantDDLCountGuard();
private:
  bool had_inc_ddl_;
  DISALLOW_COPY_AND_ASSIGN(ObTenantDDLCountGuard);
};

} // end schema
} // end share
} // end oceanbase

#endif // OCEANBASE_SHARE_SCHEDULER_OB_TENANT_DDL_COUNT_GUARD_H_
