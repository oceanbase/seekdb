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

#define USING_LOG_PREFIX STORAGE
#include "ob_standby_restore_reader.h"
#include "share/config/ob_server_config.h"
#include "storage/tx_storage/ob_ls_service.h"
#include "share/rc/ob_server_runtime.h"

namespace oceanbase
{
using namespace common;
using namespace share;
using namespace obcall;
using namespace blocksstable;

namespace storage
{
ERRSIM_POINT_DEF(EN_ONLY_COPY_OLD_VERSION_MAJOR_SSTABLE);

namespace
{
int get_sstable_data_size(ObTablet &tablet, int64_t &data_size)
{
  int ret = OB_SUCCESS;
  ObTableStoreIterator iter;
  data_size = 0;
  if (OB_FAIL(tablet.get_all_sstables(iter))) {
    LOG_WARN("failed to get tablet sstables", K(ret), K(tablet));
  }
  while (OB_SUCC(ret)) {
    ObITable *table = nullptr;
    ObSSTableMetaHandle meta_handle;
    if (OB_FAIL(iter.get_next(table))) {
      if (OB_ITER_END == ret) {
        ret = OB_SUCCESS;
      } else {
        LOG_WARN("failed to iterate tablet sstables", K(ret), K(tablet));
      }
      break;
    } else if (OB_ISNULL(table) || !table->is_sstable()) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("invalid table in sstable iterator", K(ret), KP(table));
    } else if (OB_FAIL(static_cast<ObSSTable *>(table)->get_meta(meta_handle))) {
      LOG_WARN("failed to get sstable meta", K(ret), KPC(table));
    } else {
      data_size += meta_handle.get_sstable_meta().get_basic_meta().occupy_size_;
    }
  }
  return ret;
}
} // namespace

/******************CopyMacroBlockReadInfo*********************/
ObICopyMacroBlockReader::CopyMacroBlockReadData::CopyMacroBlockReadData()
  : data_type_(ObCopyMacroBlockDataType::MAX),
    is_reuse_macro_block_(false),
    macro_data_(),
    macro_meta_(nullptr),
    macro_block_id_(),
    allocator_("CopyMacroRead")
{
}

ObICopyMacroBlockReader::CopyMacroBlockReadData::~CopyMacroBlockReadData()
{
  reset();
}

void ObICopyMacroBlockReader::CopyMacroBlockReadData::reset()
{
  data_type_ = ObCopyMacroBlockDataType::MAX;
  is_reuse_macro_block_ = false;
  macro_data_ = ObBufferReader(NULL, 0, 0);
  macro_meta_ = nullptr;
  macro_block_id_.reset();
  allocator_.reset();
}

bool ObICopyMacroBlockReader::CopyMacroBlockReadData::is_valid() const
{
  bool valid = false;

  if (ObCopyMacroBlockDataType::MACRO_META_ROW == data_type_) {
    valid = is_reuse_macro_block_ && OB_NOT_NULL(macro_meta_) && macro_meta_->is_valid();
  } else if (ObCopyMacroBlockDataType::MACRO_DATA == data_type_) {
    valid = !is_reuse_macro_block_ && macro_data_.is_valid();
  }

  return valid;
}

int ObICopyMacroBlockReader::CopyMacroBlockReadData::set_macro_meta(
  const blocksstable::ObDataMacroBlockMeta &macro_meta,
  const bool &is_reuse_macro_block)
{
  int ret = OB_SUCCESS;

  if (!macro_meta.is_valid() || !is_reuse_macro_block) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("set macro meta get invalid argument", K(ret), K(macro_meta), K(is_reuse_macro_block));
  } else if (OB_FAIL(macro_meta.deep_copy(macro_meta_, allocator_))) {
    LOG_WARN("failed to deep copy macro meta", K(ret), K(macro_meta));
  } else {
    data_type_ = ObCopyMacroBlockDataType::MACRO_META_ROW;
    is_reuse_macro_block_ = is_reuse_macro_block;
  }

  return ret;
}

int ObICopyMacroBlockReader::CopyMacroBlockReadData::set_macro_data(
  const ObBufferReader &data,
  const bool &is_reuse_macro_block)
{
  int ret = OB_SUCCESS;

  if (!data.is_valid() || is_reuse_macro_block) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("set macro data get invalid argument", K(ret), K(data), K(is_reuse_macro_block));
  } else {
    macro_data_ = data;
    data_type_ = ObCopyMacroBlockDataType::MACRO_DATA;
    is_reuse_macro_block_ = is_reuse_macro_block;
  }

  return ret;
}

void ObICopyMacroBlockReader::CopyMacroBlockReadData::set_macro_block_id(const MacroBlockId &macro_block_id)
{
  // won't check macro_block_id_ is valid
  macro_block_id_ = macro_block_id;
}


/******************ObCopyMacroBlockHandle*********************/
ObCopyMacroBlockHandle::ObCopyMacroBlockHandle()
  : is_reuse_macro_block_(false),
    read_handle_(),
    allocator_("CMacBlockHandle"),
    macro_meta_(nullptr)
{
}

void ObCopyMacroBlockHandle::reset()
{
  is_reuse_macro_block_ = false;
  read_handle_.reset();
  macro_meta_ = nullptr;
  allocator_.reset();
}

bool ObCopyMacroBlockHandle::is_valid() const
{
  return (is_reuse_macro_block_ || read_handle_.is_valid())
       && OB_NOT_NULL(macro_meta_)
       && macro_meta_->is_valid();
}

int ObCopyMacroBlockHandle::set_macro_meta(
    const blocksstable::ObDataMacroBlockMeta &macro_meta)
{
  int ret = OB_SUCCESS;
  if (!macro_meta.is_valid()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("set macro meta get invalid argument", K(ret), K(macro_meta));
  } else if (OB_FAIL(macro_meta.deep_copy(macro_meta_, allocator_))) {
    LOG_WARN("failed to deep copy macro meta", K(ret), K(macro_meta));
  }

  return ret;
}

// ==================== ObCopyMacroBlockObProducer ====================

ObCopyMacroBlockObProducer::ObCopyMacroBlockObProducer()
  : is_inited_(false),
    copy_macro_range_info_(),
    data_version_(0),
    macro_idx_(0),
    handle_idx_(0),
    prefetch_meta_time_(0),
    tablet_allocator_(),
    tablet_handle_(),
    sstable_handle_(),
    sstable_(nullptr),
    datum_range_(),
    allocator_(),
    second_meta_iterator_(),
    io_allocator_("CMBP_IOUB", OB_MALLOC_NORMAL_BLOCK_SIZE),
    meta_row_buf_("CopyMacroMetaRow")
{
  ObMemAttr attr_tablet_alloc("HaTabletHdl");
  tablet_allocator_.set_attr(attr_tablet_alloc);
  ObMemAttr attr_copy_macro_block("CopyMacroBlock");
  allocator_.set_attr(attr_copy_macro_block);
}

