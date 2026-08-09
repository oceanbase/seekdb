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

#define USING_LOG_PREFIX SHARE_SCHEMA
#include "standby/ob_standby_schema_refresh_trigger.h"
#include "lib/oblog/ob_log.h"
#include "lib/ob_running_mode.h"
#include "lib/profile/ob_trace_id.h"
#include "standby/standby_host.h"

namespace oceanbase
{
namespace standby
{

int ObStandbySchemaRefreshTrigger::init(const StandbyConfig &config, IStandbyHost &host)
{
  int ret = OB_SUCCESS;

  if (is_inited_) {
    ret = OB_INIT_TWICE;
    LOG_WARN("init twice", KR(ret));
  } else {
    config_ = &config;
    host_ = &host;
    is_inited_ = true;
    LOG_INFO("standby schema refresh trigger initialized", K(config.embedded_mode_));
  }

  return ret;
}

int ObStandbySchemaRefreshTrigger::start()
{
  int ret = OB_SUCCESS;
  if (!is_inited_) {
    ret = OB_NOT_INIT;
    LOG_WARN("standby schema refresh trigger is not initialized", KR(ret));
  } else if (config_->embedded_mode_ || is_scheduled_) {
  } else if (OB_FAIL(schedule_())) {
    LOG_WARN("failed to schedule standby schema refresh trigger", KR(ret));
  } else {
    is_scheduled_ = true;
  }
  return ret;
}

int ObStandbySchemaRefreshTrigger::stop()
{
  int ret = OB_SUCCESS;
  if (is_inited_ && is_scheduled_) {
    timer_.stop();
  }
  return ret;
}

int ObStandbySchemaRefreshTrigger::wait()
{
  int ret = OB_SUCCESS;
  if (is_inited_ && is_scheduled_ && !config_->embedded_mode_) {
    timer_.wait();
    is_scheduled_ = false;
  }
  return ret;
}

void ObStandbySchemaRefreshTrigger::destroy()
{
  LOG_INFO("ObStandbySchemaRefreshTrigger destroy");
  stop();
  wait();
  timer_.destroy();
  config_ = nullptr;
  host_ = nullptr;
  is_scheduled_ = false;
  is_inited_ = false;
}

int ObStandbySchemaRefreshTrigger::schedule_()
{
  int ret = OB_SUCCESS;
  if (OB_FAIL(timer_.init("StbySchemaRef", common::ObMemAttr("StbySchemaRef")))) {
    LOG_WARN("failed to init standby schema refresh timer", KR(ret));
  } else if (OB_FAIL(timer_.schedule(*this, DEFAULT_IDLE_TIME, true /* repeat */))) {
    LOG_WARN("failed to schedule standby schema refresh trigger task", KR(ret));
    timer_.destroy();
  }
  return ret;
}

void ObStandbySchemaRefreshTrigger::runTimerTask()
{
  int ret = OB_SUCCESS;

  if (OB_FAIL(check_inner_stat_())) {
    LOG_WARN("inner stat error", KR(ret), K_(is_inited));
  } else {
    common::ObCurTraceId::init(config_->self_addr_);
    if (OB_FAIL(submit_tenant_refresh_schema_task_())) {
      LOG_WARN("submit_tenant_refresh_schema_task_ failed", KR(ret));
    }
  }
}

int ObStandbySchemaRefreshTrigger::check_inner_stat_()
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(!is_inited_) || OB_ISNULL(config_) || OB_ISNULL(host_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", KR(ret));
  }
  return ret;
}

int ObStandbySchemaRefreshTrigger::submit_tenant_refresh_schema_task_()
{
  int ret = OB_SUCCESS;
  if (OB_FAIL(host_->refresh_schema())) {
    LOG_WARN("failed to refresh standby schema", KR(ret));
  }
  return ret;
}

} // namespace standby
} // namespace oceanbase
