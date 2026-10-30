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

#include "observer/virtual_table/ob_all_virtual_table_mgr.h"
#include "share/rc/ob_server_runtime.h"
#include "storage/tx_storage/ob_ls_service.h"

using namespace oceanbase;
using namespace common;
using namespace memtable;
using namespace storage;
using namespace observer;

ObAllVirtualTableMgr::ObAllVirtualTableMgr()
    : ObVirtualTableScannerIterator(),
      tablet_iter_(nullptr),
      tablet_allocator_("VTTable"),
      tablet_handle_(),
      table_store_iter_(),
      iter_buf_(nullptr)
{
}

ObAllVirtualTableMgr::~ObAllVirtualTableMgr()
{
  reset();
}

void ObAllVirtualTableMgr::reset()
{
  table_store_iter_.reset();
  tablet_handle_.reset();
  if (OB_NOT_NULL(tablet_iter_)) {
    tablet_iter_->~ObTabletIterator();
    tablet_iter_ = nullptr;
  }
  tablet_allocator_.reset();

  if (OB_NOT_NULL(iter_buf_)) {
    allocator_->free(iter_buf_);
    iter_buf_ = nullptr;
  }

  ObVirtualTableScannerIterator::reset();
}

int ObAllVirtualTableMgr::init(common::ObIAllocator *allocator)
{
  int ret = OB_SUCCESS;
  if (start_to_read_) {
    ret = OB_INIT_TWICE;
    SERVER_LOG(WARN, "cannot init twice", K(ret));
  } else if (OB_ISNULL(allocator)) {
    ret = OB_INVALID_ARGUMENT;
    SERVER_LOG(WARN, "invalid argument", K(ret), KP(allocator));
  } else if (OB_ISNULL(iter_buf_ = allocator->alloc(sizeof(ObTabletIterator)))) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
    SERVER_LOG(WARN, "fail to alloc tablet iter buf", K(ret));
  } else {
    allocator_ = allocator;
    start_to_read_ = true;
  }
  return ret;
}

int ObAllVirtualTableMgr::get_next_tablet()
{
  int ret = OB_SUCCESS;

  tablet_handle_.reset();
  tablet_allocator_.reuse();
  if (nullptr == tablet_iter_) {
    
    ObStorageMetaMemMgr *t3m = ::oceanbase::share::server_service<::oceanbase::storage::ObStorageMetaMemMgr>();
    if (OB_ISNULL(tablet_iter_ = new (iter_buf_) ObTabletIterator(*t3m, tablet_allocator_, nullptr/*no op*/))) {
      ret = OB_ERR_UNEXPECTED;
      SERVER_LOG(WARN, "fail to new tablet_iter_", K(ret));
    }
  }
  if (OB_FAIL(ret)) {
    // do nothing
  } else if (OB_FAIL(tablet_iter_->get_next_tablet(tablet_handle_))) {
    if (OB_UNLIKELY(OB_ITER_END != ret)) {
      SERVER_LOG(WARN, "fail to get tablet iter", K(ret));
    }
  } else if (OB_UNLIKELY(!tablet_handle_.is_valid())) {
    ret = OB_ERR_UNEXPECTED;
    SERVER_LOG(WARN, "unexpected invalid tablet", K(ret), K(tablet_handle_));
  }

  return ret;
}

int ObAllVirtualTableMgr::get_next_table(ObITable *&table)
{
  int ret = OB_SUCCESS;
  table = nullptr;
  if (OB_FAIL(table_store_iter_.get_next(table))) {
    if (OB_UNLIKELY(ret != OB_ITER_END)) {
      SERVER_LOG(WARN, "fail to iterate next table", K(ret));
    } else {
      ret = OB_SUCCESS;
      while (OB_SUCC(ret)) {
        table_store_iter_.reset();
        if (OB_FAIL(get_next_tablet())) {
          if (OB_ITER_END != ret) {
            SERVER_LOG(WARN, "fail to get next tablet", K(ret));
          }
        } else if (OB_UNLIKELY(!tablet_handle_.is_valid())) {
          ret = OB_ERR_UNEXPECTED;
          SERVER_LOG(WARN, "unexpected invalid tablet", K(ret), K_(tablet_handle));
        } else if (OB_FAIL(tablet_handle_.get_obj()->get_all_tables(table_store_iter_))) {
        } else if (0 != table_store_iter_.count()) {
          break;
        }
      }
      if (OB_FAIL(ret)) {
      } else if (OB_FAIL(table_store_iter_.get_next(table))) {
      }
    }
  }
  return ret;
}

