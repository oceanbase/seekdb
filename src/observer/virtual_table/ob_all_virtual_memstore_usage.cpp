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

#include "observer/virtual_table/ob_all_virtual_memstore_usage.h"
#include "share/rc/ob_server_runtime.h"
#include "storage/tx_storage/ob_memstore_freezer.h"

using namespace oceanbase::common;
namespace oceanbase
{
namespace observer
{

ObAllVirtualMemstoreUsage::ObAllVirtualMemstoreUsage()
    : ObVirtualTableScannerIterator()
{
}

ObAllVirtualMemstoreUsage::~ObAllVirtualMemstoreUsage()
{
  reset();
}

void ObAllVirtualMemstoreUsage::reset()
{
  ObVirtualTableScannerIterator::reset();
}

int ObAllVirtualMemstoreUsage::inner_get_next_row(ObNewRow *&row)
{
  int ret = OB_SUCCESS;
  if (NULL == allocator_) {
    ret = OB_NOT_INIT;
    SERVER_LOG(WARN, "allocator_ shouldn't be NULL", K(allocator_), K(ret));
  } else if (!start_to_read_) {
    ObObj *cells = NULL;
    // allocator_ is allocator of PageArena type, no need to free
    if (NULL == (cells = cur_row_.cells_)) {
      ret = OB_ERR_UNEXPECTED;
      SERVER_LOG(ERROR, "cur row cell is NULL", K(ret));
    } else {
      {
        int64_t active_span = 0;
        int64_t memstore_used = 0;
        int64_t freeze_trigger = 0;
        int64_t memstore_limit = 0;
        int64_t freeze_cnt = 0;
        SERVER_MODULE_SCOPE {
          storage::ObMemstoreFreezer *freezer = nullptr;
          if (FALSE_IT(freezer = ::oceanbase::share::server_service<::oceanbase::storage::ObMemstoreFreezer>())) {
          } else if (OB_FAIL(freezer->get_memstore_condition(active_span,
                                                               memstore_used,
                                                               freeze_trigger,
                                                               memstore_limit,
                                                               freeze_cnt))) {
            SERVER_LOG(WARN, "fail to get memstore used", K(ret));
          }
          for (int64_t i = 0; OB_SUCC(ret) && i < output_column_ids_.count(); ++i) {
            uint64_t col_id = output_column_ids_.at(i);
            switch (col_id) {
              case ACTIVE_SPAN:
                cells[i].set_int(active_span);
                break;
              case FREEZE_TRIGGER:
                cells[i].set_int(freeze_trigger);
                break;
              case FREEZE_CNT:
                cells[i].set_int(freeze_cnt);
                break;
              case MEMSTORE_USED:
                cells[i].set_int(memstore_used);
                break;
              case MEMSTORE_LIMIT:
                cells[i].set_int(memstore_limit);
                break;
              default:
                // abnormal column id
                ret = OB_ERR_UNEXPECTED;
                SERVER_LOG(WARN, "unexpected column id", K(ret));
                break;
            }
          }
          if (OB_SUCCESS == ret
              && OB_SUCCESS != (ret = scanner_.add_row(cur_row_))) {
            SERVER_LOG(WARN, "fail to add row", K(ret), K(cur_row_));
          }
        }
      }
      if (OB_SUCC(ret)) {
        row = &cur_row_;
      }
    }
  }
  return ret;
}

}/* ns observer*/
}/* ns oceanbase */