ObCopyMacroBlockObProducer::~ObCopyMacroBlockObProducer()
{
  for (int64_t i = 0; i < MAX_PREFETCH_MACRO_BLOCK_NUM; ++i) {
    copy_macro_block_handle_[i].reset();
  }
  second_meta_iterator_.reset();
}

int ObCopyMacroBlockObProducer::init(
    const share::ObLSID &ls_id,
    const ObITable::TableKey &table_key,
    const ObCopyMacroRangeInfo &copy_macro_range_info,
    const int64_t data_version,
    const share::SCN backfill_tx_scn)
{
  int ret = OB_SUCCESS;
  ObLSService *ls_service = nullptr;
  ObLS *ls = nullptr;
  ObTablet* tablet = nullptr;
  const bool is_reverse_scan = false;
  ObSSTableMetaHandle meta_handle;
  common::ObSafeArenaAllocator allocator(allocator_);

  if (is_inited_) {
    ret = OB_INIT_TWICE;
    LOG_WARN("cannot init twice", K(ret));
  } else if (!ls_id.is_valid() || !table_key.is_valid()
      || !copy_macro_range_info.is_valid()
      || data_version < obcall::ObCopyMacroBlockRangeArg::DISABLE_MACRO_BLOCK_REUSE_DATA_VERSION
      || !backfill_tx_scn.is_valid()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid args", K(ret), K(ls_id), K(table_key),
        K(copy_macro_range_info), K(data_version), K(backfill_tx_scn));
  } else if (OB_ISNULL(ls_service = share::server_service<ObLSService>())) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("ls service should not be null", K(ret), KP(ls_service));
  } else if (OB_FAIL(ls_service->get_ls(ls))) {
    LOG_WARN("fail to get log stream", KR(ret), K(ls_id));
  } else if (OB_UNLIKELY(nullptr == ls)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("log stream should not be NULL", KR(ret), K(ls_id), KPC(ls));
  } else if (OB_FAIL(ls->get_tablet(table_key.get_tablet_id(), tablet_handle_))) {
    LOG_WARN("failed to get tablet", K(ret), K(table_key));
  } else if (OB_UNLIKELY(nullptr == (tablet = tablet_handle_.get_obj()))) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("tablet not be NULL", KR(ret), K(ls_id), KPC(tablet));
  } else if (OB_FAIL(tablet->get_table(table_key, sstable_handle_))) {
    LOG_WARN("failed to get table", K(ret), K(table_key));
    if (OB_ENTRY_NOT_EXIST == ret) {
      ret = OB_SSTABLE_NOT_EXIST;
    }
  } else if (OB_FAIL(sstable_handle_.get_sstable(sstable_))) {
    LOG_WARN("failed to get sstable", K(ret), K(table_key));
  } else if (OB_FAIL(sstable_->get_meta(meta_handle, &allocator))) {
    LOG_WARN("failed to get sstable meta", K(ret), K(table_key));
  } else if (backfill_tx_scn != meta_handle.get_sstable_meta().get_basic_meta().filled_tx_scn_) {
    ret = OB_SSTABLE_NOT_EXIST;
    LOG_WARN("sstable has been changed", K(ret), K(table_key), K(backfill_tx_scn), KPC(sstable_));
  } else if (OB_FAIL(copy_macro_range_info_.assign(copy_macro_range_info))) {
    LOG_WARN("failed to copy macro range info", K(ret), K(table_key), K(copy_macro_range_info));
  } else {
    datum_range_.set_start_key(copy_macro_range_info_.start_macro_block_end_key_);
    datum_range_.end_key_.set_max_rowkey();
    datum_range_.set_left_closed();
    datum_range_.set_right_open();

    const storage::ObITableReadInfo *index_read_info = NULL;

    if (OB_FAIL(tablet->get_sstable_read_info(sstable_, index_read_info))) {
      LOG_WARN("failed to get index read info ", KR(ret), K(sstable_));
    } else if (OB_FAIL(second_meta_iterator_.open(datum_range_, blocksstable::DATA_BLOCK_META,
         *sstable_, *index_read_info, allocator_, is_reverse_scan))) {
      LOG_WARN("failed to open second meta iterator", K(ret), K(ls_id), K(table_key), K(copy_macro_range_info));
    } else {
      data_version_ = data_version;
      macro_idx_ = -1;
      handle_idx_ = 0;
      is_inited_ = true;
      LOG_INFO("succeed to init macro block producer",
          K(table_key), K(data_version), K(backfill_tx_scn), K(copy_macro_range_info));
    }
  }
  if (OB_SUCC(ret)) {
    for (int64_t i = 0; OB_SUCC(ret) && i < MAX_PREFETCH_MACRO_BLOCK_NUM; ++i) {
      if (OB_ISNULL(io_buf_[i] = reinterpret_cast<char*>(allocator_.alloc(OB_STORAGE_OBJECT_MGR.get_macro_block_size())))) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        int64_t io_size = OB_STORAGE_OBJECT_MGR.get_macro_block_size();
        STORAGE_LOG(WARN, "failed to alloc macro read info buffer", K(ret), K(io_size));
      }
    }
  }

  if (OB_SUCC(ret)) {
    if (OB_FAIL(prefetch_())) {
      LOG_WARN("failed to prefetch", K(ret));
    }
  }
  return ret;
}

