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

#define USING_LOG_PREFIX SERVER

#include "ob_all_virtual_px_target_monitor.h"

namespace oceanbase
{
using namespace oceanbase::common;
using namespace oceanbase::sql;
namespace observer
{

ObAllVirtualPxTargetMonitor::ObAllVirtualPxTargetMonitor()
    : row_emitted_(false)
{}

int ObAllVirtualPxTargetMonitor::init()
{
  int ret = OB_SUCCESS;
  row_emitted_ = false;
  return ret;
}

int ObAllVirtualPxTargetMonitor::inner_open()
{
  row_emitted_ = false;
  return OB_SUCCESS;
}

int ObAllVirtualPxTargetMonitor::inner_get_next_row(common::ObNewRow *&row)
{
  int ret = OB_SUCCESS;
  ObObj *cells = NULL;
  ObPxTargetInfo target_info;
  if (row_emitted_) {
    ret = OB_ITER_END;
  } else if (OB_ISNULL(cells = cur_row_.cells_)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("cur row cell is NULL", K(ret));
  } else {
    OB_PX_TARGET_MONITOR.get_target_info(target_info);
    const int64_t col_count = output_column_ids_.count();
    for (uint64_t i = 0; OB_SUCC(ret) && i < col_count; ++i) {
      uint64_t col_id = output_column_ids_.at(i);
      switch (col_id) {

        case LOCAL_TARGET: {
          cur_row_.cells_[i].set_int(target_info.local_target_);
          break;
        }
        case LOCAL_TARGET_USED: {
          cur_row_.cells_[i].set_int(target_info.target_used_);
          break;
        }
        case LOCAL_PARALLEL_SESSION_COUNT: {
          cur_row_.cells_[i].set_int(target_info.local_parallel_session_count_);
          break;
        }
        default: {
          ret = OB_ERR_UNEXPECTED;
          LOG_WARN("invalid column_id", K(ret), K(col_id));
        }
      }
    }
    if (OB_SUCC(ret)) {
      row_emitted_ = true;
      row = &cur_row_;
    }
  }
  return ret;
}

int ObAllVirtualPxTargetMonitor::inner_close()
{
  return OB_SUCCESS;
}

}//namespace observer
}//namespace oceanbase
