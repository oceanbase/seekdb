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

#ifndef OB_STORAGE_MEMTABLE_OB_MEMTABLE_BLOCK_ROW_SCANNER_H_
#define OB_STORAGE_MEMTABLE_OB_MEMTABLE_BLOCK_ROW_SCANNER_H_

#include "storage/blocksstable/ob_micro_block_row_scanner.h"

namespace oceanbase {
using namespace blocksstable;

namespace memtable {
class ObMemtableSingleRowReader;

// This is an abstract middle layer in order to reuse logic in ObIMicroBlockRowScanner
class ObMemtableBlockRowScanner : public blocksstable::ObIMicroBlockRowScanner {
public:
  ObMemtableBlockRowScanner(common::ObIAllocator &allocator)
      : ObIMicroBlockRowScanner(allocator)
  {}
  virtual ~ObMemtableBlockRowScanner()
  {}

  int init(const storage::ObTableIterParam &param,
           storage::ObTableAccessContext &context,
           ObMemtableSingleRowReader &single_row_reader);
  virtual int get_next_rows() override;

protected:
  virtual int inner_get_next_row(const ObDatumRow *&row) override;

private:
  int prefetch();
  int fetch_row(const ObDatumRow *&row);
  int inner_get_next_rows();
};

}  // end of namespace memtable
}  // end of namespace oceanbase
#endif
