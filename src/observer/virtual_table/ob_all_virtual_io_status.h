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

#ifndef OB_ALL_VIRTUAL_IO_STATUS_H
#define OB_ALL_VIRTUAL_IO_STATUS_H

#include "observer/virtual_table/ob_virtual_table_scanner_iterator.h"
#include "sql/ob_scanner.h"
#include "common/row/ob_row.h"
#include "share/io_request.h"
namespace oceanbase
{
namespace observer
{
class ObAllVirtualIOStatusIterator : public ObVirtualTableScannerIterator
{
public:
  ObAllVirtualIOStatusIterator();
  virtual ~ObAllVirtualIOStatusIterator();
  int init_addr(const common::ObAddr &addr);
  virtual void reset() override;
protected:
  bool is_inited_;
  common::ObAddr addr_;
  char ip_buf_[common::MAX_IP_ADDR_LENGTH];
private:
  DISALLOW_COPY_AND_ASSIGN(ObAllVirtualIOStatusIterator);
};

class ObAllVirtualIOStat : public ObAllVirtualIOStatusIterator
{
public:
  ObAllVirtualIOStat();
  int init(const common::ObAddr &addr);
  virtual void reset() override;
  virtual int inner_get_next_row(common::ObNewRow *&row) override;
private:
  enum COLUMN
  {
    MODULE_ID = common::OB_APP_MIN_COLUMN_ID,
    MODE,
    SIZE,
    IOPS,
    MBPS,
    TOTAL_US
  };
  struct IOStat
  {
    uint64_t module_id_ = 0;
    common::ObIOMode mode_ = common::ObIOMode::READ;
    int64_t size_ = 0;
    int64_t iops_ = 0;
    int64_t total_us_ = 0;
  };
  ObArray<IOStat> stats_;
  int64_t position_;
  DISALLOW_COPY_AND_ASSIGN(ObAllVirtualIOStat);
};

}// namespace observer
}// namespace oceanbase

#endif /* !OB_ALL_VIRTUAL_BAD_BLOCK_TABLE_H */
