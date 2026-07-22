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


#include "ob_system_admin_util.h"
#include "ob_local_management_service.h"
namespace oceanbase
{
using namespace common;
using namespace common::hash;
using namespace share;
using namespace share::schema;
using namespace obcall;

namespace rootserver
{

int ObAdminSetConfig::verify_config(obcall::ObAdminSetConfigArg &arg)
{
  int ret = OB_SUCCESS;
  void *ptr = nullptr;
  ObServerConfigChecker *cfg = nullptr;

  if (!ctx_.is_inited()) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", KR(ret));
  } else if (!arg.is_valid()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid arg", K(arg), KR(ret));
  }

  if (nullptr == cfg) {
    if (OB_ISNULL(ptr = ob_malloc(sizeof(ObServerConfigChecker),
                                ObModIds::OB_RS_PARTITION_TABLE_TEMP))) {
      ret = OB_ALLOCATE_MEMORY_FAILED;
      LOG_WARN("fail to alloc memory", KR(ret));
    } else if (OB_ISNULL(cfg = new (ptr) ObServerConfigChecker)) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("new cfg failed", KR(ret));
    }
  }

  FOREACH_X(item, arg.items_, OB_SUCCESS == ret) {
    if (item->name_.is_empty()) {
      ret = OB_INVALID_ARGUMENT;
      LOG_WARN("empty config name", "item", *item, KR(ret));
    } else {
      ObConfigItem *ci = nullptr;
      ObConfigItem * const *ci_ptr = cfg->get_container().get(
                                      ObConfigStringKey(item->name_.ptr()));
      if (OB_ISNULL(ci_ptr) || OB_ISNULL(*ci_ptr)) {
        ret = OB_ERR_SYS_CONFIG_UNKNOWN;
        LOG_WARN("can't find config item", KR(ret), "item", *item);
      } else {
        ci = *ci_ptr;
      }

      if (OB_SUCC(ret)) {
        const char *err = NULL;
        if (ci->is_not_editable() && !arg.is_inner_) {
          ret = OB_INVALID_CONFIG; //TODO: specific report not editable
          LOG_WARN("config is not editable", "item", *item, KR(ret));
        } else if (!ci->check_unit(item->value_.ptr())) {
          ret = OB_INVALID_CONFIG;
          LOG_ERROR("invalid config", "item", *item, KR(ret));
        } else if (!ci->set_value_unsafe(item->value_.ptr())) {
          ret = OB_INVALID_CONFIG;
          LOG_WARN("invalid config", "item", *item, KR(ret));
        } else if (!ci->check()) {
          ret = OB_INVALID_CONFIG;
          LOG_WARN("invalid value range", "item", *item, KR(ret));
        } else if (!ctx_.local_management_service_->check_config(*ci, err)) {
          ret = OB_INVALID_CONFIG;
          LOG_WARN("invalid value range", "item", *item, KR(ret));
        }
        if (OB_FAIL(ret)) {
          if (nullptr != err) {
            LOG_USER_ERROR(OB_INVALID_CONFIG, err);
          }
        }
      } // if
    } // else
  } // FOREACH_X

  if (nullptr != cfg) {
    cfg->~ObServerConfigChecker();
    ob_free(cfg);
    cfg = nullptr;
    ptr = nullptr;
  } else if (nullptr != ptr) {
    ob_free(ptr);
    ptr = nullptr;
  }
  return ret;
}

ERRSIM_POINT_DEF(ERRSIM_UPDATE_MIN_CONFIG_VERSION_ERROR);
int ObAdminSetConfig::update_config(obcall::ObAdminSetConfigArg &arg)
{
  int ret = OB_SUCCESS;
  if (!ctx_.is_inited()) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", KR(ret));
  } else if (!arg.is_valid()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid arg", K(arg), KR(ret));
  } else {
    for (int64_t i = 0; OB_SUCC(ret) && i < arg.items_.count(); ++i) {
      const ObAdminSetConfigItem &item = arg.items_.at(i);
      if (OB_FAIL(ret)) {
      } else if (OB_FAIL(update_sys_config_(item))) {
        LOG_WARN("fail to update sys config", KR(ret), K(item));
      }
    } // end for each item
  }

  return ret;
}

int ObAdminSetConfig::update_sys_config_(const obcall::ObAdminSetConfigItem &item)
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(GCTX.config_mgr_)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", KR(ret), KP(GCTX.config_mgr_));
  } else {
    if (OB_SUCC(ret) && OB_NOT_NULL(GCTX.config_mgr_)) {
      if (OB_FAIL(GCTX.config_mgr_->save_config(
                    item.name_.ptr(), item.value_.ptr()))) {
        LOG_WARN("failed to save config", KR(ret), K(item));
      }
    }
  }
  // try update local memory and trigger remote server to refresh this change
  if (OB_FAIL(ret)) {
  } else if (OB_FAIL(GCTX.config_mgr_->got_version())) {
    LOG_WARN("config mgr got version failed", KR(ret));
  } else if (OB_FAIL(GCTX.config_mgr_->reload_config())) {
    LOG_WARN("reload configuration failed", K(ret));
  } else {
    LOG_INFO("got new sys config", K(item));
  }
  return ret;
}

int ObAdminSetConfig::execute(obcall::ObAdminSetConfigArg &arg)
{
  LOG_INFO("execute set config request", K(arg));
  DEBUG_SYNC(BEFORE_EXECUTE_ADMIN_SET_CONFIG);
  int ret = OB_SUCCESS;
  if (!ctx_.is_inited()) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", KR(ret));
  } else if (!arg.is_valid() || OB_ISNULL(GCTX.sql_proxy_)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid arg", K(arg), KR(ret), KP(GCTX.sql_proxy_));
  } else if (OB_FAIL(verify_config(arg))) {
    LOG_WARN("verify config failed", KR(ret), K(arg));
  } else {
    if (OB_FAIL(ctx_.local_management_service_->set_config_pre_hook(arg))) {
      LOG_WARN("fail to process pre hook", K(arg), KR(ret));
    } else if (OB_FAIL(update_config(arg))) {
      LOG_WARN("update config failed", KR(ret), K(arg));
    } else {
      LOG_INFO("set config succ", K(arg));
    }
  }
  return ret;
}

DEFINE_ENUM_FUNC(ObInnerJob, inner_job, OB_INNER_JOB_DEF);

} // end namespace rootserver
} // end namespace oceanbase
