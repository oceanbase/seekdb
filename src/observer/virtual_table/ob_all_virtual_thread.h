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

#ifndef SRC_OBSERVER_VIRTUAL_TABLE_OB_ALL_VIRTUAL_THREAD_H_
#define SRC_OBSERVER_VIRTUAL_TABLE_OB_ALL_VIRTUAL_THREAD_H_

#include "share/ob_virtual_table_scanner_iterator.h"

namespace oceanbase
{
namespace observer
{

class ObAllVirtualThread : public common::ObVirtualTableScannerIterator
{
  enum COLUMN_ID_LIST
  {
    SVR_IP = common::OB_APP_MIN_COLUMN_ID,
    SVR_PORT,
    TENANT_ID,
    TID,
    TNAME,
    STATUS,
    WAIT_EVENT,
    LATCH_WAIT,
    LATCH_HOLD,
    TRACE_ID,
    LOOP_TS,
    CGROUP_PATH,
    NUMA_NODE
  };

public:
  ObAllVirtualThread();
  virtual ~ObAllVirtualThread() override;
  virtual int inner_open() override;
  virtual void reset() override;
  virtual int inner_get_next_row(common::ObNewRow *&row) override;
private:
  static const int32_t PATH_BUFSIZE = 512;
  bool is_inited_;
  bool is_config_cgroup_;
  char ip_buf_[common::OB_IP_STR_BUFF];
  char tname_[16];
  char wait_event_[96];
  char wait_addr_[16];
  char locks_addr_[256];
  char trace_id_buf_[40];
  char cgroup_path_buf_[PATH_BUFSIZE];
  int read_real_cgroup_path();

private:
  DISALLOW_COPY_AND_ASSIGN(ObAllVirtualThread);
};

}
}
#endif /* SRC_OBSERVER_VIRTUAL_TABLE_OB_ALL_VIRTUAL_THREAD_H_ */
