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

#ifndef OCEANBASE_ROOTSERVER_OB_RECCOVERY_LS_SERVICE_H
#define OCEANBASE_ROOTSERVER_OB_RECCOVERY_LS_SERVICE_H
#include "lib/thread/ob_reentrant_thread.h"//ObRsReentrantThread
#include "logservice/ob_log_base_type.h"//ObIRoleChangeSubHandler ObICheckpointSubHandler ObIReplaySubHandler
#include "logservice/palf/lsn.h"//palf::LSN
#include "rootserver/ob_primary_ls_service.h" //ObTenantThreadHelper
#include "lib/lock/ob_spin_lock.h" //ObSpinLock
#include "storage/tx/ob_multi_data_source.h" //ObTxBufferNode

namespace oceanbase
{
namespace obrpc
{
class  ObSrvRpcProxy;
}
namespace common
{
class ObMySQLProxy;
class ObISQLClient;
class ObMySQLTransaction;
}
namespace transaction
{
class ObTxBufferNode;
class ObTxLogBlock;
}
namespace share
{
class ObLSTableOperator;
struct ObLSAttr;
struct ObLSRecoveryStat;
class SCN;
class ObBalanceTaskHelper;
namespace schema
{
class ObMultiVersionSchemaService;
class ObTenantSchema;
}
}
namespace storage
{
class ObLS;
class ObLSHandle;
}
namespace logservice
{
class ObLogHandler;
}
namespace palf
{
class PalfHandleGuard;
}
namespace rootserver 
{
/*description:
 *Restores the status of each log stream according to the logs to which the
 *system log stream is synchronized, and updates the recovery progress of the
 *system log stream. This thread should only exist in the standby database or
 *the recovery process, and needs to be registered in the RestoreHandler.
 *This thread is only active on the leader of the system log stream under the user tenant*/
class ObRecoveryLSService : public ObTenantThreadHelper
{
public:
  ObRecoveryLSService() {}
  virtual ~ObRecoveryLSService() {}
  int init();
  void destroy();
  virtual void do_work() override;
  DEFINE_MTL_FUNC(ObRecoveryLSService)
};
}
}


#endif /* !OCEANBASE_ROOTSERVER_OB_RECCOVERY_LS_SERVICE_H */
