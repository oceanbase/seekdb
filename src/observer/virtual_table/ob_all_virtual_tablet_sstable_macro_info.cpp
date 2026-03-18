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

#include "ob_all_virtual_tablet_sstable_macro_info.h"
#include "storage/meta_mem/ob_tenant_meta_mem_mgr.h"
#include "storage/tablet/ob_mds_schema_helper.h"

namespace oceanbase
{
using namespace storage;
using namespace blocksstable;
using namespace common;
using namespace share;
using namespace share::schema;
namespace observer
{

static OB_INLINE bool is_safe_tail3_key_obj(const common::ObObj &obj)
{
  bool ret = true;
  // Allow MIN/MAX sentinel to avoid type mismatch comparisons.
  if (obj.is_min_value() || obj.is_max_value()) {
    ret = true;
  } else {
    const common::ObObjTypeClass tc = obj.get_type_class();
    ret = (tc == common::ObIntTC
           || tc == common::ObUIntTC
           || tc == common::ObNumberTC
           || tc == common::ObDecimalIntTC);
  }
  return ret;
}

static OB_INLINE bool normalize_key_range_to_tail3(
    const common::ObNewRange &in,
    common::ObNewRange &out3,
    common::ObObj (&sk3)[3],
    common::ObObj (&ek3)[3])
{
  bool ret = true;
  const int64_t sk_len = in.start_key_.length();
  const int64_t ek_len = in.end_key_.length();
  if (sk_len < 3 || ek_len < 3) {
    ret = false;
  } else {
    const common::ObObj *sk = in.start_key_.get_obj_ptr() + (sk_len - 3);
    const common::ObObj *ek = in.end_key_.get_obj_ptr() + (ek_len - 3);
    if (!(is_safe_tail3_key_obj(sk[0]) && is_safe_tail3_key_obj(sk[1]) && is_safe_tail3_key_obj(sk[2])
          && is_safe_tail3_key_obj(ek[0]) && is_safe_tail3_key_obj(ek[1]) && is_safe_tail3_key_obj(ek[2]))) {
      ret = false;
    } else {
      sk3[0] = sk[0]; sk3[1] = sk[1]; sk3[2] = sk[2];
      ek3[0] = ek[0]; ek3[1] = ek[1]; ek3[2] = ek[2];
      out3.table_id_ = in.table_id_;
      out3.start_key_.assign(sk3, 3);
      out3.end_key_.assign(ek3, 3);
      // Keep conservative (closed) border to avoid skipping too aggressively.
      out3.border_flag_.set_inclusive_start();
      out3.border_flag_.set_inclusive_end();
      ret = true;
    }
  }
  return ret;
}

ObAllVirtualTabletSSTableMacroInfo::MacroInfo::MacroInfo()
  : data_seq_(0),
    macro_logic_version_(0),
    macro_block_index_(-1),
    micro_block_count_(0),
    data_checksum_(0),
    occupy_size_(0),
    original_size_(0),
    data_size_(0),
    data_zsize_(0),
    macro_block_id_(),
    store_range_(),
    row_count_(0),
    compressor_type_(ObCompressorType::INVALID_COMPRESSOR),
    row_store_type_(ObRowStoreType::MAX_ROW_STORE)
{
}

ObAllVirtualTabletSSTableMacroInfo::MacroInfo::~MacroInfo()
{
  reset();
}

void ObAllVirtualTabletSSTableMacroInfo::MacroInfo::reset()
{
  data_seq_ = 0;
  macro_logic_version_ = 0;
  macro_block_index_ = -1;
  micro_block_count_ = 0;
  data_checksum_ = 0;
  occupy_size_ = 0;
  original_size_ = 0;
  data_size_ = 0;
  data_zsize_ = 0;
  store_range_.reset();
  row_count_ = 0;
  compressor_type_ = ObCompressorType::INVALID_COMPRESSOR;
  row_store_type_ = ObRowStoreType::MAX_ROW_STORE;
}

ObAllVirtualTabletSSTableMacroInfo::ObAllVirtualTabletSSTableMacroInfo()
  : ObVirtualTableScannerIterator(),
    addr_(),
    tablet_iter_(nullptr),
    tablet_allocator_("VTTable"),
    tablet_handle_(),
    cols_desc_(),
    table_store_iter_(),
    curr_sstable_(nullptr),
    curr_sstable_meta_handle_(),
    macro_iter_(nullptr),
    other_blk_iter_(),
    iter_allocator_(),
    rowkey_allocator_(),
    curr_range_(),
    block_idx_(0),
    iter_buf_(nullptr),
    io_buf_(nullptr)
{
}

ObAllVirtualTabletSSTableMacroInfo::~ObAllVirtualTabletSSTableMacroInfo()
{
  reset();
}

void ObAllVirtualTabletSSTableMacroInfo::reset()
{
  omt::ObMultiTenantOperator::reset();
  addr_.reset();

  if (OB_NOT_NULL(iter_buf_)) {
    allocator_->free(iter_buf_);
    iter_buf_ = nullptr;
  }
  if (OB_NOT_NULL(io_buf_)) {
    allocator_->free(io_buf_);
    io_buf_ = nullptr;
  }
  memset(objs_, 0, sizeof(objs_));

  ObVirtualTableScannerIterator::reset();
}
int ObAllVirtualTabletSSTableMacroInfo::init(common::ObIAllocator *allocator, common::ObAddr &addr)
{
  int ret = OB_SUCCESS;
  if (start_to_read_) {
    ret = OB_INIT_TWICE;
    SERVER_LOG(WARN, "cannot init twice", K(ret));
  } else if (OB_ISNULL(allocator)) {
    ret = OB_INVALID_ARGUMENT;
    SERVER_LOG(WARN, "invalid argument", K(ret), KP(allocator));
  } else if (OB_ISNULL(iter_buf_ = allocator->alloc(sizeof(ObTenantTabletIterator)))) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
    SERVER_LOG(WARN, "fail to alloc tablet iter buf", K(ret));
  } else if (OB_UNLIKELY(!addr.ip_to_string(ip_buf_, sizeof(ip_buf_)))) {
    ret = OB_ERR_UNEXPECTED;
    SERVER_LOG(WARN, "fail to execute ip_to_string", K(ret));
  } else {
    allocator_ = allocator;
    addr_ = addr;
    start_to_read_ = true;
  }
  return ret;
}

int ObAllVirtualTabletSSTableMacroInfo::get_next_macro_info(MacroInfo &info)
{
  int ret = OB_SUCCESS;
  blocksstable::ObMacroBlockDesc macro_desc;
  blocksstable::ObDataMacroBlockMeta macro_meta;
  macro_desc.macro_meta_ = &macro_meta;
  while (OB_SUCC(ret)) {
    if (OB_ISNULL(macro_iter_) && !other_blk_iter_.is_valid() && OB_FAIL(get_next_sstable())) {
      if (OB_ITER_END != ret) {
        SERVER_LOG(WARN, "fail to get next sstable", K(ret));
      }
    } else if (OB_ISNULL(curr_sstable_)) {
      clean_cur_sstable();
    } else if (other_blk_iter_.is_valid()) {
      blocksstable::MacroBlockId macro_id;
      if (OB_FAIL(other_blk_iter_.get_next_macro_id(macro_id))) {
        if (OB_ITER_END != ret) {
          SERVER_LOG(WARN, "fail to get next macro id", K(ret), K(other_blk_iter_));
        } else {
          other_blk_iter_.reset();
          ret = OB_SUCCESS;
        }
      } else if (OB_FAIL(get_macro_info(macro_id, info))) {
        SERVER_LOG(WARN, "fail to get macro info", K(ret), "macro_id", macro_id);
      } else {
        break;
      }
    } else if (OB_NOT_NULL(macro_iter_) && OB_FAIL(macro_iter_->get_next_macro_block(macro_desc))) {
      if (OB_ITER_END != ret) {
        SERVER_LOG(WARN, "get data next macro block failed", K(ret));
      } else {
        macro_iter_->~ObIMacroBlockIterator();
        macro_iter_ = nullptr;
        if (OB_FAIL(curr_sstable_meta_handle_.get_sstable_meta().get_macro_info().get_other_block_iter(
            other_blk_iter_))) {
          STORAGE_LOG(WARN, "fail get other block iterator", K(ret), KPC(curr_sstable_));
        }
      }
    } else if (OB_FAIL(get_macro_info(macro_desc, info))) {
      SERVER_LOG(WARN, "fail to get macro info", K(ret), K(macro_desc));
    } else {
      break;
    }
  }
  return ret;
}

int ObAllVirtualTabletSSTableMacroInfo::get_macro_info(
    const blocksstable::MacroBlockId &macro_id,
    MacroInfo &info)
{
  int ret = OB_SUCCESS;
  ObStorageObjectHandle macro_handle;
  ObStorageObjectReadInfo macro_read_info;
  macro_read_info.macro_block_id_ = macro_id;
  macro_read_info.io_desc_.set_mode(ObIOMode::READ);
  macro_read_info.io_desc_.set_wait_event(ObWaitEventIds::DB_FILE_DATA_READ);
  macro_read_info.offset_ = 0;
  macro_read_info.size_ = OB_STORAGE_OBJECT_MGR.get_macro_block_size();
  macro_read_info.io_timeout_ms_ = GCONF._data_storage_io_timeout / 1000L;
  macro_read_info.mtl_tenant_id_ = MTL_ID();

  if (OB_ISNULL(io_buf_) && OB_ISNULL(io_buf_ =
      reinterpret_cast<char*>(allocator_->alloc(OB_STORAGE_OBJECT_MGR.get_macro_block_size())))) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
    int64_t io_size = OB_STORAGE_OBJECT_MGR.get_macro_block_size();
    STORAGE_LOG(WARN, "failed to alloc macro read info buffer", K(ret), K(io_size));
  } else {
    macro_read_info.buf_ = io_buf_;
    if (OB_UNLIKELY(!macro_id.is_valid())) {
      ret = OB_INVALID_ARGUMENT;
      SERVER_LOG(WARN, "invalid argument", K(ret), K(macro_id));
    } else if (OB_FAIL(ObObjectManager::read_object(macro_read_info, macro_handle))) {
      SERVER_LOG(WARN, "fail to read macro block", K(ret), K(macro_read_info));
    } else {
      ObMacroBlockCommonHeader common_header;
      ObSSTableMacroBlockHeader macro_header;
      const char *buf = macro_read_info.buf_;
      const int64_t size = macro_handle.get_data_size();
      int64_t pos = 0;
      if (OB_FAIL(common_header.deserialize(buf, size, pos))) {
        STORAGE_LOG(ERROR, "fail to deserialize common header", K(ret), KP(buf), K(size), K(pos));
      } else if (OB_FAIL(common_header.check_integrity())) {
        STORAGE_LOG(WARN, "invalid common header", K(ret), K(common_header));
      } else if (OB_FAIL(macro_header.deserialize(buf, size, pos))) {
        STORAGE_LOG(ERROR, "fail to deserialize macro header", K(ret), KP(buf), K(size), K(pos));
      } else if (OB_UNLIKELY(!macro_header.is_valid())) {
        ret = OB_INVALID_DATA;
        STORAGE_LOG(WARN, "invalid macro header", K(ret), K(macro_header));
      } else {
        info.data_seq_ = macro_header.fixed_header_.data_seq_;
        info.macro_logic_version_ = macro_header.fixed_header_.logical_version_;
        if (macro_id.is_id_mode_local()) {
          info.macro_block_index_ = macro_id.block_index();
        } else if (macro_id.is_id_mode_backup()) {
          info.macro_block_index_ = macro_id.third_id();
        } else if (macro_id.is_shared_data_or_meta()) {
          info.macro_block_index_ = macro_id.third_id();
        } else if (macro_id.is_private_data_or_meta()) {
          info.macro_block_index_ = macro_id.tenant_seq();
        }
        info.macro_block_id_ = macro_id;
        info.row_count_ = macro_header.fixed_header_.row_count_;
        info.original_size_ = macro_header.fixed_header_.occupy_size_;
        info.data_size_ = macro_header.fixed_header_.occupy_size_;
        info.data_zsize_ = macro_header.fixed_header_.occupy_size_;
        info.occupy_size_ = macro_header.fixed_header_.occupy_size_;
        info.micro_block_count_ = macro_header.fixed_header_.micro_block_count_;
        info.data_checksum_ = macro_header.fixed_header_.data_checksum_;
        info.compressor_type_ = macro_header.fixed_header_.compressor_type_;
        info.row_store_type_ = static_cast<ObRowStoreType>(macro_header.fixed_header_.row_store_type_);
      }
    }
  }
  return ret;
}

