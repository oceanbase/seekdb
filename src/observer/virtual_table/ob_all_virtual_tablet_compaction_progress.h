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

#ifndef OB_ALL_VIRTUAL_PARITION_COMPACTION_PROGRESS_H_
#define OB_ALL_VIRTUAL_PARITION_COMPACTION_PROGRESS_H_
#include "share/ob_virtual_table_scanner_iterator.h"
#include "storage/compaction/ob_tenant_compaction_progress.h"

namespace oceanbase
{
namespace observer
{

class ObAllVirtualTabletCompactionProgress : public common::ObVirtualTableScannerIterator
{
public:
  enum COLUMN_ID_LIST
  {
    SVR_IP  = common::OB_APP_MIN_COLUMN_ID,
    SVR_PORT,
    TENANT_ID,
    MERGE_TYPE,
    LS_ID,
    TABLET_ID,
    MERGE_VERSION,
    TASK_ID,
    STATUS,
    DATA_SIZE,
    UNFINISHED_DATA_SIZE,
    PROGRESSIVE_MERGE_ROUND,
    CREATE_TIME,
    START_TIME,
    ESTIMATED_FINISH_TIME,
    START_CG_ID,
    END_CG_ID
  };
  ObAllVirtualTabletCompactionProgress();
  virtual ~ObAllVirtualTabletCompactionProgress();
  int init();
  virtual int inner_get_next_row(common::ObNewRow *&row);
  virtual void reset();
protected:
  int fill_cells();
private:
  char ip_buf_[common::OB_IP_STR_BUFF];
  char dag_id_buf_[common::OB_TRACE_STAT_BUFFER_SIZE];
  compaction::ObTabletCompactionProgress progress_;
  compaction::ObTabletCompactionProgressIterator progress_iter_;
  bool is_inited_;
  DISALLOW_COPY_AND_ASSIGN(ObAllVirtualTabletCompactionProgress);
};

} /* namespace observer */
} /* namespace oceanbase */
#endif
