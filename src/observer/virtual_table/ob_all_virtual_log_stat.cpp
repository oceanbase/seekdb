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
#include "share/rc/ob_module_provider.h"
#include "logservice/ob_log_service.h"

namespace oceanbase
{
namespace observer
{
ObAllVirtualPalfStat::~ObAllVirtualPalfStat()
{
  destroy();
}

void ObAllVirtualPalfStat::destroy()
{
}

int ObAllVirtualPalfStat::inner_get_next_row(common::ObNewRow *&row)
{
  int ret = OB_SUCCESS;
  if (false == start_to_read_) {
    logservice::ObLogStat log_stat;
    logservice::ObLogService *log_service = share::g_mp->log_service();
    if (NULL == log_service) {
      ret = OB_ERR_UNEXPECTED;
      SERVER_LOG(WARN, "tenant has no ObLogService", K(ret));
    } else if (OB_FAIL(log_service->stat_palf(log_stat.palf_stat_))) {
      SERVER_LOG(WARN, "ObLogService stat_palf failed", K(ret));
    } else if (OB_FAIL(insert_log_stat_(log_stat, &cur_row_))) {
      SERVER_LOG(WARN, "ObAllVirtualPalfStat insert_log_stat_ failed", K(ret), K(log_stat));
    } else {
      SERVER_LOG(TRACE, "stat palf success", K(log_stat));
    }
    if (OB_FAIL(ret)) {
      SERVER_LOG(WARN, "iterate log stat failed", K(ret));
    } else {
      start_to_read_ = true;
      row = &cur_row_;
    }
  } else {
    ret = OB_ITER_END;
  }
  return ret;
}

int ObAllVirtualPalfStat::insert_log_stat_(const logservice::ObLogStat &log_stat, common::ObNewRow *row)
{
  int ret = OB_SUCCESS;
  const palf::PalfStat &palf_stat = log_stat.palf_stat_;
  const int64_t count = output_column_ids_.count();
  for (int64_t i = 0; OB_SUCC(ret) && i < count; ++i) {
    uint64_t col_id = output_column_ids_.at(i);
    switch (col_id) {
      case OB_APP_MIN_COLUMN_ID: {
        if (OB_FAIL(palf::access_mode_to_string(palf_stat.access_mode_, access_mode_str_, sizeof(access_mode_str_)))) {
          SERVER_LOG(WARN, "access_mode_to_string failed", K(ret), K(palf_stat));
        } else {
          cur_row_.cells_[i].set_varchar(ObString::make_string(access_mode_str_));
          cur_row_.cells_[i].set_collation_type(ObCharset::get_default_collation(
                                                ObCharset::get_default_charset()));
        }
        break;
      }
      case OB_APP_MIN_COLUMN_ID + 1: {
        cur_row_.cells_[i].set_uint64(palf_stat.base_lsn_.val_);
        break;
      }
      case OB_APP_MIN_COLUMN_ID + 2: {
        cur_row_.cells_[i].set_uint64(palf_stat.begin_lsn_.val_);
        break;
      }
      case OB_APP_MIN_COLUMN_ID + 3: {
        cur_row_.cells_[i].set_uint64(palf_stat.begin_scn_.get_val_for_inner_table_field());
        break;
      }
      case OB_APP_MIN_COLUMN_ID + 4: {
        cur_row_.cells_[i].set_uint64(palf_stat.end_lsn_.val_);
        break;
      }
      case OB_APP_MIN_COLUMN_ID + 5: {
        cur_row_.cells_[i].set_uint64(palf_stat.end_scn_.get_val_for_inner_table_field());
        break;
      }
      case OB_APP_MIN_COLUMN_ID + 6: {
        cur_row_.cells_[i].set_uint64(palf_stat.max_lsn_.val_);
        break;
      }
      case OB_APP_MIN_COLUMN_ID + 7: {
        cur_row_.cells_[i].set_uint64(palf_stat.max_scn_.get_val_for_inner_table_field());
        break;
      }
    }
  }
  return ret;
}

}//namespace observer
}//namespace oceanbase
