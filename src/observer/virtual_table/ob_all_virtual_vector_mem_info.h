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

#ifndef OB_ALL_VIRTUAL_VECTOR_MEM_INFO_H_
#define OB_ALL_VIRTUAL_VECTOR_MEM_INFO_H_
#include "observer/virtual_table/ob_virtual_table_scanner_iterator.h"
#include "lib/alloc/ob_malloc_sample_struct.h"
#include "storage/tablet/ob_tablet_iterator.h"

namespace oceanbase
{
namespace observer
{

class ObAllVirtualVectorMemInfo : public common::ObVirtualTableScannerIterator
{
public:
  enum COLUMN_ID_LIST
  {
        RAW_MALLOC_SIZE = common::OB_APP_MIN_COLUMN_ID,
    INDEX_METADATA_SIZE,
    VECTOR_MEM_HOLD,
    VECTOR_MEM_USED,
    VECTOR_MEM_LIMIT,
    TX_SHARE_LIMIT,
    VECTOR_MEM_DETAIL_INFO,
  };
  ObAllVirtualVectorMemInfo();
  virtual ~ObAllVirtualVectorMemInfo();
public:
  virtual int inner_get_next_row(common::ObNewRow *&row);
  virtual void reset();
private:
  int64_t fill_glibc_used_info();
  lib::ObMallocSampleMap::const_iterator it_;
  lib::ObMallocSampleMap malloc_sample_map_;
  char vector_used_str_[OB_MAX_MYSQL_VARCHAR_LENGTH];
  common::ObSEArray<obcall::ObTabletPair, ObTabletCommon::DEFAULT_ITERATOR_TABLET_ID_CNT> complete_tablet_ids_;
  common::ObSEArray<obcall::ObTabletPair, ObTabletCommon::DEFAULT_ITERATOR_TABLET_ID_CNT> partial_tablet_ids_;
  common::ObSEArray<obcall::ObTabletPair, ObTabletCommon::DEFAULT_ITERATOR_TABLET_ID_CNT> cache_tablet_ids_;
private:
  DISALLOW_COPY_AND_ASSIGN(ObAllVirtualVectorMemInfo);
};

} /* namespace observer */
} /* namespace oceanbase */
#endif
