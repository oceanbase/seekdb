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
#include "ob_table_mode_control.h"

using namespace oceanbase::common;

int ::oceanbase::table::ObTableModeCtrl::check_mode(ObKvModeType tenant_mode, ObTableEntityType entity_type)
{
  int ret = OB_SUCCESS;

  switch (tenant_mode) {
    case ObKvModeType::ALL: {
      // all mode is supported
      break;
    }
    case ObKvModeType::TABLEAPI: {
      if (entity_type != ObTableEntityType::ET_KV && entity_type != ObTableEntityType::ET_DYNAMIC) {
        ret = OB_NOT_SUPPORTED;
        LOG_USER_ERROR(OB_NOT_SUPPORTED, "As the ob_kv_mode variable has been set to 'TABLEAPI', your current interfaces");
        LOG_WARN("mode not matched", K(ret), K(entity_type), K(tenant_mode));
      }
      break;
    }
    case ObKvModeType::HBASE: {
      if (entity_type != ObTableEntityType::ET_HKV && entity_type != ObTableEntityType::ET_DYNAMIC) {
        ret = OB_NOT_SUPPORTED;
        LOG_USER_ERROR(OB_NOT_SUPPORTED, "As the ob_kv_mode variable has been set to 'HBASE', your current interfaces");
        LOG_WARN("mode not matched", K(ret), K(entity_type), K(tenant_mode));
      }
      break;
    }
    case ObKvModeType::REDIS: {
      if (entity_type != ObTableEntityType::ET_REDIS && entity_type != ObTableEntityType::ET_DYNAMIC) {
        ret = OB_NOT_SUPPORTED;
        LOG_USER_ERROR(OB_NOT_SUPPORTED, "As the ob_kv_mode variable has been set to 'REDIS', your current interfaces");
        LOG_WARN("mode not matched", K(ret), K(entity_type), K(tenant_mode));
      }
      break;
    }
    case ObKvModeType::NONE: {
      // all modes are not supported
      ret = OB_NOT_SUPPORTED;
      LOG_USER_ERROR(OB_NOT_SUPPORTED, "As the ob_kv_mode variable has been set to 'NONE', your current interfaces");
      LOG_WARN("all OBKV modes are not supported", K(ret), K(entity_type), K(tenant_mode));
      break;
    }
    default: {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("unknown ob_kv_mode", K(ret), K(tenant_mode));
      break;
    }
  }

  return ret;
}
