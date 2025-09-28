/**
 * Copyright (c) 2021 OceanBase
 * OceanBase CE is licensed under Mulan PubL v2.
 * You can use this software according to the terms and conditions of the Mulan PubL v2.
 * You may obtain a copy of Mulan PubL v2 at:
 *          http://license.coscl.org.cn/MulanPubL-2.0
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND,
 * EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
 * MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
 * See the Mulan PubL v2 for more details.
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