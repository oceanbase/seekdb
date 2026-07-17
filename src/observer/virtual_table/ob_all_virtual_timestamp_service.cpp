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

#include "observer/virtual_table/ob_all_virtual_timestamp_service.h"
#include "share/rc/ob_module_provider.h"
#include "storage/tx/ob_timestamp_access.h"

using namespace oceanbase::common;
using namespace oceanbase::share;
using namespace oceanbase::share::schema;
using namespace oceanbase::transaction;

namespace oceanbase
{
namespace observer
{

void ObAllVirtualTimestampService::reset()
{
  ts_value_ = 0;
  ObVirtualTableScannerIterator::reset();
}

int ObAllVirtualTimestampService::inner_get_next_row(ObNewRow *&row)
{
  int ret = OB_SUCCESS;

  if (start_to_read_) {
    ret = OB_ITER_END;
  } else {
    start_to_read_ = true;
    MOD_SCOPE {
      share::g_mp->timestamp_access()->get_virtual_info(ts_value_);
    }
  }
  if (OB_SUCC(ret)) {
    const int64_t col_count = output_column_ids_.count();
    for (int64_t i = 0; OB_SUCC(ret) && i < col_count; ++i) {
      uint64_t col_id = output_column_ids_.at(i);
      switch (col_id) {
      case OB_APP_MIN_COLUMN_ID: { // ts_value
        cur_row_.cells_[i].set_int(ts_value_);
        break;
      }
      case OB_APP_MIN_COLUMN_ID + 1: { // ts_type
        cur_row_.cells_[i].set_varchar("GTS");
        cur_row_.cells_[i].set_collation_type(ObCharset::get_default_collation(ObCharset::get_default_charset()));
        break;
      }
      default: {
        ret = OB_ERR_UNEXPECTED;
        SERVER_LOG(WARN, "invalid coloum_id", K(ret), K(col_id));
        break;
      }
      } // switch
    } // for

    if (OB_SUCC(ret)) {
      row = &cur_row_;
    }
  }
  return ret;
}

} // observer
} // oceanbase
