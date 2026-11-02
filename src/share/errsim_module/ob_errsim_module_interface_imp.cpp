
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
#include "share/rc/ob_tenant_base.h"
#include "ob_tenant_errsim_event_mgr.h"

using namespace oceanbase::share;
namespace oceanbase {
namespace common {

int build_tenant_errsim_moulde(
    const uint64_t xid,
    const int64_t config_version,
    const common::ObArray<ObFixedLengthString<ObErrsimModuleTypeHelper::MAX_TYPE_NAME_LENGTH>> &module_array,
    const int64_t percentage)
{
  int ret = OB_SUCCESS;
  const uint64_t tmp_tid = xid;

  if (OB_INVALID_ID == tmp_tid || config_version < 0) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("build tenant module get invalid argument", K(ret), K(config_version));
  } else if (false || OB_INVALID_TENANT_ID == tmp_tid) {
    //do nothing
  } else {
    MOD_SCOPE {
      ObTenantErrsimModuleMgr *errsim_module_mgr = nullptr;
      if (OB_ISNULL(errsim_module_mgr = MTL(ObTenantErrsimModuleMgr *))) {
        ret = OB_ERR_UNEXPECTED;
        STORAGE_LOG(WARN, "errsim module mgr should not be NULL", K(ret), KP(errsim_module_mgr));
      } else if (OB_FAIL(errsim_module_mgr->build_tenant_moulde(config_version, module_array, percentage))) {
        LOG_WARN("failed to build tenant module", K(ret), K(config_version));
      }
    }
  }
  return ret;
}

bool is_errsim_module(
    const uint64_t xid,
    const ObErrsimModuleType::TYPE &type)
{
  bool b_ret = false;
  int ret = OB_SUCCESS;
  const uint64_t tmp_tid = xid;
  if (OB_INVALID_ID == tmp_tid || !ObErrsimModuleTypeHelper::is_valid(type)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("is errsim module get invalid argument", K(ret), K(type));
  } else if (false || OB_INVALID_TENANT_ID == tmp_tid) {
    b_ret = false;
  } else if (ObErrsimModuleType::ERRSIM_MODULE_NONE == type) {
    b_ret = false;
  } else {
    MOD_SCOPE {
      ObTenantErrsimModuleMgr *errsim_module_mgr = nullptr;
      if (OB_ISNULL(errsim_module_mgr = MTL(ObTenantErrsimModuleMgr *))) {
        ret = OB_ERR_UNEXPECTED;
        STORAGE_LOG(WARN, "errsim module mgr should not be NULL", K(ret), KP(errsim_module_mgr));
      } else {
        b_ret = errsim_module_mgr->is_errsim_module(type);
      }
    }
  }
  return b_ret;
}

int add_tenant_errsim_event(
    const uint64_t xid,
    const ObTenantErrsimEvent &event)
{
  bool b_ret = false;
  int ret = OB_SUCCESS;
  const uint64_t tmp_tid = xid;
  if (OB_INVALID_ID == tmp_tid || !event.is_valid()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("is errsim module get invalid argument", K(ret), K(event));
  } else if (false || OB_INVALID_TENANT_ID == tmp_tid) {
    //do nothing
  } else {
    MOD_SCOPE {
      ObTenantErrsimEventMgr *errsim_event_mgr = nullptr;
      if (OB_ISNULL(errsim_event_mgr = MTL(ObTenantErrsimEventMgr *))) {
        ret = OB_ERR_UNEXPECTED;
        STORAGE_LOG(WARN, "errsim event mgr should not be NULL", K(ret), KP(errsim_event_mgr));
      } else if (OB_FAIL(errsim_event_mgr->add_tenant_event(event))) {
        LOG_WARN("failed to add tenant event", K(ret), K(event));
      }
    }
  }
  return b_ret;
}


} // common
} // oceanbase

