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

#ifndef OCEANBASE_STORAGE_TABLELOCK_OB_TABLE_LOCK_RPC_PROXY_H_
#define OCEANBASE_STORAGE_TABLELOCK_OB_TABLE_LOCK_RPC_PROXY_H_

#include "observer/ob_server_struct.h"
#include "share/rpc/ob_async_rpc_proxy.h"
#include "rpc/obrpc/ob_rpc_proxy.h"
#include "storage/tablelock/ob_table_lock_rpc_struct.h"

namespace oceanbase
{

namespace obrpc
{

RPC_F(OB_TABLE_LOCK_TASK, transaction::tablelock::ObTableLockTaskRequest,
        transaction::tablelock::ObTableLockTaskResult, ObTableLockProxy);
RPC_F(OB_BATCH_TABLE_LOCK_TASK, transaction::tablelock::ObLockTaskBatchRequest<transaction::tablelock::ObLockParam>,
      transaction::tablelock::ObTableLockTaskResult, ObBatchLockProxy);
RPC_F(OB_HIGH_PRIORITY_TABLE_LOCK_TASK, transaction::tablelock::ObTableLockTaskRequest,
        transaction::tablelock::ObTableLockTaskResult, ObHighPriorityTableLockProxy);
RPC_F(OB_HIGH_PRIORITY_BATCH_TABLE_LOCK_TASK, transaction::tablelock::ObLockTaskBatchRequest<transaction::tablelock::ObLockParam>,
      transaction::tablelock::ObTableLockTaskResult, ObHighPriorityBatchLockProxy);
RPC_F(OB_BATCH_REPLACE_TABLE_LOCK_TASK, transaction::tablelock::ObLockTaskBatchRequest<transaction::tablelock::ObReplaceLockParam>,
      transaction::tablelock::ObTableLockTaskResult, ObBatchReplaceLockProxy);
class ObTableLockRpcProxy: public obrpc::ObRpcProxy
{
public:
  DEFINE_TO(ObTableLockRpcProxy);
  RPC_S(PR5 lock_table, OB_OUT_TRANS_LOCK_TABLE, (transaction::tablelock::ObOutTransLockTableRequest));
  RPC_S(PR4 unlock_table, OB_OUT_TRANS_UNLOCK_TABLE, (transaction::tablelock::ObOutTransUnLockTableRequest));
};

}
}

#endif /* OCEANBASE_STORAGE_TABLELOCK_OB_TABLE_LOCK_RPC_PROXY_H_ */
