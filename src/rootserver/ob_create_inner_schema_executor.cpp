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
#include "ob_create_inner_schema_executor.h"
#include "sql/engine/ob_exec_context.h"

using namespace oceanbase::common;
using namespace oceanbase::share;
using namespace oceanbase::share::schema;
using namespace oceanbase::rootserver;
using namespace oceanbase::obrpc;
using namespace oceanbase::sql;

int64_t ObCreateInnerSchemaTask::get_deep_copy_size() const
{
  return sizeof(*this);
}

ObAsyncTask *ObCreateInnerSchemaTask::deep_copy(char *buf, const int64_t buf_size) const
{
  ObAsyncTask *task = nullptr;
  int ret = OB_SUCCESS;
  const int64_t deep_copy_size = get_deep_copy_size();
  if (OB_ISNULL(buf) || buf_size < deep_copy_size) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid arguments", K(ret), KP(buf), K(deep_copy_size), K(buf_size));
  } else {
    task = new (buf) ObCreateInnerSchemaTask(*executor_);
  }
  return task;
}

int ObCreateInnerSchemaTask::process()
{
  int ret = OB_SUCCESS;
  const int64_t start = ObTimeUtility::current_time();
  LOG_INFO("[UPGRADE] start to execute create inner schema task", K(start));
  if (OB_ISNULL(executor_)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("error unexpected, executor must not be NULL", K(ret));
  } else if (OB_FAIL(executor_->execute())) {
    LOG_WARN("fail to execute create inner schema task", K(ret));
  }
  LOG_INFO("[UPGRADE] finish create inner schema task",
           K(ret), "cost_time", ObTimeUtility::current_time() - start);
  return ret;
}

ObCreateInnerSchemaExecutor::ObCreateInnerSchemaExecutor()
  : is_inited_(false), is_stopped_(false), execute_(false),
    rwlock_(ObLatchIds::CREATE_INNER_SCHEMA_EXECUTOR_LOCK), schema_service_(nullptr), sql_proxy_(nullptr),
    rpc_proxy_(nullptr)
{
}

int ObCreateInnerSchemaExecutor::init(
    share::schema::ObMultiVersionSchemaService &schema_service,
    common::ObMySQLProxy &sql_proxy,
    obrpc::ObCommonRpcProxy &rpc_proxy)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(is_inited_)) {
    ret = OB_INIT_TWICE;
    LOG_WARN("ObCreateInnerSchemaExecutor has been inited twice", K(ret));
  } else {
    schema_service_ = &schema_service;
    sql_proxy_ = &sql_proxy;
    is_inited_ = true;
    is_stopped_ = false;
    rpc_proxy_ = &rpc_proxy;
  }
  return ret;
}

int ObCreateInnerSchemaExecutor::stop()
{
  int ret = OB_SUCCESS;
  const uint64_t WAIT_US = 100 * 1000L; //100ms
  const uint64_t MAX_WAIT_US = 10 * 1000 * 1000L; //10s
  const int64_t start = ObTimeUtility::current_time();
  {
    SpinWLockGuard guard(rwlock_);
    is_stopped_ = true;
  }
  while (OB_SUCC(ret)) {
    if (ObTimeUtility::current_time() - start > MAX_WAIT_US) {
      ret = OB_TIMEOUT;
      LOG_WARN("use too much time", K(ret), "cost_us", ObTimeUtility::current_time() - start);
    } else if (!execute_) {
      break;
    } else {
      ob_usleep(WAIT_US);
    }
  }
  return ret;
}

int ObCreateInnerSchemaExecutor::check_stop()
{
  int ret = OB_SUCCESS;
  SpinRLockGuard guard(rwlock_);
  if (!is_inited_) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", K(ret));
  } else if (is_stopped_) {
    ret = OB_CANCELED;
    LOG_WARN("executor should stopped", K(ret));
  }
  return ret;
}

void ObCreateInnerSchemaExecutor::start()
{
  SpinWLockGuard guard(rwlock_);
  is_stopped_ = false;
}

int ObCreateInnerSchemaExecutor::set_execute_mark()
{
  int ret = OB_SUCCESS;
  SpinWLockGuard guard(rwlock_);
  if (!is_inited_) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", K(ret));
  } else if (is_stopped_ || execute_) {
    ret = OB_OP_NOT_ALLOW;
    LOG_WARN("can't run job at the same time", K(ret), K(is_stopped_), K(execute_));
  } else {
    execute_ = true;
  }
  return ret;
}

