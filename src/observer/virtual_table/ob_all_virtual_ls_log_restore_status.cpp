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

#define USING_LOG_PREFIX SERVER

#include "ob_all_virtual_ls_log_restore_status.h"
#include "storage/tx_storage/ob_ls_service.h"   // ObLSService
#include "rootserver/standby/ob_recovery_ls_service.h" //ObLSRecoveryService

using namespace oceanbase::share; 

namespace oceanbase
{
namespace observer
{
ObVirtualLSLogRestoreStatus::ObVirtualLSLogRestoreStatus() {}

ObVirtualLSLogRestoreStatus::~ObVirtualLSLogRestoreStatus()
{
  destroy();
}

int ObVirtualLSLogRestoreStatus::init(omt::ObMultiTenant *omt)
{
  return OB_SUCCESS;
}

int ObVirtualLSLogRestoreStatus::inner_get_next_row(common::ObNewRow *&row)
{
  return OB_ITER_END;
}

void ObVirtualLSLogRestoreStatus::destroy() {}
} // namespace observer
} // namespace oceanbase