int ObAllVirtualTabletSSTableMacroInfo::get_macro_info(
    const blocksstable::ObMacroBlockDesc &macro_desc,
    MacroInfo &info)
{
  int ret = OB_SUCCESS;
  rowkey_allocator_.reuse();
  if (OB_UNLIKELY(!macro_desc.is_valid())) {
    ret = OB_INVALID_ARGUMENT;
    SERVER_LOG(WARN, "invalid argument", K(ret), K(macro_desc));
  } else if (curr_sstable_->is_normal_cg_sstable()) {
    const storage::ObITableReadInfo *index_read_info = nullptr;
    if (OB_FAIL(MTL(ObTenantCGReadInfoMgr *)->get_index_read_info(index_read_info))) {
      SERVER_LOG(WARN, "failed to get index read info from ObTenantCGReadInfoMgr", KR(ret));
    } else if (OB_FAIL(macro_desc.range_.to_store_range(index_read_info->get_columns_desc(),
                                                 rowkey_allocator_,
                                                 info.store_range_))) {
      SERVER_LOG(WARN, "fail to get store range", K(ret), K(macro_desc.range_));
    }
  } else if (curr_sstable_->is_mds_sstable()) {
    const storage::ObITableReadInfo *index_read_info = storage::ObMdsSchemaHelper::get_instance().get_rowkey_read_info();
    if (OB_FAIL(macro_desc.range_.to_store_range(index_read_info->get_columns_desc(),
                                                 rowkey_allocator_,
                                                 info.store_range_))) {
      SERVER_LOG(WARN, "fail to get store range", K(ret), K(macro_desc.range_));
    }
  } else if (OB_FAIL(macro_desc.range_.to_store_range(cols_desc_,
                                                      rowkey_allocator_,
                                                      info.store_range_))) {
    SERVER_LOG(WARN, "fail to get store range", K(ret), K(macro_desc.range_));
  }

  if (OB_SUCC(ret)) {
    ObDataMacroBlockMeta *macro_meta = macro_desc.macro_meta_;
    info.data_seq_ = macro_meta->get_logic_id().data_seq_.macro_data_seq_;
    info.macro_logic_version_ = macro_meta->get_logic_id().logic_version_;
    if (macro_desc.macro_block_id_.is_id_mode_local()) {
      info.macro_block_index_ = macro_desc.macro_block_id_.block_index();
    } else if (macro_desc.macro_block_id_.is_id_mode_backup()) {
      info.macro_block_index_ = macro_desc.macro_block_id_.third_id();
    } else if (macro_desc.macro_block_id_.is_shared_data_or_meta()) {
      info.macro_block_index_ = macro_desc.macro_block_id_.third_id();
    } else if (macro_desc.macro_block_id_.is_private_data_or_meta()) {
      info.macro_block_index_ = macro_desc.macro_block_id_.tenant_seq();
    }
    info.macro_block_id_ = macro_desc.macro_block_id_;
    info.row_count_ = macro_desc.row_count_;
    info.original_size_ = macro_meta->val_.original_size_;
    info.data_size_ = macro_meta->val_.data_size_;
    info.data_zsize_ = macro_meta->val_.data_zsize_;
    info.occupy_size_ = macro_meta->val_.occupy_size_;
    info.micro_block_count_ = macro_meta->val_.micro_block_count_;
    info.data_checksum_ = macro_meta->val_.data_checksum_;
    info.compressor_type_ = macro_meta->val_.compressor_type_;
    info.row_store_type_ = macro_meta->val_.row_store_type_;
    ObStoreRowkey &start_key = info.store_range_.get_start_key();
    ObStoreRowkey &end_key = info.store_range_.get_end_key();
    const int64_t extra_rowkey_cnt = ObMultiVersionRowkeyHelpper::get_extra_rowkey_col_cnt();
    if (start_key != ObStoreRowkey::MIN_STORE_ROWKEY
        && OB_FAIL(start_key.assign(start_key.get_obj_ptr(),
            start_key.get_obj_cnt() > extra_rowkey_cnt ? start_key.get_obj_cnt() - extra_rowkey_cnt : start_key.get_obj_cnt()))) {
      SERVER_LOG(WARN, "fail to set start key", K(ret), K(start_key));
    } else if (end_key != ObStoreRowkey::MAX_STORE_ROWKEY
        && OB_FAIL(end_key.assign(end_key.get_obj_ptr(),
            end_key.get_obj_cnt() > extra_rowkey_cnt ? end_key.get_obj_cnt() - extra_rowkey_cnt : end_key.get_obj_cnt()))) {
      SERVER_LOG(WARN, "fail to set end key", K(ret), K(end_key));
    }
  }
  return ret;
}

