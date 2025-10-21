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

#ifndef OB_ALL_VIRTUAL_STORAGE_LEAK_INFO_H_
#define OB_ALL_VIRTUAL_STORAGE_LEAK_INFO_H_
#include "share/ob_virtual_table_scanner_iterator.h"
#include "share/cache/ob_kv_storecache.h"


namespace oceanbase
{
namespace observer
{

class ObAllVirtualStorageLeakInfo : public common::ObVirtualTableScannerIterator
{
public:
  ObAllVirtualStorageLeakInfo();
  virtual ~ObAllVirtualStorageLeakInfo();
  virtual void reset();
  OB_INLINE void set_addr(common::ObAddr &addr) {addr_ = &addr;}
  virtual int inner_get_next_row(ObNewRow *&row);

private:
  virtual int set_ip();
  virtual int inner_open() override;
  int process_row();
private:
  static const int64_t MAP_BUCKET_NUM = 10000;
  enum CHECKER_COLUMN
  {
    SVR_IP = common::OB_APP_MIN_COLUMN_ID,
    SVR_PORT,
    TENANT_ID,
    CHECK_ID,
    CHECK_MOD,
    HOLD_COUNT,
    BACKTRACE
  };

  common::ObAddr *addr_;
  common::ObString ipstr_;
  int32_t port_;
  char check_mod_[MAX_CACHE_NAME_LENGTH];
  char backtrace_[512];
  bool opened_;
  hash::ObHashMap<ObStorageCheckerValue, int64_t> map_info_;
  hash::ObHashMap<ObStorageCheckerValue, int64_t>::iterator map_iter_;
  DISALLOW_COPY_AND_ASSIGN(ObAllVirtualStorageLeakInfo);
};


};  // observer
};  // oceanbase
#endif  // OB_ALL_VIRTUAL_STORAGE_LEAK_INFO_H_
