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

#ifndef SRC_OBSERVER_VIRTUAL_TABLE_OB_ALL_VIRTUAL_SYS_TASK_STATUS_H_
#define SRC_OBSERVER_VIRTUAL_TABLE_OB_ALL_VIRTUAL_SYS_TASK_STATUS_H_

#include "share/ob_virtual_table_scanner_iterator.h"
#include "share/ob_scanner.h"
#include "common/row/ob_row.h"
#include "lib/container/ob_se_array.h"
#include "share/scheduler/ob_sys_task_stat.h"

namespace oceanbase
{
namespace observer
{
class ObAllVirtualSysTaskStatus: public common::ObVirtualTableScannerIterator
{
public:
  ObAllVirtualSysTaskStatus();
  virtual ~ObAllVirtualSysTaskStatus();

  int init (share::ObSysTaskStatMgr &status_mgr);
  virtual int inner_get_next_row(common::ObNewRow *&row);
  virtual void reset();

private:
  share::ObSysStatMgrIter iter_;
  char task_id_[common::OB_TRACE_STAT_BUFFER_SIZE];
  char svr_ip_[common::MAX_IP_ADDR_LENGTH];
  char comment_[common::OB_MAX_TASK_COMMENT_LENGTH];
  DISALLOW_COPY_AND_ASSIGN(ObAllVirtualSysTaskStatus);

};

} // observer
} // oceanbase


#endif /* SRC_OBSERVER_VIRTUAL_TABLE_OB_ALL_VIRTUAL_SYS_TASK_STATUS_H_ */
