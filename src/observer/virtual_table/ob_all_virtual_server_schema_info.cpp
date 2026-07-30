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
int ObAllVirtualServerSchemaInfo::inner_get_next_row(common::ObNewRow *&row)
{
  int ret = OB_SUCCESS;
  if (start_to_read_) {
    ret = OB_ITER_END;
  } else {
    start_to_read_ = true;
    int64_t refreshed_schema_version = OB_INVALID_VERSION;
    int64_t received_schema_version = OB_INVALID_VERSION;
    int64_t schema_count = OB_INVALID_ID;
    share::schema::ObSchemaGetterGuard schema_guard;
    if (OB_FAIL(schema_service_.get_runtime_refreshed_schema_version(refreshed_schema_version))) {
      LOG_WARN("fail to get runtime refreshed schema version", K(ret), K(refreshed_schema_version));
    } else if (OB_FAIL(schema_service_.get_published_schema_version(received_schema_version))) {
      LOG_WARN("fail to get runtime received schema version", K(ret), K(received_schema_version));
    } else {
      int tmp_ret = OB_SUCCESS;
      if (OB_SUCCESS != (tmp_ret = schema_service_.get_runtime_schema_guard(schema_guard))) {
        LOG_WARN("fail to get schema guard", K(tmp_ret));
      } else if (OB_SUCCESS != (tmp_ret = schema_guard.get_schema_count(schema_count))) {
        LOG_WARN("fail to get schema count", K(tmp_ret));
      }
    }

    const int64_t col_count = output_column_ids_.count();
    for (int64_t i = 0; OB_SUCC(ret) && i < col_count; ++i) {
      uint64_t col_id = output_column_ids_.at(i);
      switch (col_id) {
        case REFRESHED_SCHEMA_VERSION: {
          cur_row_.cells_[i].set_int(refreshed_schema_version);
          break;
        }
        case RECEIVED_SCHEMA_VERSION: {
          cur_row_.cells_[i].set_int(received_schema_version);
          break;
        }
        case SCHEMA_COUNT: {
          cur_row_.cells_[i].set_int(schema_count);
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
    }
  }
  return ret;
}
}
}
