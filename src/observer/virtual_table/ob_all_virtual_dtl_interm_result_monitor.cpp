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

#include "observer/virtual_table/ob_all_virtual_dtl_interm_result_monitor.h"
#include "observer/omt/ob_server_runtime_controller.h"
#include "share/rc/ob_server_runtime.h"
#include "observer/ob_server_utils.h"
#include "sql/dtl/ob_dtl_interm_result_manager.h"

namespace oceanbase
{
using namespace common;
using namespace sql;
using namespace sql::dtl;
namespace observer
{

ObAllDtlIntermResultMonitor::ObAllDtlIntermResultMonitor()
{
}
ObAllDtlIntermResultMonitor::~ObAllDtlIntermResultMonitor()
{
}
void ObAllDtlIntermResultMonitor::reset()
{
}

#define GET_CHUNK_STORE_INFO(store)         \
  hold_mem = store->get_mem_hold();         \
  max_hold_mem = store->get_max_hold_mem(); \
  dump_size = store->get_file_size();       \
  dump_fd = store->is_file_open() ? store->get_file_fd() : -1;           \
  dump_dir_id = store->is_file_open() ? store->get_file_dir_id() : -1;   \
  owner_len = strlen(store->get_label());   \
  owner = store->get_label();

int ObDTLIntermResultMonitorInfoGetter::consume(
    const ObDTLIntermResultKey &key,
    const ObDTLIntermResultInfo &info)
{
  int ret = OB_SUCCESS;
  
  {
    int64_t hold_mem = 0;
    int64_t max_hold_mem = 0;
    int64_t dump_size = 0;
    int64_t dump_fd = -1;
    int64_t dump_dir_id = -1;
    int64_t owner_len = 0;
    const char *owner = NULL;
    DatumValue *cells = cur_row_.cells_;
    if (info.is_store_valid()) {
      GET_CHUNK_STORE_INFO(info.datum_store_);
    }
    for (int64_t cell_idx = 0;
        OB_SUCC(ret) && cell_idx < output_column_ids_.count();
        ++cell_idx) {
      const uint64_t column_id = output_column_ids_.at(cell_idx);
      switch(column_id) {
        case ObAllDtlIntermResultMonitor::INSPECT_COLUMN::TRACE_ID: {
          char *buf = NULL;
          if (OB_ISNULL(buf = static_cast<char *>(allocator_.alloc(OB_MAX_TRACE_ID_BUFFER_SIZE)))) {
            ret = OB_ALLOCATE_MEMORY_FAILED;
            SERVER_LOG(WARN, "allocate memory failed", K(ret));
          } else {
            int len = info.trace_id_.to_string(buf, OB_MAX_TRACE_ID_BUFFER_SIZE);
            (OB_SUCC(ret) ? ret = DatumValueBuffer::set_varchar_view(cells, cur_row_.count_, cell_idx, ObString(len, buf)) : ret);
            cells[cell_idx].set_collation_type(
                ObCharset::get_default_collation(ObCharset::get_default_charset()));
          }
          break;
        }
        case ObAllDtlIntermResultMonitor::INSPECT_COLUMN::OWNER: {
          char *buf = NULL;
          if (OB_UNLIKELY(0 == owner_len || NULL == owner)) {
            (OB_SUCC(ret) ? ret = DatumValueBuffer::set_varchar_view(cells, cur_row_.count_, cell_idx, ObString()) : ret);
            cells[cell_idx].set_collation_type(
                ObCharset::get_default_collation(ObCharset::get_default_charset()));
          } else if (OB_ISNULL(buf = static_cast<char *>(allocator_.alloc(owner_len)))) {
            ret = OB_ALLOCATE_MEMORY_FAILED;
            SERVER_LOG(WARN, "allocate memory failed", K(ret), K(owner_len));
          } else {
            MEMCPY(buf, owner, owner_len);
            (OB_SUCC(ret) ? ret = DatumValueBuffer::set_varchar_view(cells, cur_row_.count_, cell_idx, ObString(owner_len, buf)) : ret);
            cells[cell_idx].set_collation_type(
                ObCharset::get_default_collation(ObCharset::get_default_charset()));
          }
          break;
        }
        case ObAllDtlIntermResultMonitor::INSPECT_COLUMN::START_TIME: {
          int64_t start_time = key.start_time_;
          (OB_SUCC(ret) ? ret = DatumValueBuffer::set_timestamp(cells, cur_row_.count_, cell_idx, start_time) : ret);
          break;
        }
        case ObAllDtlIntermResultMonitor::INSPECT_COLUMN::EXPIRE_TIME: {
          int64_t expire_time = key.timeout_ts_;
          (OB_SUCC(ret) ? ret = DatumValueBuffer::set_timestamp(cells, cur_row_.count_, cell_idx, expire_time) : ret);
          break;
        }
        case ObAllDtlIntermResultMonitor::INSPECT_COLUMN::HOLD_MEMORY: {
          (OB_SUCC(ret) ? ret = DatumValueBuffer::set_int(cells, cur_row_.count_, cell_idx, hold_mem) : ret);
          break;
        }
        case ObAllDtlIntermResultMonitor::INSPECT_COLUMN::DUMP_SIZE: {
          (OB_SUCC(ret) ? ret = DatumValueBuffer::set_int(cells, cur_row_.count_, cell_idx, dump_size) : ret);
          break;
        }
        case ObAllDtlIntermResultMonitor::INSPECT_COLUMN::DUMP_COST: {
          (OB_SUCC(ret) ? ret = DatumValueBuffer::set_int(cells, cur_row_.count_, cell_idx, info.dump_cost_) : ret);
          break;
        }
        case ObAllDtlIntermResultMonitor::INSPECT_COLUMN::DUMP_TIME: {
          if (0 != info.dump_time_) {
            (OB_SUCC(ret) ? ret = DatumValueBuffer::set_timestamp(cells, cur_row_.count_, cell_idx, info.dump_time_) : ret);
          } else {
            // not dump.
            cells[cell_idx].set_null();
          }
          break;
        }
        case ObAllDtlIntermResultMonitor::INSPECT_COLUMN::DUMP_FD: {
          (OB_SUCC(ret) ? ret = DatumValueBuffer::set_int(cells, cur_row_.count_, cell_idx, dump_fd) : ret);
          break;
        }
        case ObAllDtlIntermResultMonitor::INSPECT_COLUMN::DUMP_DIR_ID: {
          (OB_SUCC(ret) ? ret = DatumValueBuffer::set_int(cells, cur_row_.count_, cell_idx, dump_dir_id) : ret);
          break;
        }
        case ObAllDtlIntermResultMonitor::INSPECT_COLUMN::CHANNEL_ID: {
          (OB_SUCC(ret) ? ret = DatumValueBuffer::set_int(cells, cur_row_.count_, cell_idx, key.channel_id_) : ret);
          break;
        }
        case ObAllDtlIntermResultMonitor::INSPECT_COLUMN::QC_ID: {
          (OB_SUCC(ret) ? ret = DatumValueBuffer::set_int(cells, cur_row_.count_, cell_idx, info.monitor_info_.qc_id_) : ret);
          break;
        }
        case ObAllDtlIntermResultMonitor::INSPECT_COLUMN::DFO_ID: {
          (OB_SUCC(ret) ? ret = DatumValueBuffer::set_int(cells, cur_row_.count_, cell_idx, info.monitor_info_.dfo_id_) : ret);
          break;
        }
        case ObAllDtlIntermResultMonitor::INSPECT_COLUMN::SQC_ID: {
          (OB_SUCC(ret) ? ret = DatumValueBuffer::set_int(cells, cur_row_.count_, cell_idx, info.monitor_info_.sqc_id_) : ret);
          break;
        }
        case ObAllDtlIntermResultMonitor::INSPECT_COLUMN::BATCH_ID: {
          (OB_SUCC(ret) ? ret = DatumValueBuffer::set_int(cells, cur_row_.count_, cell_idx, key.batch_id_) : ret);
          break;
        }
        case ObAllDtlIntermResultMonitor::INSPECT_COLUMN::MAX_HOLD_MEM: {
          (OB_SUCC(ret) ? ret = DatumValueBuffer::set_int(cells, cur_row_.count_, cell_idx, max_hold_mem) : ret);
          break;
        }
        default: {
          ret = OB_ERR_UNEXPECTED;
          SERVER_LOG(WARN, "invalid column id", K(cell_idx),
              K_(output_column_ids), K(ret));
          break;
        }
      }
    }
    if (OB_SUCCESS == ret && OB_FAIL(scanner_.add_row(cur_row_))) {
      SERVER_LOG(WARN, "fail to add row", K(ret), K(cur_row_));
    }
  }
  return ret;
}

int ObAllDtlIntermResultMonitor::inner_get_next_row(ObNewRow *&row)
{
  int ret = OB_SUCCESS;
  DatumValue *cells = cur_row_.cells_;
  if (!start_to_read_) {
    if (OB_FAIL(fill_scanner())) {
    } else {
      start_to_read_ = true;
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
  return ret;
}

int ObAllDtlIntermResultMonitor::fill_scanner()
{
  int ret = OB_SUCCESS;
  DatumValue *cells = NULL;
    if (OB_ISNULL(allocator_) || OB_ISNULL(session_)) {
    ret = OB_NOT_INIT;
    SERVER_LOG(WARN, "allocator_ or null", K_(allocator), K(ret));
  } else if (OB_ISNULL(cells = cur_row_.cells_)) {
    ret = OB_ERR_UNEXPECTED;
    SERVER_LOG(WARN, "cur row cell is NULL", K(ret));
  } else {
    
    {
      {
        {
          ObDTLIntermResultMonitorInfoGetter monitor_getter(scanner_, *allocator_, output_column_ids_,
                                  cur_row_);
          SERVER_MODULE_SCOPE {
            if (OB_FAIL(::oceanbase::share::server_service<::oceanbase::sql::dtl::ObDTLIntermResultManager>()->generate_monitor_info_rows(monitor_getter))) {
            }
          } else {
            // The server module set may not be ready during shutdown.
            ret = OB_SUCCESS;
          }
        }
      }
    }
    if(OB_SUCC(ret)) {
      scanner_it_ = scanner_.begin();
      start_to_read_ = true;
    }
  }
  return ret;
}

}/* ns observer*/
}/* ns oceanbase */
