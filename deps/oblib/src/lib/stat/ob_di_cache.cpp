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

#include "lib/stat/ob_di_cache.h"
#include "lib/stat/ob_session_stat.h"
#include "lib/utility/ob_tracepoint.h" // for ERRSIM_POINT_DEF

namespace oceanbase
{
namespace common
{

ObDISessionCollect::ObDISessionCollect()
  : session_id_(0),
    base_value_(),
    lock_()
{
}

ObDISessionCollect::~ObDISessionCollect()
{
}


ObDITenantCollect::ObDITenantCollect(ObIAllocator *allocator)
  : tenant_id_(0),
    last_access_time_(0),
    base_value_(allocator)
{
}

ObDITenantCollect::~ObDITenantCollect()
{
}


}
}
