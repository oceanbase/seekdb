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

#include "ob_all_virtual_ha_diagnose.h"
#include "share/rc/ob_module_provider.h"
#include "storage/tx_storage/ob_ls_service.h"

namespace oceanbase
{
namespace observer
{
int ObAllVirtualHADiagnose::inner_get_next_row(common::ObNewRow *&row)
{
  int ret = OB_SUCCESS;
  if (false == start_to_read_) {
    storage::DiagnoseInfo diagnose_info;
    storage::ObLSService *ls_service = share::g_mp->ls_service();
    if (NULL == ls_service) {
      ret = OB_ERR_UNEXPECTED;
      SERVER_LOG(WARN, "ls service is null", K(ret));
    } else if (OB_FAIL(ls_service->diagnose(diagnose_info))) {
      SERVER_LOG(WARN, "diagnose ls failed", K(ret));
    } else if (OB_FAIL(insert_stat_(diagnose_info))) {
      SERVER_LOG(WARN, "insert stat failed", K(ret), K(diagnose_info));
    // Some varchar cells reference buffers owned by diagnose_info, so copy the row
    // into scanner_ before diagnose_info is destroyed.
    } else if (OB_FAIL(scanner_.add_row(cur_row_))) {
      SERVER_LOG(WARN, "add diagnose info to scanner failed", K(ret), K(diagnose_info));
    } else {
      scanner_it_ = scanner_.begin();
      start_to_read_ = true;
      SERVER_LOG(INFO, "diagnose ls success", K(diagnose_info));
    }
    if (OB_FAIL(ret)) {
      SERVER_LOG(WARN, "iter tenant failed", K(ret));
    }
  }
  if (OB_SUCC(ret) && start_to_read_) {
    if (OB_FAIL(scanner_it_.get_next_row(cur_row_))) {
      if (OB_ITER_END != ret) {
        SERVER_LOG(WARN, "get next row failed", K(ret));
      }
    } else {
      row = &cur_row_;
    }
  }
  return ret;
}

int ObAllVirtualHADiagnose::insert_stat_(storage::DiagnoseInfo &diagnose_info)
{
  int ret = OB_SUCCESS;
  const int64_t count = output_column_ids_.count();
  for (int64_t i = 0; OB_SUCC(ret) && i < count; i++) {
    uint64_t col_id = output_column_ids_.at(i);
    switch (col_id) {
      case PALF_STATE:
        cur_row_.cells_[i].set_varchar(ObString::make_string(log_state_to_string(diagnose_info.palf_diagnose_info_.log_state_)));
        cur_row_.cells_[i].set_collation_type(ObCharset::get_default_collation(
                                              ObCharset::get_default_charset()));
        break;
      case MAX_APPLIED_SCN:
        cur_row_.cells_[i].set_uint64(diagnose_info.apply_diagnose_info_.max_applied_scn_.get_val_for_inner_table_field());
        break;
      case MAX_REPLAYED_LSN:
        cur_row_.cells_[i].set_uint64(diagnose_info.replay_diagnose_info_.max_replayed_lsn_.val_);
        break;
      case MAX_REPLAYED_SCN:
        cur_row_.cells_[i].set_uint64(diagnose_info.replay_diagnose_info_.max_replayed_scn_.get_val_for_inner_table_field());
        break;
      case REPLAY_DIAGNOSE_INFO:
        cur_row_.cells_[i].set_varchar((diagnose_info.replay_diagnose_info_.diagnose_str_.string()));
        cur_row_.cells_[i].set_collation_type(ObCharset::get_default_collation(
                                              ObCharset::get_default_charset()));
        break;
      case CHECKPOINT_SCN:
        cur_row_.cells_[i].set_uint64(diagnose_info.ls_clog_checkpoint_stat_.clog_checkpoint_scn_.get_val_for_inner_table_field());
        break;
      case MIN_REC_SCN:
        cur_row_.cells_[i].set_uint64(diagnose_info.ls_clog_checkpoint_stat_.min_rec_scn_.get_val_for_inner_table_field());
        break;
      case MIN_REC_SCN_LOG_TYPE:
        if (OB_FAIL(log_base_type_to_string(diagnose_info.ls_clog_checkpoint_stat_.min_rec_scn_log_type_,
                                            min_rec_log_scn_log_type_str_,
                                            sizeof(min_rec_log_scn_log_type_str_)))) {
          SERVER_LOG(WARN, "log_base_type_to_string failed", K(ret), K(diagnose_info));
        } else {
          cur_row_.cells_[i].set_varchar(ObString::make_string(min_rec_log_scn_log_type_str_));
          cur_row_.cells_[i].set_collation_type(ObCharset::get_default_collation(
                                                ObCharset::get_default_charset()));
        }
        break;
      case READ_TX:
        cur_row_.cells_[i].set_varchar(diagnose_info.read_only_tx_info_);
        cur_row_.cells_[i].set_collation_type(ObCharset::get_default_collation(
                                              ObCharset::get_default_charset()));

        break;
      default:
        ret = OB_ERR_UNEXPECTED;
        SERVER_LOG(WARN, "unkown column");
        break;
    }
  }
  return ret;
}

} // namespace observer
} // namespace oceanbase
