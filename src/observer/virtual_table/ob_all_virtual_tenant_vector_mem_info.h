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

#ifndef OB_ALL_VIRTUAL_TENANT_VECTOR_MEM_INFO_H_
#define OB_ALL_VIRTUAL_TENANT_VECTOR_MEM_INFO_H_
#include "share/ob_virtual_table_scanner_iterator.h"
#include "lib/alloc/ob_malloc_sample_struct.h"
#include "storage/tablet/ob_tablet_iterator.h"
#include "observer/omt/ob_multi_tenant_operator.h"

namespace oceanbase
{
namespace observer
{

class ObAllVirtualTenantVectorMemInfo : public common::ObVirtualTableScannerIterator
{
public:
  enum COLUMN_ID_LIST
  {
    SVR_IP  = common::OB_APP_MIN_COLUMN_ID,
    SVR_PORT,
    TENANT_ID,
    RAW_MALLOC_SIZE,
    INDEX_METADATA_SIZE,
    VECTOR_MEM_HOLD,
    VECTOR_MEM_USED,
    VECTOR_MEM_LIMIT,
    TX_SHARE_LIMIT,
    VECTOR_MEM_DETAIL_INFO,
  };
  ObAllVirtualTenantVectorMemInfo();
  virtual ~ObAllVirtualTenantVectorMemInfo();
public:
  inline void set_addr(common::ObAddr &addr) { addr_ = addr; }
  virtual int inner_get_next_row(common::ObNewRow *&row);
  virtual void reset();
private:
  int64_t fill_glibc_used_info(uint64_t tenant_id);
  common::ObAddr addr_;
  uint64_t current_pos_;
  lib::ObMallocSampleMap::const_iterator it_;
  lib::ObMallocSampleMap malloc_sample_map_;
  char vector_used_str_[OB_MAX_MYSQL_VARCHAR_LENGTH];
  common::ObSEArray<obrpc::ObLSTabletPair, ObTabletCommon::DEFAULT_ITERATOR_TABLET_ID_CNT> complete_tablet_ids_;
  common::ObSEArray<obrpc::ObLSTabletPair, ObTabletCommon::DEFAULT_ITERATOR_TABLET_ID_CNT> partial_tablet_ids_;
  common::ObSEArray<obrpc::ObLSTabletPair, ObTabletCommon::DEFAULT_ITERATOR_TABLET_ID_CNT> cache_tablet_ids_;
private:
  DISALLOW_COPY_AND_ASSIGN(ObAllVirtualTenantVectorMemInfo);
};

} /* namespace observer */
} /* namespace oceanbase */
#endif
