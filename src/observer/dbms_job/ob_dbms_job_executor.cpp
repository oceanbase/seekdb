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

#define USING_LOG_PREFIX SERVER

#include "ob_dbms_job_utils.h"
#include "ob_dbms_job_executor.h"


#include "observer/ob_inner_sql_connection_pool.h"

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

int ObDBMSJobExecutor::init_session(
  sql::ObSQLSessionInfo &session,
  ObSchemaGetterGuard &schema_guard,
  const ObString &tenant_name, uint64_t tenant_id,
  const ObString &database_name, uint64_t database_id,
  const ObUserInfo* user_info,
  ObExecEnv &exec_env)
{
  int ret = OB_SUCCESS;
  ObObj oracle_sql_mode;
  ObArenaAllocator *allocator = NULL;
  const bool print_info_log = true;
  const bool is_sys_tenant = true;
  ObPCMemPctConf pc_mem_conf;
  ObObj oracle_mode;
  oracle_mode.set_int(1);
  oracle_sql_mode.set_uint(ObUInt64Type, DEFAULT_ORACLE_MODE);
  OZ (session.init(1, 1, allocator));
  OX (session.set_inner_session());
  OZ (session.load_default_sys_variable(print_info_log, is_sys_tenant));
  OZ (session.update_max_packet_size());
  OZ (session.init_tenant(tenant_name.ptr(), tenant_id));
  OZ (session.load_all_sys_vars(schema_guard));
  OZ (session.update_sys_variable(share::SYS_VAR_SQL_MODE, oracle_sql_mode));
  OZ (session.update_sys_variable(share::SYS_VAR_OB_COMPATIBILITY_MODE, oracle_mode));
  OZ (session.update_sys_variable(share::SYS_VAR_NLS_DATE_FORMAT,
                                  ObTimeConverter::COMPAT_OLD_NLS_DATE_FORMAT));
  OZ (session.update_sys_variable(share::SYS_VAR_NLS_TIMESTAMP_FORMAT,
                                  ObTimeConverter::COMPAT_OLD_NLS_TIMESTAMP_FORMAT));
  OZ (session.update_sys_variable(share::SYS_VAR_NLS_TIMESTAMP_TZ_FORMAT,
                                  ObTimeConverter::COMPAT_OLD_NLS_TIMESTAMP_TZ_FORMAT));
  OZ (session.set_default_database(database_name));
  OZ (session.get_pc_mem_conf(pc_mem_conf));
  CK (OB_NOT_NULL(GCTX.sql_engine_));
  OX (session.set_database_id(database_id));
  OZ (session.set_user(
    user_info->get_user_name(), user_info->get_host_name_str(), user_info->get_user_id()));
  OX (session.set_user_priv_set(OB_PRIV_ALL | OB_PRIV_GRANT));
  OX (session.init_use_rich_format());

  OZ (exec_env.store(session));
  return ret;
}


int ObDBMSJobExecutor::run_dbms_job(
  uint64_t tenant_id, ObDBMSJobInfo &job_info, ObIAllocator &allocator)
{
  int ret = OB_SUCCESS;
    UNUSEDx(tenant_id, job_info, allocator);
    ret = OB_NOT_SUPPORTED;
    LOG_WARN("not support", K(ret));
  return ret;
}

int ObDBMSJobExecutor::run_dbms_job(uint64_t tenant_id, uint64_t job_id)
{
  int ret = OB_SUCCESS;
  ObDBMSJobInfo job_info;
  ObArenaAllocator allocator;

  THIS_WORKER.set_timeout_ts(INT64_MAX);

  OZ (job_utils_.get_dbms_job_info(tenant_id, job_id, allocator, job_info));

  if (OB_SUCC(ret)) {
    OZ (job_utils_.update_for_start(tenant_id, job_info));

    OZ (run_dbms_job(tenant_id, job_info, allocator));

    int tmp_ret = OB_SUCCESS;
    ObString errmsg = common::ob_get_tsi_err_msg(ret);
    if (errmsg.empty() && ret != OB_SUCCESS) {
      errmsg = ObString(strlen(ob_errpkt_strerror(ret, lib::is_oracle_mode())),
                        ob_errpkt_strerror(ret, lib::is_oracle_mode()));
    }
    if ((tmp_ret = job_utils_.update_for_end(tenant_id, job_info, ret, errmsg)) != OB_SUCCESS) {
      LOG_WARN("update dbms job failed", K(tmp_ret), K(ret));
    }
    ret = OB_SUCCESS == ret ? tmp_ret : ret;
  }
  return ret;
}

}
}
