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

#include "rust/config.h"
#include "ob_index_block_dumper.h"
#include "storage/blocksstable/index_block/ob_index_block_builder.h"
namespace oceanbase
{
namespace blocksstable
{

////////////////////////////////////// Data Block Info //////////////////////////////////////////

////////////////////////////////////// Index Block Info //////////////////////////////////////////

ObIndexBlockInfo::ObIndexBlockInfo()
    : macro_meta_list_(nullptr), micro_block_desc_(nullptr),
      block_write_ctx_(nullptr),
      micro_block_cnt_(0), row_count_(0),
      state_(ObMacroMetaStorageState::INVALID_STORAGE_STATE),
      need_rewrite_(false)
{
}

ObIndexBlockInfo::~ObIndexBlockInfo()
{
  reset();
}

void ObIndexBlockInfo::reset()
{
  if (micro_block_desc_ != nullptr) {
    micro_block_desc_->~ObMicroBlockDesc();
  }
  if (block_write_ctx_ != nullptr) {
    block_write_ctx_->~ObMacroBlocksWriteCtx();
  }
  if (macro_meta_list_ != nullptr) {
    macro_meta_list_->~ObMacroMetasArray();
  }
  micro_block_desc_ = nullptr;
  block_write_ctx_ = nullptr;
  micro_block_cnt_ = 0;
  row_count_ = 0;
  state_ = ObMacroMetaStorageState::INVALID_STORAGE_STATE;
  need_rewrite_ = false;
}

////////////////////////////////////// Index Block Dumper //////////////////////////////////////////
ObBaseIndexBlockDumper::ObBaseIndexBlockDumper()
  : index_store_desc_(nullptr),
    container_store_desc_(nullptr),
    sstable_index_builder_(nullptr),
    sstable_allocator_(nullptr),
    task_allocator_(nullptr),
    row_allocator_(),
    meta_micro_writer_(nullptr),
    meta_macro_writer_(nullptr),
    micro_block_adaptive_splitter_(),
    last_rowkey_(),
    compressor_type_(INVALID_COMPRESSOR),
    micro_block_cnt_(0),
    row_count_(0),
    need_check_order_(true),
    is_inited_(false)
{
}

ObBaseIndexBlockDumper::~ObBaseIndexBlockDumper()
{
  reset();
}

void ObBaseIndexBlockDumper::reset()
{
  if (OB_NOT_NULL(meta_micro_writer_)) {
    meta_micro_writer_->~ObIMicroBlockWriter();
    task_allocator_->free(meta_micro_writer_);
    meta_micro_writer_ = nullptr;
  }
  if (OB_NOT_NULL(meta_macro_writer_)) {
    meta_macro_writer_->~ObMacroBlockWriter();
    task_allocator_->free(meta_macro_writer_);
    meta_macro_writer_ = nullptr;
  }
  index_store_desc_ = nullptr;
  container_store_desc_ = nullptr;

  macro_metas_ = nullptr; // allocated by sstable allocator
  sstable_allocator_ = nullptr;
  task_allocator_ = nullptr;
  micro_block_adaptive_splitter_.reset();
  last_rowkey_.reset();
  row_allocator_.reset();
  micro_block_cnt_ = 0;
  row_count_ = 0;
  enable_dump_disk_ = false;
  is_inited_ = false;
  compressor_type_ = INVALID_COMPRESSOR;
}

int ObBaseIndexBlockDumper::init(const ObDataStoreDesc &index_store_desc,
                             const ObDataStoreDesc &container_store_desc,
                             ObSSTableIndexBuilder *sstable_index_builder,
                             common::ObIAllocator &sstable_allocator,
                             common::ObIAllocator &task_allocator,
                             bool need_check_order,
                             bool enable_dump_disk)
{
  int ret = OB_SUCCESS;
  if (IS_INIT) {
    ret = OB_INIT_TWICE;
    STORAGE_LOG(WARN, "Init twice", K(ret));
  } else if (!enable_dump_disk) {
    void *array_buf = nullptr;
    if (OB_ISNULL(array_buf = sstable_allocator.alloc(sizeof(ObMacroMetasArray)))) {
      ret = OB_ALLOCATE_MEMORY_FAILED;
      STORAGE_LOG(WARN, "fail to alloc memory", K(ret));
    } else if (OB_ISNULL(macro_metas_ = new (array_buf)
                            ObMacroMetasArray(sizeof(ObDataMacroBlockMeta *),
                                              ModulePageAllocator(sstable_allocator)))) {
      ret = OB_ERR_UNEXPECTED;
      STORAGE_LOG(WARN, "fail to new ObMacroMetasArray", K(ret));
    }
  } else if (OB_FAIL(ObMacroBlockWriter::build_micro_writer(&container_store_desc, task_allocator, meta_micro_writer_))) {
  } else if (OB_FAIL(micro_block_adaptive_splitter_.init(container_store_desc.get_macro_block_size(),
      ObBaseIndexBlockBuilder::MIN_INDEX_MICRO_BLOCK_ROW_CNT /*min_micro_row_count*/, true/*is_use_adaptive*/))) {
  }

  if (OB_FAIL(ret)) {
  } else {
    index_store_desc_ = &index_store_desc;
    container_store_desc_ = &container_store_desc;
    sstable_index_builder_ = sstable_index_builder;
    task_allocator_ = &task_allocator;
    sstable_allocator_ = &sstable_allocator;

    need_check_order_ = need_check_order;
    enable_dump_disk_ = enable_dump_disk;
    is_inited_ = true;
    compressor_type_ = container_store_desc_->get_compressor_type();
  }

  if (OB_FAIL(ret) && OB_NOT_NULL(meta_micro_writer_)) {
    meta_micro_writer_->~ObIMicroBlockWriter();
    task_allocator.free(meta_micro_writer_);
    meta_micro_writer_ = nullptr;
  }
  return ret;
}

int ObBaseIndexBlockDumper::check_order(const ObDatumRow &row)
{
  int ret = OB_SUCCESS;
  ObDatumRowkey cur_key;
  if (!last_rowkey_.is_valid()) {
    // skip
  } else if (OB_FAIL(cur_key.assign(row.storage_datums_, container_store_desc_->get_rowkey_column_count()))) {
  } else {
    const ObStorageDatumUtils &datum_utils = container_store_desc_->get_datum_utils();
    int32_t compare_result = 0;
    if (OB_FAIL(cur_key.compare(last_rowkey_, datum_utils, compare_result))) {
    } else if (OB_UNLIKELY(compare_result < 0)) {
      ret = OB_ROWKEY_ORDER_ERROR;
      STORAGE_LOG(ERROR, "input rowkey is less then last rowkey.", K(cur_key), K(last_rowkey_), K(ret));
    }
  }
  return ret;
}

int ObBaseIndexBlockDumper::append_row(const ObDatumRow &row)
{
  int ret = OB_SUCCESS;
  bool is_split = false;
  const int64_t cur_macro_block_size = meta_macro_writer_ == nullptr ? 0 : meta_macro_writer_->get_macro_data_size();
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    STORAGE_LOG(WARN, "Not inited", K(ret));
  } else if (need_check_order_ && OB_FAIL(check_order(row))) {
    STORAGE_LOG(WARN, "fail to check macro meta order", K(ret), K(row));
  }
  if(OB_FAIL(ret)) {
  } else if (!enable_dump_disk_) {
    ObDataMacroBlockMeta *dst_macro_meta = nullptr;
    ObDataMacroBlockMeta tmp_macro_meta;
    //do not worry, won't change row, just for api. deep_copy() also be const.
    if (OB_FAIL(tmp_macro_meta.parse_row(const_cast<ObDatumRow &>(row)))) {
    } else if (OB_FAIL(tmp_macro_meta.deep_copy(dst_macro_meta, *sstable_allocator_))) {
    } else if (OB_FAIL(macro_metas_->push_back(dst_macro_meta))) {
    }
  } else if (0 < meta_micro_writer_->get_row_count() &&
        OB_FAIL(micro_block_adaptive_splitter_.check_need_split(meta_micro_writer_->get_block_size(),
        meta_micro_writer_->get_row_count(), container_store_desc_->get_micro_block_size(),
        cur_macro_block_size, false /*is_keep_freespace*/, is_split))) {
      STORAGE_LOG(WARN, "Failed to check need split", K(ret),
          "micro_block_size", container_store_desc_->get_micro_block_size(),
          "current_macro_size", cur_macro_block_size, KPC(meta_micro_writer_));
  } else if (is_split || OB_FAIL(meta_micro_writer_->append_row(row))) {
    if (OB_FAIL(ret) && OB_UNLIKELY(OB_BUF_NOT_ENOUGH != ret)) {
      STORAGE_LOG(WARN, "fail to append macro row", K(ret), K(row));
    } else if (OB_UNLIKELY(0 == meta_micro_writer_->get_row_count())) {
      ret = OB_NOT_SUPPORTED;
      STORAGE_LOG(WARN, "The single row is too large", K(ret), K(row));
    } else if (OB_FAIL(build_and_append_block())) {
    } else if (OB_FAIL(meta_micro_writer_->append_row(row))) {
    }
  }

