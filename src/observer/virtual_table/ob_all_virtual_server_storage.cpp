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

#include "observer/virtual_table/ob_all_virtual_server_storage.h"
#include "observer/ob_server.h"

namespace oceanbase
{
namespace observer
{
ObAllVirtualServerStorage::ObAllVirtualServerStorage()
  : ObVirtualTableScannerIterator(),
    server_storage_info_array_(),
    storage_pos_(0)
{
  MEMSET(ip_buf_, 0, sizeof(ip_buf_));
}

ObAllVirtualServerStorage::~ObAllVirtualServerStorage() { reset(); }

void ObAllVirtualServerStorage::reset()
{
  server_storage_info_array_.reset();
  storage_pos_ = 0;
  MEMSET(ip_buf_, 0, sizeof(ip_buf_));
  ObVirtualTableScannerIterator::reset();
}

int ObAllVirtualServerStorage::inner_open()
{
  int ret = OB_SUCCESS;
  return ret;
}

int ObAllVirtualServerStorage::inner_get_next_row(ObNewRow *&row)
{
  int ret = OB_SUCCESS;
  row = nullptr;
  ObObj *cells = cur_row_.cells_;
  if (OB_UNLIKELY(nullptr == cells)) {
    ret = OB_NOT_INIT;
    SERVER_LOG(WARN, "not init", K(ret), KP(cur_row_.cells_));
  } else if (storage_pos_ >= server_storage_info_array_.count()) {
    row = nullptr;
    ret = OB_ITER_END;
  } else {
    ObServerStorageInfo &item = server_storage_info_array_.at(storage_pos_);
    for (int64_t i = 0; OB_SUCC(ret) && i < output_column_ids_.count(); ++i) {
      const uint64_t column_id = output_column_ids_.at(i);
      switch (column_id) {
        case PATH: {
          cells[i].set_varchar(item.path_);
          cells[i].set_collation_type(
            ObCharset::get_default_collation(ObCharset::get_default_charset()));
          break;
        }
        case ENDPOINT: {
          cells[i].set_varchar(item.endpoint_);
          cells[i].set_collation_type(
            ObCharset::get_default_collation(ObCharset::get_default_charset()));
          break;
        }
        case USED_FOR: {
          cells[i].set_varchar(item.used_for_);
          cells[i].set_collation_type(
            ObCharset::get_default_collation(ObCharset::get_default_charset()));
          break;
        }
        case STORAGE_ID: {
          cells[i].set_int(static_cast<int64_t>(item.storage_id_));
          break;
        }
        case MAX_IOPS: {
          cells[i].set_int(static_cast<int64_t>(item.max_iops_));
          break;
        }
        case MAX_BANDWIDTH: {
          cells[i].set_int(static_cast<int64_t>(item.max_bandwidth_));
          break;
        }
        case CREATE_TIME: {
          if (is_valid_timestamp_(item.create_time_)) {
            cells[i].set_timestamp(item.create_time_);
          } else {
            // if invalid timestamp, display NULL
            cells[i].reset();
          }
          break;
        }
        case OP_ID: {
          cells[i].set_int(static_cast<int64_t>(item.op_id_));
          break;
        }
        case SUB_OP_ID: {
          cells[i].set_int(static_cast<int64_t>(item.sub_op_id_));
          break;
        }
        case AUTHORIZATION: {
          cells[i].set_varchar(item.authorization_);
          cells[i].set_collation_type(
            ObCharset::get_default_collation(ObCharset::get_default_charset()));
          break;
        }
        case ENCRYPT_INFO: {
          cells[i].set_varchar(item.encrypt_info_);
          cells[i].set_collation_type(
            ObCharset::get_default_collation(ObCharset::get_default_charset()));
          break;
        }
        case STATE: {
          cells[i].set_varchar(item.state_);
          cells[i].set_collation_type(
            ObCharset::get_default_collation(ObCharset::get_default_charset()));
          break;
        }
        case STATE_INFO: {
          cells[i].set_varchar(item.state_info_);
          cells[i].set_collation_type(
            ObCharset::get_default_collation(ObCharset::get_default_charset()));
          break;
        }
        case LAST_CHECK_TIMESTAMP: {
          if (is_valid_timestamp_(item.last_check_timestamp_)) {
            cells[i].set_timestamp(item.last_check_timestamp_);
          } else {
            // if invalid timestamp, display NULL
            cells[i].reset();
          }
          break;
        }
        case EXTENSION: {
          cells[i].set_varchar(item.extension_);
          cells[i].set_collation_type(
            ObCharset::get_default_collation(ObCharset::get_default_charset()));
          break;
        }
        default: {
          ret = OB_ERR_UNEXPECTED;
          SERVER_LOG(WARN, "invalid column id", KR(ret), K(column_id));
          break;
        }
      } // end switch
    } // end for-loop
    if (OB_SUCC(ret)) {
      row = &cur_row_;
    }
    ++storage_pos_;
  }
  return ret;
}

bool ObAllVirtualServerStorage::is_valid_timestamp_(const int64_t timestamp) const
{
  bool ret_bool = true;
  if (INT64_MAX == timestamp || 0 > timestamp) {
    ret_bool = false;
  }
  return ret_bool;
}

} // namespace observer
} // namespace oceanbase
