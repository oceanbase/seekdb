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

#ifndef OB_RC_H_
#define OB_RC_H_

#include "lib/ob_define.h"

namespace oceanbase
{
namespace lib
{
uint64_t current_tenant_id();
// The current resource_owner_id is tenant_id
uint64_t current_resource_owner_id();
} // end of namespace lib
} // end of namespace oceanbase

#endif // OB_RC_H_