int ObAllVirtualTabletSSTableMacroInfo::set_key_ranges(const ObIArray<ObNewRange> &key_ranges)
{
  int ret = OB_SUCCESS;
  if (key_ranges.empty()) {
    ret = OB_INVALID_ARGUMENT;
    SERVER_LOG(WARN, "invalid key_ranges", K(ret), K(key_ranges));
  } else {
    for (int64_t i = 0; OB_SUCC(ret) && i < key_ranges.count(); ++i) {
      const ObNewRange &key_range = key_ranges.at(i);
      ObNewRange range = key_range;
      range.border_flag_.set_inclusive_start();
      range.border_flag_.set_inclusive_end();
      // Some scan params append a tail MIN/MAX obj to represent open interval.
      // Make it robust: only trim when the tail is MIN/MAX.
      const int64_t sk_len = key_range.start_key_.length();
      if (sk_len > 0) {
        const ObObj &sk_tail = key_range.start_key_.ptr()[sk_len - 1];
        if (sk_tail.is_max_value() || sk_tail.is_min_value()) {
          range.start_key_.set_length(sk_len - 1);
        }
        if (sk_tail.is_max_value()) {
          range.border_flag_.unset_inclusive_start();
        }
      }
      const int64_t ek_len = key_range.end_key_.length();
      if (ek_len > 0) {
        const ObObj &ek_tail = key_range.end_key_.ptr()[ek_len - 1];
        if (ek_tail.is_max_value() || ek_tail.is_min_value()) {
          range.end_key_.set_length(ek_len - 1);
        }
        if (ek_tail.is_min_value()) {
          range.border_flag_.unset_inclusive_end();
        }
      }
      if (OB_FAIL(key_ranges_.push_back(range))) {
        SERVER_LOG(WARN, "push_back failed", K(ret));
      }
    }
  }
  return ret;
}

