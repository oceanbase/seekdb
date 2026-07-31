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

#include "observer/virtual_table/ob_all_virtual_ls_info.h"
#include "share/rc/ob_server_runtime.h"
#include "storage/ls/ob_ls.h"
#include "storage/tx_storage/ob_ls_service.h"

using namespace oceanbase::common;
using namespace oceanbase::storage;
namespace oceanbase
{
namespace observer
{

ObAllVirtualLSInfo::ObAllVirtualLSInfo()
    : ObVirtualTableScannerIterator(),
      ls_(nullptr)
{
}

ObAllVirtualLSInfo::~ObAllVirtualLSInfo()
{
  reset();
}

void ObAllVirtualLSInfo::reset()
{
  ls_ = nullptr;
  ObVirtualTableScannerIterator::reset();
}

int ObAllVirtualLSInfo::inner_get_next_row(ObNewRow *&row)
{
  int ret = OB_SUCCESS;
  ObLSVTInfo ls_info;
  if (NULL == allocator_) {
    ret = OB_NOT_INIT;
    SERVER_LOG(WARN, "allocator_ shouldn't be NULL", K(allocator_), K(ret));
  } else if (start_to_read_) {
    ret = OB_ITER_END;
  } else if (OB_ISNULL(::oceanbase::share::server_service<::oceanbase::storage::ObLSService>())) {
    ret = OB_ERR_UNEXPECTED;
    SERVER_LOG(WARN, "ls service is null", K(ret));
  } else if (OB_FAIL(::oceanbase::share::server_service<::oceanbase::storage::ObLSService>()->get_ls(ls_))) {
    SERVER_LOG(WARN, "get log stream failed", K(ret));
  } else if (OB_FAIL(ls_->get_ls_info(ls_info))) {
    SERVER_LOG(WARN, "get log stream info failed", K(ret));
  } else {
    start_to_read_ = true;
    const int64_t col_count = output_column_ids_.count();
    for (int64_t i = 0; OB_SUCC(ret) && i < col_count; ++i) {
      uint64_t col_id = output_column_ids_.at(i);
      switch (col_id) {
        case OB_APP_MIN_COLUMN_ID:
          // tablet_count
          cur_row_.cells_[i].set_int(ls_info.tablet_count_);
          break;
        case OB_APP_MIN_COLUMN_ID + 1:
          // weak_read_timestamp
          cur_row_.cells_[i].set_uint64(ls_info.weak_read_scn_.get_val_for_inner_table_field());
          break;
        case OB_APP_MIN_COLUMN_ID + 2:
          // clog_checkpoint_ts
          cur_row_.cells_[i].set_uint64(!ls_info.checkpoint_scn_.is_valid() ? 0 : ls_info.checkpoint_scn_.get_val_for_tx());
          break;
        case OB_APP_MIN_COLUMN_ID + 3:
          // clog_checkpoint_lsn
          cur_row_.cells_[i].set_uint64(ls_info.checkpoint_lsn_ < 0 ? 0 : ls_info.checkpoint_lsn_);
          break;
        case OB_APP_MIN_COLUMN_ID + 4:
          // tablet_change_checkpoint_scn
          cur_row_.cells_[i].set_uint64(!ls_info.tablet_change_checkpoint_scn_.is_valid() ? 0 : ls_info.tablet_change_checkpoint_scn_.get_val_for_inner_table_field());
          break;
        case OB_APP_MIN_COLUMN_ID + 5:
          // tx blocked
          cur_row_.cells_[i].set_int(ls_info.tx_blocked_);
          break;
        default:
          ret = OB_ERR_UNEXPECTED;
          SERVER_LOG(WARN, "invalid col_id", K(ret), K(col_id));
          break;
      }
    }
  }
  if (OB_SUCC(ret)) {
    row = &cur_row_;
  }
  return ret;
}

} // observer
} // oceanbase
