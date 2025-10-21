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

#ifndef OCEANBASE_OBSERVER_VIRTUAL_TABLE_OB_INFORMATION_KVCACHE_TABLE_
#define OCEANBASE_OBSERVER_VIRTUAL_TABLE_OB_INFORMATION_KVCACHE_TABLE_

#include "share/ob_virtual_table_scanner_iterator.h"
#include "share/cache/ob_kv_storecache.h"
#include "lib/stat/ob_di_cache.h"


namespace oceanbase
{
namespace common
{
class ObObj;
}

namespace observer
{

class ObInfoSchemaKvCacheTable : public common::ObVirtualTableScannerIterator
{
public:
  ObInfoSchemaKvCacheTable();
  virtual ~ObInfoSchemaKvCacheTable();
  virtual int inner_get_next_row(common::ObNewRow *&row);
  virtual void reset();
  inline void set_addr(common::ObAddr &addr) {addr_ = &addr;}

private:
  virtual int set_ip();
  virtual int inner_open() override;
  int get_tenant_info();
  int get_handles(ObKVCacheInst *&inst, ObDiagnoseTenantInfo *& tenant_info);
  int set_diagnose_info(ObKVCacheInst *inst, ObDiagnoseTenantInfo *tenant_info);
  int process_row(const ObKVCacheInst *inst);

private:
  enum CACHE_COLUMN
  {
    TENANT_ID = common::OB_APP_MIN_COLUMN_ID,
    SVR_IP,
    SVR_PORT,
    CACHE_NAME,
    CACHE_ID,
    PRIORITY,
    CACHE_SIZE,
    CACHE_STORE_SIZE,
    CACHE_MAP_SIZE,
    KV_CNT,
    HIT_RATIO,
    TOTAL_PUT_CNT,
    TOTAL_HIT_CNT,
    TOTAL_MISS_CNT,
    HOLD_SIZE
  };
  common::ObAddr *addr_;
  common::ObString ipstr_;
  int32_t port_;
  common::ObSEArray<common::ObKVCacheInstHandle, 100 > inst_handles_;
  int16_t cache_iter_;
  common::ObStringBuf str_buf_;
  common::ObObj cells_[common::OB_ROW_MAX_COLUMNS_COUNT];
  common::ObArenaAllocator arenallocator_;
  common::ObDiagnoseTenantInfo tenant_di_info_;
  common::ObArray<std::pair<uint64_t, common::ObDiagnoseTenantInfo*> > tenant_dis_;
  DISALLOW_COPY_AND_ASSIGN(ObInfoSchemaKvCacheTable);
};

}
}
#endif /* OCEANBASE_OBSERVER_VIRTUAL_TABLE_OB_INFORMATION_KVCACHE_TABLE */