int ObCopyMacroBlockObProducer::get_next_macro_block(
    blocksstable::ObBufferReader &data,
    ObCopyMacroBlockHeader &copy_macro_block_header)
{
  int ret = OB_SUCCESS;
  copy_macro_block_header.reset();
  meta_row_buf_.reuse();
  int64_t occupy_size = 0;

  if (!is_inited_) {
    ret = OB_NOT_INIT;
    LOG_WARN("not inited", K(ret));
  } else if (macro_idx_ < 0 || macro_idx_ > copy_macro_range_info_.macro_block_count_) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("invalid macro_idx_", K(ret), K(macro_idx_), K(copy_macro_range_info_));
  } else if (copy_macro_range_info_.macro_block_count_ == macro_idx_) {
    ret = OB_ITER_END;
    LOG_INFO("get next macro block end");
  } else if (!copy_macro_block_handle_[handle_idx_].is_valid()) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("copy macro block handle is not valid, cannot wait", K(ret), K(handle_idx_));
  } else if (!copy_macro_block_handle_[handle_idx_].is_reuse_macro_block_
      && OB_FAIL(copy_macro_block_handle_[handle_idx_].read_handle_.wait())) {
    LOG_ERROR("failed to wait read handle", K(ret), K(handle_idx_),
        KPC(copy_macro_block_handle_[handle_idx_].macro_meta_));
  } else {
    if (copy_macro_block_handle_[handle_idx_].is_reuse_macro_block_) {
      // only copy macro meta when reuse macro block
      blocksstable::ObDatumRow macro_meta_row;
      common::ObArenaAllocator meta_row_allocator; // use temporary allocator to get datum row
      int64_t pos = 0;
      uint64_t data_version = 0;

      if (OB_ISNULL(copy_macro_block_handle_[handle_idx_].macro_meta_)) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("macro meta is null", K(ret), K(handle_idx_));
      } else if (!copy_macro_block_handle_[handle_idx_].macro_meta_->is_valid()) {
        ret = OB_INVALID_ARGUMENT;
        LOG_WARN("macro meta is not valid", K(ret), KPC(copy_macro_block_handle_[handle_idx_].macro_meta_));
      } else if (FALSE_IT(data_version = DATA_CURRENT_VERSION)) {
      } else if (OB_FAIL(macro_meta_row.init(copy_macro_block_handle_[handle_idx_].macro_meta_->get_meta_val().rowkey_count_ + 1))) {
        // meta row's cell: all row keys (key) + value column
        LOG_WARN("failed to init macro meta row", K(ret), KPC(copy_macro_block_handle_[handle_idx_].macro_meta_));
      } else if (OB_FAIL(copy_macro_block_handle_[handle_idx_].macro_meta_->build_row(macro_meta_row, meta_row_allocator, data_version))) {
        LOG_WARN("failed to build macro row", K(ret), KPC(copy_macro_block_handle_[handle_idx_].macro_meta_), K(data_version));
      } else if (OB_FAIL(meta_row_buf_.write_serialize(macro_meta_row))) {
        LOG_WARN("failed to write serialize macro meta row into meta row buf", K(ret), K(macro_meta_row), K_(meta_row_buf));
      } else if (FALSE_IT(occupy_size = meta_row_buf_.length())) {
      } else {
        data.assign(meta_row_buf_.data(), occupy_size, occupy_size);
        copy_macro_block_header.occupy_size_ = occupy_size;
        copy_macro_block_header.is_reuse_macro_block_ = true;
        copy_macro_block_header.data_type_ = ObCopyMacroBlockDataType::MACRO_META_ROW;
      }
    } else {
      blocksstable::ObMacroBlockCommonHeader common_header;
      int64_t pos = 0;

      if (OB_FAIL(common_header.deserialize(
          copy_macro_block_handle_[handle_idx_].read_handle_.get_buffer(),
          copy_macro_block_handle_[handle_idx_].read_handle_.get_data_size(), pos))) {
        STORAGE_LOG(ERROR, "Deserialize common header failed, ", K(ret), "read handle",
            copy_macro_block_handle_[handle_idx_].read_handle_, K(pos), K(common_header));
      } else if (OB_FAIL(common_header.check_integrity())) {
        ret = OB_INVALID_DATA;
        STORAGE_LOG(ERROR, "Invalid common header, ", K(ret), K(common_header));
      } else {
        occupy_size = common_header.get_header_size() + common_header.get_payload_size();
        data.assign(copy_macro_block_handle_[handle_idx_].read_handle_.get_buffer(), occupy_size, occupy_size);
        copy_macro_block_header.is_reuse_macro_block_ = false;
        copy_macro_block_header.occupy_size_ = occupy_size;
        copy_macro_block_header.data_type_ = ObCopyMacroBlockDataType::MACRO_DATA;
      }
    }
  }

  if (OB_SUCC(ret)) {
    if (OB_FAIL(prefetch_())) {
      LOG_WARN("failed to do prefetch", K(ret));
    }
  }
  return ret;
}

int ObCopyMacroBlockObProducer::get_read_info_(
    const blocksstable::ObDataMacroBlockMeta &macro_meta,
    blocksstable::ObStorageObjectReadInfo &read_info)
{
  int ret = OB_SUCCESS;
  read_info = blocksstable::ObStorageObjectReadInfo();

  if (OB_ISNULL(sstable_)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("sstable is null", K(ret));
  } else if (OB_UNLIKELY(!macro_meta.is_valid())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("macro meta is invalid", K(ret), K(macro_meta));
  } else {
    read_info.macro_block_id_ = macro_meta.get_macro_id();
    read_info.offset_ = sstable_->is_small_sstable() ? macro_meta.nested_offset_ : sstable_->get_macro_offset();
    read_info.size_ = sstable_->is_small_sstable() ? macro_meta.nested_size_ : sstable_->get_macro_read_size();
    read_info.io_desc_.set_mode(ObIOMode::READ);
    read_info.io_desc_.set_wait_event(ObWaitEventIds::DB_FILE_DATA_READ);
    read_info.io_timeout_ms_ = (GCONF._data_storage_io_timeout / 1000L);
    read_info.buf_ = io_buf_[handle_idx_];
    read_info.io_desc_.set_sys_module_id(ObIOModule::SSTABLE_WHOLE_SCANNER_IO);
    if (OB_UNLIKELY(!read_info.is_valid())) {
      ret = OB_INVALID_ARGUMENT;
      LOG_WARN("read info is invalid", K(ret), K(read_info), K(macro_meta), KPC(sstable_));
    }
  }
  return ret;
}

