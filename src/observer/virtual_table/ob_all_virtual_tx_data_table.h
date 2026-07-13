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

#ifndef OB_ALL_VIRTUAL_TX_DATA_TABLE_H_
#define OB_ALL_VIRTUAL_TX_DATA_TABLE_H_

#include "observer/virtual_table/ob_virtual_table_scanner_iterator.h"

namespace oceanbase
{
namespace storage
{
class ObLS;
}
namespace observer
{
class ObAllVirtualTxDataTable : public common::ObVirtualTableScannerIterator {
private:
  struct RowData {
    const char *state_;
    int64_t tx_data_count_;
    share::SCN min_tx_scn_;
    share::SCN max_tx_scn_;
    RowData() : state_(""), tx_data_count_(-1), min_tx_scn_(), max_tx_scn_() {}
  };

  enum VirtualTxDataTableColumnID : uint64_t {
    STATE_COL = OB_APP_MIN_COLUMN_ID,
    START_SCN_COL,
    END_SCN_COL,
    TX_DATA_COUNT_COL,
    MIN_TX_SCN_COL,
    MAX_TX_SCN_COL
  };

public:
  ObAllVirtualTxDataTable();
  ~ObAllVirtualTxDataTable();

  TO_STRING_KV(K_(memtable_array_pos), K_(sstable_array_pos));
public:
  virtual int inner_get_next_row(common::ObNewRow *&row);
  virtual void reset();

private:
  int get_next_tx_data_table_(ObITable *&tx_data_memtable);

  int prepare_row_data_(ObITable *tx_data_table, RowData &row_data);

private:
  int64_t memtable_array_pos_;
  int64_t sstable_array_pos_;
  storage::ObLS *ls_;
  bool tables_loaded_;
  ObTabletHandle tablet_handle_;
  ObTabletMemberWrapper<ObTabletTableStore> table_store_wrapper_;
  ObMemtableMgrHandle mgr_handle_;
  common::ObSEArray<ObTableHandleV2, 1> memtable_handles_;
  common::ObSEArray<ObITable *, 8> sstable_handles_;

private:
  DISALLOW_COPY_AND_ASSIGN(ObAllVirtualTxDataTable);
};
}  // namespace observer
}  // namespace oceanbase
#endif /* OB_ALL_VIRTUAL_MEMSTORE_INFO_H */
