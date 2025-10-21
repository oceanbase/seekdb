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

#ifndef OCEANBASE_OBSERVER_VIRTUAL_TABLE_OB_ALL_VIRTUAL_PX_TARGET_MONITOR_H_
#define OCEANBASE_OBSERVER_VIRTUAL_TABLE_OB_ALL_VIRTUAL_PX_TARGET_MONITOR_H_


#include "share/ob_virtual_table_scanner_iterator.h"
#include "share/ob_scanner.h"
#include "common/row/ob_row.h"
#include "sql/engine/px/ob_px_target_mgr.h"

namespace oceanbase
{
namespace observer
{
class ObAllVirtualPxTargetMonitor: public common::ObVirtualTableScannerIterator
{
public:
  ObAllVirtualPxTargetMonitor();
  virtual ~ObAllVirtualPxTargetMonitor() {}
public:
  int init();
  virtual int inner_open();
  virtual int inner_get_next_row(common::ObNewRow *&row);
  virtual int inner_close();
private:
  int prepare_start_to_read();
  int get_next_target_info(ObPxTargetInfo &target_info);
private:
  enum TARGET_MONITOR_COLUMN
  {
    SVR_IP = common::OB_APP_MIN_COLUMN_ID,
    SVR_PORT,
    TENANT_ID,
    IS_LEADER,
    VERSION,
    PEER_IP,
    PEER_PORT,
    PEER_TARGET,
    PEER_TARGET_USED,
    LOCAL_TARGET_USED,
    LOCAL_PARALLEL_SESSION_COUNT
  };
  common::ObSEArray<uint64_t, 4> tenand_array_;
  uint64_t tenant_idx_;
  common::ObSEArray<ObPxTargetInfo, 10> target_info_array_;
  uint64_t target_usage_idx_;
  char svr_ip_buff_[common::OB_IP_PORT_STR_BUFF];
  char peer_ip_buff_[common::OB_IP_PORT_STR_BUFF];
}; //class ObAllVirtualPxTargetMonitor
}//namespace observer
}//namespace oceanbase
#endif //OCEANBASE_OBSERVER_VIRTUAL_TABLE_OB_ALL_VIRTUAL_PX_TARGET_MONITOR_H_
