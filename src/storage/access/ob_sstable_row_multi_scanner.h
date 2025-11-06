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

#ifndef OB_STORAGE_OB_SSTABLE_ROW_MULTI_SCANNER_H_
#define OB_STORAGE_OB_SSTABLE_ROW_MULTI_SCANNER_H_

#include "ob_sstable_row_scanner.h"
#include "storage/blocksstable/ob_micro_block_row_getter.h"

namespace oceanbase {
namespace storage {
template<typename PrefetchType = ObIndexTreeMultiPassPrefetcher<>>
class ObSSTableRowMultiScanner : public ObSSTableRowScanner<PrefetchType>
{
public:
  ObSSTableRowMultiScanner()
  {
    this->type_ = ObStoreRowIterator::IteratorMultiScan;
  }
  virtual ~ObSSTableRowMultiScanner();
  virtual void reset() override;
  virtual void reuse() override;
};

}
}
#endif //OB_STORAGE_OB_SSTABLE_ROW_MULTI_SCANNER_H_
