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

#ifndef OB_ALL_VIRTUAL_DUMP_INFO_H_
#define OB_ALL_VIRTUAL_DUMP_INFO_H_
#include "observer/virtual_table/ob_virtual_table_scanner_iterator.h"

namespace oceanbase
{
namespace observer
{
class ObAllVirtualDumpInfo : public common::ObVirtualTableScannerIterator
{
public:
  ObAllVirtualDumpInfo();
  virtual ~ObAllVirtualDumpInfo();
  virtual int inner_get_next_row(common::ObNewRow *&row);
private:
  enum COLUMN_ID {
    MIN_CPU = common::OB_APP_MIN_COLUMN_ID,
    MAX_CPU,
    STOPPED,
    RECV_MYSQL_COUNT,
    RECV_TASK_COUNT,
    WORKER_COUNT,
    REQUEST_QUEUE_SIZE,
    QUEUE_0_SIZE,
    QUEUE_1_SIZE,
    QUEUE_2_SIZE,
    QUEUE_3_SIZE,
    QUEUE_4_SIZE,
    QUEUE_5_SIZE
  };
  bool is_inited_;
  DISALLOW_COPY_AND_ASSIGN(ObAllVirtualDumpInfo);
};

} /* namespace observer */
} /* namespace oceanbase */
#endif /* OB_ALL_VIRTUAL_DUMP_INFO_H_ */
