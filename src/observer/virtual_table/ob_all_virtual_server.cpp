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

#include "observer/virtual_table/ob_all_virtual_server.h"
#include "share/rc/ob_module_provider.h"

#include "observer/ob_service.h"
#include "observer/ob_server_struct.h"
#include "share/config/ob_server_config.h"
#include "share/ob_server_role.h"
#include "share/ob_server_info.h"
#include "storage/ls/ob_ls.h"
#include "storage/tx_storage/ob_ls_service.h"

using namespace oceanbase;
using namespace oceanbase::observer;
using namespace oceanbase::common;

ObAllVirtualServer::ObAllVirtualServer()
    : ObVirtualTableScannerIterator(),
      addr_(),
      config_(nullptr)
{
  ip_buf_[0] = '\0';
  role_buf_[0] = '\0';
  switchover_status_buf_[0] = '\0';
}

ObAllVirtualServer::~ObAllVirtualServer()
{
  addr_.reset();
  ip_buf_[0] = '\0';
  role_buf_[0] = '\0';
  switchover_status_buf_[0] = '\0';
  config_ = nullptr;
}

int ObAllVirtualServer::init(common::ObAddr &addr, common::ObServerConfig *config)
{
  addr_ = addr;
  ip_buf_[0] = '\0';
  config_ = config;
  return OB_SUCCESS;
}