int ObAllVirtualTableMgr::inner_get_next_row(common::ObNewRow *&row)
{
  int ret = OB_SUCCESS;
  ObITable *table = nullptr;
  if (OB_UNLIKELY(!start_to_read_)) {
    ret = OB_NOT_INIT;
    SERVER_LOG(WARN, "not inited", K(start_to_read_), K(ret));
  } else if (NULL == cur_row_.cells_) {
    ret = OB_ERR_UNEXPECTED;
    SERVER_LOG(ERROR, "cur row cell is NULL", K(ret));
  } else if (OB_FAIL(get_next_table(table))) {
    if (OB_ITER_END != ret) {
      SERVER_LOG(WARN, "get_next_table failed", K(ret));
    }
  } else if (NULL == table) {
    ret = OB_ERR_UNEXPECTED;
    SERVER_LOG(WARN, "table shouldn't NULL here", K(ret), K(table));
  } else {
    const int64_t nested_offset = table->is_sstable()
        ? static_cast<ObSSTable *>(table)->get_macro_offset() : 0;
    const int64_t nested_size = table->is_sstable()
        ? static_cast<ObSSTable *>(table)->get_macro_read_size() : 0;
    const ObITable::TableKey &table_key = table->get_key();
    const int64_t col_count = output_column_ids_.count();
    for (int64_t i = 0; OB_SUCC(ret) && i < col_count; ++i) {
      uint64_t col_id = output_column_ids_.at(i);
      switch (col_id) {
        case TABLET_ID:
          (OB_SUCC(ret) ? ret = DatumValueBuffer::set_int(cur_row_.cells_, cur_row_.count_, i, table_key.tablet_id_.id()) : ret);
          break;
          //TODO:SCN
        case START_LOG_SCN: {
          uint64_t v = table_key.scn_range_.start_scn_.get_val_for_inner_table_field();
          (OB_SUCC(ret) ? ret = DatumValueBuffer::set_uint64(cur_row_.cells_, cur_row_.count_, i, v) : ret);
          break;
        }
        case END_LOG_SCN: {
          uint64_t v = table_key.scn_range_.end_scn_.get_val_for_inner_table_field();
          (OB_SUCC(ret) ? ret = DatumValueBuffer::set_uint64(cur_row_.cells_, cur_row_.count_, i, v) : ret);
        }
          break;
        case UPPER_TRANS_VERSION: {
          uint64_t v =  table->get_upper_trans_version() < 0 ? 0 : (uint64_t)table->get_upper_trans_version();
          (OB_SUCC(ret) ? ret = DatumValueBuffer::set_uint64(cur_row_.cells_, cur_row_.count_, i, v) : ret);
          break;
        }
        case TABLE_TYPE:
          (OB_SUCC(ret) ? ret = DatumValueBuffer::set_int(cur_row_.cells_, cur_row_.count_, i, table_key.table_type_) : ret);
          break;
        case SIZE: {
          int64_t size = 0;
          if (table->is_memtable()) {
            size = static_cast<ObIMemtable *>(table)->get_occupied_size();
          } else if (table->is_sstable()) {
            size = static_cast<blocksstable::ObSSTable *>(table)->get_occupy_size();
          }
          (OB_SUCC(ret) ? ret = DatumValueBuffer::set_int(cur_row_.cells_, cur_row_.count_, i, size) : ret);
          break;
        }
        case DATA_BLOCK_CNT: {
          const int64_t blk_cnt = table->is_memtable() ? 0
            : static_cast<ObSSTable *>(table)->get_data_macro_block_count();
          (OB_SUCC(ret) ? ret = DatumValueBuffer::set_int(cur_row_.cells_, cur_row_.count_, i, blk_cnt) : ret);
          break;
        }
        case INDEX_BLOCK_CNT: {
          int64_t blk_cnt = 0;
          if (table->is_sstable()) {
            blocksstable::ObSSTable * sstable = static_cast<blocksstable::ObSSTable *>(table);
            blk_cnt = sstable->get_total_macro_block_count() - sstable->get_data_macro_block_count();
          }
          (OB_SUCC(ret) ? ret = DatumValueBuffer::set_int(cur_row_.cells_, cur_row_.count_, i, blk_cnt) : ret);
          break;
        }
        case LINKED_BLOCK_CNT: {
          int64_t blk_cnt = 0;
          if (table->is_sstable()) {
            blocksstable::ObSSTableMetaHandle sst_meta_hdl;
            if (OB_FAIL(static_cast<blocksstable::ObSSTable *>(table)->get_meta(sst_meta_hdl))) {
            } else {
              blk_cnt = sst_meta_hdl.get_sstable_meta().get_linked_macro_block_count();
            }
          }
          (OB_SUCC(ret) ? ret = DatumValueBuffer::set_int(cur_row_.cells_, cur_row_.count_, i, blk_cnt) : ret);
          break;
        }
        case REF:
          (OB_SUCC(ret) ? ret = DatumValueBuffer::set_int(cur_row_.cells_, cur_row_.count_, i, table->get_ref()) : ret);
          break;
        case IS_ACTIVE: {
          bool is_active = false;
          if (table->is_memtable()) {
            is_active = static_cast<ObIMemtable *>(table)->is_active_memtable();
          }
          (OB_SUCC(ret) ? ret = DatumValueBuffer::set_varchar_view(cur_row_.cells_, cur_row_.count_, i, is_active ? "YES" : "NO") : ret);
          cur_row_.cells_[i].set_collation_type(ObCharset::get_default_collation(ObCharset::get_default_charset()));
          break;
        }
        case CONTAIN_UNCOMMITTED_ROW: {
          bool contain_uncommitted_row = false;
          if (table->is_memtable()) {
            contain_uncommitted_row = true;
          } else if (table->is_sstable()) {
            contain_uncommitted_row = static_cast<blocksstable::ObSSTable *>(table)->contain_uncommitted_row();
          }
          (OB_SUCC(ret) ? ret = DatumValueBuffer::set_varchar_view(cur_row_.cells_, cur_row_.count_, i, contain_uncommitted_row ? "YES" : "NO") : ret);
          cur_row_.cells_[i].set_collation_type(ObCharset::get_default_collation(ObCharset::get_default_charset()));
          break;
        }
        case NESTED_OFFSET:
          (OB_SUCC(ret) ? ret = DatumValueBuffer::set_int(cur_row_.cells_, cur_row_.count_, i, nested_offset) : ret);
          break;
        case NESTED_SIZE:
          (OB_SUCC(ret) ? ret = DatumValueBuffer::set_int(cur_row_.cells_, cur_row_.count_, i, nested_size) : ret);
          break;
        case DATA_CHECKSUM: {
          int64_t data_checksum = 0;
          if (table->is_memtable()) {
            // memtable has no data checksum, do nothing
          } else if (table->is_sstable()) {
            data_checksum = static_cast<blocksstable::ObSSTable *>(table)->get_data_checksum();
          }
          (OB_SUCC(ret) ? ret = DatumValueBuffer::set_int(cur_row_.cells_, cur_row_.count_, i, data_checksum) : ret);
          break;
        }
        default:
          ret = OB_ERR_UNEXPECTED;
          SERVER_LOG(WARN, "invalid col_id", K(ret), K(col_id));
          break;
      }
    }
  }
  if (OB_SUCC(ret)) {
    row = &cur_row_;
  }
  return ret;
}
