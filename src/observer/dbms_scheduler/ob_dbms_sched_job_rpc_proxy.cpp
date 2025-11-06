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



#include "ob_dbms_sched_job_rpc_proxy.h"
#include "ob_dbms_sched_job_rpc_processor.h"

using namespace oceanbase::common;
using namespace oceanbase::share;
using namespace oceanbase::observer;

namespace oceanbase
{
namespace obrpc
{

OB_SERIALIZE_MEMBER(ObDBMSSchedJobArg, tenant_id_, job_id_, server_addr_, master_addr_, is_oracle_tenant_, job_name_);
OB_SERIALIZE_MEMBER(ObDBMSSchedJobResult, tenant_id_, job_id_, server_addr_, status_code_);
OB_SERIALIZE_MEMBER(ObDBMSSchedStopJobArg, tenant_id_, job_name_, session_id_, rpc_send_time_);
OB_SERIALIZE_MEMBER(ObDBMSSchedPurgeArg, tenant_id_, rpc_send_time_);

int ObDBMSSchedJobRpcProxy::run_dbms_sched_job(
  uint64_t tenant_id, bool is_oracle_tenant, uint64_t job_id, ObString &job_name, ObAddr server_addr, ObAddr master_addr, int64_t group_id)
{
  int ret = OB_SUCCESS;
  ObDBMSSchedJobArg arg(tenant_id, job_id, server_addr, master_addr, is_oracle_tenant, job_name);
  ObRpcAPDBMSSchedJobCB cb;
  CK (arg.is_valid());
  OZ (this->to(arg.server_addr_).by(arg.tenant_id_).group_id(group_id).run_dbms_sched_job(arg, &cb), arg);
  return ret;
}

int ObDBMSSchedJobRpcProxy::stop_dbms_sched_job(
  uint64_t tenant_id, ObString &job_name, ObAddr server_addr, uint64_t session_id)
{
  int ret = OB_SUCCESS;
  ObDBMSSchedStopJobArg arg(tenant_id, job_name, session_id, ObTimeUtility::current_time());
  CK (arg.is_valid());
  OZ (this->to(server_addr).by(arg.tenant_id_).stop_dbms_sched_job(arg));
  return ret;
}

int ObDBMSSchedJobRpcProxy::purge_run_detail(
  uint64_t tenant_id, ObAddr server_addr)
{
  int ret = OB_SUCCESS;
  ObDBMSSchedPurgeArg arg(tenant_id, ObTimeUtility::current_time());
  ObRpcDBMSSchedPurgeCB cb;
  CK (arg.is_valid());
  OZ (this->to(server_addr).by(arg.tenant_id_).purge_run_detail(arg, &cb), arg);
  return ret;
}

}/* ns obrpc*/
}/* ns oceanbase */
