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

#include "ob_dbms_job_utils.h"
#include "ob_dbms_job_executor.h"
#include "lib/oblog/ob_warning_buffer.h"
#include "share/ob_server_struct.h"
#include "sql/plan_cache/ob_plan_cache_util.h"
#include "sql/session/ob_sql_session_info.h"

namespace oceanbase
{
using namespace common;
using namespace common::sqlclient;
using namespace share::schema;
using namespace observer;
using namespace sql;

namespace dbms_job
{

int ObDBMSJobExecutor::init(
  common::ObMySQLProxy *sql_proxy, ObMultiVersionSchemaService *schema_service)
{
  int ret = OB_SUCCESS;
  if (inited_) {
    ret = OB_INIT_TWICE;
    LOG_WARN("job scheduler executor already init", K(inited_), K(ret));
  } else if (OB_ISNULL(sql_proxy_ = sql_proxy)
          || OB_ISNULL(schema_service_ = schema_service)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("sql proxy or schema service is null", K(sql_proxy), K(ret));
  } else if (OB_FAIL(job_utils_.init(sql_proxy_))) {
    LOG_WARN("fail to init action record", K(ret));
  } else {
    inited_ = true;
  }
  return ret;
}

int ObDBMSJobExecutor::run_dbms_job(
  ObDBMSJobInfo &job_info, ObIAllocator &allocator)
{
  int ret = OB_SUCCESS;
    UNUSEDx(job_info, allocator);
    ret = OB_NOT_SUPPORTED;
    LOG_WARN("not support", K(ret));
  return ret;
}

int ObDBMSJobExecutor::run_dbms_job(uint64_t job_id)
{
  int ret = OB_SUCCESS;
  ObDBMSJobInfo job_info;
  ObArenaAllocator allocator;

  THIS_WORKER.set_timeout_ts(INT64_MAX);

  OZ (job_utils_.get_dbms_job_info(job_id, allocator, job_info));

  if (OB_SUCC(ret)) {
    OZ (job_utils_.update_for_start(job_info));

    OZ (run_dbms_job(job_info, allocator));

    int tmp_ret = OB_SUCCESS;
    ObString errmsg = common::ob_get_tsi_err_msg(ret);
    if (errmsg.empty() && ret != OB_SUCCESS) {
      errmsg = ObString(strlen(ob_errpkt_strerror(ret)),
                        ob_errpkt_strerror(ret));
    }
    if ((tmp_ret = job_utils_.update_for_end(job_info, ret, errmsg)) != OB_SUCCESS) {
      LOG_WARN("update dbms job failed", K(tmp_ret), K(ret));
    }
    ret = OB_SUCCESS == ret ? tmp_ret : ret;
  }
  return ret;
}

}
}
