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
