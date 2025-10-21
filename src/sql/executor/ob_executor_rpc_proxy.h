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

#ifndef OCEANBASE_SQL_EXECUTOR_OB_EXECUTOR_RPC_PROXY_
#define OCEANBASE_SQL_EXECUTOR_OB_EXECUTOR_RPC_PROXY_

#include "rpc/obrpc/ob_rpc_proxy.h"
#include "sql/executor/ob_task.h"
#include "sql/executor/ob_task_event.h"
#include "share/config/ob_server_config.h"
#include "observer/ob_server_struct.h"

namespace oceanbase
{
namespace sql
{

struct ObEraseDtlIntermResultArg
{
  OB_UNIS_VERSION(1);
public:
  ObEraseDtlIntermResultArg() {}

  ObSEArray<uint64_t, 4> interm_result_ids_;

  TO_STRING_KV(K_(interm_result_ids));
};

}

namespace obrpc
{
class ObExecutorRpcProxy : public obrpc::ObRpcProxy
{
public:
  DEFINE_TO(ObExecutorRpcProxy);

  // For remote plan
  RPC_SS(@PR5 task_execute, obrpc::OB_REMOTE_EXECUTE, (sql::ObTask), common::ObScanner);
  RPC_SS(@PR5 remote_task_execute, obrpc::OB_REMOTE_SYNC_EXECUTE, (sql::ObRemoteTask), common::ObScanner);
  RPC_S(@PR5 task_kill, obrpc::OB_TASK_KILL, (sql::ObTaskID));

  // For DTL
  RPC_AP(@PR5 erase_dtl_interm_result, obrpc::OB_ERASE_DTL_INTERM_RESULT, (sql::ObEraseDtlIntermResultArg));
};
}
}
#endif /* OCEANBASE_SQL_EXECUTOR_OB_EXECUTOR_RPC_PROXY_ */
//// end of header file