int ObAllVirtualTabletSSTableMacroInfo::gen_row(
    const MacroInfo &macro_info,
    ObNewRow *&row)
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(curr_sstable_)) {
    ret = OB_ERR_UNEXPECTED;
    SERVER_LOG(WARN, "unexpected null curr sstable", K(ret));
  } else {
    const ObITable::TableKey &table_key = curr_sstable_->get_key();
    for (int64_t i = 0; OB_SUCC(ret) && i < output_column_ids_.count(); ++i) {
      uint64_t col_id = output_column_ids_.at(i);
      switch(col_id) {
      case TABLET_ID:
        //tablet_id
        cur_row_.cells_[i].set_int(table_key.tablet_id_.id());
        break;
      case MACRO_IDX_IN_SSTABLE:
        //macro_idx_in_sstable
        cur_row_.cells_[i].set_int(block_idx_);
        break;
      case END_LOG_SCN:
        //end_log_scn
        cur_row_.cells_[i].set_uint64(!table_key.get_end_scn().is_valid() ? 0 : table_key.get_end_scn().get_val_for_inner_table_field());
        break;
      case MACRO_LOGIC_VERSION:
        //macro_logic_version
        cur_row_.cells_[i].set_uint64(macro_info.macro_logic_version_ < 0 ? 0 : macro_info.macro_logic_version_);
        break;
      case MACRO_BLOCK_IDX:
        //macro_block_index
        cur_row_.cells_[i].set_int(macro_info.macro_block_index_);
        break;
      case DATA_SEQ:
        //data_seq_
        cur_row_.cells_[i].set_int(macro_info.data_seq_);
        break;
      case ROW_COUNT: {
        //row_count
        cur_row_.cells_[i].set_int(macro_info.row_count_);
        break;
      }
      case ORIGINAL_SIZE:
        //original_size
        cur_row_.cells_[i].set_int(macro_info.original_size_);
        break;
      case ENCODING_SIZE:
        //encoding_size
        cur_row_.cells_[i].set_int(macro_info.data_size_);
        break;
      case COMPRESSED_SIZE:
        //compressed_size
        cur_row_.cells_[i].set_int(macro_info.data_zsize_);
        break;
      case OCCUPY_SIZE:
        //occupy_size
        cur_row_.cells_[i].set_int(macro_info.occupy_size_);
        break;
      case MICRO_BLOCK_CNT:
        //micro_block_count
        cur_row_.cells_[i].set_int(macro_info.micro_block_count_);
        break;
      case DATA_CHECKSUM:
        //data_checksum
        cur_row_.cells_[i].set_int(macro_info.data_checksum_);
        break;
      case START_KEY: {
        if (macro_info.store_range_.get_start_key().to_plain_string(start_key_buf_, sizeof(start_key_buf_)) >= 0) {
          cur_row_.cells_[i].set_varchar(start_key_buf_);
          cur_row_.cells_[i].set_collation_type(ObCharset::get_default_collation(ObCharset::get_default_charset()));
        } else {
          // if error occur, set to null
          cur_row_.cells_[i].set_null();
        }
        break;
      }
      case END_KEY: {
        if (macro_info.store_range_.get_end_key().to_plain_string(end_key_buf_, sizeof(end_key_buf_)) >= 0) {
          cur_row_.cells_[i].set_varchar(end_key_buf_);
          cur_row_.cells_[i].set_collation_type(ObCharset::get_default_collation(ObCharset::get_default_charset()));
        } else {
          // if error occur, set to null
          cur_row_.cells_[i].set_null();
        }
        break;
      }
      case BLOCK_TYPE: {
        //block type
        blocksstable::ObMacroDataSeq macro_data_seq(macro_info.data_seq_);
        if (GCTX.is_shared_storage_mode()) {
          // Shared Storage
          if (macro_info.macro_block_id_.is_data()) {
            cur_row_.cells_[i].set_varchar(ObString::make_string("data_block"));
          } else if (macro_info.macro_block_id_.is_meta()) {
            cur_row_.cells_[i].set_varchar(ObString::make_string("meta_block"));
          } else {
            ret = OB_ERR_UNEXPECTED;
            SERVER_LOG(WARN, "unexpected block type, ", K(ret), K(macro_data_seq), K(macro_info));
          }
        } else {
          // Shared Nothing
          if (macro_data_seq.is_data_block()) {
            cur_row_.cells_[i].set_varchar(ObString::make_string("data_block"));
          } else if (macro_data_seq.is_index_block()) {
            cur_row_.cells_[i].set_varchar(ObString::make_string("index_block"));
          } else if (macro_data_seq.is_meta_block()) {
            cur_row_.cells_[i].set_varchar(ObString::make_string("meta_block"));
          } else {
            ret = OB_ERR_UNEXPECTED;
            SERVER_LOG(WARN, "unexpected block type, ", K(ret), K(macro_data_seq));
          }
        }
        break;
      }
      case COMPRESSOR_NAME: {
        //compressor name
        cur_row_.cells_[i].set_varchar(all_compressor_name[macro_info.compressor_type_]);
        cur_row_.cells_[i].set_collation_type(ObCharset::get_default_collation(ObCharset::get_default_charset()));
        break;
      }
      case ROW_STORE_TYPE:
        //row_store_type
        cur_row_.cells_[i].set_varchar(ObString::make_string(ObStoreFormat::get_row_store_name(static_cast<ObRowStoreType>(macro_info.row_store_type_))));
        break;
      case CG_IDX:
        //cg_idx
        cur_row_.cells_[i].set_int(table_key.get_column_group_id());
        break;
      default:
        ret = OB_ERR_UNEXPECTED;
        SERVER_LOG(WARN, "invalid column id, ", K(ret), K(col_id));
      }
    }
  }

  if (OB_SUCC(ret)) {
    row = &cur_row_;
  }
  return ret;
}