int ObCopyMacroBlockObProducer::prefetch_()
{
  int ret = OB_SUCCESS;
  blocksstable::ObStorageObjectReadInfo read_info;
  prefetch_meta_time_ = ObTimeUtility::current_time();
  ++macro_idx_;
  handle_idx_ = (handle_idx_ + 1) % MAX_PREFETCH_MACRO_BLOCK_NUM;
  copy_macro_block_handle_[handle_idx_].reset();
  ObDataMacroBlockMeta macro_meta;

  if (!is_inited_) {
    ret = OB_NOT_INIT;
    LOG_WARN("not inited", K(ret));
  } else if (macro_idx_ < 0 || macro_idx_ > copy_macro_range_info_.macro_block_count_) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("invalid macro_idx_", K(ret), K(macro_idx_), K(copy_macro_range_info_));
  } else if (macro_idx_ == copy_macro_range_info_.macro_block_count_) {
    // no need to
    LOG_INFO("has finish, no need do prefetch", K(macro_idx_), K(copy_macro_range_info_));
  } else {
    int64_t copy_snapshot_version = 0;

    if (OB_FAIL(second_meta_iterator_.get_next(macro_meta))) {
      LOG_WARN("failed to get next macro meta", K(ret), K(macro_idx_), K(copy_macro_range_info_));
    } else if (sstable_->is_small_sstable()
               && (ObIndexBlockRowHeader::DEFAULT_IDX_ROW_MACRO_ID == macro_meta.get_macro_id()
                   || !macro_meta.get_macro_id().is_valid())) {
      ObSSTableMetaHandle meta_handle;
      ObMacroIdIterator id_iterator;
      MacroBlockId macro_id;
      if (OB_FAIL(sstable_->get_meta(meta_handle))) {
        LOG_WARN("failed to get sstable meta for small sstable", K(ret), KPC(sstable_));
      } else if (OB_FAIL(meta_handle.get_sstable_meta().get_macro_info().get_data_block_iter(id_iterator))) {
        LOG_WARN("failed to get small sstable data block iterator", K(ret), KPC(sstable_));
      } else if (OB_FAIL(id_iterator.get_next_macro_id(macro_id))) {
        LOG_WARN("failed to get small sstable data macro id", K(ret), KPC(sstable_));
      } else if (OB_UNLIKELY(!macro_id.is_valid())) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("small sstable data macro id is invalid", K(ret), K(macro_id), KPC(sstable_));
      } else {
        macro_meta.val_.macro_id_ = macro_id;
        LOG_INFO("filled small sstable macro id for copy",
            K(macro_idx_), K(copy_macro_range_info_), K(macro_id), K(macro_meta));
      }
    }

    if (OB_FAIL(ret)) {
    } else if (OB_FAIL(copy_macro_block_handle_[handle_idx_].set_macro_meta(macro_meta))) {
      LOG_WARN("failed to set macro meta", K(ret), K(macro_meta));
    } else {
      copy_macro_block_handle_[handle_idx_].is_reuse_macro_block_ = false;

      if (OB_FAIL(get_read_info_(macro_meta, read_info))) {
        LOG_WARN("failed to build macro block read info", K(ret), K(macro_meta));
      } else if (OB_FAIL(ObObjectManager::async_read_object(
          read_info, copy_macro_block_handle_[handle_idx_].read_handle_))) {
        STORAGE_LOG(ERROR, "Fail to async read block, ", K(ret), K(read_info), K(macro_meta));
      }
    }

    if (OB_SUCC(ret)) {
      LOG_INFO("do prefetch", K(macro_idx_), "macro block count",copy_macro_range_info_.macro_block_count_ ,
          "logical id", macro_meta.get_logic_id(), "physical id", macro_meta.get_macro_id(), K(data_version_), "src macro version", copy_snapshot_version);
    }
  }
  return ret;
}

ObCopyTabletInfoObProducer::ObCopyTabletInfoObProducer()
  : is_inited_(false),
    tablet_id_array_(),
    tablet_index_(0),
    ls_(nullptr)
{
}

ObCopyTabletInfoObProducer::~ObCopyTabletInfoObProducer()
{
}

int ObCopyTabletInfoObProducer::init(
    const share::ObLSID &ls_id,
    const common::ObIArray<common::ObTabletID> &tablet_id_array)
{
  int ret = OB_SUCCESS;
  ObLSService *ls_service = nullptr;

  if (is_inited_) {
    ret = OB_INIT_TWICE;
    LOG_WARN("copy table info ob producer init twice", K(ret));
  } else if (!ls_id.is_valid() || tablet_id_array.empty()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("copy tablet info ob producer init get invalid argument", K(ret), K(ls_id), K(tablet_id_array));
  } else if (OB_ISNULL(ls_service = share::server_service<ObLSService>())) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("ls service should not be null", K(ret), KP(ls_service));
  } else if (OB_FAIL(ls_service->get_ls(ls_))) {
    LOG_WARN("fail to get log stream", KR(ret), K(ls_id));
  } else if (OB_UNLIKELY(nullptr == ls_)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("log stream should not be NULL", KR(ret), K(ls_id));
  } else if (OB_FAIL(tablet_id_array_.assign(tablet_id_array))) {
    LOG_WARN("failed to assign tablet id array", K(ret), K(ls_id), K(tablet_id_array));
  } else {
    is_inited_ = true;
  }
  return ret;
}

int ObCopyTabletInfoObProducer::get_next_tablet_info(obcall::ObCopyTabletInfo &tablet_info)
{
  int ret = OB_SUCCESS;
  tablet_info.reset();
  ObLS *ls = nullptr;
  ObTabletHandle tablet_handle;
  ObTablet *tablet = nullptr;

  if (!is_inited_) {
    ret = OB_NOT_INIT;
    LOG_WARN("copy tablet info ob producer do not init", K(ret));
  } else if (tablet_index_ == tablet_id_array_.count()) {
    ret = OB_ITER_END;
  } else {
    const ObTabletID &tablet_id = tablet_id_array_.at(tablet_index_);
    tablet_info.tablet_id_ = tablet_id;
    tablet_info.version_ = DATA_CURRENT_VERSION;
    if (OB_ISNULL(ls = ls_)) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("log stream should not be NULL", K(ret), KP(ls));
    } else if (OB_FAIL(ls->get_tablet(tablet_id, tablet_handle))
               && OB_TABLET_NOT_EXIST != ret) {
      LOG_WARN("failed to get tablet", K(ret), K(tablet_id), K(tablet_handle));
    } else if (OB_TABLET_NOT_EXIST == ret) {
      ret = OB_SUCCESS;
      tablet_info.status_ = ObCopyTabletStatus::TABLET_NOT_EXIST;
      if (OB_FAIL(tablet_info.param_.build_deleted_tablet_info(ObLSID(ObLSID::SYS_LS_ID), tablet_id))) {
        LOG_WARN("failed to build deleted tablet info", K(ret), K(tablet_id));
      } else {
        LOG_INFO("tablet not exist, build deleted tablet info", K(tablet_id));
      }
    } else if (OB_ISNULL(tablet = tablet_handle.get_obj())) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("tablet should not be NULL", K(ret), KP(tablet), K(tablet_id));
    } else if (OB_FAIL(tablet_info.param_.build_from_tablet(*tablet))) {
      LOG_WARN("failed to build migration tablet param", K(ret), K(tablet_id));
    } else if (OB_FAIL(get_sstable_data_size(*tablet, tablet_info.data_size_))) {
      LOG_WARN("failed to get sstable size", K(ret), K(tablet_id));
    } else {
      tablet_info.status_ = ObCopyTabletStatus::TABLET_EXIST;
      LOG_INFO("succeed get copy tablet info", K(tablet_info), K(tablet_index_));
    }
    tablet_index_++;
  }
  return ret;
}

ObCopyTabletsSSTableInfoObProducer::ObCopyTabletsSSTableInfoObProducer()
  : is_inited_(false),
    ls_(nullptr),
    tablet_sstable_info_array_(),
    tablet_index_(0)
{
}

