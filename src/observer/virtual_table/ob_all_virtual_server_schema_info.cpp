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

# define USING_LOG_PREFIX SERVER
#include "ob_all_virtual_server_schema_info.h"
#include "observer/ob_server_struct.h"

namespace oceanbase
{
namespace observer
{
int ObAllVirtualServerSchemaInfo::inner_open()
{
  int ret = OB_SUCCESS;
  idx_ = 0;
  share::schema::ObSchemaGetterGuard guard;
  if (OB_FAIL(schema_service_.get_tenant_schema_guard(OB_SYS_TENANT_ID, guard))) {
    LOG_WARN("fail to get schema guard", K(ret));
  } else if (OB_INVALID_TENANT_ID == effective_tenant_id_) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("invalid tenant_id", KR(ret), K_(effective_tenant_id));
  } else if (is_sys_tenant(effective_tenant_id_)) {
    // all tenant's schema is visible in sys tenant
    if (OB_FAIL(guard.get_tenant_ids(tenant_ids_))) {
      LOG_WARN("fail to get tenant_ids", KR(ret));
    }
  } else {
    // user/meta tenant can see its own schema
    bool is_exist = false;
    if (OB_FAIL(guard.check_tenant_exist(effective_tenant_id_, is_exist))) {
      LOG_WARN("fail to check tenant exist", KR(ret), K_(effective_tenant_id));
    } else if (is_exist && OB_FAIL(tenant_ids_.push_back(effective_tenant_id_))) {
      LOG_WARN("fail to push back effective_tenant_id", KR(ret), K_(effective_tenant_id));
    }
  }
  return ret;
}

int ObAllVirtualServerSchemaInfo::inner_get_next_row(common::ObNewRow *&row)
{
  int ret = OB_SUCCESS;
  if (idx_ >= tenant_ids_.count()) {
    ret = OB_ITER_END;
  } else if (OB_INVALID_TENANT_ID == tenant_ids_[idx_]) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("invalid tenant_id", K(ret));
  } else {
    const uint64_t tenant_id = tenant_ids_[idx_];
    int64_t refreshed_schema_version = OB_INVALID_VERSION;
    int64_t received_schema_version = OB_INVALID_VERSION;
    int64_t schema_count = OB_INVALID_ID;
    int64_t schema_size = OB_INVALID_ID;
    share::schema::ObSchemaGetterGuard schema_guard;
    if (OB_FAIL(schema_service_.get_tenant_refreshed_schema_version(tenant_id, refreshed_schema_version))) {
      LOG_WARN("fail to get tenant refreshed schema version", K(ret), K(tenant_id), K(refreshed_schema_version));
    } else if (OB_FAIL(schema_service_.get_tenant_received_broadcast_version(tenant_id, received_schema_version))) {
      LOG_WARN("fail to get tenant receieved schema version", K(ret), K(tenant_id), K(received_schema_version));
    } else {
      int tmp_ret = OB_SUCCESS;
      if (OB_SUCCESS != (tmp_ret = schema_service_.get_tenant_schema_guard(tenant_id, schema_guard))) {
        LOG_WARN("fail to get schema guard", K(tmp_ret), K(tenant_id));
      } else if (OB_SUCCESS != (tmp_ret = schema_guard.get_schema_count(tenant_id, schema_count))) {
        LOG_WARN("fail to get schema count", K(tmp_ret), K(tenant_id));
      }
    }

    // Column order after removing svr_ip and svr_port:
    // OB_APP_MIN_COLUMN_ID (16): refreshed_schema_version
    // OB_APP_MIN_COLUMN_ID + 1 (17): received_schema_version
    // OB_APP_MIN_COLUMN_ID + 2 (18): schema_count
    // OB_APP_MIN_COLUMN_ID + 3 (19): schema_size
    // OB_APP_MIN_COLUMN_ID + 4 (20): min_sstable_schema_version
    const int64_t col_count = output_column_ids_.count();
    for (int64_t i = 0; OB_SUCC(ret) && i < col_count; ++i) {
      uint64_t col_id = output_column_ids_.at(i);
      switch (col_id) {
        case OB_APP_MIN_COLUMN_ID: { // refreshed_schema_version
          cur_row_.cells_[i].set_int(refreshed_schema_version);
          break;
        }
        case OB_APP_MIN_COLUMN_ID + 1: { // received_schema_version
          cur_row_.cells_[i].set_int(received_schema_version);
          break;
        }
        case OB_APP_MIN_COLUMN_ID + 2: { // schema_count
          cur_row_.cells_[i].set_int(schema_count);
          break;
        }
        case OB_APP_MIN_COLUMN_ID + 3: { // schema_size
          cur_row_.cells_[i].set_int(schema_size);
          break;
        }
        case OB_APP_MIN_COLUMN_ID + 4: { // min_sstable_schema_version
          cur_row_.cells_[i].set_int(OB_INVALID_VERSION);
          break;
        }
        default : {
          ret = OB_ERR_UNEXPECTED;
          LOG_WARN("invalid col_id", K(ret), K(col_id));
        }
      }
    }

    if (OB_SUCC(ret)) {
      row = &cur_row_;
      idx_++;
    }
  }
  return ret;
}
}
}
