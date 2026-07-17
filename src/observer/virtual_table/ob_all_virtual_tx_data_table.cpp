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

#include "observer/virtual_table/ob_all_virtual_tx_data_table.h"
#include "share/rc/ob_module_provider.h"
#include "storage/ls/ob_ls.h"
#include "storage/tx_storage/ob_ls_service.h"

using namespace oceanbase::common;
using namespace oceanbase::memtable;
using namespace oceanbase::storage;
namespace oceanbase {
namespace observer {

ObAllVirtualTxDataTable::ObAllVirtualTxDataTable()
    : ObVirtualTableScannerIterator(),
      memtable_array_pos_(-1),
      sstable_array_pos_(-1),
      ls_(nullptr),
      tables_loaded_(false),
      tablet_handle_(),
      table_store_wrapper_(),
      mgr_handle_(),
      memtable_handles_(),
      sstable_handles_()
{}

ObAllVirtualTxDataTable::~ObAllVirtualTxDataTable()
{
  reset();
}

void ObAllVirtualTxDataTable::reset()
{
  mgr_handle_.reset();
  ls_ = nullptr;
  tables_loaded_ = false;
  memtable_array_pos_ = -1;
  sstable_array_pos_ = -1;
  ObVirtualTableScannerIterator::reset();
}

int ObAllVirtualTxDataTable::inner_get_next_row(common::ObNewRow *&row)
{
  int ret = OB_SUCCESS;

  ObITable *tx_data_table = nullptr;
  RowData row_data;

  if (nullptr == allocator_) {
    ret = OB_NOT_INIT;
    SERVER_LOG(WARN, "allocator_ shouldn't be nullptr", K(allocator_), KR(ret));
  } else if (FALSE_IT(start_to_read_ = true)) {
  } else if (OB_FAIL(get_next_tx_data_table_(tx_data_table))) {
    if (OB_ITER_END != ret) {
      SERVER_LOG(WARN, "get next tx data table failed", KR(ret));
    }
  } else if (OB_UNLIKELY(nullptr == tx_data_table)) {
    ret = OB_ERR_UNEXPECTED;
    SERVER_LOG(WARN, "tx_data_table shouldn't nullptr here", KR(ret), KP(tx_data_table));
  } else if (OB_FAIL(prepare_row_data_(tx_data_table, row_data))) {
    SERVER_LOG(WARN, "prepare_row_data_ fail", KR(ret), KP(tx_data_table));
  } else {
    const int64_t col_count = output_column_ids_.count();
    for (int64_t i = 0; OB_SUCC(ret) && i < col_count; ++i) {
      uint64_t col_id = output_column_ids_.at(i);
      switch (col_id) {
        case STATE_COL:
          cur_row_.cells_[i].set_varchar(row_data.state_);
          cur_row_.cells_[i].set_collation_type(ObCharset::get_default_collation(ObCharset::get_default_charset()));
          break;
        case START_SCN_COL: {
          uint64_t v = tx_data_table->get_key().scn_range_.start_scn_.get_val_for_inner_table_field();
          cur_row_.cells_[i].set_uint64(v);
          break;
        }
        case END_SCN_COL: {
          uint64_t v = tx_data_table->get_key().scn_range_.end_scn_.get_val_for_inner_table_field();
          cur_row_.cells_[i].set_uint64(v);
          break;
        }
        case TX_DATA_COUNT_COL:
          cur_row_.cells_[i].set_int(row_data.tx_data_count_);
          break;
        case MIN_TX_SCN_COL:
          cur_row_.cells_[i].set_uint64(row_data.min_tx_scn_.get_val_for_inner_table_field());
          break;
        case MAX_TX_SCN_COL:
          cur_row_.cells_[i].set_uint64(row_data.max_tx_scn_.get_val_for_inner_table_field());
          break;
        default:
          ret = OB_ERR_UNEXPECTED;
          SERVER_LOG(WARN, "invalid col_id", KR(ret), K(col_id));
          break;
      }
    }
  }
  if (OB_SUCC(ret)) {
    row = &cur_row_;
  }

  return ret;
}

int ObAllVirtualTxDataTable::get_next_tx_data_table_(ObITable *&tx_data_table)
{
  int ret = OB_SUCCESS;

  if (!tables_loaded_) {
    ObTablet *tablet = nullptr;
    ObIMemtableMgr *memtable_mgr = nullptr;
    memtable_handles_.reset();
    sstable_handles_.reset();
    tablet_handle_.reset();
    mgr_handle_.reset();
    table_store_wrapper_.reset();

    auto *ls_service = share::g_mp->ls_service();
    if (OB_ISNULL(ls_service)) {
      ret = OB_ERR_UNEXPECTED;
      SERVER_LOG(WARN, "ls service is null", KR(ret));
    } else if (OB_FAIL(ls_service->get_ls(ls_))) {
      SERVER_LOG(WARN, "get log stream failed", KR(ret));
    }

    if (OB_FAIL(ret)) {
    } else if (OB_FAIL(ls_->get_tablet_svr()->get_tx_data_memtable_mgr(mgr_handle_))) {
      SERVER_LOG(WARN, "fail to get tx data memtable mgr.", KR(ret));
    } else if (FALSE_IT(memtable_mgr = mgr_handle_.get_memtable_mgr())) {
    } else if (OB_FAIL(memtable_mgr->get_all_memtables(memtable_handles_))) {
      SERVER_LOG(WARN, "fail to get all memtables for log stream", KR(ret));
    } else if (OB_FAIL(ls_->get_tablet_svr()->get_tablet(LS_TX_DATA_TABLET, tablet_handle_))) {
      SERVER_LOG(WARN, "fail to get tx data tablet", KR(ret));
    } else if (FALSE_IT(tablet = tablet_handle_.get_obj())) {
    } else if (OB_FAIL(tablet->fetch_table_store(table_store_wrapper_))) {
      SERVER_LOG(WARN, "fail to fetch table store", K(ret));
    } else {
      const ObSSTableArray &minor_tables = table_store_wrapper_.get_member()->get_minor_sstables();
      for (int64_t i = 0; OB_SUCC(ret) && i < minor_tables.count(); ++i) {
        if (OB_ISNULL(minor_tables[i])) {
          ret = OB_ERR_UNEXPECTED;
          SERVER_LOG(WARN, "get unexpected null sstable", KR(ret));
        } else if (OB_FAIL(sstable_handles_.push_back(minor_tables[i]))) {
          SERVER_LOG(WARN, "fail to add sstable", KR(ret));
        }
      }
    }

    if (OB_SUCC(ret)) {
      // iterate from the newest memtable in memtable handles
      memtable_array_pos_ = memtable_handles_.count() - 1;
      // iterate from the newest sstable in sstable handles
      sstable_array_pos_ = sstable_handles_.count() - 1;
      tables_loaded_ = true;
      if (memtable_array_pos_ < 0 && sstable_array_pos_ < 0) {
        SERVER_LOG(INFO,
                   "transaction data tables are empty",
                   KR(ret),
                   K(memtable_array_pos_),
                   K(sstable_array_pos_));
      }
    }
  }

  if (OB_SUCC(ret) && memtable_array_pos_ < 0 && sstable_array_pos_ < 0) {
    ret = OB_ITER_END;
  }

  if (OB_FAIL(ret)) {
  } else if (memtable_array_pos_ >= 0) {
    tx_data_table = memtable_handles_[memtable_array_pos_--].get_table();
  } else if (sstable_array_pos_ >= 0) {
    tx_data_table = sstable_handles_[sstable_array_pos_--];
  } else {
    ret = OB_ITER_END;
  }

  return ret;
}

int ObAllVirtualTxDataTable::prepare_row_data_(ObITable *tx_data_table, RowData &row_data)
{
  int ret = OB_SUCCESS;
  if (ObITable::TableType::TX_DATA_MEMTABLE == tx_data_table->get_key().table_type_) {
    ObTxDataMemtable *tx_data_memtable = static_cast<ObTxDataMemtable *>(tx_data_table);
    row_data.state_ = tx_data_memtable->get_state_string();
    row_data.tx_data_count_ = tx_data_memtable->size();
    row_data.min_tx_scn_ = tx_data_memtable->get_min_tx_scn();
    row_data.max_tx_scn_ = tx_data_memtable->get_max_tx_scn();
  } else if (tx_data_table->is_multi_version_minor_sstable()) {
    ObSSTable *tx_data_sstable = static_cast<ObSSTable *>(tx_data_table);
    ObSSTableMetaHandle sstable_meta_hdl;
    if (OB_FAIL(tx_data_sstable->get_meta(sstable_meta_hdl))) {
      STORAGE_LOG(WARN, "fail to get sstable meta handle", K(ret), KPC(tx_data_sstable));
    } else {
      row_data.state_ = ObITable::get_table_type_name(tx_data_table->get_key().table_type_);
      row_data.tx_data_count_ = sstable_meta_hdl.get_sstable_meta().get_row_count();
      row_data.min_tx_scn_ = sstable_meta_hdl.get_sstable_meta().get_filled_tx_scn();
      row_data.max_tx_scn_ = tx_data_sstable->get_key().scn_range_.end_scn_;
    }
  } else {
    STORAGE_LOG_RET(WARN, OB_ERR_UNEXPECTED, "Iterate an invalid table while select virtual tx data table.");
  }
  return ret;
}

}  // namespace observer
}  // namespace oceanbase
