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

#ifndef OB_STORAGE_MEMTABLE_OB_MEMTABLE_READ_ROW_UTIL_H_
#define OB_STORAGE_MEMTABLE_OB_MEMTABLE_READ_ROW_UTIL_H_

#include "share/ob_define.h"

namespace oceanbase {

namespace common {
class ObStoreRowkey;
}

namespace storage {
class ObITableReadInfo;
}

namespace blocksstable {
class ObDatumRow;
}

namespace memtable {
class ObMvccValueIterator;
class ObNopBitMap;
class ObMvccTransNode;

class ObReadRow {
  DEFINE_ALLOCATOR_WRAPPER
public:
  static int iterate_row(const storage::ObITableReadInfo &read_info,
                         const common::ObStoreRowkey &key,
                         ObMvccValueIterator &value_iter,
                         blocksstable::ObDatumRow &row,
                         ObNopBitMap &bitmap,
                         int64_t &row_scn);

  static int iterate_delete_insert_row(const storage::ObITableReadInfo &read_info,
                                       const ObStoreRowkey &key,
                                       ObMvccValueIterator &value_iter,
                                       blocksstable::ObDatumRow &latest_row,
                                       blocksstable::ObDatumRow &earliest_row,
                                       ObNopBitMap &bitmap,
                                       int64_t &latest_row_scn,
                                       int64_t &earliest_row_scn,
                                       int64_t &acquired_row_cnt);
  static int iterate_row_key(const common::ObStoreRowkey &rowkey, blocksstable::ObDatumRow &row);

private:
  DISALLOW_COPY_AND_ASSIGN(ObReadRow);
private:
  static int iterate_row_value_(const storage::ObITableReadInfo &read_info,
                                ObMvccValueIterator &value_iter,
                                blocksstable::ObDatumRow &row,
                                ObNopBitMap &bitmap,
                                int64_t &row_scn);
  static int fill_in_row_with_tx_node_(const storage::ObITableReadInfo &read_info,
                                       ObMvccValueIterator &value_iter,
                                       const ObMvccTransNode *tx_node,
                                       blocksstable::ObDatumRow &row,
                                       ObNopBitMap &bitmap,
                                       int64_t &row_scn,
                                       bool &read_finished);
  static int acquire_delete_insert_tx_node_(ObMvccValueIterator &value_iter,
                                            const ObMvccTransNode *&latest_tx_node,
                                            const ObMvccTransNode *&earliest_tx_node);
};

}  // namespace memtable
}  // namespace oceanbase
#endif