  if (OB_SUCC(ret)) {
    ObDatumRowkey rowkey;
    last_rowkey_.reset();
    row_allocator_.reuse();
    if (OB_FAIL(rowkey.assign(row.storage_datums_, container_store_desc_->get_rowkey_column_count()))) {
    } else if (OB_FAIL(rowkey.deep_copy(last_rowkey_, row_allocator_))) {
    } else {
      row_count_++;
    }
  }
  return ret;
}

int ObBaseIndexBlockDumper::new_macro_writer()
{
  int ret = OB_SUCCESS;
  ObMacroSeqParam macro_seq_param;
  macro_seq_param.seq_type_ = ObMacroSeqParam::SEQ_TYPE_INC;
  macro_seq_param.start_ = 0;
  share::ObPreWarmerParam pre_warm_param(share::MEM_PRE_WARM);
  if (OB_UNLIKELY(compressor_type_ != container_store_desc_->get_compressor_type())) {
    ret = OB_ERR_UNEXPECTED;
  } else if (OB_ISNULL(meta_macro_writer_ = OB_NEWx(ObMacroBlockWriter, task_allocator_))) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
    STORAGE_LOG(WARN, "failed to alloc macro writer", K(ret));
  // Callback is only used for data macro blocks. The dumper builds macro meta
  // blocks, so it does not need one.
  } else if (OB_FAIL(meta_macro_writer_->open(*container_store_desc_, 0 /*parallel_idx*/,
      macro_seq_param, pre_warm_param, nullptr, nullptr))) {
  }

