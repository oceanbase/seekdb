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

#include "observer/virtual_table/ob_information_kvcache_table.h"
#include "observer/ob_server_struct.h"
#include "observer/omt/ob_multi_tenant.h"

using namespace oceanbase::common;

namespace oceanbase
{
namespace observer
{

ObInfoSchemaKvCacheTable::ObInfoSchemaKvCacheTable()
    : ObVirtualTableScannerIterator(),
    addr_(NULL),
    ipstr_(),
    port_(0),
    cache_iter_(0),
    str_buf_()
{
}

ObInfoSchemaKvCacheTable::~ObInfoSchemaKvCacheTable()
{
  reset();
}

void ObInfoSchemaKvCacheTable::reset()
{
  ObVirtualTableScannerIterator::reset();
  cache_iter_ = 0;
  addr_ = NULL;
  port_ = 0;
  ipstr_.reset();
  inst_handles_.reset();
  str_buf_.reset();
  for (int64_t i = 0; i  < OB_ROW_MAX_COLUMNS_COUNT; i++) {
    cells_[i].reset();
  }
}

int ObInfoSchemaKvCacheTable::inner_get_next_row(ObNewRow *&row)
{
  int ret = OB_SUCCESS;

  row = nullptr;
  ObKVCacheInst * inst = NULL;
  if (OB_UNLIKELY(NULL == allocator_)) {
    ret = OB_NOT_INIT;
    SERVER_LOG(WARN, "Invalid allocator, not init", K(ret), KP(allocator_));
  } else if (OB_FAIL(get_next_inst(inst))) {
    if (OB_ITER_END != ret) {
      SERVER_LOG(WARN, "Fail to get cache inst", K(ret));
    }
  } else if (OB_FAIL(process_row(inst))) {
    SERVER_LOG(WARN, "Fail to process current row", K(ret));
  } else {
    row = &cur_row_;
  }

  return ret;
}

int ObInfoSchemaKvCacheTable::set_ip()
{
  int ret = OB_SUCCESS;
  char ipbuf[common::OB_IP_STR_BUFF];
  if (nullptr == addr_) {
    ret = OB_ENTRY_NOT_EXIST;
    SERVER_LOG(WARN, "Null address", K(ret), KP(addr_));
  } else if (!addr_->ip_to_string(ipbuf, sizeof(ipbuf))) {
    ret = OB_ERR_UNEXPECTED;
    SERVER_LOG(ERROR, "Fail to cast ip to string", K(ret));
  } else {
    ipstr_ = ObString::make_string(ipbuf);
    if (OB_FAIL(ob_write_string(*allocator_, ipstr_, ipstr_))) {
      SERVER_LOG(WARN, "Failed to write string", K(ret));
    }
    port_ = addr_->get_port();
  }
  return ret;
}

int ObInfoSchemaKvCacheTable::inner_open()
{
  int ret = OB_SUCCESS;

  inst_handles_.reuse();
  if (OB_FAIL(set_ip())) {
    SERVER_LOG(WARN, "Fail to set ip from addr", K(ret), K(addr_));
  } else if (OB_FAIL(ObKVGlobalCache::get_instance().get_cache_inst_info(inst_handles_))) {
    SERVER_LOG(WARN, "Fail to get cache info", K(ret));
  }

  return ret;
}

int ObInfoSchemaKvCacheTable::get_next_inst(ObKVCacheInst *&inst)
{
  int ret = OB_SUCCESS;
  inst = nullptr;
  if (cache_iter_ >= inst_handles_.count()) {
    ret = OB_ITER_END;
  } else {
    inst = inst_handles_.at(cache_iter_++).get_inst();
    if (OB_ISNULL(inst)) {
      ret = OB_ERR_UNEXPECTED;
    }
  }
  return ret;
}

int ObInfoSchemaKvCacheTable::process_row(const ObKVCacheInst *inst)
{
  int ret = OB_SUCCESS;

  uint64_t cell_idx = 0;
  cur_row_.cells_ = cells_;
  cur_row_.count_ = reserved_column_cnt_;
  for (int64_t i = 0 ; OB_SUCC(ret) && i < output_column_ids_.count() ; ++i) {
    uint64_t col_id = output_column_ids_.at(i);
    switch(col_id) {
      case CACHE_NAME: {
        if (NULL != inst->status_.config_) {
          cells_[cell_idx].set_varchar(inst->status_.config_->cache_name_);
          cells_[cell_idx].set_collation_type(ObCharset::get_default_collation(ObCharset::get_default_charset()));
        }
        break;
      }
      case CACHE_ID: {
        cells_[cell_idx].set_int(inst->cache_id_);
        break;
      }
      case CACHE_SIZE: {
        cells_[cell_idx].set_int(inst->status_.store_size_);
        break;
      }
      case KV_CNT: {
        cells_[cell_idx].set_int(inst->status_.kv_cnt_);
        break;
      }
      case HIT_RATIO: {
        str_buf_.reset();
        number::ObNumber num;
        double value = inst->status_.get_hit_ratio() * 100;
        static const int64_t MAX_DOUBLE_PRINT_SIZE = 64;
        char buf[MAX_DOUBLE_PRINT_SIZE];
        memset(buf, 0, MAX_DOUBLE_PRINT_SIZE);
        if (OB_UNLIKELY(0 > snprintf(buf, MAX_DOUBLE_PRINT_SIZE, "%lf", value))) {
          ret = OB_IO_ERROR;
          SERVER_LOG(WARN, "Fail to snprintf hit ratio", K(ret), K(errno), KERRNOMSG(errno));
        } else if (OB_FAIL(num.from(buf, str_buf_))) {
          SERVER_LOG(WARN, "Fail to cast to number", K(ret));
        } else {
          cells_[cell_idx].set_number(num);
        }
        break;
      }
      case TOTAL_PUT_CNT: {
        cells_[cell_idx].set_int(inst->status_.total_put_cnt_.value());
        break;
      }
      case TOTAL_HIT_CNT: {
        cells_[cell_idx].set_int(inst->status_.total_hit_cnt_.value());
        break;
      }
      case TOTAL_MISS_CNT: {
        cells_[cell_idx].set_int(inst->status_.total_miss_cnt_);
        break;
      }
      default: {
        ret = OB_ERR_UNEXPECTED;
        SERVER_LOG(WARN, "Invalid column id", K(ret), K(cell_idx), K(output_column_ids_), K(col_id));
        break;
      }
    }
    ++cell_idx;
  }

  return ret;
}

}/* ns observer*/
}/* ns oceanbase */
