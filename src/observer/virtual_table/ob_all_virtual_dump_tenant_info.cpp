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

#include "ob_all_virtual_dump_tenant_info.h"
#include "observer/omt/ob_tenant.h"

namespace oceanbase
{
namespace observer
{
ObAllVirtualDumpTenantInfo::ObAllVirtualDumpTenantInfo()
{
}

ObAllVirtualDumpTenantInfo::~ObAllVirtualDumpTenantInfo()
{
}

int ObAllVirtualDumpTenantInfo::inner_get_next_row(common::ObNewRow *&row)
{
  int ret = OB_SUCCESS;
  if (start_to_read_) {
    ret = OB_ITER_END;
  } else {
    start_to_read_ = true;
    omt::ObTenant &tenant = *static_cast<omt::ObTenant *>(MTL_CTX());
    common::ObObj *cells = cur_row_.cells_;
    for (int64_t i = 0; OB_SUCC(ret) && i < output_column_ids_.count(); ++i) {
      const uint64_t col_id = output_column_ids_.at(i);
      switch (col_id) {
      case OB_APP_MIN_COLUMN_ID:
        cells[i].set_int(static_cast<int64_t>(tenant.get_compat_mode()));
        break;
      case OB_APP_MIN_COLUMN_ID + 1:
        cells[i].set_double(tenant.unit_min_cpu_);
        break;
      case OB_APP_MIN_COLUMN_ID + 2:
        cells[i].set_double(tenant.unit_max_cpu_);
        break;
      case OB_APP_MIN_COLUMN_ID + 3:
        cells[i].set_double(0);
        break;
      case OB_APP_MIN_COLUMN_ID + 4:
        cells[i].set_double(0);
        break;
      case OB_APP_MIN_COLUMN_ID + 5:
        cells[i].set_int(tenant.worker_count());
        break;
      case OB_APP_MIN_COLUMN_ID + 6:
        cells[i].set_int(tenant.worker_count());
        break;
      case OB_APP_MIN_COLUMN_ID + 7:
        cells[i].set_int(tenant.stopped_);
        break;
      case OB_APP_MIN_COLUMN_ID + 8:
        cells[i].set_int(0);
        break;
      case OB_APP_MIN_COLUMN_ID + 9:
        cells[i].set_int(tenant.recv_hp_rpc_cnt_);
        break;
      case OB_APP_MIN_COLUMN_ID + 10:
        cells[i].set_int(tenant.recv_np_rpc_cnt_);
        break;
      case OB_APP_MIN_COLUMN_ID + 11:
        cells[i].set_int(tenant.recv_lp_rpc_cnt_);
        break;
      case OB_APP_MIN_COLUMN_ID + 12:
        cells[i].set_int(tenant.recv_mysql_cnt_);
        break;
      case OB_APP_MIN_COLUMN_ID + 13:
        cells[i].set_int(tenant.recv_task_cnt_);
        break;
      case OB_APP_MIN_COLUMN_ID + 14:
        cells[i].set_int(tenant.workers_.get_size());
        break;
      case OB_APP_MIN_COLUMN_ID + 15:
        cells[i].set_int(tenant.workers_.get_size());
        break;
      case OB_APP_MIN_COLUMN_ID + 16:
        cells[i].set_int(tenant.req_queue_.size());
        break;
      case OB_APP_MIN_COLUMN_ID + 17:
        cells[i].set_int(tenant.req_queue_.queue_size(0));
        break;
      case OB_APP_MIN_COLUMN_ID + 18:
        cells[i].set_int(tenant.req_queue_.queue_size(1));
        break;
      case OB_APP_MIN_COLUMN_ID + 19:
        cells[i].set_int(tenant.req_queue_.queue_size(2));
        break;
      case OB_APP_MIN_COLUMN_ID + 20:
        cells[i].set_int(tenant.req_queue_.queue_size(3));
        break;
      case OB_APP_MIN_COLUMN_ID + 21:
        cells[i].set_int(tenant.req_queue_.queue_size(4));
        break;
      case OB_APP_MIN_COLUMN_ID + 22:
        cells[i].set_int(tenant.req_queue_.queue_size(5));
        break;
      default:
        ret = OB_ERR_UNEXPECTED;
        SERVER_LOG(WARN, "invalid column id", K(ret), K(col_id));
        break;
      }
    }
    if (OB_SUCC(ret)) {
      row = &cur_row_;
    }
  }
  return ret;
}

} /* namespace observer */
} /* namespace oceanbase */
