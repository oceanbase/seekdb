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

#include "ob_information_session_status_table.h"

#include "share/datum/datum_value_cast.h"
#include "observer/ob_server_runtime_access.h"
#include "share/ob_time_utility2.h"
#include "sql/session/ob_sql_session_mgr.h"
#include "rpc/obmysql/ob_sql_sock_session.h"

namespace oceanbase
{
using namespace common;
using namespace share;

namespace observer
{
const char *const ObInfoSchemaSessionStatusTable::variables_name[] =
{
  "Threads_connected",
  "Uptime",
  "Compression",
  NULL,
};

ObInfoSchemaSessionStatusTable::ObInfoSchemaSessionStatusTable() :
    ObVirtualTableScannerIterator(), cur_session_(NULL), global_ctx_(NULL)
{
}

ObInfoSchemaSessionStatusTable::~ObInfoSchemaSessionStatusTable()
{
  reset();
}

void ObInfoSchemaSessionStatusTable::reset()
{
  ObVirtualTableScannerIterator::reset();
  cur_session_ = NULL;
  global_ctx_ = NULL;
}

int ObInfoSchemaSessionStatusTable::fetch_all_session_status(
    AllStatus &all_status)
{
  int ret = OB_SUCCESS;

  if (OB_ISNULL(global_ctx_) ||
      OB_ISNULL(get_observer_sql_session_mgr())) {
    ret = OB_ERR_UNEXPECTED;
    SERVER_LOG(WARN, "global_ctx_ is NULL or session_mgr_ is NULL", K(ret));
  } else {
    const int64_t value_count = ARRAYSIZEOF(variables_name) - 1;
    DatumValue *status_values = nullptr;
    if (OB_FAIL(DatumValueBuffer::allocate(row_calc_buf_, value_count, status_values))) {}
    for (int64_t i = 0; OB_SUCC(ret) && NULL != variables_name[i]; ++i) {
      DatumValue &obj = status_values[i];
      obj.reset();
      switch (i) {
      case THREADS_CONNECTED: {
          int64_t session_cnt = 0;
          if (OB_FAIL(get_observer_sql_session_mgr()->get_session_count(session_cnt))) {
          } else {
            ret = DatumValueBuffer::set_int(status_values, value_count, i, session_cnt);
          }
          break;
        }
      case UPTIME: {
          int64_t uptime = ObTimeUtility2::extract_second(ObTimeUtility::current_time())
              - ObTimeUtility2::extract_second(global_ctx_->start_time_);
          ret = DatumValueBuffer::set_int(status_values, value_count, i, uptime);
          break;
        }
      case COMPRESSION: {
          const obmysql::ObSqlSockSession *sock_session = cur_session_->get_sock_desc().sock_desc_;
          const bool compressed = NULL != sock_session &&
              common::OB_MYSQL_COMPRESS_CS_TYPE == sock_session->conn_.get_cs_protocol_type();
          if (OB_FAIL(DatumValueBuffer::set_varchar_view(status_values, value_count, i,
                  ObString::make_string(compressed ? "ON" : "OFF")))) {
          }
          obj.set_collation_type(ObCharset::get_system_collation());
          break;
        }
      default: {
          ret = OB_ERR_UNEXPECTED;
          break;
        }
      }
      if (OB_SUCC(ret)) {
        if (OB_FAIL(all_status.set_refactored(variables_name[i], obj))) {
        }
      }
    }
  }

  return ret;
}

int ObInfoSchemaSessionStatusTable::inner_get_next_row(ObNewRow *&row)
{
  int ret = OB_SUCCESS;

  if (OB_ISNULL(allocator_) || OB_ISNULL(global_ctx_) || OB_ISNULL(cur_session_)) {
    ret = OB_NOT_INIT;
    SERVER_LOG(WARN, "allocator is NULL or gloabl_ctx_ or cur_session_ is NULL", K(ret),
               KP(allocator_), KP(global_ctx_), KP(cur_session_));
  } else {
    if (!start_to_read_) {
      DatumValue *cells = NULL;
      const int64_t col_count = output_column_ids_.count();
      if (OB_ISNULL(cells = cur_row_.cells_)) {
        ret = OB_ERR_UNEXPECTED;
        SERVER_LOG(WARN, "cur row cell is NULL", K(ret));
      } else if (OB_UNLIKELY(col_count < 0 ||
                             col_count > SESSION_STATUS_COLUMN_COUNT)) {
        ret = OB_ERR_UNEXPECTED;
        SERVER_LOG(WARN, "column count error ", K(ret), K(col_count));
      } else if (OB_UNLIKELY(col_count > reserved_column_cnt_)) {
        ret = OB_ERR_UNEXPECTED;
        SERVER_LOG(WARN, "cells count error", K(ret), K(col_count),
                   K(reserved_column_cnt_));
      } else {
        AllStatus all_status;
        if (OB_FAIL(all_status.create(SESSION_STATUS_MAP_BUCKET_NUM,
                                      ObModIds::OB_HASH_BUCKET_SESSION_STATUS_MAP))) {
        } else if (OB_FAIL(fetch_all_session_status(all_status))) {
        } else {
          DatumValue casted_cell;
          const ObDataTypeCastParams dtc_params
                  = sql::ObBasicSessionInfo::create_dtc_params(session_);
          ObCastCtx cast_ctx(allocator_, &dtc_params, CM_NONE, ObCharset::get_system_collation());
          AllStatus::const_iterator it_begin = all_status.begin();
          AllStatus::const_iterator it_end = all_status.end();
          for (; OB_SUCC(ret) && it_begin != it_end; ++it_begin) {
            uint64_t cell_idx = 0;
            for (int64_t j = 0; OB_SUCC(ret) && j < col_count; ++j) {
              uint64_t col_id = output_column_ids_.at(j);
              switch (col_id) {
                case VARIABLE_NAME: {
                  (OB_SUCC(ret) ? ret = DatumValueBuffer::set_varchar_view(cells, cur_row_.count_, cell_idx, it_begin->first) : ret);
                  cells[cell_idx].set_collation_type(ObCharset::get_default_collation(

                                                         ObCharset::get_default_charset()));
                  break;
                }
                case VARIABLE_VALUE: {
                  casted_cell.reset();
                  if (OB_FAIL(DatumValueCaster::to_type(ObVarcharType, cast_ctx,
                          it_begin->second, *allocator_, casted_cell))) {
                  } else {
                    ret = DatumValueBuffer::assign_view(casted_cell, cells, cur_row_.count_, cell_idx);
                  }
                  break;
                }
                default: {
                  ret = OB_ERR_UNEXPECTED;
                  SERVER_LOG(WARN, "invalid column id", K(ret), K(cell_idx), K(j),
                             K(output_column_ids_), K(col_id));
                  break;
                }
              }
              if (OB_SUCC(ret)) {
                ++cell_idx;
              }
            }
            if (OB_SUCCESS == ret && OB_FAIL(scanner_.add_row(cur_row_))) {
              SERVER_LOG(WARN, "fail to add row", K(ret), K(cur_row_));
            }
          }
          if (OB_SUCC(ret)) {
            scanner_it_ = scanner_.begin();
            start_to_read_ = true;
          }
        }
        // No need to consider the return code, memory should be released regardless of success or failure
        int tmp_ret = OB_SUCCESS;
        if (OB_SUCCESS != (tmp_ret = all_status.destroy())) {
          SERVER_LOG(WARN, "fail to destroy all status", K(tmp_ret));
          ret = tmp_ret;
        }
      }
    }
    if (OB_SUCCESS == ret && start_to_read_) {
      if (OB_FAIL(scanner_it_.get_next_row(cur_row_))) {
        if (OB_ITER_END != ret) {
          SERVER_LOG(WARN, "fail to get next row", K(ret));
        }
      } else {
        row = &cur_row_;
      }
    }
  }

  return ret;
}

} // namespace observer
} // namespace oceanbase