  if (OB_FAIL(ret) && OB_NOT_NULL(meta_macro_writer_)) {
    meta_macro_writer_->~ObMacroBlockWriter();
    task_allocator_->free(meta_macro_writer_);
    meta_macro_writer_ = nullptr;
  }
  return ret;
}

int ObBaseIndexBlockDumper::build_and_append_block()
{
  int ret = OB_SUCCESS;
  int64_t block_size = 0;
  ObMicroBlockDesc micro_block_desc;
  if (OB_ISNULL(meta_macro_writer_) && OB_FAIL(new_macro_writer())) {
    STORAGE_LOG(WARN, "fail to new macro writer", K(ret));
  } else if (OB_FAIL(meta_micro_writer_->build_micro_block_desc(micro_block_desc))) {
  } else if (FALSE_IT(micro_block_desc.last_rowkey_ = last_rowkey_)) {
  } else if (FALSE_IT(block_size = micro_block_desc.buf_size_)) {
  } else if (OB_FAIL(meta_macro_writer_->append_index_micro_block(micro_block_desc))) {
    meta_micro_writer_->dump_diagnose_info(); //ignore dump error
    STORAGE_LOG(WARN, "failed to append micro block of meta", K(ret), K(micro_block_desc));
  } else if (OB_FAIL(micro_block_adaptive_splitter_.update_compression_info(micro_block_desc.row_count_,
      block_size, micro_block_desc.buf_size_))) {
    STORAGE_LOG(WARN, "fail to append next row", K(ret), K(micro_block_desc));
  } else {
    clean_status();
    micro_block_cnt_++;
  }
  return ret;
}

