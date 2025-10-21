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

#ifndef OB_ALL_VIRTUAL_IO_STAT_H_
#define OB_ALL_VIRTUAL_IO_STAT_H_

#include "share/ob_virtual_table_scanner_iterator.h"

namespace oceanbase
{
namespace observer
{

class ObAllVirtualIOStat : public common::ObVirtualTableScannerIterator
{
public:
  ObAllVirtualIOStat();
  virtual ~ObAllVirtualIOStat();
  virtual int inner_get_next_row(common::ObNewRow *&row);
private:
  enum IOStatColumn
  {
    SVR_IP = common::OB_APP_MIN_COLUMN_ID,
    SVR_PORT,
    DISK_TYPE,
    SYS_IO_UP_LIMIT_IN_MB,
    SYS_IO_BAND_IN_MB,
    SYS_IO_LOW_WATERMARK_IN_MB,
    SYS_IO_HIGH_WATERMARK_IN_MB,
    IO_BENCH_RESULT,
  };

private:
  char svr_ip_[common::OB_IP_STR_BUFF];
  char disk_type_[common::OB_MAX_DISK_TYPE_LENGTH];
  char io_bench_result_[common::OB_MAX_IO_BENCH_RESULT_LENGTH];

private:
  DISALLOW_COPY_AND_ASSIGN(ObAllVirtualIOStat);
};


} // namespace observer
} // namespace oceanbase


#endif
