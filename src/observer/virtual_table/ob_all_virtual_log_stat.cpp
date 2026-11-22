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

#include "ob_all_virtual_log_stat.h"
#include "share/rc/ob_server_runtime.h"
#include "logservice/ob_log_service.h"

namespace oceanbase
{
namespace observer
{
ObAllVirtualWalStat::~ObAllVirtualWalStat()
{
  destroy();
}

void ObAllVirtualWalStat::destroy()
{
}

int ObAllVirtualWalStat::inner_get_next_row(common::ObNewRow *&row)
{
  int ret = OB_SUCCESS;
  if (false == start_to_read_) {
    wal::Stat wal_stat;
    logservice::ObLogService *log_service = ::oceanbase::share::server_service<::oceanbase::logservice::ObLogService>();
    if (NULL == log_service) {
      ret = OB_ERR_UNEXPECTED;
      SERVER_LOG(WARN, "log service is unavailable", K(ret));
    } else if (OB_FAIL(log_service->stat_wal(wal_stat))) {
    } else if (OB_FAIL(insert_log_stat_(wal_stat))) {
    } else {
    }
    if (OB_FAIL(ret)) {
    } else {
      start_to_read_ = true;
      row = &cur_row_;
    }
  } else {
    ret = OB_ITER_END;
  }
  return ret;
}

int ObAllVirtualWalStat::insert_log_stat_(const wal::Stat &wal_stat)
{
  int ret = OB_SUCCESS;
  const int64_t count = output_column_ids_.count();
  for (int64_t i = 0; OB_SUCC(ret) && i < count; ++i) {
    uint64_t col_id = output_column_ids_.at(i);
    switch (col_id) {
      case OB_APP_MIN_COLUMN_ID: {
        cur_row_.cells_[i].set_varchar(ObString::make_string("APPEND"));
        cur_row_.cells_[i].set_collation_type(ObCharset::get_default_collation(
                                              ObCharset::get_default_charset()));
        break;
      }
      case OB_APP_MIN_COLUMN_ID + 1: {
        cur_row_.cells_[i].set_uint64(wal_stat.base_lsn_.val_);
        break;
      }
      case OB_APP_MIN_COLUMN_ID + 2: {
        cur_row_.cells_[i].set_uint64(wal_stat.begin_lsn_.val_);
        break;
      }
      case OB_APP_MIN_COLUMN_ID + 3: {
        cur_row_.cells_[i].set_uint64(wal_stat.begin_scn_.get_val_for_inner_table_field());
        break;
      }
      case OB_APP_MIN_COLUMN_ID + 4: {
        cur_row_.cells_[i].set_uint64(wal_stat.end_lsn_.val_);
        break;
      }
      case OB_APP_MIN_COLUMN_ID + 5: {
        cur_row_.cells_[i].set_uint64(wal_stat.end_scn_.get_val_for_inner_table_field());
        break;
      }
      case OB_APP_MIN_COLUMN_ID + 6: {
        cur_row_.cells_[i].set_uint64(wal_stat.max_lsn_.val_);
        break;
      }
      case OB_APP_MIN_COLUMN_ID + 7: {
        cur_row_.cells_[i].set_uint64(wal_stat.max_scn_.get_val_for_inner_table_field());
        break;
      }
      default:
        ret = OB_ERR_UNEXPECTED;
        SERVER_LOG(WARN, "unknown column", K(ret), K(col_id));
        break;
    }
  }
  return ret;
}

}//namespace observer
}//namespace oceanbase
