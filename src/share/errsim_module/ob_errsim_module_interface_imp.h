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

#ifndef SRC_SHARE_ERRSIM_MODULE_OB_TENANT_ERRSIM_MODULE_INTERFACE_IMP_H_
#define SRC_SHARE_ERRSIM_MODULE_OB_TENANT_ERRSIM_MODULE_INTERFACE_IMP_H_

#include "ob_tenant_errsim_module_mgr.h"
#include "lib/ob_define.h"
#include "lib/utility/ob_print_utils.h"
#include "common/errsim_module/ob_tenant_errsim_event.h"


namespace oceanbase
{
namespace common
{

int build_tenant_errsim_moulde(
    const uint64_t tenant_id,
    const int64_t config_version,
    const common::ObArray<ObFixedLengthString<ObErrsimModuleTypeHelper::MAX_TYPE_NAME_LENGTH>> &module_array,
    const int64_t percentage);
bool is_errsim_module(
    const uint64_t tenant_id,
    const ObErrsimModuleType::TYPE &type);
int add_tenant_errsim_event(
    const uint64_t tenant_id,
    const ObTenantErrsimEvent &event);


} // namespace common
} // namespace oceanbase
#endif
