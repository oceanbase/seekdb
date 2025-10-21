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

#define USING_LOG_PREFIX RS
#include "ob_recovery_ls_service.h"

#include "logservice/ob_log_service.h"//open_palf
#ifdef OB_BUILD_LOG_STORAGE_COMPRESS
#include "logservice/ob_log_compression.h"
#endif
#include "rootserver/ob_tenant_info_loader.h" // ObTenantInfoLoader
#include "rootserver/ob_ls_recovery_reportor.h" //ObLSRecoveryReportor
#include "rootserver/ob_disaster_recovery_task_utils.h"//DisasterRecoveryUtils
#include "src/share/balance/ob_balance_task_helper_operator.h"//insert_new_ls
#include "share/ls/ob_ls_life_manager.h"            //ObLSLifeManger
#include "share/ob_upgrade_utils.h"  // ObUpgradeChecker
#include "share/ob_global_stat_proxy.h" // ObGlobalStatProxy
#include "rootserver/tenant_snapshot/ob_tenant_snapshot_util.h" // ObTenantSnapshotUtil

namespace oceanbase
{
using namespace logservice;
using namespace transaction;
using namespace share;
using namespace storage;
using namespace palf;
namespace rootserver
{
ERRSIM_POINT_DEF(ERRSIM_END_TRANS_ERROR);

int ObRecoveryLSService::init()
{
  return OB_SUCCESS;
}

void ObRecoveryLSService::destroy()
{
  LOG_INFO("recovery ls service destory", KPC(this));
  ObTenantThreadHelper::destroy();
}

void ObRecoveryLSService::do_work()
{
  int ret = OB_SUCCESS;
}
}//end of namespace rootserver
}//end of namespace oceanbase