ObCopyTabletsSSTableInfoObProducer::~ObCopyTabletsSSTableInfoObProducer()
{
}

int ObCopyTabletsSSTableInfoObProducer::init(
    const share::ObLSID &ls_id,
    const common::ObIArray<obcall::ObCopyTabletSSTableInfoArg> &tablet_sstable_info_array)
{
  int ret = OB_SUCCESS;
  ObLSService *ls_service = nullptr;

  if (is_inited_) {
    ret = OB_INIT_TWICE;
    LOG_WARN("copy tablets sstable info ob producer init twice", K(ret));
  } else if (!ls_id.is_valid() || tablet_sstable_info_array.count() < 0) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("copy sstable info ob producer init get invalid argument", K(ret), K(ls_id));
  } else if (OB_ISNULL(ls_service = share::server_service<ObLSService>())) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("ls service should not be null", K(ret), KP(ls_service));
  } else if (OB_FAIL(ls_service->get_ls(ls_))) {
    LOG_WARN("fail to get log stream", KR(ret), K(ls_id));
  } else if (OB_UNLIKELY(nullptr == ls_)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("log stream should not be NULL", KR(ret), K(ls_id));
  } else if (OB_FAIL(tablet_sstable_info_array_.assign(tablet_sstable_info_array))) {
    LOG_WARN("failed to assign tablet sstable info", K(ret), K(tablet_sstable_info_array));
  } else {
    is_inited_ = true;
  }
  return ret;
}

int ObCopyTabletsSSTableInfoObProducer::get_next_tablet_sstable_info(
    obcall::ObCopyTabletSSTableInfoArg &arg)
{
  int ret = OB_SUCCESS;
  arg.reset();

  if (!is_inited_) {
    ret = OB_NOT_INIT;
    LOG_WARN("copy tablets sstable info ob producer do not init", K(ret));
  } else if (tablet_index_ == tablet_sstable_info_array_.count()) {
    ret = OB_ITER_END;
  } else {
    arg = tablet_sstable_info_array_.at(tablet_index_);
    tablet_index_++;
  }
  return ret;
}



ObCopySSTableInfoObProducer::ObCopySSTableInfoObProducer()
  : is_inited_(false),
    ls_id_(),
    tablet_sstable_info_(),
    tablet_handle_(),
    iter_(),
    status_(ObCopyTabletStatus::MAX_STATUS)
{
}
#ifdef ERRSIM
void errsim_copy_new_sstable_array(const ObTabletID &tablet_id, ObTableStoreIterator &iter)
{
  int ret = EN_ONLY_COPY_OLD_VERSION_MAJOR_SSTABLE ? : OB_SUCCESS;
  if (OB_FAIL(ret) && tablet_id.id() > ObTabletID::MIN_USER_TABLET_ID) {
    ObTableStoreIterator tmp_iter;
    ObITable *table = nullptr;
    ObITable *old_major = nullptr;
    ret = OB_SUCCESS;
    while (OB_SUCC(ret)) { // loop all sstable to skip new version major
      bool push_flag = true;
      if (OB_FAIL(iter.get_next(table))) {
        if (OB_ITER_END != ret) {
          LOG_WARN("failed to get next table", K(ret), K(iter));
        }
      } else if (table->is_major_sstable()) {
        if (NULL == old_major) {
          old_major = table;
        } else {
          push_flag = false;
          FLOG_INFO("ERRSIM EN_ONLY_COPY_OLD_VERSION_MAJOR_SSTABLE, skip copy major sstable", KR(ret), KPC(table));
        }
      }
      if (OB_FAIL(ret) || !push_flag) {
      } else if (OB_FAIL(tmp_iter.add_table(table))) {
        LOG_WARN("failed to add table", KR(ret));
      }
    } // while
    ret = (OB_ITER_END == ret ? OB_SUCCESS : ret);
    if (OB_SUCC(ret)) {
      iter.reset();
      if (OB_FAIL(iter.assign(tmp_iter))) {
        LOG_WARN("failed to assgin tablet store iter", KR(ret), K(tmp_iter));
      } else {
        FLOG_INFO("get copy sstable after ERRSIM EN_ONLY_COPY_OLD_VERSION_MAJOR_SSTABLE", KR(ret), K(iter));
      }
    }
  }
}
#endif

int ObCopySSTableInfoObProducer::init(
    const obcall::ObCopyTabletSSTableInfoArg &tablet_sstable_info,
    ObLS *ls)
{
  int ret = OB_SUCCESS;
  ObTablet *tablet = nullptr;

  if (is_inited_) {
    ret = OB_INIT_TWICE;
    LOG_WARN("copy sstable info ob producer init twice", K(ret));
  } else if (!tablet_sstable_info.is_valid() || OB_ISNULL(ls)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("copy sstable info ob producer init get invalid argument",
        K(ret), K(tablet_sstable_info), KP(ls));
  } else if (OB_FAIL(ls->get_tablet(tablet_sstable_info.tablet_id_, tablet_handle_))) {
    if (OB_TABLET_NOT_EXIST == ret) {
      status_ = ObCopyTabletStatus::TABLET_NOT_EXIST;
      ret = OB_SUCCESS;
    } else {
      LOG_WARN("failed to get tablet handle", K(ret), K(tablet_sstable_info));
    }
  } else if (OB_ISNULL(tablet = tablet_handle_.get_obj())) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("tablet should not be NULL", K(ret), K(tablet_sstable_info));
  } else if (!tablet_sstable_info.ddl_sstable_scn_range_.is_empty()) {
    if (tablet->get_tablet_meta().get_ddl_sstable_start_scn() < tablet_sstable_info.ddl_sstable_scn_range_.start_scn_) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("ddl start scn fall back", K(ret), K(tablet->get_tablet_meta()), K(tablet_sstable_info));
    } else if (tablet->get_tablet_meta().get_ddl_sstable_start_scn() == tablet_sstable_info.ddl_sstable_scn_range_.start_scn_) {
      if (tablet->get_tablet_meta().ddl_checkpoint_scn_ < tablet_sstable_info.ddl_sstable_scn_range_.end_scn_) {
        ret = OB_DDL_SSTABLE_RANGE_CROSS;
        LOG_WARN("ddl sstable not exist", K(ret), K(tablet_sstable_info), KPC(tablet));
      }
    } else {
      LOG_INFO("ddl start scn advanced, the expired ddl sstable has been cleaned", "tablet_id", tablet_sstable_info.tablet_id_,
          K(tablet->get_tablet_meta().ddl_start_scn_), K(tablet_sstable_info.ddl_sstable_scn_range_));
    }
  }
  if (OB_SUCC(ret) && nullptr != tablet) {
    if (OB_FAIL(tablet->get_all_sstables(iter_))) {
      LOG_WARN("failed to get read tables", K(ret));
    } else {
      status_ = ObCopyTabletStatus::TABLET_EXIST;
    }
  }

  if (OB_FAIL(ret)) {
  } else {
    ls_id_ = ObLSID(ObLSID::SYS_LS_ID);
    tablet_sstable_info_ = tablet_sstable_info;
    is_inited_ = true;
  }
  return ret;
}

