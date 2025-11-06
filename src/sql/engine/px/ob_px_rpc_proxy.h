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

#ifndef OB_PX_RPC_PROXY_H
#define OB_PX_RPC_PROXY_H

#include "rpc/obrpc/ob_rpc_proxy.h"
#include "sql/engine/px/ob_dfo.h"
#include "share/config/ob_server_config.h"
#include "observer/ob_server_struct.h"
#include "sql/engine/px/ob_px_target_monitor_rpc.h"

namespace oceanbase {

namespace obrpc {


class ObPxRpcProxy
    : public ObRpcProxy
{
public:
  DEFINE_TO(ObPxRpcProxy);
  // init sqc rpc synchronously
  RPC_S(PR5 init_sqc, OB_PX_INIT_SQC, (sql::ObPxRpcInitSqcArgs), sql::ObPxRpcInitSqcResponse);
  RPC_S(PR5 init_task, OB_PX_INIT_TASK, (sql::ObPxRpcInitTaskArgs), sql::ObPxRpcInitTaskResponse);
  // init sqc rpc asynchronously
  RPC_AP(PR5 async_init_sqc, OB_PX_ASYNC_INIT_SQC, (sql::ObPxRpcInitSqcArgs), sql::ObPxRpcInitSqcResponse);
  // Single dfo scheduling rpc
  RPC_AP(PR5 fast_init_sqc, OB_PX_FAST_INIT_SQC, (sql::ObPxRpcInitSqcArgs), sql::ObPxRpcInitSqcResponse);
  // px resource monitoring
  RPC_S(PR5 fetch_statistics, OB_PX_TARGET_REQUEST, (sql::ObPxRpcFetchStatArgs), sql::ObPxRpcFetchStatResponse);
  RPC_AP(PR5 clean_dtl_interm_result, OB_CLEAN_DTL_INTERM_RESULT, (sql::ObPxCleanDtlIntermResArgs));
};

}  // obrpc
}  // oceanbase


#endif /* OB_PX_RPC_PROXY_H */
