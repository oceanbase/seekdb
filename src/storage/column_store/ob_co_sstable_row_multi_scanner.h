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
#ifndef OB_STORAGE_COLUMN_STORE_OB_CO_SSTABLE_ROW_MULTI_SCANNER_H_
#define OB_STORAGE_COLUMN_STORE_OB_CO_SSTABLE_ROW_MULTI_SCANNER_H_
#include "ob_co_sstable_row_scanner.h"

namespace oceanbase
{
namespace storage
{
class ObCOSSTableRowMultiScanner : public ObCOSSTableRowScanner
{
public:
  ObCOSSTableRowMultiScanner()
      : ObCOSSTableRowScanner(),
      ranges_(nullptr)
  {
    this->type_ = ObStoreRowIterator::IteratorCOMultiScan;
  }
  virtual ~ObCOSSTableRowMultiScanner() {};
  virtual void reset() override;
  virtual void reuse() override;
private:
  virtual int init_row_scanner(
      const ObTableIterParam &param,
      ObTableAccessContext &context,
      ObITable *table,
      const void *query_range) override;
  virtual int get_group_idx(int64_t &group_idx) override;
  const common::ObIArray<blocksstable::ObDatumRange> *ranges_;
};
}
}

#endif