int ObBaseIndexBlockDumper::close(ObIndexBlockInfo& index_block_info)
{
  int ret = OB_SUCCESS;
  index_block_info.reset();
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    STORAGE_LOG(WARN, "Not inited", K(ret));
  } else if (OB_UNLIKELY(compressor_type_ != container_store_desc_->get_compressor_type())) {
    ret = OB_ERR_UNEXPECTED;
  } else if (micro_block_cnt_ == 0 && row_count_ == 0) {
  } else if (!enable_dump_disk_) {
    if (OB_FAIL(close_to_array(index_block_info))) {
    }
  } else if (OB_NOT_NULL(meta_macro_writer_)) {
    if (OB_FAIL(close_to_disk(index_block_info))) {
    }
  } else if (OB_FAIL(close_to_mem(index_block_info))) {
  }
  return ret;
}

int ObBaseIndexBlockDumper::close_to_array(ObIndexBlockInfo& index_block_info)
{
  index_block_info.macro_meta_list_ = macro_metas_;
  index_block_info.row_count_ = macro_metas_->count();
  index_block_info.state_ = ObMacroMetaStorageState::IN_ARRAY;
  return OB_SUCCESS;
}

int ObBaseIndexBlockDumper::close_to_disk(ObIndexBlockInfo& index_block_info)
{
  int ret = OB_SUCCESS;
  ObMicroBlockDesc micro_block_desc;
  if (OB_UNLIKELY(0 == meta_micro_writer_->get_row_count())) {
  } else if (OB_FAIL(build_and_append_block())) {
  } else if (OB_FAIL(meta_macro_writer_->close())) {
  } else if (OB_FAIL(meta_macro_writer_->get_macro_block_write_ctx().deep_copy(
      index_block_info.block_write_ctx_, *sstable_allocator_))) {
  } else if (OB_FAIL(meta_macro_writer_->check_meta_macro_block_need_rewrite(index_block_info.need_rewrite_))){
    STORAGE_LOG(WARN, "failed to check need rewrite", K(ret));
  } else {
    index_block_info.state_ = ObMacroMetaStorageState::IN_DISK;
    index_block_info.row_count_ = row_count_;
    index_block_info.micro_block_cnt_ = micro_block_cnt_;
  }
  return ret;
}

int ObBaseIndexBlockDumper::close_to_mem(ObIndexBlockInfo& index_block_info)
{
  int ret = OB_SUCCESS;
  char *block_buffer = nullptr;
  int block_size = 0;
  ObMicroBlockDesc tmp_desc;
  if (OB_UNLIKELY(0 == meta_micro_writer_->get_row_count())) {
  } else if (OB_FAIL(meta_micro_writer_->build_micro_block_desc(tmp_desc))) {
  } else if (FALSE_IT(tmp_desc.last_rowkey_ = last_rowkey_)) {
  } else if (OB_ISNULL(index_block_info.micro_block_desc_ = OB_NEWx(ObMicroBlockDesc, sstable_allocator_))){
    ret = OB_ALLOCATE_MEMORY_FAILED;
    STORAGE_LOG(WARN, "failed to alloc micro block desc", K(ret));
  } else if (OB_FAIL(tmp_desc.deep_copy(*sstable_allocator_, *index_block_info.micro_block_desc_))) {
  } else {
    index_block_info.state_ = ObMacroMetaStorageState::IN_MEM;
    index_block_info.row_count_ = row_count_;
    index_block_info.micro_block_cnt_ = ++micro_block_cnt_;
  }

  if (OB_FAIL(ret) && OB_NOT_NULL(index_block_info.micro_block_desc_)) {
    index_block_info.micro_block_desc_->~ObMicroBlockDesc();
    sstable_allocator_->free(index_block_info.micro_block_desc_);
    index_block_info.micro_block_desc_ = nullptr;
  }
  return ret;
}

void ObBaseIndexBlockDumper::clean_status()
{
  meta_micro_writer_->reuse();
}

////////////////////////////////////// Index Block Loader //////////////////////////////////////////
ObIndexBlockLoader::ObIndexBlockLoader()
    : macro_io_handle_(), micro_reader_helper_(), cur_micro_block_(), micro_iter_(), row_allocator_("IndexBlkLoader"),
      micro_reader_(nullptr), index_block_info_(nullptr), macro_id_array_(nullptr), io_allocator_(nullptr), micro_buf_(nullptr),
      curr_block_row_idx_(-1), curr_block_row_cnt_(-1), cur_block_idx_(-1), prefetch_idx_(-1),
      data_version_(0), is_inited_(false)
{
}

