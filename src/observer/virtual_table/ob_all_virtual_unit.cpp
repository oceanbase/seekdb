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

#include "observer/virtual_table/ob_all_virtual_unit.h"
#include "share/rc/ob_module_provider.h"
#include "observer/ob_server.h"
#include "observer/omt/ob_tenant.h"
#include "logservice/ob_log_service.h"

using namespace oceanbase;
using namespace oceanbase::common;
using namespace oceanbase::storage;
using namespace oceanbase::observer;
using namespace oceanbase::omt;
using namespace oceanbase::logservice;

ObAllVirtualUnit::ObAllVirtualUnit()
    : ObVirtualTableScannerIterator(),
      addr_(),
      tenant_meta_(),
      has_row_(false),
      consumed_(false)
{
}

ObAllVirtualUnit::~ObAllVirtualUnit()
{
  reset();
}

void ObAllVirtualUnit::reset()
{
  addr_.reset();
  tenant_meta_ = omt::ObTenantMeta();
  has_row_ = false;
  consumed_ = false;
  ObVirtualTableScannerIterator::reset();
}

int ObAllVirtualUnit::init(common::ObAddr &addr)
{
  int ret = OB_SUCCESS;
  if (start_to_read_) {
    ret = OB_INIT_TWICE;
    SERVER_LOG(WARN, "cannot init twice", K(ret));
  } else {
    addr_ = addr;
    start_to_read_ = true;
  }
  return ret;
}

int ObAllVirtualUnit::inner_open()
{
  int ret = OB_SUCCESS;

  if (OB_ISNULL(GCTX.omt_)) {
    ret = OB_ERR_UNEXPECTED;
    SERVER_LOG(WARN, "fail to get multi tenant from GCTX", K(ret));
  } else {
    bool exist = false;
    if (OB_FAIL(GCTX.omt_->get_tenant_meta(tenant_meta_, exist))) {
    } else {
      has_row_ = exist;
    }
  }

  return ret;
}

int ObAllVirtualUnit::inner_get_next_row(ObNewRow *&row)
{
  int ret = OB_SUCCESS;

  if (OB_UNLIKELY(!start_to_read_)) {
    ret = OB_NOT_INIT;
    SERVER_LOG(WARN, "not inited", K(start_to_read_), K(ret));
  } else if (NULL == cur_row_.cells_) {
    ret = OB_ERR_UNEXPECTED;
    SERVER_LOG(ERROR, "cur row cell is NULL", K(ret));
  } else if (!has_row_ || consumed_) {
    ret = OB_ITER_END;
  } else {
    const ObTenantMeta &tenant_meta = tenant_meta_;
    consumed_ = true;
    const int64_t col_count = output_column_ids_.count();

    for (int64_t i = 0; OB_SUCC(ret) && i < col_count; ++i) {
      uint64_t col_id = output_column_ids_.at(i);
      switch (col_id) {
        case MIN_CPU: {
          cur_row_.cells_[i].set_double(tenant_meta.unit_.config_.min_cpu());
          break;
        }
        case MAX_CPU: {
          cur_row_.cells_[i].set_double(tenant_meta.unit_.config_.max_cpu());
          break;
        }
        case MEMORY_SIZE:
          cur_row_.cells_[i].set_int(tenant_meta.unit_.config_.memory_size());
          break;
        case MIN_IOPS: {
          cur_row_.cells_[i].set_int(tenant_meta.unit_.config_.min_iops());
          break;
        }
        case MAX_IOPS: {
          cur_row_.cells_[i].set_int(tenant_meta.unit_.config_.max_iops());
          break;
        }
        case IOPS_WEIGHT: {
          cur_row_.cells_[i].set_int(tenant_meta.unit_.config_.iops_weight());
          break;
        }
        case LOG_DISK_SIZE:
          cur_row_.cells_[i].set_int(tenant_meta.unit_.config_.log_disk_size());
          break;
        case LOG_DISK_IN_USE: {
          int64_t clog_disk_in_use = 0;
          
          if (OB_FAIL(get_clog_disk_used_size_( clog_disk_in_use))) {
          } else {
            cur_row_.cells_[i].set_int(clog_disk_in_use);
          }
          break;
        }
        case DATA_DISK_SIZE: {
          cur_row_.cells_[i].set_null();
          break;
        }
        case DATA_DISK_IN_USE: {
          int64_t data_disk_in_use = 0;
          {
            if (OB_ISNULL(GCTX.disk_reporter_)) {
              ret = OB_ERR_UNEXPECTED;
              SERVER_LOG(WARN, "disk_reporter_ is nullptr", KR(ret), KP(GCTX.disk_reporter_));
            } else if (OB_FAIL(static_cast<ObDiskUsageReportTask*>(GCTX.disk_reporter_)
                               ->get_data_disk_used_size( data_disk_in_use))) {
            }
          }
          if (OB_SUCC(ret)) {
            cur_row_.cells_[i].set_int(data_disk_in_use);
          }
          break;
        }
        case MAX_NET_BANDWIDTH: {
          cur_row_.cells_[i].set_int(tenant_meta.unit_.config_.max_net_bandwidth());
          break;
        }
        case NET_BANDWIDTH_WEIGHT: {
          cur_row_.cells_[i].set_int(tenant_meta.unit_.config_.net_bandwidth_weight());
          break;
        }
        case STATUS: {
          const char* status_str = share::ObUnitInfoGetter::get_unit_status_str(tenant_meta.unit_.unit_status_);
          cur_row_.cells_[i].set_varchar(status_str);
          cur_row_.cells_[i].set_collation_type(ObCharset::get_default_collation(ObCharset::get_default_charset()));
          break;
        }
        case CREATE_TIME:
          cur_row_.cells_[i].set_int(tenant_meta.unit_.create_timestamp_);
          break;
        default: {
          ret = OB_ERR_UNEXPECTED;
          SERVER_LOG(WARN, "invalid col_id", K(ret), K(col_id));
          break;
        }
      }
    }
  }
  if (OB_SUCC(ret)) {
    row = &cur_row_;
  }
  return ret;
}

int ObAllVirtualUnit::get_clog_disk_used_size_(int64_t &log_used_size)
{
  int ret = OB_SUCCESS;
  log_used_size = 0;
  MAKE_TENANT_SWITCH_SCOPE_GUARD(guard);
  if (OB_SUCC(guard.switch_to())) {
    ObLogService *log_service = share::g_mp->log_service();
    int64_t unused_log_disk_total_size = 0;
    if (OB_ISNULL(log_service)) {
      ret = OB_ERR_UNEXPECTED;
    } else if (OB_FAIL(log_service->get_palf_stable_disk_usage(log_used_size,
                                                               unused_log_disk_total_size))) {
    }
  }
  // return OB_SUCCESS whatever.
  return OB_SUCCESS;
}