int ObCopySSTableInfoObProducer::get_next_sstable_info(
    obcall::ObCopyTabletSSTableInfo &sstable_info)
{
  int ret = OB_SUCCESS;
  sstable_info.reset();

  if (!is_inited_) {
    ret = OB_NOT_INIT;
    LOG_WARN("copy sstable info ob producer do not init", K(ret));
  } else {
    while (OB_SUCC(ret)) {
      ObITable *table = nullptr;
      ObSSTable *sstable = nullptr;
      bool need_copy_sstable = false;

      if (OB_FAIL(iter_.get_next(table))) {
        if (OB_ITER_END != ret) {
          LOG_WARN("failed to get next table", K(ret), K(tablet_sstable_info_));
        }
      } else if (OB_ISNULL(table) || table->is_memtable()) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("table is null or table type is unexpected", K(ret), KPC(table));
      } else if (FALSE_IT(sstable = static_cast<ObSSTable *> (table))) {
      } else if (OB_FAIL(check_need_copy_sstable_(sstable, need_copy_sstable))) {
        LOG_WARN("failed to check need copy sstable", K(ret), K(tablet_sstable_info_), KPC(sstable));
      } else if (!need_copy_sstable) {
       //do nothing
        LOG_INFO("no need copy sstable", KPC(sstable), K(tablet_sstable_info_));
      } else if (OB_FAIL(sstable_info.param_.build_from_sstable(*sstable))) {
        LOG_WARN("failed to build migration sstable param", K(ret), K(*table));
      } else {
        sstable_info.tablet_id_ = tablet_sstable_info_.tablet_id_;
        sstable_info.table_key_ = table->get_key();
        LOG_INFO("succeed get sstable info", K(sstable_info), K(tablet_sstable_info_));
        break;
      }
    }
  }
  return ret;
}

int ObCopySSTableInfoObProducer::get_copy_tablet_sstable_header(
    obcall::ObCopyTabletSSTableHeader &copy_header)
{
  int ret = OB_SUCCESS;
  copy_header.reset();

  if (!is_inited_) {
    ret = OB_NOT_INIT;
    LOG_WARN("copy sstable info ob producer do not init", K(ret));
  } else {
    copy_header.version_ = DATA_CURRENT_VERSION;
    copy_header.tablet_id_ = tablet_sstable_info_.tablet_id_;
    copy_header.status_ = status_;
    if (ObCopyTabletStatus::TABLET_EXIST == status_) {
      if (OB_FAIL(get_tablet_meta_(copy_header.tablet_meta_))) {
        LOG_WARN("failed to get tablet meta", K(ret), K(tablet_sstable_info_));
      } else if (OB_FAIL(get_copy_sstable_count_(copy_header.sstable_count_))) {
        LOG_WARN("failed to get copy sstable count", K(ret), K(tablet_sstable_info_));
      }
    } else if (ObCopyTabletStatus::TABLET_NOT_EXIST == status_) {
      if (OB_FAIL(fake_deleted_tablet_meta_(copy_header.tablet_meta_))) {
        LOG_WARN("failed to fake deleted tablet meta", K(ret), K(copy_header));
      } else {
        copy_header.sstable_count_ = 0;
      }
    } else {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("copy tablet status is unexpected", K(ret), K(status_), K(tablet_sstable_info_));
    }
  }
  return ret;
}

int ObCopySSTableInfoObProducer::check_need_copy_sstable_(
    blocksstable::ObSSTable *sstable,
    bool &need_copy_sstable)
{
  int ret = OB_SUCCESS;
  need_copy_sstable = true;

  if (!is_inited_) {
    ret = OB_NOT_INIT;
    LOG_WARN("copy sstable info ob producer do not init", K(ret));
  } else if (OB_ISNULL(sstable) || !sstable->is_sstable()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("check need copy sstable get invalid argument", K(ret), KPC(sstable), K(tablet_sstable_info_));
  } else {
    if (sstable->is_major_sstable()) {
      need_copy_sstable = sstable->get_key().get_snapshot_version()
          > tablet_sstable_info_.max_major_sstable_snapshot_;
    } else if (sstable->is_minor_sstable()) {
      need_copy_sstable = true;
    } else if (sstable->is_ddl_dump_sstable()) {
      const SCN ddl_sstable_start_scn = tablet_sstable_info_.ddl_sstable_scn_range_.start_scn_;
      const SCN ddl_sstable_end_scn = tablet_sstable_info_.ddl_sstable_scn_range_.end_scn_;
      if (tablet_sstable_info_.ddl_sstable_scn_range_.is_empty()) {
        need_copy_sstable = false;
      } else if (sstable->get_key().scn_range_.start_scn_ >= ddl_sstable_end_scn) {
        need_copy_sstable = false;
      } else if (sstable->get_key().scn_range_.start_scn_ >= ddl_sstable_start_scn
          && sstable->get_key().scn_range_.end_scn_ <= ddl_sstable_end_scn) {
        need_copy_sstable = true;
      } else {
        ret = OB_DDL_SSTABLE_RANGE_CROSS;
        LOG_WARN("ddl sstable version range across", K(ret), K(tablet_sstable_info_), KPC(sstable));
      }
    } else if (sstable->is_mds_sstable()) {
      need_copy_sstable = true;
    } else {
      need_copy_sstable = false;
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("sstable type is unexpected, cannot check need copy sstable", K(ret),
          KPC(sstable), K(tablet_sstable_info_));
    }
  }
  return ret;
}

int ObCopySSTableInfoObProducer::get_copy_sstable_count_(int64_t &sstable_count)
{
  int ret = OB_SUCCESS;
  sstable_count = 0;

  if (!is_inited_) {
    ret = OB_NOT_INIT;
    LOG_WARN("copy sstable info ob producer do not init", K(ret));
  } else if (0 == iter_.count()) {
    sstable_count = 0;
  } else {
    while (OB_SUCC(ret)) {
      ObITable *table = nullptr;
      ObSSTable *sstable = nullptr;
      bool need_copy_sstable = false;

      if (OB_FAIL(iter_.get_next(table))) {
        if (OB_ITER_END != ret) {
          LOG_WARN("failed to get next table", K(ret), K(tablet_sstable_info_));
        } else {
          ret = OB_SUCCESS;
          break;
        }
      } else if (OB_ISNULL(table) || table->is_memtable()) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("table is null or table type is unexpected", K(ret), KPC(table));
      } else if (FALSE_IT(sstable = static_cast<ObSSTable *> (table))) {
      } else if (OB_FAIL(check_need_copy_sstable_(sstable, need_copy_sstable))) {
        LOG_WARN("failed to check need copy sstable", K(ret), K(tablet_sstable_info_), KPC(sstable));
      } else if (!need_copy_sstable) {
       //do nothing
        LOG_INFO("no need copy sstable", KPC(sstable), K(tablet_sstable_info_));
      } else {
        sstable_count++;
      }
    }
    iter_.resume();
  }
  return ret;
}


