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
#include "config_bridge.h"
#include "config_checkers.h"
#include <utility>
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

  if (!ctx_.is_inited()) {
    ret = OB_NOT_INIT;
  } else if (!arg.is_valid()) {
    ret = OB_INVALID_ARGUMENT;
  }

  FOREACH_X(item, arg.items_, OB_SUCCESS == ret) {
    if (item->name_.is_empty()) {
      ret = OB_INVALID_ARGUMENT;
    } else {
      const char *name = item->name_.ptr();
      const char *value = item->value_.ptr();
      if (!config::parameter_exists(rust::Str(name))) {
        ret = OB_ERR_SYS_CONFIG_UNKNOWN;
      }

      if (OB_SUCC(ret)) {
        const char *err = NULL;
        if (!config::parameter_valid(rust::Str(name), rust::Str(value))) {
          ret = OB_INVALID_CONFIG;
        } else if (config::parameter_readonly(rust::Str(name)) && !arg.is_inner_) {
          ret = OB_INVALID_CONFIG; //TODO: specific report not editable
        } else if (!config::check_parameter(name, value)) {
          ret = OB_INVALID_CONFIG;
        } else if (!ctx_.local_management_service_->check_config(name, value, err)) {
          ret = OB_INVALID_CONFIG;
        }
        if (OB_FAIL(ret)) {
          if (nullptr != err) {
            LOG_USER_ERROR(OB_INVALID_CONFIG, err);
          }
        }
      } // if
    } // else
  } // FOREACH_X

  return ret;
}

ERRSIM_POINT_DEF(ERRSIM_UPDATE_MIN_CONFIG_VERSION_ERROR);
int ObAdminSetConfig::update_config(obcall::ObAdminSetConfigArg &arg)
{
  int ret = OB_SUCCESS;
  if (!ctx_.is_inited()) {
    ret = OB_NOT_INIT;
  } else if (!arg.is_valid()) {
    ret = OB_INVALID_ARGUMENT;
  } else {
    for (int64_t i = 0; OB_SUCC(ret) && i < arg.items_.count(); ++i) {
      const ObAdminSetConfigItem &item = arg.items_.at(i);
      if (OB_FAIL(ret)) {
      } else if (OB_FAIL(update_sys_config_(item, arg))) {
      }
    } // end for each item
  }

  return ret;
}

int ObAdminSetConfig::checked_config_callback(void *context)
{
  auto *check = static_cast<std::pair<ObAdminSetConfig *, obcall::ObAdminSetConfigArg *> *>(context);
  return check->first->verify_config(*check->second);
}

int ObAdminSetConfig::update_sys_config_(const obcall::ObAdminSetConfigItem &item,
                                         obcall::ObAdminSetConfigArg &arg)
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(GCTX.config_mgr_)) {
    ret = OB_INVALID_ARGUMENT;
  } else {
    bool after_replace = false;
    std::pair<ObAdminSetConfig *, obcall::ObAdminSetConfigArg *> check_context(this, &arg);
    const int save_ret = GCTX.config_mgr_->update_checked(
        item.name_.ptr(), item.value_.ptr(), item.is_reset_,
        checked_config_callback, &check_context, &after_replace);
    if (OB_SUCCESS == save_ret || after_replace) {
      // A durability error can follow a successful rename. In that case the
      // visible file must still be loaded before returning the save error.
      int apply_ret = GCTX.config_mgr_->got_version();
      if (OB_SUCCESS == apply_ret) {
        apply_ret = GCTX.config_mgr_->reload_config();
      }
      if (OB_SUCCESS != save_ret) {
        ret = save_ret;
        if (OB_SUCCESS != apply_ret) {
          LOG_ERROR("reloading replaced config failed", K(apply_ret), K(item));
        }
      } else if (OB_SUCCESS != apply_ret) {
        ret = OB_INVALID_CONFIG;
        LOG_ERROR("instance parameter saved but could not be applied", K(apply_ret), K(item));
        LOG_USER_ERROR(OB_INVALID_CONFIG, "parameter was saved but could not be applied");
      } else {
        LOG_INFO("got new sys config", K(item));
      }
    } else {
      ret = save_ret;
    }
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
  } else if (!arg.is_valid() || OB_ISNULL(GCTX.sql_proxy_)) {
    ret = OB_INVALID_ARGUMENT;
  } else if (OB_FAIL(verify_config(arg))) {
  } else {
    if (OB_FAIL(ctx_.local_management_service_->set_config_pre_hook(arg))) {
    } else if (OB_FAIL(update_config(arg))) {
    } else {
      LOG_INFO("set config succ", K(arg));
    }
  }
  return ret;
}

DEFINE_ENUM_FUNC(ObInnerJob, inner_job, OB_INNER_JOB_DEF);

} // end namespace rootserver
} // end namespace oceanbase
