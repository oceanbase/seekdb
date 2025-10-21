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

#define USING_LOG_PREFIX SERVER

#include "observer/table_load/backup/v_1_4/ob_table_load_backup_macro_block_reader_v_1_4.h"

namespace oceanbase
{
namespace observer
{
using namespace common;

/**
 * ObTableLoadBackupMacroBlockReader_V_1_4
 */
int ObTableLoadBackupMacroBlockReader_V_1_4::init(const char *buf, int64_t buf_size)
{
  int ret = OB_SUCCESS;
  if (IS_INIT) {
    ret = OB_INIT_TWICE;
    LOG_WARN("already init", KR(ret), KP(this));
  } else if (OB_UNLIKELY(buf == nullptr || buf_size <= 0)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid args", KR(ret), KP(buf), K(buf_size));
  } else {
    buf_ = buf;
    buf_size_ = buf_size;
    if (OB_FAIL(inner_init())) {
      LOG_WARN("fail to inner_init", KR(ret));
    } else {
      is_inited_ = true;
    }
  }

  return ret;
}

int ObTableLoadBackupMacroBlockReader_V_1_4::inner_init()
{
  int ret = OB_SUCCESS;
  int64_t meta_len = 0;
  int64_t macro_block_size = 0;
  if (OB_ISNULL(meta_.endkey_ = static_cast<ObObj *>(allocator_.alloc(sizeof(ObObj) * 
                  ObTableLoadBackupColumnMap_V_1_4::OB_TABLE_LOAD_PRE_ROW_MAX_COLUMNS_COUNT)))) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
    LOG_WARN("fail to alloc_memory", KR(ret));
  } else if (OB_FAIL(serialization::decode_i64(buf_, 
                                        buf_size_, 
                                        pos_, 
                                        &meta_len))) {
    LOG_WARN("fail to decode", KR(ret));
  } else if (OB_FAIL(meta_.deserialize(buf_, 
                                       buf_size_, 
                                       pos_))) {
    LOG_WARN("fail to deserialize meta_", KR(ret));
  } else if (OB_FAIL(serialization::decode_i64(buf_, 
                                               buf_size_, 
                                               pos_, 
                                               &macro_block_size))) {
    LOG_WARN("fail to decode", KR(ret));
  } else if (OB_FAIL(column_map_.init(&meta_))) {
    LOG_WARN("fail to init column_map_", KR(ret));
  } else if (OB_FAIL(ObCompressorPool::get_instance().get_compressor(meta_.compressor_, compressor_))) {
    LOG_WARN("fail to get compressor_", KR(ret), K(meta_.compressor_));
  } else {
    micro_index_ = reinterpret_cast<const ObTableLoadBackupMicroBlockIndex_V_1_4 *>(buf_ + pos_ + meta_.micro_block_index_offset_);
  }
  if (meta_.endkey_ != nullptr) {
    allocator_.free(meta_.endkey_);
    meta_.endkey_ = nullptr;
  }

  return ret;
}

void ObTableLoadBackupMacroBlockReader_V_1_4::reset()
{
  allocator_.reset();
  column_map_.reuse();
  micro_index_ = nullptr;
  compressor_ = nullptr;
  buf_ = nullptr;
  buf_size_ = 0;
  decomp_buf_ = nullptr;
  decomp_buf_size_ = 0;
  uncomp_buf_ = nullptr;
  pos_ = 0;
  is_inited_ = false;
}

int ObTableLoadBackupMacroBlockReader_V_1_4::decompress_data(const int32_t micro_block_idx)
{
  int ret = OB_SUCCESS;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", KR(ret), KP(this));
  } else if (OB_UNLIKELY(micro_block_idx < 0 || micro_block_idx >= meta_.micro_block_count_)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid args", KR(ret), K(micro_block_idx));
  } else {
    if (micro_block_idx == 0) {
      micro_offset_ = meta_.micro_block_data_offset_;
    } else {
      micro_offset_ += micro_index_[micro_block_idx].data_offset_ - micro_index_[micro_block_idx - 1].data_offset_;
    }
    int64_t size = micro_index_[micro_block_idx + 1].data_offset_ - micro_index_[micro_block_idx].data_offset_;
    if (OB_UNLIKELY(size <= (int64_t)sizeof(ObTableLoadBackupMicroBlockRecordHeader_V_1_4))) {
      ret = OB_INVALID_ARGUMENT;
      LOG_WARN("invalid args", KR(ret), K(micro_block_idx), K(size));
    } else if (OB_FAIL(ObTableLoadBackupMicroBlockRecordHeader_V_1_4::check_record(buf_ + pos_ + micro_offset_, size))) {
      LOG_WARN("fail to check record", KR(ret));
    } else {
      const ObTableLoadBackupMicroBlockRecordHeader_V_1_4 *header = nullptr;
      header = reinterpret_cast<const ObTableLoadBackupMicroBlockRecordHeader_V_1_4*>(buf_ + pos_ + micro_offset_);
      const char *comp_buf = buf_ + pos_ + micro_offset_ + sizeof(ObTableLoadBackupMicroBlockRecordHeader_V_1_4);
      int64_t comp_size = size - sizeof(ObTableLoadBackupMicroBlockRecordHeader_V_1_4);
      if (header->is_compressed_data()) {
        int64_t decomp_size = 0;
        if (OB_FAIL(alloc_buf(header->data_length_))) {
          LOG_WARN("fail to allocate buf", KR(ret));
        } else if (OB_FAIL(compressor_ != nullptr && compressor_->decompress(comp_buf, 
                                                                             comp_size, 
                                                                             decomp_buf_, 
                                                                             decomp_buf_size_,
                                                                             decomp_size))) {
          LOG_WARN("fail to decompress", KR(ret));
        } else {
          uncomp_buf_ = decomp_buf_;
        }
      } else {
        uncomp_buf_ = comp_buf;
      }
    }
  }

  return ret;
}

int ObTableLoadBackupMacroBlockReader_V_1_4::alloc_buf(const int64_t buf_size)
{
  int ret = OB_SUCCESS;
  if (decomp_buf_ == nullptr || decomp_buf_size_ < buf_size) {
    allocator_.reuse();
    if (OB_ISNULL(decomp_buf_ = static_cast<char*>(allocator_.alloc(buf_size)))) {
      ret = OB_ALLOCATE_MEMORY_FAILED;
      LOG_WARN("fail to alloc memory", K(buf_size));
    } else {
      decomp_buf_size_ = buf_size;
    }
  } 

  return ret;
}

} // namespace observer
} // namespace oceanbase
