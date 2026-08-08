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

#include "observer/virtual_table/ob_all_virtual_tablet_ddl_kv_info.h"
#include "share/rc/ob_server_runtime.h"
#include "storage/ls/ob_ls.h"
#include "storage/tx_storage/ob_ls_service.h"
#include "storage/ddl/ob_tablet_ddl_kv.h"

using namespace oceanbase::common;
using namespace oceanbase::storage;
namespace oceanbase
{
namespace observer
{

ObAllVirtualTabletDDLKVInfo::ObAllVirtualTabletDDLKVInfo()
    : ObVirtualTableScannerIterator(),
      ls_(nullptr),
      tablet_iter_(ObMDSGetTabletMode::READ_ALL_COMMITED),
      ddl_kvs_handle_(),
      curr_tablet_id_(),
      ddl_kv_idx_(-1)
{
}

ObAllVirtualTabletDDLKVInfo::~ObAllVirtualTabletDDLKVInfo()
{
  reset();
}

void ObAllVirtualTabletDDLKVInfo::reset()
{
  ddl_kv_idx_ = -1;
  ddl_kvs_handle_.reset();
  curr_tablet_id_.reset();
  tablet_iter_.reset();
  ls_ = nullptr;
  ObVirtualTableScannerIterator::reset();
}

int ObAllVirtualTabletDDLKVInfo::get_next_ddl_kv_mgr(ObDDLKvMgrHandle &ddl_kv_mgr_handle)
{
  int ret = OB_SUCCESS;
  if (!tablet_iter_.is_valid()) {
    if (OB_FAIL(::oceanbase::share::server_service<::oceanbase::storage::ObLSService>()->get_ls(ls_))) {
    } else if (OB_FAIL(ls_->build_tablet_iter(tablet_iter_))) {
    }
  }
  if (OB_SUCC(ret) && OB_FAIL(tablet_iter_.get_next_ddl_kv_mgr(ddl_kv_mgr_handle))) {
    if (OB_ITER_END != ret) {
      SERVER_LOG(WARN, "fail to get next tablet", K(ret));
    }
  } else if (OB_SUCC(ret)) {
    curr_tablet_id_ = ddl_kv_mgr_handle.get_obj()->get_tablet_id();
  }
  return ret;
}

int ObAllVirtualTabletDDLKVInfo::get_next_ddl_kv(ObDDLKV *&ddl_kv)
{
  int ret = OB_SUCCESS;
  while (OB_SUCC(ret)) {
    if (ddl_kv_idx_ < 0 || ddl_kv_idx_ >= ddl_kvs_handle_.count()) {
      ObDDLKvMgrHandle ddl_kv_mgr_handle;
      if (OB_FAIL(get_next_ddl_kv_mgr(ddl_kv_mgr_handle))) {
        if (OB_ITER_END != ret) {
          SERVER_LOG(WARN, "get_next_ddl_kv_mgr failed", K(ret));
        }
      } else if (OB_FAIL(ddl_kv_mgr_handle.get_obj()->get_ddl_kvs(false/*frozen_only*/, ddl_kvs_handle_))) {
      } else if (ddl_kvs_handle_.count() > 0) {
        ddl_kv_idx_ = 0;
      }
    }

    if (OB_SUCC(ret) && ddl_kv_idx_ >= 0 && ddl_kv_idx_ < ddl_kvs_handle_.count()) {
      ddl_kv = ddl_kvs_handle_.at(ddl_kv_idx_).get_obj();
      if (OB_ISNULL(ddl_kv)) {
        ret = OB_ERR_UNEXPECTED;
        SERVER_LOG(WARN, "fail to get ddl kv", K(ret), K(ddl_kv_idx_));
      } else {
        ddl_kv_idx_++;
        break;
      }
    }
  }
  return ret;
}

int ObAllVirtualTabletDDLKVInfo::inner_get_next_row(ObNewRow *&row)
{
  int ret = OB_SUCCESS;
  ObDDLKV *cur_kv = nullptr;
  const int64_t col_count = output_column_ids_.count();
  if (OB_FAIL(get_next_ddl_kv(cur_kv))) {
    if (OB_ITER_END != ret) {
      SERVER_LOG(WARN, "fail to get next tablet with ddl kv", K(ret));
    }
  } else if (OB_ISNULL(cur_kv)) {
    ret = OB_ERR_UNEXPECTED;
    SERVER_LOG(WARN, "current ddl kv is null", K(ret), KP(cur_kv));
  } else {
    for (int64_t i = 0; OB_SUCC(ret) && i < col_count; ++i) {
      uint64_t col_id = output_column_ids_.at(i);
      switch (col_id) {
        case OB_APP_MIN_COLUMN_ID + 0:
          // tablet_id
          cur_row_.cells_[i].set_int(curr_tablet_id_.id());
          break;
        case OB_APP_MIN_COLUMN_ID + 1:
          // freeze_scn
          cur_row_.cells_[i].set_uint64(cur_kv->get_freeze_scn().get_val_for_inner_table_field());
          break;
        case OB_APP_MIN_COLUMN_ID + 2:
          // start_scn
          cur_row_.cells_[i].set_uint64(cur_kv->get_ddl_start_scn().get_val_for_inner_table_field());
          break;
        case OB_APP_MIN_COLUMN_ID + 3:
          // min_scn
          cur_row_.cells_[i].set_uint64(cur_kv->get_min_scn().get_val_for_inner_table_field());
          break;
        case OB_APP_MIN_COLUMN_ID + 4:
          // macro_block_cnt
          cur_row_.cells_[i].set_int(cur_kv->get_macro_block_cnt());
          break;
        case OB_APP_MIN_COLUMN_ID + 5:
          // ref_cnt
          cur_row_.cells_[i].set_int(cur_kv->get_ref());
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

}/* ns observer*/
}/* ns oceanbase */
