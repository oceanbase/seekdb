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

#define USING_LOG_PREFIX STORAGE

#include "ob_all_virtual_checkpoint_diagnose_info.h"
#include "src/observer/virtual_table/ob_all_virtual_checkpoint_diagnose_info.h"

namespace oceanbase
{
using namespace share;
using namespace storage;
using namespace common;
using namespace omt;
namespace observer
{

bool ObAllVirtualCheckpointDiagnoseInfo::is_need_process(uint64_t tenant_id)
{
  if (is_sys_tenant(effective_tenant_id_) || tenant_id == effective_tenant_id_) {
    return true;
  }
  return false;
}

int GenerateTraceRow::operator()(const storage::checkpoint::ObTraceInfo &trace_info) const
{
  int ret = OB_SUCCESS;
  for (int64_t i = 0; OB_SUCC(ret) && i < virtual_table_.output_column_ids_.count(); ++i) {
    uint64_t col_id = virtual_table_.output_column_ids_.at(i);
    switch (col_id) {

      // trace_id
      case OB_APP_MIN_COLUMN_ID + 0:
        virtual_table_.cur_row_.cells_[i].set_int(trace_info.trace_id_);
        break;
      // freeze_clock
      case OB_APP_MIN_COLUMN_ID + 1:
        virtual_table_.cur_row_.cells_[i].set_uint32(trace_info.freeze_clock_);
        break;
      // checkpoint_thread_name
      case OB_APP_MIN_COLUMN_ID + 2:
        virtual_table_.cur_row_.cells_[i].set_varchar(trace_info.thread_name_);
        virtual_table_.cur_row_.cells_[i].set_collation_type(ObCharset::get_default_collation(ObCharset::get_default_charset()));
        break;
      // checkpoint_start_time
      case OB_APP_MIN_COLUMN_ID + 3:
        virtual_table_.cur_row_.cells_[i].set_timestamp(trace_info.checkpoint_start_time_);
        break;
      default:
        ret = OB_ERR_UNEXPECTED;
        SERVER_LOG(WARN, "unexpected type", KR(ret), K(col_id));
        break;
    }
  }
  if (OB_SUCC(ret)) {
    if (OB_FAIL(virtual_table_.scanner_.add_row(virtual_table_.cur_row_))) {
      SERVER_LOG(WARN, "failed to add row", K(ret), K(virtual_table_.cur_row_));
      if (OB_SIZE_OVERFLOW == ret) {
        ret = OB_SUCCESS;
      }
    }
  }
  return ret;
}

int ObAllVirtualCheckpointDiagnoseInfo::process_curr_tenant(common::ObNewRow *&row)
{
  int ret = OB_SUCCESS;
  if (!start_to_read_) {
    SERVER_LOG(INFO, "__all_virtual_checkpoint_diagnose_info start");
    if(OB_FAIL(MTL(storage::checkpoint::ObCheckpointDiagnoseMgr*)->read_trace_info(GenerateTraceRow(*this)))) {
      SERVER_LOG(WARN, "failed to read trace info", K(ret), K(cur_row_));

    } else {
      scanner_it_ = scanner_.begin();
      start_to_read_ = true;
    }
  }
  if (OB_FAIL(ret)) {
  } else if (OB_FAIL(scanner_it_.get_next_row(cur_row_))) {
    if (OB_ITER_END != ret) {
      SERVER_LOG(WARN, "fail to get next row", K(ret));
    }
  } else {
    row = &cur_row_;
  }
  
  return ret;
}

void ObAllVirtualCheckpointDiagnoseInfo::release_last_tenant()
{
  scanner_.reuse();
  start_to_read_ = false;
}

}
}