ObIndexBlockLoader::~ObIndexBlockLoader()
{
  reset();
}

void ObIndexBlockLoader::reset()
{
  micro_reader_helper_.reset();
  cur_micro_block_.reset();
  micro_iter_.reset();
  micro_reader_ = nullptr; //get from micro reader helper
  for (int64_t i = 0; i < PREFETCH_DEPTH; ++i) {
    macro_io_handle_[i].reset();
  }
  if (OB_NOT_NULL(micro_buf_)) {
    io_allocator_->free(micro_buf_);
    micro_buf_ = nullptr;
  }
  curr_block_row_idx_ = -1;
  curr_block_row_cnt_ = -1;
  cur_block_idx_ = -1;
  prefetch_idx_ = -1;
  data_version_ = 0;
  macro_id_array_ = nullptr;
  io_allocator_ = nullptr;
  is_inited_ = false;
}

void ObIndexBlockLoader::reuse()
{
  cur_micro_block_.reset();
  micro_iter_.reuse();
  micro_reader_ = nullptr;
  curr_block_row_idx_ = -1;
  curr_block_row_cnt_ = -1;
  cur_block_idx_ = -1;
  prefetch_idx_ = -1;
  macro_id_array_ = nullptr;
}

int ObIndexBlockLoader::init(common::ObIAllocator &allocator, const uint64_t data_version)
{
  int ret = OB_SUCCESS;
  if (IS_INIT) {
    ret = OB_INIT_TWICE;
    STORAGE_LOG(WARN, "Init twice", K(ret));
  } else if (OB_UNLIKELY(DATA_CURRENT_VERSION != data_version)) {
    ret = OB_INVALID_ARGUMENT;
  } else if (OB_FAIL(micro_reader_helper_.init(allocator))) {
  } else {
    io_allocator_ = &allocator;
    if (OB_ISNULL(micro_buf_ = static_cast<char *>(io_allocator_->alloc(common::OB_DEFAULT_MACRO_BLOCK_SIZE)))) {
      ret = OB_ALLOCATE_MEMORY_FAILED;
    }
    if (OB_SUCC(ret)) {
      curr_block_row_idx_ = -1;
      curr_block_row_cnt_ = -1;
      cur_block_idx_ = -1;
      prefetch_idx_ = -1;
      data_version_ = data_version;
      is_inited_ = true;
    }
  }
  return ret;
}

int ObIndexBlockLoader::open(const ObIndexBlockInfo& index_block_info)
{
  int ret = OB_SUCCESS;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    STORAGE_LOG(WARN, "Not inited", K(ret));
  } else if (!index_block_info.is_valid()) {
    ret = OB_INVALID_ARGUMENT;
    STORAGE_LOG(WARN, "invalid macro meta info", K(ret), K(index_block_info));
  } else if (index_block_info.in_disk()) {
    macro_id_array_ = &index_block_info.block_write_ctx_->get_macro_block_list();
    if (OB_FAIL(open_next_macro_block())) {
    }
  } else if (index_block_info.in_array()) {
    curr_block_row_idx_ = 0;
    curr_block_row_cnt_ = index_block_info.macro_meta_list_->count();
  }

  if (OB_SUCC(ret)) {
    index_block_info_ = &index_block_info;
  }
  return ret;
}

int ObIndexBlockLoader::get_next_array_row(ObDatumRow &row)
{
  int ret = OB_SUCCESS;
  if (curr_block_row_idx_ >= curr_block_row_cnt_) {
    ret = OB_ITER_END;
  } else {
    row_allocator_.reuse();
    const ObDataMacroBlockMeta *macro_meta = index_block_info_->macro_meta_list_->at(curr_block_row_idx_);
    if (OB_FAIL(macro_meta->build_row(row, row_allocator_, data_version_))) {
    } else {
      curr_block_row_idx_++;
    }
  }
  return ret;
}

