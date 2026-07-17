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

#define USING_LOG_PREFIX RS
#include "ob_tenant_thread_helper.h"
#include "src/logservice/applyservice/ob_log_apply_service.h"

namespace oceanbase
{
using namespace common;
using namespace share;
using namespace transaction;
using namespace palf;
using namespace lib;
namespace rootserver
{
//////////////ObTenantThreadHelper
int ObTenantThreadHelper::create(
    const char* thread_name, int64_t thread_cnt, ObTenantThreadHelper &tenant_thread)
{
  int ret = OB_SUCCESS;
  UNUSED(tenant_thread);
  if (OB_UNLIKELY(is_created_)) {
    ret = OB_INIT_TWICE;
    LOG_WARN("has inited", KR(ret));
  } else if (OB_ISNULL(thread_name)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("thread name is null", KR(ret));
  } else if (thread_cnt <= 0) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid thread count", KR(ret), K(thread_cnt));
  } else if (OB_FAIL(thread_cond_.init(ObWaitEventIds::REENTRANT_THREAD_COND_WAIT))) {
    LOG_WARN("fail to init cond, ", KR(ret));
  } else {
    thread_name_ = thread_name;
    thread_cnt_ = thread_cnt;
    is_created_ = true;
    is_first_time_to_start_ = true;
  }
  return ret;
}

int ObTenantThreadHelper::start()
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(!is_created_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", KR(ret));
  } else if (is_first_time_to_start_) {
    if (OB_FAIL(share::ObReentrantThread::create(thread_cnt_, thread_name_))) {
      LOG_WARN("fail ed to start at first time", KR(ret), K(thread_cnt_), K(thread_name_));
    } else if (OB_FAIL(share::ObReentrantThread::logical_start())) {
      LOG_WARN("failed to logical start at first time", KR(ret), K(thread_cnt_), K(thread_name_));
    } else {
      is_first_time_to_start_ = false;
    }
  } else if (OB_FAIL(share::ObReentrantThread::logical_start())) {
    LOG_WARN("failed to start", KR(ret));
  }
  LOG_INFO("[TENANT THREAD] thread start", KR(ret), K(thread_cnt_), K(thread_name_));
  return ret;
}

ERRSIM_POINT_DEF(ERRSIM_SKIP_TENANT_THREAD_STOP);
void ObTenantThreadHelper::stop()
{
  int ret = OB_SUCCESS;
  LOG_INFO("[TENANT THREAD] thread stop start", K(thread_cnt_), K(thread_name_));
  ret = ERRSIM_SKIP_TENANT_THREAD_STOP;
  if (OB_UNLIKELY(ERRSIM_SKIP_TENANT_THREAD_STOP)) {
    LOG_ERROR("[TENANT THREAD] skip tenant thread stop");
  } else if (!is_first_time_to_start_) {
    share::ObReentrantThread::logical_stop();
  }
  LOG_INFO("[TENANT THREAD] thread stop finish", K(thread_cnt_), K(thread_name_), KR(ret));
}

void ObTenantThreadHelper::wait()
{
  LOG_INFO("[TENANT THREAD] thread wait start", K(thread_cnt_), K(thread_name_));
  if (!is_first_time_to_start_) {
    share::ObReentrantThread::logical_wait();
  }
  LOG_INFO("[TENANT THREAD] thread wait finish", K(thread_cnt_), K(thread_name_));
}

void ObTenantThreadHelper::mtl_thread_stop()
{
  LOG_INFO("[TENANT THREAD] thread stop start", K(thread_cnt_), K(thread_name_));
  if (!is_first_time_to_start_) {
    share::ObReentrantThread::stop();
  }
  LOG_INFO("[TENANT THREAD] thread stop finish", K(thread_cnt_), K(thread_name_));
}

void ObTenantThreadHelper::mtl_thread_wait()
{
  LOG_INFO("[TENANT THREAD] thread wait start", K(thread_cnt_), K(thread_name_));
  if (!is_first_time_to_start_) {
    {
      ObThreadCondGuard guard(thread_cond_);
      thread_cond_.broadcast();
    }
    share::ObReentrantThread::wait();
    share::ObReentrantThread::destroy();
    is_first_time_to_start_ = true;
  }
  LOG_INFO("[TENANT THREAD] thread wait finish", K(thread_cnt_), K(thread_name_));
}
void ObTenantThreadHelper::destroy()
{
  LOG_INFO("[TENANT THREAD] thread destory start", K(thread_cnt_), K(thread_name_));
  if (is_created_) {
    if (!is_first_time_to_start_) {
      share::ObReentrantThread::stop();
    }
    {
      ObThreadCondGuard guard(thread_cond_); 
      thread_cond_.broadcast();
    }
    if (!is_first_time_to_start_) {
      share::ObReentrantThread::wait();
      share::ObReentrantThread::destroy();
    }
    thread_cond_.destroy();
  }
  is_created_ = false;
  is_first_time_to_start_ = true;
  thread_cnt_ = 0;
  LOG_INFO("[TENANT THREAD] thread destory finish", K(thread_cnt_), K(thread_name_));
}