void ObAllVirtualTabletSSTableMacroInfo::clean_cur_sstable()
{
  if (OB_NOT_NULL(macro_iter_)) {
    macro_iter_->~ObIMacroBlockIterator();
    macro_iter_ = nullptr;
  }
  iter_allocator_.reuse();
  curr_range_.set_whole_range();
  curr_sstable_ = nullptr;
  curr_sstable_meta_handle_.reset();
  block_idx_ = 0;
  other_blk_iter_.reset();
}

int ObAllVirtualTabletSSTableMacroInfo::inner_get_next_row(ObNewRow *&row)
{
  int ret = OB_SUCCESS;
  if (OB_FAIL(execute(row))) {
    SERVER_LOG(WARN, "fail to execute", K(ret));
  }
  return ret;
}

void ObAllVirtualTabletSSTableMacroInfo::release_last_tenant()
{
  clean_cur_sstable();
  cols_desc_.reset();
  table_store_iter_.reset();
  tablet_handle_.reset();
  if (OB_NOT_NULL(tablet_iter_)) {
    tablet_iter_->~ObTenantTabletIterator();
    tablet_iter_ = nullptr;
  }
  iter_allocator_.reset();
  rowkey_allocator_.reset();
  tablet_allocator_.reset();
}

bool ObAllVirtualTabletSSTableMacroInfo::is_need_process(uint64_t tenant_id)
{
  if (!is_virtual_tenant_id(tenant_id) &&
      (is_sys_tenant(effective_tenant_id_) || tenant_id == effective_tenant_id_)){
    bool need_ignore = check_tenant_need_ignore(tenant_id);
    return !need_ignore;
  }
  return false;
}