int ObIndexBlockLoader::get_next_row(ObDatumRow &row)
{
  int ret = OB_SUCCESS;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    STORAGE_LOG(WARN, "macro meta loader not inited", K(ret));
  } else if (OB_ISNULL(index_block_info_)) {
    ret = OB_ERR_UNEXPECTED;
    STORAGE_LOG(WARN, "have not open any macro meta info", K(ret));
  } else if (index_block_info_->empty()) {
    ret = OB_ITER_END;
  } else {
    switch (index_block_info_->state_) {
      case IN_MEM:
        if (OB_FAIL(get_next_mem_row(row))) {
          if (OB_UNLIKELY(ret != OB_ITER_END)) {
            STORAGE_LOG(WARN, "Fail to get mem row", K(ret));
          }
        }
        break;
      case IN_DISK:
        if (OB_FAIL(get_next_disk_row(row))){
          if (OB_UNLIKELY(ret != OB_ITER_END)) {
            STORAGE_LOG(WARN, "Fail to get disk row", K(ret));
          }
        }
        break;
      case IN_ARRAY:
        if (OB_FAIL(get_next_array_row(row))){
          if (OB_UNLIKELY(ret != OB_ITER_END)) {
            STORAGE_LOG(WARN, "Fail to get array row", K(ret));
          }
        }
        break;
      default:
      ret = OB_ERR_UNEXPECTED;
      STORAGE_LOG(WARN, "the state is invalid", K(ret), K(index_block_info_->state_));
    }
  }
  return ret;
}

int ObIndexBlockLoader::get_next_micro_block_desc(blocksstable::ObMicroBlockDesc &micro_block_desc,
                                                  const ObDataStoreDesc &data_store_desc,
                                                  ObIAllocator &allocator)
{
  int ret = OB_SUCCESS;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    STORAGE_LOG(WARN, "macro meta loader not inited", K(ret));
  } else if (OB_ISNULL(index_block_info_) || index_block_info_->in_mem()) {
    ret = OB_ERR_UNEXPECTED;
    STORAGE_LOG(WARN, "unexpected index block info", K(ret), K(index_block_info_));
  } else if (index_block_info_->empty()) {
    ret = OB_ITER_END;
  } else {
    bool rewrite = index_block_info_->need_rewrite_ && is_last();
    if (OB_FAIL(micro_iter_.get_next_micro_block_desc(micro_block_desc, data_store_desc, allocator, rewrite))) {
      if (OB_UNLIKELY(OB_ITER_END != ret)) {
        STORAGE_LOG(WARN, "Fail to get next micro block data", K(ret), K(rewrite), K(micro_iter_));
      } else if (OB_FAIL(open_next_macro_block())) {
        if (OB_UNLIKELY(OB_ITER_END != ret)) {
          STORAGE_LOG(WARN, "Fail to open next macro block", K(ret));
        }
      } else if (FALSE_IT(rewrite = index_block_info_->need_rewrite_ && is_last())) {
      } else if (OB_FAIL(micro_iter_.get_next_micro_block_desc(micro_block_desc, data_store_desc, allocator, rewrite))) {
      }
    }
    if (OB_SUCC(ret)) {
      micro_block_desc.macro_id_ = macro_id_array_->at(cur_block_idx_);
    }
  }
  return ret;
}

int ObIndexBlockLoader::prefetch()
{
  int ret = OB_SUCCESS;
  while(OB_SUCC(ret)) {
    if (prefetch_idx_ - cur_block_idx_ < PREFETCH_DEPTH
        && prefetch_idx_ < macro_id_array_->count() - 1) {
      prefetch_idx_++;
      int64_t io_index = prefetch_idx_ % PREFETCH_DEPTH;
      blocksstable::ObStorageObjectHandle &macro_io_handle = macro_io_handle_[io_index];
      blocksstable::ObStorageObjectReadInfo read_info;
      macro_io_handle.reset();
      read_info.macro_block_id_ = macro_id_array_->at(prefetch_idx_);
      read_info.offset_ = 0;
      read_info.size_ = common::OB_DEFAULT_MACRO_BLOCK_SIZE;
      
      if (OB_FAIL(blocksstable::ObObjectManager::async_read_object(read_info, macro_io_handle))) {
      }
    } else {
      break;
    }
  }
  return ret;
}

