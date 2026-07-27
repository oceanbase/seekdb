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
 
#include "ob_all_virtual_mds_node_stat.h"
#include "share/rc/ob_module_provider.h"
#include "storage/ls/ob_ls.h"
#include "storage/tx_storage/ob_ls_service.h"

namespace oceanbase
{
using namespace share;
using namespace storage;
using namespace storage::mds;
using namespace common;
using namespace omt;
namespace observer
{

static constexpr int64_t BUFFER_SIZE = 32_MB;

struct ApplyOnTabletOp {
  ApplyOnTabletOp(ObAllVirtualMdsNodeStat *table, char *temp_buffer) : table_(table), temp_buffer_(temp_buffer) {}
  int operator()(ObTablet &tablet) {
    int ret = OB_SUCCESS;
    MdsNodeInfoForVirtualTable mds_info;
    mds::MdsTableHandle mds_table_handle;
    ObArray<MdsNodeInfoForVirtualTable> row_array;
    if (OB_FAIL(table_->get_mds_table_handle_(tablet, mds_table_handle, false))) {
      if (OB_ENTRY_NOT_EXIST == ret) {
        ret = OB_SUCCESS;
      } else {
        MDS_LOG(WARN, "failed to get_mds_table_handle_", K(ret), K(*table_));
      }
    } else if (OB_FAIL(mds_table_handle.fill_virtual_info(row_array))) {
      MDS_LOG(WARN, "failed to fill_virtual_info from mds_table", K(ret), K(*table_));
    }
    if (OB_SUCC(ret)) {
      if (OB_FAIL(tablet.fill_virtual_info(row_array))) {
        MDS_LOG(WARN, "failed to fill_virtual_info from tablet", K(ret), K(*table_));
      } else {
        for (int64_t idx = 0; idx < row_array.count() && OB_SUCC(ret); ++idx) {
          if (OB_FAIL(table_->convert_node_info_to_row_(row_array[idx], temp_buffer_, BUFFER_SIZE, table_->cur_row_))) {
            MDS_LOG(WARN, "failed to convert_node_info_to_row_", K(ret), K(*table_));
          } else if (OB_FAIL(table_->scanner_.add_row(table_->cur_row_))) {
            MDS_LOG(WARN, "fail to add_row to scanner_", K(*table_));
          }
        }
      }
    }
    return ret;
  }
  ObAllVirtualMdsNodeStat *table_;
  char *temp_buffer_;
};

int ObAllVirtualMdsNodeStat::get_mds_table_handle_(ObTablet &tablet,
                                                   mds::MdsTableHandle &handle,
                                                   const bool create_if_not_exist)
{
  return tablet.get_mds_table_handle_(handle, create_if_not_exist);
}

int ObAllVirtualMdsNodeStat::inner_get_next_row(common::ObNewRow *&row)
{
  int ret = OB_SUCCESS;
  if (false == start_to_read_) {
    if (OB_FAIL(get_primary_key_ranges_())) {
      MDS_LOG(WARN, "fail to get index scan ranges", KR(ret), K(*this));
    } else if (tablet_points_.empty()) {
      ret = OB_NOT_SUPPORTED;
      MDS_LOG(WARN, "tablet_id must be specified", KR(ret), K(*this));
    } else {
      char *temp_buffer = nullptr;
      if (OB_ISNULL(temp_buffer = (char *)server_malloc(BUFFER_SIZE, "VirMdsStat"))) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        MDS_LOG(WARN, "fail to alloc buffer", K(*this));
      } else {
        ApplyOnTabletOp apply_on_table_op(this, temp_buffer);
        ObLS *ls = nullptr;
        ObLSService *ls_service = share::g_mp->ls_service();
        if (OB_ISNULL(ls_service)) {
          ret = OB_ERR_UNEXPECTED;
          MDS_LOG(WARN, "ls service is null", K(ret));
        } else if (OB_FAIL(ls_service->get_ls(ls))) {
          MDS_LOG(WARN, "get log stream failed", K(ret));
        } else if (OB_FAIL(get_tablet_info_(*ls, apply_on_table_op))) {
          MDS_LOG(WARN, "iterate mds nodes failed", K(ret));
          ret = OB_SUCCESS;
        }
        if (OB_FAIL(ret)) {
          MDS_LOG(WARN, "iterate mds node failed", K(ret), K(*this));
        } else {
          scanner_it_ = scanner_.begin();
          start_to_read_ = true;
        }
        server_free(temp_buffer);
      }
    }
  }
  if (OB_SUCC(ret) && true == start_to_read_) {
    if (OB_FAIL(scanner_it_.get_next_row(cur_row_))) {
      if (OB_ITER_END != ret) {
        MDS_LOG(WARN, "failed to get_next_row", K(ret), K(*this));
      }
    } else {
      row = &cur_row_;
    }
  }
  return ret;
}

int ObAllVirtualMdsNodeStat::convert_node_info_to_row_(const storage::mds::MdsNodeInfoForVirtualTable &node_info,
                                                       char *buffer,
                                                       int64_t buffer_size,
                                                       common::ObNewRow &row)
{
  int ret = OB_SUCCESS;
  const int64_t count = output_column_ids_.count();
  for (int64_t i = 0; OB_SUCC(ret) && i < count; ++i) {
    uint64_t col_id = output_column_ids_.at(i);
    switch (col_id) {
      case OB_APP_MIN_COLUMN_ID + 0: {// tablet_id
        cur_row_.cells_[i].set_int(node_info.tablet_id_.id());
        break;
      }
      
      case OB_APP_MIN_COLUMN_ID + 1: {// user_key
        int64_t write_n = node_info.user_key_.to_string(buffer, buffer_size);
        buffer += write_n;
        buffer_size -= write_n;
        cur_row_.cells_[i].set_string(ObLongTextType, ObString(write_n, buffer - write_n));
        break;
      }
      case OB_APP_MIN_COLUMN_ID + 2: {// version_idx
        cur_row_.cells_[i].set_int(node_info.version_idx_);
        break;
      }
      case OB_APP_MIN_COLUMN_ID + 3: {// writer_type
        int64_t pos = 0;
        databuff_printf(buffer, buffer_size, pos, "%s", mds::obj_to_string(node_info.writer_.writer_type_));
        buffer += pos;
        buffer_size -= pos;
        cur_row_.cells_[i].set_string(ObLongTextType, ObString(pos, buffer - pos));
        break;
      }
      case OB_APP_MIN_COLUMN_ID + 4: {// writer_id
        cur_row_.cells_[i].set_int(node_info.writer_.writer_id_);
        break;
      }
      case OB_APP_MIN_COLUMN_ID + 5: {// seq_no
        cur_row_.cells_[i].set_int(node_info.seq_no_.cast_to_int());
        break;
      }
      case OB_APP_MIN_COLUMN_ID + 6: {// redo_scn
        cur_row_.cells_[i].set_uint64(node_info.redo_scn_.get_val_for_inner_table_field());
        break;
      }
      case OB_APP_MIN_COLUMN_ID + 7: {// end_scn
        cur_row_.cells_[i].set_uint64(node_info.end_scn_.get_val_for_inner_table_field());
        break;
      }
      case OB_APP_MIN_COLUMN_ID + 8: {// trans_version
        cur_row_.cells_[i].set_uint64(node_info.trans_version_.get_val_for_inner_table_field());
        break;
      }
      case OB_APP_MIN_COLUMN_ID + 9: {// node_type
        int64_t pos = 0;
        databuff_printf(buffer, buffer_size, pos, "%s", mds::obj_to_string(node_info.node_type_));
        buffer += pos;
        buffer_size -= pos;
        cur_row_.cells_[i].set_string(ObLongTextType, ObString(pos, buffer - pos));
        break;
      }
      case OB_APP_MIN_COLUMN_ID + 10: {// state
        int64_t pos = 0;
        databuff_printf(buffer, buffer_size, pos, "%s", mds::obj_to_string(node_info.state_));
        buffer += pos;
        buffer_size -= pos;
        cur_row_.cells_[i].set_string(ObLongTextType, ObString(pos, buffer - pos));
        break;
      }
      case OB_APP_MIN_COLUMN_ID + 11: {// position
        int64_t pos = 0;
        databuff_printf(buffer, buffer_size, pos, "%s", mds::obj_to_string(node_info.position_));
        buffer += pos;
        buffer_size -= pos;
        cur_row_.cells_[i].set_string(ObLongTextType, ObString(pos, buffer - pos));
        break;
      }
      case OB_APP_MIN_COLUMN_ID + 12: {// user_data
        int64_t write_n = node_info.user_data_.to_string(buffer, buffer_size);
        buffer += write_n;
        buffer_size -= write_n;
        cur_row_.cells_[i].set_string(ObLongTextType, ObString(write_n, buffer - write_n));
        break;
      }
    }
  }
  return ret;
}

int ObAllVirtualMdsNodeStat::get_primary_key_ranges_()
{
  int ret = OB_SUCCESS;
  // In single-node mode, rowkey only has tablet_id (index 0)
  if (key_ranges_.count() >= 1) {
    for (int64_t i = 0; OB_SUCC(ret) && i < key_ranges_.count(); i++) {
      ObNewRange &key_range = key_ranges_.at(i);
      if (OB_UNLIKELY(key_range.get_start_key().get_obj_cnt() < 1
                      || key_range.get_end_key().get_obj_cnt() < 1)) {
        ret = OB_ERR_UNEXPECTED;
        MDS_LOG(ERROR, "unexpected  # of rowkey columns",
                  K(ret),
                  "size of start key", key_range.get_start_key().get_obj_cnt(),
                  "size of end key", key_range.get_end_key().get_obj_cnt());
      } else {
        ObObj tablet_obj_low = (key_range.get_start_key().get_obj_ptr()[0]);
        ObObj tablet_obj_high = (key_range.get_end_key().get_obj_ptr()[0]);

        ObTabletID tablet_low = tablet_obj_low.is_min_value() ? ObTabletID(0) : ObTabletID(tablet_obj_low.get_uint64());
        ObTabletID tablet_high = tablet_obj_high.is_max_value() ? ObTabletID(UINT64_MAX) : ObTabletID(tablet_obj_high.get_uint64());

        if (tablet_low == tablet_high) {
          if (OB_FAIL(tablet_points_.push_back(tablet_low))) {
            MDS_LOG(WARN, "fail to push back", KR(ret), K(*this));
          }
        } else if (OB_SUCCESS != (ret =
          (tablet_ranges_.push_back(ObTuple<common::ObTabletID, common::ObTabletID>(tablet_low, tablet_high))))) {
          MDS_LOG(WARN, "fail to push back", KR(ret), K(*this));
        }
      }
    }
  }
  MDS_LOG(INFO, "get_primary_key_ranges_", KR(ret), K(key_ranges_), K(*this));
  return ret;
}

int ObAllVirtualMdsNodeStat::get_tablet_info_(ObLS &ls, const ObFunction<int(ObTablet &)> &apply_on_tablet_op)
{
  int ret = OB_SUCCESS;
  if (!apply_on_tablet_op.is_valid()) {
    ret = OB_INVALID_ARGUMENT;
    MDS_LOG(ERROR, "invalid ob function", KR(ret), K(key_ranges_), K(*this));
  } else {
    for (int64_t idx = 0; idx < tablet_points_.count() && OB_SUCC(ret); ++idx) {
      ObTabletHandle tablet_handle;
      if (OB_FAIL(ls.get_tablet(tablet_points_[idx], tablet_handle, 0, storage::ObMDSGetTabletMode::READ_WITHOUT_CHECK))) {
        MDS_LOG(WARN, "fail to get tablet", KR(ret), K(key_ranges_), K(*this));
      } else if (OB_ISNULL(tablet_handle.get_obj())) {
        ret = OB_ERR_UNEXPECTED;
        MDS_LOG(ERROR, "get null tablet ptr", KR(ret), K(key_ranges_), K(*this));
      } else if (OB_FAIL(apply_on_tablet_op(*tablet_handle.get_obj()))) {
        MDS_LOG(WARN, "fail to apply op on tablet", KR(ret), K(key_ranges_), K(*this));
      }
    }
  }
  MDS_LOG(INFO, "get_tablet_info_", KR(ret), K(key_ranges_), K(*this));
  return ret;
}

}
}
