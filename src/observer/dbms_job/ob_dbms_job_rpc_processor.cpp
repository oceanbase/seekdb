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

#include "ob_dbms_job_rpc_processor.h"


#include "ob_dbms_job_master.h"
#include "ob_dbms_job_executor.h"

using namespace oceanbase::common;
using namespace oceanbase::dbms_job;
using namespace oceanbase::share::schema;

namespace oceanbase
{
namespace obrpc
{

int ObRpcAPDBMSJobCB::process()
{
  int ret = OB_SUCCESS;
  ObDBMSJobResult &result = result_;
  ObDBMSJobMaster &job_master = ObDBMSJobMaster::get_instance();
  if (!job_master.is_inited()) {
    ret = OB_NOT_INIT;
    LOG_WARN("dbms job master not init", K(ret), K(job_master.is_inited()));
  } else if (!result.is_valid()) {
    ret = OB_INVALID_ERROR;
    LOG_WARN("dbms job result is invalid", K(ret), K(result));
  } else {
    LOG_INFO("dbms job run done!");
  }
  return ret;
}

int ObRpcRunDBMSJobP::process()
{
  int ret = OB_SUCCESS;
  const ObDBMSJobArg &arg = arg_;
  ObDBMSJobResult &result = result_;
  ObDBMSJobExecutor executor;
  result.set_tenant_id(arg.tenant_id_);
  result.set_job_id(arg.job_id_);
  result.set_server_addr(arg.server_addr_);

  LOG_INFO("dbms job run rpc process start", K(ret));

  if (!arg.is_valid()) {
    ret = OB_INVALID_ERROR;
    LOG_WARN("fail to get dbms job arg", K(ret), K(arg));
  } else if (OB_ISNULL(gctx_.sql_proxy_) || OB_ISNULL(gctx_.schema_service_)) {
    ret = OB_INVALID_ERROR;
    LOG_WARN("null ptr",
             K(ret), K(gctx_.sql_proxy_), K(gctx_.schema_service_));
  } else if (OB_FAIL(executor.init(gctx_.sql_proxy_, gctx_.schema_service_))) {
    LOG_WARN("fail to init dbms job executor", K(ret));
  } else if (OB_FAIL(executor.run_dbms_job(arg.tenant_id_, arg.job_id_))) {
    LOG_WARN("fail to executor dbms job", K(ret), K(arg));
  }
  LOG_INFO("dbms job run rpc process end", K(ret), K(arg_));
  result.set_status_code(ret);

  return ret;
}

}
} // namespace oceanbase