int ObAllVirtualServer::inner_get_next_row(ObNewRow *&row)
{
  int ret = OB_SUCCESS;
  share::ObServerResourceInfo resource_info;
  // server resource info are get in ObService::get_server_resource_info()

  ObDeviceHealthStatus dhs = DEVICE_HEALTH_NORMAL;
  int64_t data_disk_abnormal_time = 0;

  if (start_to_read_) {
    ret = OB_ITER_END;
  } else if (OB_ISNULL(cur_row_.cells_)) {
    ret = OB_ERR_UNEXPECTED;
    SERVER_LOG(ERROR, "cur row cell is NULL", KR(ret));
  } else if (OB_ISNULL(GCTX.ob_service_) || OB_ISNULL(config_)) {
    ret = OB_ERR_UNEXPECTED;
    SERVER_LOG(ERROR, "ob_service_ is NULL", KR(ret), KP(GCTX.ob_service_), KP(config_));
  } else if (OB_FAIL(GCTX.ob_service_->get_server_resource_info(resource_info))) {
    SERVER_LOG(ERROR, "fail to get_server_resource_info", KR(ret));
  } else if (OB_FAIL(ObIOManager::get_instance().get_device_health_status(dhs,
      data_disk_abnormal_time))) {
    SERVER_LOG(WARN, "get device health status fail", KR(ret));
  } else {
    const int64_t col_count = output_column_ids_.count();
    const int64_t data_disk_allocated =
        OB_STORAGE_OBJECT_MGR.get_total_macro_block_count() * OB_STORAGE_OBJECT_MGR.get_macro_block_size();
    const char *data_disk_health_status = device_health_status_to_str(dhs);
    const int64_t ssl_cert_expired_time = GCTX.ssl_key_expired_time_;

    share::ObServerInfo server_info;
    const int load_info_ret = share::ObServerInfoProxy::load_server_info(server_info);

    role_buf_[0] = '\0';
    switchover_status_buf_[0] = '\0';
    if (OB_SUCCESS == load_info_ret && server_info.is_valid()) {
      switch (server_info.get_server_role().value()) {
        case share::ObServerRole::PRIMARY_ROLE:
          snprintf(role_buf_, sizeof(role_buf_), "PRIMARY");
          break;
        case share::ObServerRole::STANDBY_ROLE:
          snprintf(role_buf_, sizeof(role_buf_), "STANDBY");
          break;
        default:
          snprintf(role_buf_, sizeof(role_buf_), "UNKNOWN");
          break;
      }
      snprintf(switchover_status_buf_, sizeof(switchover_status_buf_), "%s",
               server_info.get_switchover_status().to_str());
    } else {
      if (OB_SUCCESS != load_info_ret) {
        SERVER_LOG(WARN, "load server info failed", K(load_info_ret));
      }
      snprintf(role_buf_, sizeof(role_buf_), "UNKNOWN");
      snprintf(switchover_status_buf_, sizeof(switchover_status_buf_), "UNKNOWN");
    }

    // Get sync_scn and readable_scn from LS in real-time
    uint64_t sync_scn_val = 0;
    uint64_t readable_scn_val = 0;
    storage::ObLS *ls = nullptr;
    if (OB_FAIL(share::g_mp->ls_service()->get_ls(ls))){
      SERVER_LOG(WARN, "get ls failed", K(ret));
    } else {
      share::SCN sync_scn;
      share::SCN readable_scn;
      if (OB_FAIL(ls->get_end_scn(sync_scn))) {
        SERVER_LOG(WARN, "get end scn failed", K(ret));
      } else if (OB_FAIL(ls->get_max_decided_scn(readable_scn))) {
        SERVER_LOG(WARN, "get decided scn failed", K(ret));
      } else {
        sync_scn_val = sync_scn.get_val_for_inner_table_field();
        readable_scn_val = readable_scn.get_val_for_inner_table_field();
      }
    }

    for (int64_t i = 0; OB_SUCC(ret) && i < col_count; ++i) {
      uint64_t col_id = output_column_ids_.at(i);
      switch (col_id) {
        case SVR_IP:
          if (addr_.ip_to_string(ip_buf_, sizeof(ip_buf_))) {
            cur_row_.cells_[i].set_varchar(ip_buf_);
            cur_row_.cells_[i].set_collation_type(ObCharset::get_default_collation(ObCharset::get_default_charset()));
          } else {
            ret = OB_ERR_UNEXPECTED;
            SERVER_LOG(WARN, "fail to execute ip_to_string", K(addr_), KR(ret));
          }
          break;
        case SVR_PORT:
          cur_row_.cells_[i].set_int(addr_.get_port());
          break;
        case SQL_PORT:
          cur_row_.cells_[i].set_int(GCONF.mysql_port);
          break;
        case RPC_PORT:
          cur_row_.cells_[i].set_int(GCONF.rpc_port);
          break;
        case CPU_CAPACITY:
          cur_row_.cells_[i].set_int(resource_info.cpu_);
          break;
        case CPU_CAPACITY_MAX:
          cur_row_.cells_[i].set_double(resource_info.cpu_);
          break;
        case CPU_ASSIGNED:
          cur_row_.cells_[i].set_double(resource_info.report_cpu_assigned_);
          break;
        case CPU_ASSIGNED_MAX:
          cur_row_.cells_[i].set_double(resource_info.report_cpu_max_assigned_);
          break;
        case MEM_CAPACITY:
          cur_row_.cells_[i].set_int(resource_info.mem_total_);
          break;
        case MEM_ASSIGNED:
          cur_row_.cells_[i].set_int(resource_info.report_mem_assigned_);
          break;
        case DATA_DISK_CAPACITY:
          cur_row_.cells_[i].set_int(resource_info.data_disk_total_);
          break;
        case DATA_DISK_IN_USE:
          cur_row_.cells_[i].set_int(resource_info.data_disk_in_use_);
          break;
        case DATA_DISK_ALLOCATED:
          cur_row_.cells_[i].set_int(data_disk_allocated);
          break;
        case DATA_DISK_HEALTH_STATUS:
          cur_row_.cells_[i].set_varchar(data_disk_health_status);
          cur_row_.cells_[i].set_collation_type(ObCharset::get_default_collation(ObCharset::get_default_charset()));
          break;
        case DATA_DISK_ABNORMAL_TIME:
          cur_row_.cells_[i].set_int(data_disk_abnormal_time);
          break;
        case LOG_DISK_CAPACITY:
          cur_row_.cells_[i].set_int(resource_info.log_disk_total_);
          break;
        case LOG_DISK_ASSIGNED:
          cur_row_.cells_[i].set_int(resource_info.report_log_disk_assigned_);
          break;
        case LOG_DISK_IN_USE:
          cur_row_.cells_[i].set_int(resource_info.log_disk_in_use_);
          break;
        case RPC_CERT_EXPIRE_TIME:
          cur_row_.cells_[i].set_int(0);
          break;
        case RPC_TLS_ENABLED:
          cur_row_.cells_[i].set_int(0);
          break;
        case MEMORY_LIMIT:
          // Keep the legacy column name for virtual-table compatibility.
          cur_row_.cells_[i].set_int(GMEMCONF.get_server_memory_budget());
          break;
        case START_SERVICE_TIME:
          cur_row_.cells_[i].set_int(GCTX.start_service_time_);
          break;
        case CREATE_TIME:
          cur_row_.cells_[i].set_int(config_->server_create_time);
          break;
        case ROLE:
          cur_row_.cells_[i].set_varchar(role_buf_);
          cur_row_.cells_[i].set_collation_type(ObCharset::get_default_collation(ObCharset::get_default_charset()));
          break;
        case SWITCHOVER_STATUS:
          cur_row_.cells_[i].set_varchar(switchover_status_buf_);
          cur_row_.cells_[i].set_collation_type(ObCharset::get_default_collation(ObCharset::get_default_charset()));
          break;
        case SYNC_SCN:
          cur_row_.cells_[i].set_uint64(sync_scn_val);
          break;
        case READABLE_SCN:
          cur_row_.cells_[i].set_uint64(readable_scn_val);
          break;
        default: {
          ret = OB_ERR_UNEXPECTED;
          SERVER_LOG(WARN, "invalid col_id", KR(ret), K(col_id));
          break;
        }
      }
    }
  }
  if (OB_SUCC(ret)) {
    row = &cur_row_;
    start_to_read_ = true;
  }
  return ret;
}
