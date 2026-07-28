
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
#define USING_LOG_PREFIX COMMON
#include "ob_errsim_module_interface_imp.h"
#include <stdio.h>
#include <string.h>
#include <pthread.h>
#include "lib/ob_define.h"
#include "share/rc/ob_server_runtime.h"
#include "share/rc/ob_module_provider.h"

using namespace oceanbase::share;
namespace oceanbase {
namespace common {

int update_errsim_module_config(
    const int64_t config_version,
    const common::ObArray<ObFixedLengthString<ObErrsimModuleTypeHelper::MAX_TYPE_NAME_LENGTH>> &module_array,
    const int64_t percentage)
{
  int ret = OB_SUCCESS;
  if (config_version < 0) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid errsim module configuration", K(ret), K(config_version));
  } else {
    SERVER_MODULE_SCOPE {
      ObErrsimModuleMgr *errsim_module_mgr = nullptr;
      if (OB_ISNULL(errsim_module_mgr = share::g_mp->errsim_module_mgr())) {
        ret = OB_ERR_UNEXPECTED;
        STORAGE_LOG(WARN, "errsim module mgr should not be NULL", K(ret), KP(errsim_module_mgr));
      } else if (OB_FAIL(errsim_module_mgr->update_config(config_version, module_array, percentage))) {
        LOG_WARN("failed to update errsim module configuration", K(ret), K(config_version));
      }
    }
  }
  return ret;
}

bool is_errsim_module(
    const ObErrsimModuleType::TYPE &type)
{
  bool b_ret = false;
  int ret = OB_SUCCESS;
  if (!ObErrsimModuleTypeHelper::is_valid(type)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("is errsim module get invalid argument", K(ret), K(type));
  } else if (ObErrsimModuleType::ERRSIM_MODULE_NONE == type) {
    b_ret = false;
  } else {
    SERVER_MODULE_SCOPE {
      ObErrsimModuleMgr *errsim_module_mgr = nullptr;
      if (OB_ISNULL(errsim_module_mgr = share::g_mp->errsim_module_mgr())) {
        ret = OB_ERR_UNEXPECTED;
        STORAGE_LOG(WARN, "errsim module mgr should not be NULL", K(ret), KP(errsim_module_mgr));
      } else {
        b_ret = errsim_module_mgr->is_errsim_module(type);
      }
    }
  }
  return b_ret;
}

} // common
} // oceanbase