int ObCopySSTableInfoObProducer::get_tablet_meta_(ObMigrationTabletParam &tablet_meta)
{
  int ret = OB_SUCCESS;
  tablet_meta.reset();
  ObTablet *tablet = nullptr;

  if (!is_inited_) {
    ret = OB_NOT_INIT;
    LOG_WARN("copy sstable info ob producer do not init", K(ret));
  } else if (OB_ISNULL(tablet = tablet_handle_.get_obj())) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("tablet should not be NULL", K(ret), KP(tablet));
  } else if (OB_FAIL(tablet_meta.build_from_tablet(*tablet))) {
    LOG_WARN("failed to build migration tablet param", K(ret), KPC(tablet));
  }
  return ret;
}

int ObCopySSTableInfoObProducer::fake_deleted_tablet_meta_(
    ObMigrationTabletParam &tablet_meta)
{
  int ret = OB_SUCCESS;
  tablet_meta.reset();

  if (!is_inited_) {
    ret = OB_NOT_INIT;
    LOG_WARN("copy sstable info ob producer do not init", K(ret));
  } else if (OB_FAIL(tablet_meta.build_deleted_tablet_info(ls_id_, tablet_sstable_info_.tablet_id_))) {
    LOG_WARN("failed to build deleted tablet info", K(ret), K(ls_id_), K(tablet_sstable_info_));
  }
  return ret;
}

// ==================== ObCopySSTableMacroObProducer ====================

ObCopySSTableMacroObProducer::ObCopySSTableMacroObProducer()
  : is_inited_(false),
    copy_table_key_array_(),
    sstable_index_(0),
    is_sstable_iter_init_(false),
    ls_(nullptr),
    tablet_handle_(),
    macro_range_max_marco_count_(0)
{
}

int ObCopySSTableMacroObProducer::init(
    const share::ObLSID & ls_id,
    const common::ObTabletID &tablet_id,
    const common::ObIArray<ObITable::TableKey> &copy_table_key_array,
    const int64_t macro_range_max_marco_count)
{
  int ret = OB_SUCCESS;
  ObLSService *ls_service = nullptr;
  ObLS *ls = nullptr;

  if (is_inited_) {
    ret = OB_INIT_TWICE;
    LOG_WARN("copy sstable macro ob producer init twice", K(ret));
  } else if (!ls_id.is_valid() || !tablet_id.is_valid()
      || copy_table_key_array.empty() || macro_range_max_marco_count <= 0) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("init copy sstable macro ob producer get invalid argument", K(ret),
        K(ls_id), K(tablet_id), K(copy_table_key_array), K(macro_range_max_marco_count));
  } else if (OB_ISNULL(ls_service = share::server_service<ObLSService>())) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("ls service should not be null", K(ret), KP(ls_service));
  } else if (OB_FAIL(ls_service->get_ls(ls_))) {
    LOG_WARN("fail to get log stream", KR(ret), K(ls_id));
  } else if (OB_UNLIKELY(nullptr == (ls = ls_))) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("log stream should not be NULL", KR(ret), K(ls_id));
  } else if (OB_FAIL(ls->get_tablet(tablet_id, tablet_handle_))) {
    LOG_WARN("failed to get tablet", K(ret), K(tablet_id));
  } else if (OB_FAIL(copy_table_key_array_.assign(copy_table_key_array))) {
    LOG_WARN("failed to assign sstable array", K(ret), K(ls_id), K(tablet_id), K(copy_table_key_array));
  } else {
    macro_range_max_marco_count_ = macro_range_max_marco_count;
    sstable_index_ = 0;
    is_sstable_iter_init_ = false;
    is_inited_ = true;
  }
  return ret;
}

int ObCopySSTableMacroObProducer::get_next_sstable_macro_range_info(
    obcall::ObCopySSTableMacroRangeInfoHeader &macro_range_info_header)
{
  int ret = OB_SUCCESS;
  macro_range_info_header.reset();

  if (!is_inited_) {
    ret = OB_NOT_INIT;
    LOG_WARN("copy sstable macro ob producer do not init", K(ret));
  } else if (sstable_index_ == copy_table_key_array_.count()) {
    ret = OB_ITER_END;
  } else if (OB_FAIL(get_next_sstable_macro_range_info_(macro_range_info_header))) {
    LOG_WARN("failed to get next sstable macro range info", K(ret), K(copy_table_key_array_), K(sstable_index_));
  } else {
    sstable_index_++;
  }
  return ret;
}

int ObCopySSTableMacroObProducer::get_next_sstable_macro_range_info_(
    obcall::ObCopySSTableMacroRangeInfoHeader &macro_range_info_header)
{
  int ret = OB_SUCCESS;
  ObTablet *tablet = nullptr;
  ObTableHandleV2 table_handle;
  ObSSTable *sstable = nullptr;
  ObSSTableMetaHandle meta_handle;
  int64_t macro_block_count = 0;

  if (!is_inited_) {
    ret = OB_NOT_INIT;
    LOG_WARN("copy sstable macro ob producer do not init", K(ret));
  } else {
    const ObITable::TableKey &copy_table_key = copy_table_key_array_.at(sstable_index_);
    const int64_t max_range_max_macro_count = macro_range_max_marco_count_;
    if (copy_table_key.is_memtable()) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("table type is unexpected", K(ret), K(copy_table_key));
    } else if (OB_ISNULL(tablet = tablet_handle_.get_obj())) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("tablet should not be NULL", K(ret), K(copy_table_key));
    } else if (OB_FAIL(tablet->get_table(copy_table_key, table_handle))) {
      LOG_WARN("failed to get table handle", K(ret), K(copy_table_key));
      if (OB_ENTRY_NOT_EXIST == ret) {
        ret = OB_SSTABLE_NOT_EXIST;
      }
    } else if (OB_FAIL(table_handle.get_sstable(sstable))) {
      LOG_WARN("failed to get sstable", K(ret), K(copy_table_key));
    } else if (OB_ISNULL(sstable))  {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("sstable should not be NULL", K(ret), K(copy_table_key), KP(sstable));
    } else {
      macro_block_count = sstable->get_data_macro_block_count();
      macro_range_info_header.copy_table_key_ = copy_table_key;
      if (0 == macro_block_count) {
        macro_range_info_header.macro_range_count_ = 0;
      } else {
        macro_range_info_header.macro_range_count_ =
            (macro_block_count + max_range_max_macro_count - 1) / max_range_max_macro_count;
      }
    }
  }
  return ret;
}