int ObCreateInnerSchemaExecutor::execute()
{
  ObCurTraceId::init(GCONF.self_addr_);
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(!is_inited_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("ObCreateInnerSchemaExecutor has not been inited", K(ret));
  } else if (OB_FAIL(set_execute_mark())) {
    LOG_WARN("fail to set execute mark", K(ret));
  } else {
    int64_t job_id = OB_INVALID_ID;
    // unused
    // bool can_run_job = false;
    ObRsJobType job_type = ObRsJobType::JOB_TYPE_CREATE_INNER_SCHEMA;
    if (OB_FAIL(RS_JOB_CREATE_WITH_RET(job_id, job_type, *sql_proxy_, "tenant_id", 0))) {
      LOG_WARN("fail to create rs job", K(ret));
    } else if (job_id <= 0) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("job is is invalid", K(ret), K(job_id));
    } else if (OB_FAIL(do_create_inner_schema())) {
      LOG_WARN("fail to do create inner schema job", K(ret));
    }
    if (job_id > 0) {
      int tmp_ret = OB_SUCCESS;
      if (OB_SUCCESS != (tmp_ret = RS_JOB_COMPLETE(job_id, ret, *sql_proxy_))) {
        LOG_ERROR("fail to complete job", K(tmp_ret), K(ret), K(job_id));
                ret = OB_SUCCESS == ret ? tmp_ret : ret;
      }
    }
    // no need lock, because single-machine concurrency is prevented in the process
    execute_ = false;
  }
  return ret;
}

int ObCreateInnerSchemaExecutor::do_create_inner_schema_by_tenant(
    uint64_t tenant_id,
    oceanbase::lib::Worker::CompatMode compat_mode,
    ObSchemaGetterGuard &schema_guard,
    common::ObMySQLProxy *sql_proxy,
    obrpc::ObCommonRpcProxy *rpc_proxy)
{
  int ret = OB_SUCCESS;
  return ret;
}

int ObCreateInnerSchemaExecutor::do_create_inner_schema()
{
  ObSchemaGetterGuard schema_guard;
  ObArray<uint64_t> tenant_ids;
  const int64_t start = ObTimeUtility::current_time();
  LOG_INFO("[UPGRADE] execute job create_inner_schema start", K(start));
  int ret = OB_SUCCESS;
  if (!is_inited_) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", K(ret));
  } else if (OB_ISNULL(schema_service_)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("schema_service is null", K(ret));
  } else if (OB_FAIL(check_stop())) {
    LOG_WARN("executor should stopped", K(ret));
  } else if (OB_FAIL(schema_service_->get_tenant_schema_guard(OB_SYS_TENANT_ID, schema_guard))) {
    LOG_WARN("get schema guard failed", K(ret));
  } else if (OB_FAIL(schema_guard.get_tenant_ids(tenant_ids))) {
    LOG_WARN("get tenant ids failed", K(ret));
  } else if (OB_ISNULL(sql_proxy_))  {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("sql_proxy is null", K(ret));
  } else {
    uint64_t tenant_id = OB_INVALID_TENANT_ID;
    oceanbase::lib::Worker::CompatMode compat_mode;
    for (int64_t i = tenant_ids.size() - 1; i >= 0 && OB_SUCC(ret); i--) {
      if (OB_FAIL(check_stop())) {
        LOG_WARN("create inner user or role is stopped", K(ret));
      } else {
        tenant_id = tenant_ids.at(i);
        if (OB_FAIL(schema_guard.get_tenant_compat_mode(tenant_id, compat_mode))) {
          LOG_WARN("get tenant compat mode failed", K(ret));
        } else {
          OZ (schema_service_->get_tenant_schema_guard(tenant_id, schema_guard));
          OZ (do_create_inner_schema_by_tenant(tenant_id, compat_mode, schema_guard,
                                               sql_proxy_, rpc_proxy_));
        }
      }
    }
  }
  LOG_INFO("[UPGRADE] execute job create_inner_schema finish",
           K(ret), "cost_us", ObTimeUtility::current_time() - start);
  return ret;
}

int ObCreateInnerSchemaExecutor::can_execute()
{
  int ret = OB_SUCCESS;
  SpinWLockGuard guard(rwlock_);
  if (!is_inited_) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", K(ret));
  } else if (is_stopped_) {
    ret = OB_OP_NOT_ALLOW;
    LOG_WARN("status not matched", K(ret),
             "stopped", is_stopped_ ? "true" : "false");
  }
  return ret;
}
