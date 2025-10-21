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

#ifndef OCEANBASE_OBSERVER_VIRTUAL_TABLE_OB_DISK_STAT_TABLE_
#define OCEANBASE_OBSERVER_VIRTUAL_TABLE_OB_DISK_STAT_TABLE_

#include "share/ob_virtual_table_scanner_iterator.h"


namespace oceanbase
{
namespace common
{
class ObObj;
}

namespace observer
{

class ObInfoSchemaDiskStatTable : public common::ObVirtualTableScannerIterator
{
public:
  ObInfoSchemaDiskStatTable();
  virtual ~ObInfoSchemaDiskStatTable();
  virtual int inner_get_next_row(common::ObNewRow *&row);
  virtual void reset();
  inline void set_addr(common::ObAddr &addr) {addr_ = &addr;}
  virtual int set_ip(common::ObAddr *addr);

private:
  enum DISK_COLUMN
  {
    SVR_IP = common::OB_APP_MIN_COLUMN_ID,
    SVR_PORT,
    TOTAL_SIZE,
    USED_SIZE,
    FREE_SIZE,
    IS_DISK_VALID,
    DISK_ERROR_BEGIN_TS,
    ALLOCATED_SIZE
  };
  common::ObAddr *addr_;
  common::ObString ipstr_;
  int32_t port_;
  bool is_end_;
  DISALLOW_COPY_AND_ASSIGN(ObInfoSchemaDiskStatTable);
};

}
}
#endif /* OCEANBASE_OBSERVER_VIRTUAL_TABLE_OB_DISK_STAT_TABLE */