// ==================== ObCopySSTableMacroRangeObProducer ====================

ObCopySSTableMacroRangeObProducer::ObCopySSTableMacroRangeObProducer()
  : is_inited_(false),
    table_key_(),
    macro_range_count_(0),
    macro_range_index_(0),
    macro_range_max_marco_count_(0),
    tablet_handle_(),
    table_handle_(),
    datum_range_(),
    allocator_(),
    second_meta_iterator_()
{
  ObMemAttr attr("CopySSTMacro");
  allocator_.set_attr(attr);
}

int ObCopySSTableMacroRangeObProducer::init(
    const share::ObLSID &ls_id,
    const common::ObTabletID &tablet_id,
    const obcall::ObCopySSTableMacroRangeInfoHeader &header,
    const int64_t macro_range_max_marco_count)
{
  int ret = OB_SUCCESS;
  ObLSService *ls_service = nullptr;
  ObLS *ls = nullptr;
  ObTablet *tablet = nullptr;
  ObSSTable *sstable = nullptr;
  const bool is_reverse_scan = false;
  const storage::ObITableReadInfo *index_read_info = nullptr;

  if (is_inited_) {
    ret = OB_INIT_TWICE;
    LOG_WARN("copy sstable macro range ob producer init twice", K(ret));
  } else if (!ls_id.is_valid() || !tablet_id.is_valid()
      || !header.is_valid() || macro_range_max_marco_count <= 0) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("init copy sstable macro range get invalid argument",
        K(ret), K(ls_id), K(tablet_id), K(header), K(macro_range_max_marco_count));
  } else if (OB_ISNULL(ls_service = share::server_service<ObLSService>())) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("ls service should not be null", K(ret), KP(ls_service));
  } else if (OB_FAIL(ls_service->get_ls(ls))) {
    LOG_WARN("fail to get log stream", KR(ret), K(ls_id));
  } else if (OB_UNLIKELY(nullptr == ls)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("log stream should not be NULL", KR(ret), K(ls_id));
  } else if (OB_FAIL(ls->get_tablet(tablet_id, tablet_handle_))) {
    LOG_WARN("failed to get tablet", K(ret), K(tablet_id));
  } else if (OB_ISNULL(tablet = tablet_handle_.get_obj())) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("tablet should not be NULL", K(ret), KP(tablet), K(ls_id), K(tablet_id), K(header));
  } else if (OB_FAIL(tablet->get_table(header.copy_table_key_, table_handle_))) {
    LOG_WARN("failed to get table", K(ret), K(tablet_id), K(header));
    if (OB_ENTRY_NOT_EXIST == ret) {
      ret = OB_SSTABLE_NOT_EXIST;
    }
  } else if (OB_FAIL(table_handle_.get_sstable(sstable))) {
    LOG_WARN("failed to get sstable", K(ret), K(header), K(tablet_id), K(ls_id));
  } else if (FALSE_IT(datum_range_.set_whole_range())) {
  } else if (OB_FAIL(tablet->get_sstable_read_info(sstable, index_read_info))) {
    LOG_WARN("failed to get index read info ", KR(ret), K(sstable));
  } else if (OB_FAIL(second_meta_iterator_.open(datum_range_, blocksstable::DATA_BLOCK_META,
      *sstable, *index_read_info, allocator_, is_reverse_scan))) {
    LOG_WARN("failed to open second meta iterator", K(ret), K(header), K(tablet_id));
  } else {
    table_key_ = header.copy_table_key_;
    macro_range_count_ = header.macro_range_count_;
    macro_range_index_ = 0;
    macro_range_max_marco_count_ = macro_range_max_marco_count;
    is_inited_ = true;
  }
  return ret;
}

int ObCopySSTableMacroRangeObProducer::get_next_macro_range_info(
    ObCopyMacroRangeInfo &macro_range_info)
{
  int ret = OB_SUCCESS;
  macro_range_info.reuse();
  const ObSSTable *sstable = nullptr;
  int64_t macro_block_count = 0;
  ObDataMacroBlockMeta macro_meta;

  if (!is_inited_) {
    ret = OB_NOT_INIT;
    LOG_WARN("copy sstable macro range ob producer do not init", K(ret));
  } else if (OB_FAIL(table_handle_.get_sstable(sstable))) {
    LOG_WARN("failed to get sstable", K(ret), K(table_key_));
  } else if (macro_range_index_ == macro_range_count_) {
    if (OB_ITER_END != second_meta_iterator_.get_next(macro_meta)) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("second meta iter has not reach end but macro range index reach macro range count",
          K(ret), K(table_key_), K(macro_range_index_), K(macro_range_count_));
    } else {
      ret = OB_ITER_END;
    }
  } else {
    ObLogicMacroBlockId end_macro_block_id;
    while (OB_SUCC(ret) && macro_block_count < macro_range_max_marco_count_) {
      if (OB_FAIL(second_meta_iterator_.get_next(macro_meta))) {
        if (OB_ITER_END == ret) {
          ret = OB_SUCCESS;
          break;
        } else {
          LOG_WARN("failed to get next second meta", K(ret), K(macro_range_index_), K(macro_range_count_), K(table_key_));
        }
      } else if (0 == macro_block_count) {
        macro_range_info.start_macro_block_id_ = macro_meta.get_logic_id();
        ObDatumRowkey end_key;
        if (OB_FAIL(macro_meta.get_rowkey(end_key))) {
          LOG_WARN("failed to get rowkey", K(ret), K(table_key_), K(macro_range_index_), K(macro_range_count_));
        } else if (OB_FAIL(macro_range_info.deep_copy_start_end_key(end_key))) {
          LOG_WARN("failed to deep copy start end key", K(ret), K(end_key), K(table_key_), K(macro_range_index_), K(macro_range_count_));
        } else {
          LOG_INFO("succeed get start logical id end key",
              K(end_key), K(macro_meta), K(table_key_), K(macro_range_index_), K(macro_range_count_));
        }
      }

      if (OB_FAIL(ret)) {
      } else {
        end_macro_block_id = macro_meta.get_logic_id();
        macro_block_count++;
      }
    }

    if (OB_SUCC(ret)) {
      macro_range_info.end_macro_block_id_ = end_macro_block_id;
      macro_range_info.macro_block_count_ = macro_block_count;
      macro_range_index_++;
    }
  }
  return ret;
}

}
}