int ObAllVirtualTabletSSTableMacroInfo::process_curr_tenant(common::ObNewRow *&row)
{
  int ret = OB_SUCCESS;
  MacroInfo macro_info;
  if (!start_to_read_) {
    ret = OB_NOT_INIT;
    SERVER_LOG(WARN, "ObAllVirtualTabletSSTableMacroInfo not inited, ", K(ret));
  } else if (NULL == cur_row_.cells_) {
    ret = OB_ERR_UNEXPECTED;
    SERVER_LOG(ERROR, "cur row cell is NULL", K(ret));
  } else if (OB_FAIL(get_next_macro_info(macro_info))) {
    SERVER_LOG(WARN, "fail to get next macro info", K(ret));
  } else if (OB_FAIL(gen_row(macro_info, row))) {
    SERVER_LOG(WARN, "gen_row failed", K(ret));
  } else {
    ++block_idx_;
  }
  return ret;
}

int ObAllVirtualTabletSSTableMacroInfo::get_next_tablet()
{
  int ret = OB_SUCCESS;
  tablet_handle_.reset();
  tablet_allocator_.reuse();
  if (nullptr == tablet_iter_) {
    tablet_allocator_.set_tenant_id(MTL_ID());
    iter_allocator_.set_tenant_id(MTL_ID());
    rowkey_allocator_.set_tenant_id(MTL_ID());
    ObTenantMetaMemMgr *t3m = MTL(ObTenantMetaMemMgr*);
    if (OB_ISNULL(tablet_iter_ = new (iter_buf_) ObTenantTabletIterator(*t3m, tablet_allocator_, nullptr/*no op*/))) {
      ret = OB_ERR_UNEXPECTED;
      SERVER_LOG(WARN, "fail to new tablet_iter_", K(ret));
    }
  }
  while(OB_SUCC(ret)) {
    tablet_handle_.reset();
    tablet_allocator_.reuse();
    if (OB_FAIL(tablet_iter_->get_next_tablet(tablet_handle_))) {
      if (OB_UNLIKELY(OB_ITER_END != ret)) {
        SERVER_LOG(WARN, "fail to get tablet iter", K(ret));
      }
    } else if (OB_UNLIKELY(!tablet_handle_.is_valid())) {
      ret = OB_ERR_UNEXPECTED;
      SERVER_LOG(WARN, "unexpected invalid tablet", K(ret), K(tablet_handle_));
    } else if (tablet_handle_.get_obj()->is_empty_shell()) {
    } else {
      bool need_ignore = check_tablet_need_ignore(tablet_handle_.get_obj()->get_tablet_meta());
      if (!need_ignore) {
    	  const ObIArray<ObColDesc> &cols_desc = tablet_handle_.get_obj()->get_rowkey_read_info().get_columns_desc();

    	  cols_desc_.reuse();
    	  if (OB_FAIL(cols_desc_.assign(cols_desc))) {
          SERVER_LOG(WARN, "fail to assign rowkey col desc, ", K(ret));
    	  } else if (OB_FAIL(ObMultiVersionRowkeyHelpper::add_extra_rowkey_cols(cols_desc_))) {
    	    SERVER_LOG(WARN, "fail to add extra rowkey info, ", K(ret));
    	  } else {
    	    break;
    	  }
      }
    }
  }
  return ret;
}

