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

#ifndef OCEANBASE_OBSERVER_VIRTUAL_TABLE_OB_ALL_VIRTUAL_MALLOC_SAMPLE_INFO_H_
#define OCEANBASE_OBSERVER_VIRTUAL_TABLE_OB_ALL_VIRTUAL_MALLOC_SAMPLE_INFO_H_

#include "lib/container/ob_array.h"
#include "lib/alloc/ob_malloc_sample_struct.h"
#include "share/ob_virtual_table_scanner_iterator.h"

namespace oceanbase
{
namespace observer
{
class ObMallocSampleInfo : public common::ObVirtualTableScannerIterator
{
public:
  ObMallocSampleInfo();
  virtual ~ObMallocSampleInfo();
  virtual int inner_open();
  virtual void reset();
  virtual int inner_get_next_row(common::ObNewRow *&row);
private:
  int fill_row(common::ObNewRow *&row);
private:
  enum CACHE_COLUMN
  {
    SVR_IP = common::OB_APP_MIN_COLUMN_ID,
    SVR_PORT,
    TENANT_ID,
    CTX_ID,
    MOD_NAME,
    BACKTRACE,
    CTX_NAME,
    ALLOC_COUNT,
    ALLOC_BYTES,
  };
  char ip_buf_[common::OB_IP_STR_BUFF];
  char bt_[lib::MAX_BACKTRACE_LENGTH];
  lib::ObMallocSampleMap::const_iterator it_;
  lib::ObMallocSampleMap malloc_sample_map_;
  int64_t col_count_;
  bool opened_;
private:
  DISALLOW_COPY_AND_ASSIGN(ObMallocSampleInfo);
};
}
}

#endif 
