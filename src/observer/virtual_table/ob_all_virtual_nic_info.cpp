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

#include "ob_all_virtual_nic_info.h"
#include "observer/ob_server.h"

namespace oceanbase
{
namespace observer
{
ObAllVirtualNicInfo::ObAllVirtualNicInfo()
    : ObVirtualTableScannerIterator(),
      is_end_(false)
{}

ObAllVirtualNicInfo::~ObAllVirtualNicInfo()
{
  reset();
}

void ObAllVirtualNicInfo::reset()
{
  is_end_ = false;
  ObVirtualTableScannerIterator::reset();
}

int ObAllVirtualNicInfo::inner_open()
{
  int ret = OB_SUCCESS;
  if (!start_to_read_) {
    start_to_read_ = true;
  }
  return ret;
}

int ObAllVirtualNicInfo::inner_get_next_row(common::ObNewRow *&row)
{
  int ret = OB_SUCCESS;
  if (!start_to_read_) {
    ret = OB_NOT_INIT;
    SERVER_LOG(WARN, "not inited", K(ret));
  } else if (is_end_) {
    ret = OB_ITER_END;
  } else {
    ObObj *cells = cur_row_.cells_;
    if (OB_UNLIKELY(nullptr == cells)) {
      ret = OB_ERR_UNEXPECTED;
      SERVER_LOG(WARN, "cur row cell is NULL", K(ret));
    } else {
      for (int64_t i = 0; OB_SUCC(ret) && i < output_column_ids_.count(); i++) {
        uint64_t col_id = output_column_ids_.at(i);
        switch (col_id) {
          case SPEED_MBPS: {
            // bytes/sec --> Mbits/sec: speed_Mbps = speed_byte_ps * 8 / 1024 / 1024 
            cells[i].set_int((ObServer::get_instance().get_network_speed()) >> 17);
            break;
          }
          default: {
            ret = OB_ERR_UNEXPECTED;
            SERVER_LOG(WARN, "unexpected column id", K(col_id), K(i), K(ret));
            break;
          }
        }
      }
      if (OB_SUCC(ret)) {
        is_end_ = true;
        row = &cur_row_;
      }
    }
  }
  return ret;
}

} // namespace observer
} // namespace oceanbase