int ObAllVirtualTabletSSTableMacroInfo::get_next_sstable()
{
  int ret = OB_SUCCESS;
  bool need_ignore = false;
  clean_cur_sstable();
  blocksstable::ObDatumRange curr_range;
  ObITable *table = nullptr;
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
        } else if (OB_FAIL(tablet_handle_.get_obj()->get_all_sstables(table_store_iter_, true/*unpack co table*/))) {
          SERVER_LOG(WARN, "fail to get all tables", K(ret), K_(tablet_handle), K_(table_store_iter));
        } else if (0 != table_store_iter_.count()) {
          break;
        }
      }
      if (OB_FAIL(ret)) {
      } else if (OB_FAIL(table_store_iter_.get_next(table))) {
        SERVER_LOG(WARN, "fail to get table after switch tablet", K(ret));
      }
    }
  }

  if (OB_FAIL(ret)) {
  } else if (OB_ISNULL(curr_sstable_ = static_cast<ObSSTable *>(table))) {
    ret = OB_ERR_UNEXPECTED;
    SERVER_LOG(WARN, "unexpected null curr sstable", K(ret));
  } else {
    if (curr_sstable_->is_empty()
        || check_sstable_need_ignore(curr_sstable_->get_key())) {
      clean_cur_sstable();
    } else if (OB_FAIL(curr_sstable_->get_meta(curr_sstable_meta_handle_))) {
      SERVER_LOG(WARN, "fail to get curr sstable meta handle", K(ret));
    } else {
      const storage::ObITableReadInfo *index_read_info = nullptr;
      if (OB_FAIL(tablet_handle_.get_obj()->get_sstable_read_info(curr_sstable_, index_read_info))) {
        SERVER_LOG(WARN, "failed to get index read info ", KR(ret), KPC_(curr_sstable));
      } else if (OB_FAIL(curr_sstable_->scan_macro_block(
          curr_range_,
          *index_read_info,
          iter_allocator_,
          macro_iter_,
          false,
          false,
          true/*need_scan_sec_meta*/))) {
        SERVER_LOG(WARN, "Fail to scan macro block", K(ret), K(curr_range_));
      }
    }
  }
  return ret;
}

