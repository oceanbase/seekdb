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

#ifndef SRC_OBSERVER_VIRTUAL_TABLE_OB_ALL_VIRTUAL_UNIT_H_
#define SRC_OBSERVER_VIRTUAL_TABLE_OB_ALL_VIRTUAL_UNIT_H_

#include "common/row/ob_row.h"
#include "sql/ob_scanner.h"
#include "observer/virtual_table/ob_virtual_table_scanner_iterator.h"
#include "observer/omt/ob_tenant_meta.h"

namespace oceanbase
{
namespace observer
{
class ObAllVirtualUnit : public common::ObVirtualTableScannerIterator
{
  enum COLUMN_ID_LIST
  {
        MIN_CPU = common::OB_APP_MIN_COLUMN_ID,
    MAX_CPU,
    MEMORY_SIZE,
    MIN_IOPS,
    MAX_IOPS,
    IOPS_WEIGHT,
    LOG_DISK_SIZE,
    LOG_DISK_IN_USE,
    DATA_DISK_IN_USE,
    STATUS,
    CREATE_TIME,
    DATA_DISK_SIZE,
    MAX_NET_BANDWIDTH,
    NET_BANDWIDTH_WEIGHT
  };

public:
  ObAllVirtualUnit();
  virtual ~ObAllVirtualUnit();
  int init();
  virtual int inner_open();
  virtual void reset();
  virtual int inner_get_next_row(common::ObNewRow *&row);
private:
  int get_clog_disk_used_size_(int64_t &log_used_size);
private:
  omt::ObTenantMeta tenant_meta_;
  bool has_row_;
  bool consumed_;

private:
  DISALLOW_COPY_AND_ASSIGN(ObAllVirtualUnit);
};

}
}
#endif /* SRC_OBSERVER_VIRTUAL_TABLE_OB_ALL_VIRTUAL_UNIT_H_ */
