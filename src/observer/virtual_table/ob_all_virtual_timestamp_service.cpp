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

#include "observer/virtual_table/ob_all_virtual_timestamp_service.h"
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

void ObAllVirtualTimestampService::reset()
{
  init_ = false;
  
  
  ts_value_ = 0;
  service_role_ = ObTimestampAccess::ServiceType::FOLLOWER;
  is_primary_ = false;
  role_ = common::ObRole::FOLLOWER;
  service_epoch_ = 0;
  done_ = false;
  ObVirtualTableScannerIterator::reset();
}

int ObAllVirtualTimestampService::prepare_start_to_read_()
{
  int ret = OB_SUCCESS;
  const int64_t execute_timeout = 10 * 1000 * 1000; // 10s
  if (OB_FAIL(fill_ids_())) {
  } else {
    start_to_read_ = true;
  }
  return ret;
}

int ObAllVirtualTimestampService::get_next_info_()
{
  int ret = OB_SUCCESS;
  if (done_) {
    ret = OB_ITER_END;
  }
  if (OB_SUCC(ret)) {
    
    MOD_SCOPE {
      bool exist = false;
      if (OB_FAIL(share::g_mp->ls_service()->check_ls_exist(IDS_LS, exist))) {
      } else if (!exist) {
        ret = OB_LS_NOT_EXIST;
        done_ = true;
      } else {
        share::g_mp->timestamp_access()->get_virtual_info(ts_value_, service_role_, role_, service_epoch_);
        is_primary_ = true;
        done_ = true;
      }
    } else {
      done_ = true;
    }
  }

  return ret;
}

int ObAllVirtualTimestampService::fill_ids_()
{
  int ret = OB_SUCCESS;

  if (OB_ISNULL(GCTX.omt_)) {
    ret = OB_ERR_UNEXPECTED;
    SERVER_LOG(WARN, "failed to get multi tenant from GCTX", K(ret));
  }

  return ret;
}

int ObAllVirtualTimestampService::inner_get_next_row(ObNewRow *&row)
{
  int ret = OB_SUCCESS;

  if (!start_to_read_ && OB_FAIL(prepare_start_to_read_())) {
    SERVER_LOG(WARN, "prepare start to read error", K(ret), K(start_to_read_));
  } else {
    do {
      if (OB_FAIL(get_next_info_())) {
        if (OB_ITER_END != ret && OB_LS_NOT_EXIST != ret) {
          SERVER_LOG(WARN, "ObAllVirtualTimestampService iter error", K(ret));
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
      case OB_APP_MIN_COLUMN_ID: { // ts_value
        cur_row_.cells_[i].set_int(ts_value_);
        break;
      }
      case OB_APP_MIN_COLUMN_ID + 1: { // ts_type
        cur_row_.cells_[i].set_varchar(ObTimestampAccess::ts_type_to_cstr(is_primary_));
        cur_row_.cells_[i].set_collation_type(ObCharset::get_default_collation(ObCharset::get_default_charset()));
        break;
      }
      case OB_APP_MIN_COLUMN_ID + 2: { // service_role
        cur_row_.cells_[i].set_varchar((ObTimestampAccess::service_type_to_cstr(service_role_)));
        cur_row_.cells_[i].set_collation_type(ObCharset::get_default_collation(ObCharset::get_default_charset()));
        break;
      }
      case OB_APP_MIN_COLUMN_ID + 3: { // role
        if (OB_FAIL(role_to_string(role_, role_str_, sizeof(role_str_)))) {
        } else {
          cur_row_.cells_[i].set_varchar(ObString::make_string(role_str_));
          cur_row_.cells_[i].set_collation_type(ObCharset::get_default_collation(
                                                ObCharset::get_default_charset()));
        }
        break;
      }
      case OB_APP_MIN_COLUMN_ID + 4: { // service_epoch
        cur_row_.cells_[i].set_int(service_epoch_);
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
