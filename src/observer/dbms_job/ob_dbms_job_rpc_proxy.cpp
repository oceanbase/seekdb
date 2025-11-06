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



#include "ob_dbms_job_rpc_proxy.h"
#include "ob_dbms_job_rpc_processor.h"

using namespace oceanbase::common;
using namespace oceanbase::share;
using namespace oceanbase::observer;

namespace oceanbase
{
namespace obrpc
{

OB_SERIALIZE_MEMBER(ObDBMSJobArg, tenant_id_, job_id_, server_addr_, master_addr_);
OB_SERIALIZE_MEMBER(ObDBMSJobResult, tenant_id_, job_id_, server_addr_, status_code_);

int ObDBMSJobRpcProxy::run_dbms_job(
  uint64_t tenant_id, uint64_t job_id, ObAddr server_addr, ObAddr master_addr)
{
  int ret = OB_SUCCESS;
  ObDBMSJobArg arg(tenant_id, job_id, server_addr, master_addr);
  ObRpcAPDBMSJobCB cb;
  CK (arg.is_valid());
  OZ (this->to(arg.server_addr_).by(arg.tenant_id_).run_dbms_job(arg, &cb), arg);
  return ret;
}

}/* ns obrpc*/
}/* ns oceanbase */