int ObIndexBlockLoader::open_mem_block()
{
  int ret = OB_SUCCESS;
  ObMicroBlockDesc *micro_block_desc = index_block_info_->micro_block_desc_;
  ObMicroBlockHeader *header = const_cast<ObMicroBlockHeader *>(micro_block_desc->header_);
  int64_t size = header->get_serialize_size() + micro_block_desc->buf_size_;
  int64_t pos = 0;
  if (OB_FAIL(header->serialize(micro_buf_, header->header_size_, pos))) {
  } else {
    MEMCPY(micro_buf_ + pos, micro_block_desc->buf_, micro_block_desc->buf_size_);
    cur_micro_block_.get_buf() = micro_buf_;
    cur_micro_block_.get_buf_size() = size;
  }
  if (OB_FAIL(ret)) {
  } else if (OB_FAIL(open_micro_block(cur_micro_block_))) {
  } else {
    cur_block_idx_++;
  }
  return ret;
}

int ObIndexBlockLoader::open_next_macro_block()
{
  int ret = OB_SUCCESS;
  micro_iter_.reuse();
  if (OB_UNLIKELY(is_disk_iter_end())) {
    ret = OB_ITER_END;
  } else if (OB_FAIL(prefetch())) {
  } else {
    const int64_t io_timeout_ms = std::max(config::_data_storage_io_timeout() / 1000, DEFAULT_IO_WAIT_TIME_MS);
    cur_block_idx_++;
    blocksstable::ObStorageObjectHandle &macro_io_handle = macro_io_handle_[cur_block_idx_ % PREFETCH_DEPTH];
    if (OB_FAIL(macro_io_handle.wait())) {
    } else if (OB_FAIL(micro_iter_.open(macro_io_handle.get_buffer(),
                                        macro_io_handle.get_data_size()))) {
    }
  }
  return ret;
}

int ObIndexBlockLoader::get_next_mem_row(ObDatumRow &row)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(not_open_mem_block()) && OB_FAIL(open_mem_block())) {
    STORAGE_LOG(WARN, "Fail to open mem micro block", K(ret));
  } else if (curr_block_row_idx_ >= curr_block_row_cnt_) {
    ret = OB_ITER_END;
  } else if (OB_FAIL(micro_reader_->get_row(curr_block_row_idx_, row))) {
  } else {
    curr_block_row_idx_++;
  }
  return ret;
}

int ObIndexBlockLoader::get_next_disk_row(ObDatumRow &row)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(curr_block_row_idx_ >= curr_block_row_cnt_)) {
    if (OB_FAIL(get_next_disk_micro_block_data(cur_micro_block_))) {
      if (OB_ITER_END != ret) {
        STORAGE_LOG(WARN, "Fail to get next row", K(ret));
      }
    } else if (OB_FAIL(open_micro_block(cur_micro_block_))) {
    }
  }
  if (OB_FAIL(ret)){
  } else if (OB_FAIL(micro_reader_->get_row(curr_block_row_idx_, row))) {
  } else {
    curr_block_row_idx_++;
  }
  return ret;
}

int ObIndexBlockLoader::open_micro_block(ObMicroBlockData &micro_block_data)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(!micro_block_data.is_valid())) {
    ret = OB_ERR_UNEXPECTED;
    STORAGE_LOG(WARN, "invalid micro block data", K(ret), K(micro_block_data));
  } else if (OB_FAIL(micro_reader_helper_.get_reader(micro_block_data.get_store_type(), micro_reader_))) {
  } else if (OB_FAIL(micro_reader_->init(micro_block_data, nullptr))) {
  } else if (OB_FAIL(micro_reader_->get_row_count(curr_block_row_cnt_))) {
  } else {
    curr_block_row_idx_ = 0;
  }
  return ret;
}

int ObIndexBlockLoader::get_next_disk_micro_block_data(ObMicroBlockData &micro_block_data)
{
  int ret = OB_SUCCESS;
  if (OB_FAIL(micro_iter_.get_next_micro_block_data(micro_block_data))) {
    if (OB_UNLIKELY(OB_ITER_END != ret)) {
      STORAGE_LOG(WARN, "Fail to get next micro block data", K(ret));
    } else if (OB_FAIL(open_next_macro_block())) {
      if (OB_UNLIKELY(OB_ITER_END != ret)) {
        STORAGE_LOG(WARN, "Fail to open next macro block", K(ret));
      }
    } else if (OB_FAIL(micro_iter_.get_next_micro_block_data(micro_block_data))) {
    }
  }
  return ret;
}

} // namespace blocksstable
} // namespace oceanbase
