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

#include "observer/virtual_table/ob_all_virtual_id_service.h"
#include "share/rc/ob_module_provider.h"
#include "storage/tx_storage/ob_ls_service.h"

using namespace oceanbase::common;
using namespace oceanbase::share;
using namespace oceanbase::share::schema;
using namespace oceanbase::transaction;

namespace oceanbase
{
namespace observer
{

void ObAllVirtualIDService::reset()
{
  init_ = false;
  
  service_types_index_ = -1;
  for(int i=0; i<transaction::ObIDService::MAX_SERVICE_TYPE; i++) {
    service_type_[i] = -1;
  }
  expire_time_ = 0;
  
  last_id_ = 0;
  limit_id_ = 0;
  rec_log_ts_.reset();
  latest_log_ts_.reset();
  pre_allocated_range_ = 0;
  submit_log_ts_ = 0;
  is_master_ = false;
  ObVirtualTableScannerIterator::reset();
}

int ObAllVirtualIDService::prepare_start_to_read_()
{
  int ret = OB_SUCCESS;
  const int64_t execute_timeout = 10 * 1000 * 1000; // 10s
  if (OB_FAIL(fill_ids_())) {
  } else {
    int64_t request_ts = ObTimeUtility::current_time();
    expire_time_ = request_ts + execute_timeout;
    start_to_read_ = true;
    transaction::ObIDService::get_all_id_service_type(service_type_);
  }
  return ret;
}

int ObAllVirtualIDService::get_next_info_()
{
  int ret = OB_SUCCESS;
  if (transaction::ObIDService::MAX_SERVICE_TYPE == service_types_index_ + 1) {
    ret = OB_ITER_END;
  } else {
    service_types_index_++;
  }
  if (OB_SUCC(ret)) {
    
    MOD_SCOPE {
      bool exist = false;
      if (OB_FAIL(share::g_mp->ls_service()->check_ls_exist(IDS_LS, exist))) {
      } else if (!exist) {
        ret = OB_LS_NOT_EXIST;
      } else {
        transaction::ObIDService *id_service = NULL;
        if (OB_FAIL(transaction::ObIDService::get_id_service(service_type_[service_types_index_], id_service))) {
        } else if (OB_ISNULL(id_service)) {
          ret = OB_ERR_UNEXPECTED;
          SERVER_LOG(WARN, "id service is null", K(ret), K(service_type_[service_types_index_]));
        } else {
          id_service->get_virtual_info(last_id_, limit_id_, rec_log_ts_, latest_log_ts_,
                                       pre_allocated_range_, submit_log_ts_, is_master_);
        }
      }
    }
  }

  return ret;
}

int ObAllVirtualIDService::fill_ids_()
{
  int ret = OB_SUCCESS;

  if (OB_ISNULL(GCTX.omt_)) {
    ret = OB_ERR_UNEXPECTED;
    SERVER_LOG(WARN, "failed to get multi tenant from GCTX", K(ret));
  }

  return ret;
}

int ObAllVirtualIDService::inner_get_next_row(ObNewRow *&row)
{
  int ret = OB_SUCCESS;

  if (!start_to_read_ && OB_FAIL(prepare_start_to_read_())) {
    SERVER_LOG(WARN, "prepare start to read error", K(ret), K(start_to_read_));
  } else {
    do {
      if (OB_FAIL(get_next_info_())) {
        if (OB_ITER_END != ret) {
          SERVER_LOG(WARN, "ObAllVirtualIDService iter error", K(ret));
        }
      }
    } while (OB_TENANT_NOT_IN_SERVER == ret || OB_LS_NOT_EXIST == ret);
  }
  if (OB_SUCC(ret)) {
    const ObAddr self = GCTX.self_addr();
    const int64_t col_count = output_column_ids_.count();
    for (int64_t i = 0; OB_SUCC(ret) && i < col_count; ++i) {
      uint64_t col_id = output_column_ids_.at(i);
      switch (col_id) {
      case OB_APP_MIN_COLUMN_ID: { // id_service_type
        cur_row_.cells_[i].set_int(service_type_[service_types_index_]);
        break;
      }
      case OB_APP_MIN_COLUMN_ID + 1: { // last_id
        cur_row_.cells_[i].set_int(last_id_);
        break;
      }
      case OB_APP_MIN_COLUMN_ID + 2: { // limit_id
        cur_row_.cells_[i].set_int(limit_id_);
        break;
      }
      case OB_APP_MIN_COLUMN_ID + 3: { // rec_log_scn
        uint64_t v = rec_log_ts_.is_valid() ? rec_log_ts_.get_val_for_inner_table_field() : 0;
        cur_row_.cells_[i].set_uint64(v);
        break;
      }
      case OB_APP_MIN_COLUMN_ID + 4: { // latest_log_scn
        uint64_t v = latest_log_ts_.is_valid() ? latest_log_ts_.get_val_for_inner_table_field() : 0;
        cur_row_.cells_[i].set_uint64(v);
        break;
      }
      case OB_APP_MIN_COLUMN_ID + 5: { // pre_allocated_range
        cur_row_.cells_[i].set_int(pre_allocated_range_);
        break;
      }
      case OB_APP_MIN_COLUMN_ID + 6: { // submit_log_ts
        cur_row_.cells_[i].set_int(submit_log_ts_);
        break;
      }
      case OB_APP_MIN_COLUMN_ID + 7: { // is_master
        cur_row_.cells_[i].set_bool(is_master_);
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
