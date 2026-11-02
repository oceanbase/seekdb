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

#include "ob_object_storage_base.h"

namespace oceanbase
{
namespace common
{

thread_local int64_t ObObjectStorageTenantGuard::tl_timeout_us_ = OB_STORAGE_MAX_IO_TIMEOUT_US;

ObObjectStorageTenantGuard::ObObjectStorageTenantGuard(
    const int64_t timeout_us)
    : old_timeout_us_(tl_timeout_us_)
{
  tl_timeout_us_ = timeout_us;
}

ObObjectStorageTenantGuard::~ObObjectStorageTenantGuard()
{
  tl_timeout_us_ = old_timeout_us_;
}

int64_t ObObjectStorageTenantGuard::get_timeout_us()
{
  return tl_timeout_us_;
}

} // common
} // oceanbase
