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
#include "observer/virtual_table/ob_virtual_span_info.h"

using namespace oceanbase::common;
using namespace oceanbase::obmysql;
using namespace oceanbase::omt;
using namespace oceanbase::share;
namespace oceanbase {
namespace observer {

ObVirtualSpanInfo::ObVirtualSpanInfo() :
    ObVirtualTableScannerIterator(),
    cur_flt_span_mgr_(nullptr),
    start_id_(INT64_MAX),
    end_id_(INT64_MIN),
    cur_id_(0),
    ref_(),
    addr_(NULL),
    ipstr_(),
    port_(0),
    is_first_get_(true),
    with_tenant_ctx_(nullptr)
{
}

ObVirtualSpanInfo::~ObVirtualSpanInfo() {
  reset();
}

void ObVirtualSpanInfo::reset()
{
  if (cur_flt_span_mgr_ != nullptr && ref_.idx_ != -1) {
    cur_flt_span_mgr_->revert(&ref_);
  }
  ObVirtualTableScannerIterator::reset();
  is_first_get_ = true;
  cur_id_ = 0;
  start_id_ = INT64_MAX;
  end_id_ = INT64_MIN;
  cur_flt_span_mgr_ = nullptr;
  addr_ = nullptr;
  port_ = 0;
  ipstr_.reset();
}

int ObVirtualSpanInfo::inner_open()
{
  int ret = OB_SUCCESS;

  if (OB_SUCC(ret)) {
    if (NULL == allocator_) {
      ret = OB_INVALID_ARGUMENT;
      SERVER_LOG(WARN, "Invalid Allocator", K(ret));
    } else if (OB_FAIL(set_ip(addr_))) {
      SERVER_LOG(WARN, "failed to set server ip addr", K(ret));
    } else {
      // do nothing
    }
  }
  return ret;
}

int ObVirtualSpanInfo::set_ip(common::ObAddr *addr)
{
  int ret = OB_SUCCESS;
  MEMSET(server_ip_, 0, sizeof(server_ip_));
  if (NULL == addr){
    ret = OB_ENTRY_NOT_EXIST;
  } else if (!addr_->ip_to_string(server_ip_, sizeof(server_ip_))) {
    SERVER_LOG(ERROR, "ip to string failed");
    ret = OB_ERR_UNEXPECTED;
  } else {
    ipstr_ = ObString::make_string(server_ip_);
    port_ = addr_->get_port();
  }
  return ret;
}

int ObVirtualSpanInfo::inner_get_next_row(common::ObNewRow *&row)
{
  int ret = OB_SUCCESS;

  if (NULL == allocator_) {
    ret = OB_NOT_INIT;
    SERVER_LOG(WARN, "invalid argument", KP(allocator_), K(ret));
  } else if (is_first_get_) {
    bool is_valid = true;
    cur_flt_span_mgr_ = nullptr;
    if (OB_FAIL(check_ip_and_port(is_valid))) {
      SERVER_LOG(WARN, "check ip and port failed", K(ret));
    } else if (!is_valid) {
      ret = OB_ITER_END;;
    }
  }

  if (OB_SUCC(ret)) {
    if (nullptr == cur_flt_span_mgr_) {
      cur_flt_span_mgr_ = nullptr;
      uint64_t t_id = effective_tenant_id_;
      cur_flt_span_mgr_ = MTL(sql::ObFLTSpanMgr*);

      if (nullptr == cur_flt_span_mgr_) {
        ret = OB_ERR_UNEXPECTED;
        SERVER_LOG(WARN, "req manager doest not exist", K(t_id));
      } else if (OB_SUCC(ret)) {
        start_id_ = cur_flt_span_mgr_->get_start_idx();
        end_id_ = cur_flt_span_mgr_->get_end_idx();
        if (start_id_ >= end_id_) {
          // iter end do nothing
        } else {
          cur_id_ = start_id_;
        }
        SERVER_LOG(TRACE, "start to get rows from virtual span info",
                   K(start_id_), K(end_id_), K(cur_id_), K(t_id));
      }
    }

    if (cur_id_ < start_id_ || cur_id_ >= end_id_) {
      ret = OB_ITER_END;
      SERVER_LOG(INFO, "scan finished", K(start_id_), K(end_id_), K(cur_id_));
    }
    // if has no more record, free current flt span manager
    if (OB_ITER_END == ret) {
      if (cur_flt_span_mgr_ != nullptr && ref_.idx_ != -1) {
        cur_flt_span_mgr_->revert(&ref_);
      }
    }
  }

  if (OB_SUCC(ret)) {
    void *rec = NULL;
    if (ref_.idx_ != -1) {
      cur_flt_span_mgr_->revert(&ref_);
    }
    do {
      ref_.reset();
      if (OB_ENTRY_NOT_EXIST == (ret = cur_flt_span_mgr_->get(cur_id_, rec, &ref_))) {
        cur_id_ += 1;
      }
    } while (OB_ENTRY_NOT_EXIST == ret && cur_id_ < end_id_ && cur_id_ >= start_id_);

    if (OB_SUCC(ret)) {
      if (NULL != rec) {
        sql::ObFLTSpanRec *record = static_cast<sql::ObFLTSpanRec*> (rec);
        if (OB_FAIL(fill_cells(*record))) {
          SERVER_LOG(WARN, "failed to fill cells", K(ret));
        } else {
          //finish fetch one row
          row = &cur_row_;
          SERVER_LOG(TRACE, "request_info_table get next row succ", K(cur_id_), K(cur_row_));
        }
      } else {
        ret = OB_ERR_UNEXPECTED;
        SERVER_LOG(WARN, "unexpected null rec",
                   K(rec), K(cur_id_), K(ret));
      }
    }

    is_first_get_ = false;

    // move to next slot
    if (OB_SUCC(ret)) {
      cur_id_++;
    }
  }


  return ret;
}

int ObVirtualSpanInfo::fill_cells(sql::ObFLTSpanRec &record)
{
  int ret = OB_SUCCESS;
  const int64_t col_count = output_column_ids_.count();
  ObObj *cells = cur_row_.cells_;

  if (OB_ISNULL(cells)) {
    ret = OB_INVALID_ARGUMENT;
    SERVER_LOG(WARN, "invalid argument", K(cells));
  } else {
    for (int64_t cell_idx = 0; OB_SUCC(ret) && cell_idx < col_count; cell_idx++) {
      uint64_t col_id = output_column_ids_.at(cell_idx);
      switch(col_id) {
        //server ip
      case SVR_IP: {
        cells[cell_idx].set_varchar(ipstr_); //ipstr_ and port_ were set in set_ip func call
        cells[cell_idx].set_collation_type(ObCharset::get_default_collation(
                                             ObCharset::get_default_charset()));
      } break;
        //server port
      case SVR_PORT: {
        cells[cell_idx].set_int(port_);
      } break;
      case TENANT_ID: {
        cells[cell_idx].set_int(record.data_.tenant_id_);
      } break;
      case TRACE_ID: {
        cells[cell_idx].set_varchar(record.data_.trace_id_);
        cells[cell_idx].set_collation_type(ObCharset::get_default_collation(
                                             ObCharset::get_default_charset()));
      } break;
      case REQUEST_ID: {
        cells[cell_idx].set_int(record.data_.req_id_);
      } break;
      case SPAN_ID: {
        cells[cell_idx].set_varchar(record.data_.span_id_);
        cells[cell_idx].set_collation_type(ObCharset::get_default_collation(
                                     ObCharset::get_default_charset()));
      } break;
      case PARENT_SPAN_ID: {
        cells[cell_idx].set_varchar(record.data_.parent_span_id_);
        cells[cell_idx].set_collation_type(ObCharset::get_default_collation(
                                     ObCharset::get_default_charset()));
      } break;
      case SPAN_NAME: {
        cells[cell_idx].set_varchar(record.data_.span_name_);
        cells[cell_idx].set_collation_type(ObCharset::get_default_collation(
                                     ObCharset::get_default_charset()));
      } break;
      case REF_TYPE: {
        if (record.data_.ref_type_ == 0) {
          cells[cell_idx].set_varchar("CHILD");
          cells[cell_idx].set_collation_type(ObCharset::get_default_collation(
                                     ObCharset::get_default_charset()));
        } else if (record.data_.ref_type_ == 1) {
          cells[cell_idx].set_varchar("FOLLOW");
          cells[cell_idx].set_collation_type(ObCharset::get_default_collation(
                                     ObCharset::get_default_charset()));
        } else {
          // do nothing
        }
      } break;
      case START_TS: {
        cells[cell_idx].set_int(record.data_.start_ts_);
      } break;
      case END_TS: {
        cells[cell_idx].set_int(record.data_.end_ts_);
      } break;
      case TAGS: {
        cells[cell_idx].set_lob_value(ObLongTextType,
                                      record.data_.tags_.ptr(),
                                      record.data_.tags_.length());
        cells[cell_idx].set_collation_type(ObCharset::get_default_collation(
                                     ObCharset::get_default_charset()));
      } break;
      case LOGS: {
        cells[cell_idx].set_lob_value(ObLongTextType,
                                      record.data_.logs_.ptr(),
                                      record.data_.logs_.length());
        cells[cell_idx].set_collation_type(ObCharset::get_default_collation(
                                     ObCharset::get_default_charset()));
      } break;
      default: {
        ret = OB_ERR_UNEXPECTED;
        SERVER_LOG(WARN, "invalid column id", K(ret), K(cell_idx), K(col_id));
      } break;
      }
    }
  }
  return ret;
}


int ObVirtualSpanInfo::check_ip_and_port(bool &is_valid)
{
  int ret = OB_SUCCESS;
  is_valid = true;
  // is_serving_tenant changed to (svr_ip, svr_port) in (ip1, port1), (ip2, port2), ...
  // The extracted query range is [(ip1, port1), (ip1, port1)], [(ip2, port2), (ip2, port2)], ...
  // Need to traverse all query ranges, and determine if the local ip & port falls within any one of the query ranges
  if (key_ranges_.count() >= 1) {
    is_valid = false;
    for (int64_t i = 0; OB_SUCC(ret) && !is_valid && i < key_ranges_.count(); i++) {
      ObNewRange &req_id_range = key_ranges_.at(i);
      if (OB_UNLIKELY(req_id_range.get_start_key().get_obj_cnt() != 4
                      || req_id_range.get_end_key().get_obj_cnt() != 4)) {
        ret = OB_ERR_UNEXPECTED;
        SERVER_LOG(WARN, "unexpected  # of rowkey columns",
                   K(ret),
                   "size of start key", req_id_range.get_start_key().get_obj_cnt(),
                   "size of end key", req_id_range.get_end_key().get_obj_cnt());
      } else {
        ObObj ip_obj;
        ObObj ip_low = (req_id_range.get_start_key().get_obj_ptr()[PRI_KEY_IP_IDX]);
        ObObj ip_high = (req_id_range.get_end_key().get_obj_ptr()[PRI_KEY_IP_IDX]);
        ip_obj.set_varchar(ipstr_);
        ip_obj.set_collation_type(ip_low.get_collation_type());
        if (ip_obj.compare(ip_low) >= 0 && ip_obj.compare(ip_high) <= 0) {
          ObObj port_obj;
          port_obj.set_int32(port_);
          ObObj port_low = (req_id_range.get_start_key().get_obj_ptr()[PRI_KEY_PORT_IDX]);
          ObObj port_high = (req_id_range.get_end_key().get_obj_ptr()[PRI_KEY_PORT_IDX]);
          if (port_obj.compare(port_low) >= 0 && port_obj.compare(port_high) <= 0) {
            is_valid = true;
          }
        } else {
          // do nothing
        }
      }
    }
  }
  SERVER_LOG(DEBUG, "check ip and port", K(key_ranges_), K(is_valid), K(ipstr_), K(port_));
  return ret;
}
} //namespace observer
} //namespace oceanbase
