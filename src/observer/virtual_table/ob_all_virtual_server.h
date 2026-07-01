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

#ifndef SRC_OBSERVER_VIRTUAL_TABLE_OB_ALL_VIRTUAL_SERVER_H_
#define SRC_OBSERVER_VIRTUAL_TABLE_OB_ALL_VIRTUAL_SERVER_H_

#include "share/ob_virtual_table_scanner_iterator.h"  // ObVirtualTableScannerIterator

namespace oceanbase
{
namespace observer
{
class ObAllVirtualServer : public common::ObVirtualTableScannerIterator
{
  enum COLUMN_ID_LIST
  {
    SVR_IP = common::OB_APP_MIN_COLUMN_ID,
    SVR_PORT,
    SQL_PORT,
    RPC_PORT,
    CPU_CAPACITY,
    CPU_CAPACITY_MAX,
    CPU_ASSIGNED,
    CPU_ASSIGNED_MAX,
    MEM_CAPACITY,
    MEM_ASSIGNED,
    DATA_DISK_CAPACITY,
    DATA_DISK_IN_USE,
    DATA_DISK_HEALTH_STATUS,
    DATA_DISK_ABNORMAL_TIME,
    LOG_DISK_CAPACITY,
    LOG_DISK_ASSIGNED,
    LOG_DISK_IN_USE,
    RPC_CERT_EXPIRE_TIME,
    RPC_TLS_ENABLED,
    MEMORY_LIMIT,
    DATA_DISK_ALLOCATED,
    DATA_DISK_ASSIGNED,
    START_SERVICE_TIME,
    CREATE_TIME,
    ROLE,
    SWITCHOVER_STATUS,
    LOG_RESTORE_SOURCE,
    SYNC_SCN,
    READABLE_SCN
  };

public:
  ObAllVirtualServer();
  virtual ~ObAllVirtualServer();
  int init(common::ObAddr &addr, common::ObServerConfig *config);
  virtual int inner_open();
  virtual int inner_get_next_row(common::ObNewRow *&row);

private:
  char ip_buf_[common::OB_IP_STR_BUFF];
  common::ObAddr addr_;
  common::ObServerConfig *config_;
  char log_restore_source_buf_[1024];
  char role_buf_[64];
  char switchover_status_buf_[128];

private:
  DISALLOW_COPY_AND_ASSIGN(ObAllVirtualServer);
};

}
}
#endif /* SRC_OBSERVER_VIRTUAL_TABLE_OB_ALL_VIRTUAL_SERVER_H_ */
