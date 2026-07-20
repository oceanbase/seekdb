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

#include "observer/virtual_table/ob_all_virtual_tenant_memstore_info.h"
#include "share/rc/ob_module_provider.h"
#include "storage/tx_storage/ob_tenant_freezer.h"

using namespace oceanbase::common;
namespace oceanbase
{
namespace observer
{

ObAllVirtualTenantMemstoreInfo::ObAllVirtualTenantMemstoreInfo()
    : ObVirtualTableScannerIterator()
{
}

ObAllVirtualTenantMemstoreInfo::~ObAllVirtualTenantMemstoreInfo()
{
  reset();
}

void ObAllVirtualTenantMemstoreInfo::reset()
{
  ObVirtualTableScannerIterator::reset();
}

int ObAllVirtualTenantMemstoreInfo::inner_get_next_row(ObNewRow *&row)
{
  int ret = OB_SUCCESS;
  if (start_to_read_) {
    ret = OB_ITER_END;
  } else {
    start_to_read_ = true;
    int64_t active_span = 0;
    int64_t memstore_used = 0;
    int64_t freeze_trigger = 0;
    int64_t memstore_limit = 0;
    int64_t freeze_cnt = 0;
    MOD_SCOPE {
      storage::ObTenantFreezer *freezer = share::g_mp->tenant_freezer();
      if (OB_FAIL(freezer->get_tenant_memstore_cond(active_span,
                                                     memstore_used,
                                                     freeze_trigger,
                                                     memstore_limit,
                                                     freeze_cnt))) {
        SERVER_LOG(WARN, "fail to get memstore used", K(ret));
      }
      for (int64_t i = 0; OB_SUCC(ret) && i < output_column_ids_.count(); ++i) {
        switch (output_column_ids_.at(i)) {
          case ACTIVE_SPAN:
            cur_row_.cells_[i].set_int(active_span);
            break;
          case FREEZE_TRIGGER:
            cur_row_.cells_[i].set_int(freeze_trigger);
            break;
          case FREEZE_CNT:
            cur_row_.cells_[i].set_int(freeze_cnt);
            break;
          case MEMSTORE_USED:
            cur_row_.cells_[i].set_int(memstore_used);
            break;
          case MEMSTORE_LIMIT:
            cur_row_.cells_[i].set_int(memstore_limit);
            break;
          default:
            ret = OB_ERR_UNEXPECTED;
            SERVER_LOG(WARN, "unexpected column id", K(ret), K(output_column_ids_.at(i)));
            break;
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
