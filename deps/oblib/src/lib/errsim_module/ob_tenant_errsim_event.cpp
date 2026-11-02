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
#include "ob_tenant_errsim_event.h"

namespace oceanbase
{
using namespace lib;

namespace common
{

ObTenantErrsimEvent::ObTenantErrsimEvent()
    : timestamp_(0),
      type_(),
      errsim_error_(OB_SUCCESS),
      backtrace_()
{
}

void ObTenantErrsimEvent::reset()
{
  timestamp_ = 0;
  type_.reset();
  errsim_error_ = OB_SUCCESS;
  backtrace_.reset();
}

bool ObTenantErrsimEvent::is_valid() const
{
  return timestamp_ > 0 && type_.is_valid() && !backtrace_.is_empty();
}

void ObTenantErrsimEvent::build_event(const int32_t result)
{
  timestamp_ = ObTimeUtil::current_time();
  errsim_error_ = result;
  lbt(backtrace_.ptr(), backtrace_.capacity());
#ifdef ERRSIM
  type_ = THIS_WORKER.get_module_type();
#else
  ObErrsimModuleType tmp_type(ObErrsimModuleType::ERRSIM_MODULE_NONE);
  type_ = tmp_type;
#endif
}

} //common
} //oceanbase
