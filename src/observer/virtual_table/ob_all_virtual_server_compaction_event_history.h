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

#ifndef OB_ALL_VIRTUAL_SERVER_COMPACTION_EVENT_HISTORY_H_
#define OB_ALL_VIRTUAL_SERVER_COMPACTION_EVENT_HISTORY_H_
#include "share/ob_virtual_table_scanner_iterator.h"
#include "storage/compaction/ob_server_compaction_event_history.h"

namespace oceanbase
{
namespace observer
{

class ObAllVirtualServerCompactionEventHistory : public common::ObVirtualTableScannerIterator
{
public:
  enum COLUMN_ID_LIST
  {
    SVR_IP  = common::OB_APP_MIN_COLUMN_ID,
    SVR_PORT,
    ZONE,
    TENANT_ID,
    MERGE_TYPE,
    COMPACTION_SCN,
    EVENT_TIMESTAMP,
    EVENT,
    ROLE,
  };
  ObAllVirtualServerCompactionEventHistory();
  virtual ~ObAllVirtualServerCompactionEventHistory();
  int init();
  virtual int inner_get_next_row(common::ObNewRow *&row);
  virtual void reset();
protected:
  int fill_cells();
private:
  char ip_buf_[common::OB_IP_STR_BUFF];
  char event_buf_[common::OB_COMPACTION_EVENT_STR_LENGTH];
  compaction::ObServerCompactionEvent event_;
  compaction::ObServerCompactionEventIterator event_iter_;
  bool is_inited_;
  DISALLOW_COPY_AND_ASSIGN(ObAllVirtualServerCompactionEventHistory);
};

} /* namespace observer */
} /* namespace oceanbase */
#endif
