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

#include "observer/virtual_table/ob_virtual_sql_monitor_statname.h"

using namespace oceanbase::observer;
using namespace oceanbase::common;
using namespace oceanbase::sql;

ObVirtualSqlMonitorStatname::ObVirtualSqlMonitorStatname()
    : ObVirtualTableScannerIterator(),
      stat_iter_(1)
{
}

ObVirtualSqlMonitorStatname::~ObVirtualSqlMonitorStatname()
{
  reset();
}

void ObVirtualSqlMonitorStatname::reset()
{
  ObVirtualTableScannerIterator::reset();
  stat_iter_ = 1; // The first item is MONITOR_STATNAME_BEGIN, skip
  for (int64_t i = 0; i  < OB_ROW_MAX_COLUMNS_COUNT; i++) {
    cells_[i].reset();
  }
}

int ObVirtualSqlMonitorStatname::inner_get_next_row(ObNewRow *&row)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(NULL == allocator_)) {
    ret = OB_NOT_INIT;
    SERVER_LOG(WARN, "allocator is NULL", K(ret));
  } else {
    const int64_t col_count = output_column_ids_.count();
    cur_row_.cells_ = cells_;
    cur_row_.count_ = reserved_column_cnt_;

    if (stat_iter_ >= ObSqlMonitorStatIds::MONITOR_STATNAME_END) {
      ret = OB_ITER_END;
    }

    if (OB_SUCC(ret)) {
      uint64_t cell_idx = 0;
      for (int64_t i = 0; OB_SUCC(ret) && i < col_count; ++i) {
        uint64_t col_id = output_column_ids_.at(i);
        switch(col_id) {
          case ID: {
            cells_[cell_idx].set_int(stat_iter_);
            break;
          }
          case GROUP_ID: {
            cells_[cell_idx].set_int(0);
            break;
          }
          case NAME: {
            cells_[cell_idx].set_varchar(OB_MONITOR_STATS[stat_iter_].name_);
            cells_[cell_idx].set_collation_type(ObCharset::get_default_collation(ObCharset::get_default_charset()));
            break;
          }
          case DESCRIPTION: {
            cells_[cell_idx].set_varchar(OB_MONITOR_STATS[stat_iter_].description_);
            cells_[cell_idx].set_collation_type(ObCharset::get_default_collation(ObCharset::get_default_charset()));
            break;
          }
          case TYPE: {
            cells_[cell_idx].set_int(OB_MONITOR_STATS[stat_iter_].type_);
            break;
          }
          default: {
            ret = OB_ERR_UNEXPECTED;
            SERVER_LOG(WARN, "invalid column id", K(ret), K(cell_idx),
                       K(output_column_ids_), K(col_id));
            break;
          }
        }
        if (OB_SUCC(ret)) {
          cell_idx++;
        }
      }
    }

    if (OB_SUCC(ret)) {
      stat_iter_++;
      row = &cur_row_;
      if (ObSqlMonitorStatIds::MONITOR_STATNAME_END == stat_iter_) {
        stat_iter_++;
      }
    }
  }
  return ret;
}
