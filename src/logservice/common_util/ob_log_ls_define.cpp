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

#include "ob_log_ls_define.h"

namespace oceanbase
{
namespace logservice
{
int TenantLSID::compare(const TenantLSID &other) const
{
  int cmp_ret = 0;

  if (tenant_id_ > other.tenant_id_) {
    cmp_ret = 1;
  } else if (tenant_id_ < other.tenant_id_) {
    cmp_ret = -1;
  } else if (ls_id_ > other.ls_id_) {
    cmp_ret = 1;
  } else if (ls_id_ < other.ls_id_) {
    cmp_ret = -1;
  } else {
    cmp_ret = 0;
  }

  return cmp_ret;
}

TenantLSID &TenantLSID::operator=(const TenantLSID &other)
{
  this->tenant_id_ = other.get_tenant_id();
  this->ls_id_ = other.get_ls_id();
  return *this;
}

} // namespace logservice
} // namespace oceanbase