bool ObAllVirtualTabletSSTableMacroInfo::check_tenant_need_ignore(uint64_t tenant_id)
{
  // In this feature branch, rowkey of __all_virtual_tablet_sstable_macro_info is:
  //   (tablet_id, end_log_scn, macro_idx_in_sstable)
  // It doesn't contain tenant_id/svr_ip/svr_port/ls_id, so we cannot safely prune tenants by key_ranges_.
  UNUSED(tenant_id);
  return false;
}

bool ObAllVirtualTabletSSTableMacroInfo::check_tablet_need_ignore(const ObTabletMeta &tablet_meta)
{
  bool need_ignore = true;
  ObNewRange tablet_range;
  if (key_ranges_.empty()) {
    need_ignore = false;
  } else {
    ObObj start_objs[3];
    ObObj end_objs[3];
    start_objs[0].set_int(tablet_meta.tablet_id_.id());
    start_objs[1].set_min_value();
    start_objs[2].set_min_value();
    end_objs[0].set_int(tablet_meta.tablet_id_.id());
    end_objs[1].set_max_value();
    end_objs[2].set_max_value();

    tablet_range.table_id_ = OB_ALL_VIRTUAL_TABLET_SSTABLE_MACRO_INFO_TID;
    tablet_range.start_key_.assign(start_objs, 3);
    tablet_range.end_key_.assign(end_objs, 3);
    tablet_range.border_flag_.set_inclusive_start();
    tablet_range.border_flag_.set_inclusive_end();

    for (int64_t i = 0; i < key_ranges_.count() && need_ignore; ++i) {
      const ObNewRange &kr = key_ranges_.at(i);
      if (kr.table_id_ != OB_ALL_VIRTUAL_TABLET_SSTABLE_MACRO_INFO_TID) {
        need_ignore = false; // unexpected, do not skip
      } else {
        ObNewRange kr3;
        ObObj sk3[3];
        ObObj ek3[3];
        if (!normalize_key_range_to_tail3(kr, kr3, sk3, ek3)) {
          need_ignore = false; // cannot safely compare, do not skip
        } else if (kr3.compare_endkey_with_startkey(tablet_range) >= 0
            && tablet_range.compare_endkey_with_startkey(kr3) >= 0) {
          need_ignore = false;
        }
      }
    }
  }
  SERVER_LOG(DEBUG, "sstable_macro_info try to skip tablet", K(need_ignore), K(tablet_range), K(tablet_meta));
  return need_ignore;
}

bool ObAllVirtualTabletSSTableMacroInfo::check_sstable_need_ignore(const ObITable::TableKey &table_key)
{
  bool need_ignore = true;
  ObNewRange sstable_range;

  if (key_ranges_.empty()) {
    need_ignore = false;
  } else {
    const uint64_t end_log_scn = !table_key.get_end_scn().is_valid()
        ? 0
        : table_key.get_end_scn().get_val_for_inner_table_field();

    ObObj start_objs[3];
    ObObj end_objs[3];
    start_objs[0].set_int(table_key.tablet_id_.id());
    start_objs[1].set_uint64(end_log_scn);
    start_objs[2].set_min_value();
    end_objs[0].set_int(table_key.tablet_id_.id());
    end_objs[1].set_uint64(end_log_scn);
    end_objs[2].set_max_value();

    sstable_range.table_id_ = OB_ALL_VIRTUAL_TABLET_SSTABLE_MACRO_INFO_TID;
    sstable_range.start_key_.assign(start_objs, 3);
    sstable_range.end_key_.assign(end_objs, 3);
    sstable_range.border_flag_.set_inclusive_start();
    sstable_range.border_flag_.set_inclusive_end();

    for (int64_t i = 0; i < key_ranges_.count() && need_ignore; ++i) {
      const ObNewRange &kr = key_ranges_.at(i);
      if (kr.table_id_ != OB_ALL_VIRTUAL_TABLET_SSTABLE_MACRO_INFO_TID) {
        need_ignore = false; // unexpected, do not skip
      } else {
        ObNewRange kr3;
        ObObj sk3[3];
        ObObj ek3[3];
        if (!normalize_key_range_to_tail3(kr, kr3, sk3, ek3)) {
          need_ignore = false; // cannot safely compare, do not skip
        } else if (kr3.compare_endkey_with_startkey(sstable_range) >= 0
            && sstable_range.compare_endkey_with_startkey(kr3) >= 0) {
          need_ignore = false;
        }
      }
    }
  }
  SERVER_LOG(DEBUG, "sstable_macro_info try to skip sstable", K(need_ignore), K(sstable_range), K(table_key));
  return need_ignore;
}

} /* namespace observer */
} /* namespace oceanbase */
