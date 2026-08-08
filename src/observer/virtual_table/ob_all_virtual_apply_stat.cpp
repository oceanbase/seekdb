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

#include "ob_all_virtual_apply_stat.h"
#include "share/rc/ob_server_runtime.h"
#include "logservice/ob_log_service.h"

namespace oceanbase
{
namespace observer
{
int ObAllVirtualApplyStat::inner_get_next_row(common::ObNewRow *&row)
{
  int ret = OB_SUCCESS;
  if (false == start_to_read_) {
    logservice::LSApplyStat apply_stat;
    logservice::ObLogService *log_service = ::oceanbase::share::server_service<::oceanbase::logservice::ObLogService>();
    if (NULL == log_service) {
      ret = OB_ERR_UNEXPECTED;
      SERVER_LOG(WARN, "log service is unavailable", K(ret));
    } else if (OB_FAIL(log_service->stat_apply(apply_stat))) {
      SERVER_LOG(WARN, "stat apply failed", K(ret));
    } else if (OB_FAIL(insert_stat_(apply_stat))) {
      SERVER_LOG(WARN, "insert stat failed", K(ret), K(apply_stat));
    } else {
      SERVER_LOG(INFO, "stat apply success", K(apply_stat));
    }
    if (OB_FAIL(ret)) {
      SERVER_LOG(WARN, "iterate apply stat failed", K(ret));
    } else {
      start_to_read_ = true;
      row = &cur_row_;
    }
  } else {
    ret = OB_ITER_END;
  }
  return ret;
}

int ObAllVirtualApplyStat::insert_stat_(logservice::LSApplyStat &apply_stat)
{
  int ret = OB_SUCCESS;
  const int64_t count = output_column_ids_.count();
  for (int64_t i = 0; OB_SUCC(ret) && i < count; i++) {
    uint64_t col_id = output_column_ids_.at(i);
    switch (col_id) {
      case OB_APP_MIN_COLUMN_ID:
        cur_row_.cells_[i].set_uint64(apply_stat.end_lsn_.val_);
        break;
      case OB_APP_MIN_COLUMN_ID + 1:
        cur_row_.cells_[i].set_int(apply_stat.pending_cnt_);
        break;
      default:
        ret = OB_ERR_UNEXPECTED;
        SERVER_LOG(WARN, "unkown column");
        break;
    }
  }
  return ret;
}
} // namespace observer
} // namespace oceanbase
