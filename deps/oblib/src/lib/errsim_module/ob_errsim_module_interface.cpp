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
#include "ob_errsim_module_interface.h"

#include <stdio.h>
#include <string.h>
#include <pthread.h>
#include "lib/ob_define.h"

using namespace oceanbase::common;

namespace oceanbase {
namespace common {

int OB_WEAK_SYMBOL build_tenant_errsim_moulde(
    const int64_t config_version,
    const common::ObArray<ObFixedLengthString<ObErrsimModuleTypeHelper::MAX_TYPE_NAME_LENGTH>> &module_array,
    const int64_t percentage)
{
  int ret = OB_SUCCESS;
  UNUSED(config_version);
  UNUSED(module_array);
  UNUSED(percentage);
  return ret;
}

bool OB_WEAK_SYMBOL is_errsim_module(
    const ObErrsimModuleType::TYPE &type)
{
  bool b_ret = false;
  UNUSED(type);
  return b_ret;
}

int OB_WEAK_SYMBOL add_tenant_errsim_event(
    const ObTenantErrsimEvent &event)
{
  int ret = OB_SUCCESS;
  UNUSED(event);
  return ret;
}

} // common
} // oceanbase