void ObTenantThreadHelper::deactivate()
{
  stop();
}
int ObTenantThreadHelper::activate()
{
  int ret = OB_SUCCESS;
  LOG_INFO("[TENANT THREAD] thread start", K(thread_cnt_), K(thread_name_));
  if (OB_FAIL(start())) {
    LOG_WARN("failed to start thread", KR(ret));
  } else {
    ObThreadCondGuard guard(thread_cond_);
    if (OB_FAIL(thread_cond_.broadcast())) {
      LOG_WARN("failed to weakup thread cond", KR(ret));
    }
  }
  LOG_INFO("[TENANT THREAD] thread start finish", K(thread_cnt_), K(thread_name_));
  return ret;
}

int ObTenantThreadHelper::wait_tenant_schema_ready_()
{
  int ret = OB_SUCCESS;
  bool is_ready = false;
  share::schema::ObTenantSchema tenant_schema;
  while (!is_ready && !has_set_stop()) {
    ret = OB_SUCCESS;
    if (OB_FAIL(get_tenant_schema( tenant_schema))) {
      LOG_WARN("failed to get tenant schema", KR(ret));
    } else if (tenant_schema.is_creating()) {
      ret = OB_NEED_WAIT;
      LOG_WARN("tenant schema not ready, no need tenant balance", KR(ret), K(tenant_schema));
    } else {
      is_ready = true;
    }
    if (!is_ready) {
      idle(10 * 1000 * 1000);
    }
  }
  if (has_set_stop()) {
    LOG_WARN("thread has been stopped", K(is_ready));
    ret = OB_IN_STOP_STATE;
  }
  return ret;
}

int ObTenantThreadHelper::wait_tenant_schema_and_version_ready_()
{
  int ret = OB_SUCCESS;
  if (OB_FAIL(wait_tenant_schema_ready_())) {
    LOG_WARN("failed to wait tenant schema ready", KR(ret));
  }
  return ret;
}

void ObTenantThreadHelper::run2() {
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(!is_created_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", K(ret));
  } else {
    lib::set_thread_name(thread_name_);
    LOG_INFO("thread run", K(thread_name_));
    do_work();
  }
}
void ObTenantThreadHelper::idle(const int64_t idle_time_us)
{
  ObThreadCondGuard guard(thread_cond_);
  thread_cond_.wait_us(idle_time_us);
}

int ObTenantThreadHelper::get_tenant_schema( 
  share::schema::ObTenantSchema &tenant_schema)
{
  int ret = OB_SUCCESS;
  share::schema::ObSchemaGetterGuard schema_guard;
  const share::schema::ObTenantSchema *cur_tenant_schema = NULL;
  if (OB_ISNULL(GCTX.schema_service_)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("unexpected error", KR(ret), KP(GCTX.schema_service_));
  } else if (OB_FAIL(GCTX.schema_service_->get_tenant_schema_guard(
          schema_guard))) {
    LOG_WARN("fail to get schema guard", KR(ret));
  } else if (OB_FAIL(schema_guard.get_tenant_info(cur_tenant_schema))) {
    LOG_WARN("failed to get tenant ids", KR(ret));
  } else if (OB_ISNULL(cur_tenant_schema)) {
    ret = OB_TENANT_NOT_EXIST;
    LOG_WARN("tenant not exist", KR(ret));
  } else if (OB_FAIL(tenant_schema.assign(*cur_tenant_schema))) {
    LOG_WARN("failed to get cur tenant schema", KR(ret), KP(cur_tenant_schema));
  }
  return ret;
}

//TODO meta tenant and user tenant maybe not in same observer
int ObTenantThreadHelper::check_can_do_recovery_()
{
  int ret = OB_SUCCESS;
  // lite: recovery is user-tenant-only; sys tenant is never a user tenant
  ret = OB_INVALID_ARGUMENT;
  LOG_WARN("only user tenant need check recovery", KR(ret));
  return ret;
}

void ObTenantThreadHelper::wakeup()
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(!is_created_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", KR(ret));
  } else {
    ObThreadCondGuard guard(thread_cond_);
    thread_cond_.broadcast();
  }
}

}//end of rootserver
}
