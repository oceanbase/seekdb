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

#include "storage/ddl/ob_ddl_row_tmp_file.h"
#include "sql/engine/ob_bit_vector.h"
#include "storage/blocksstable/ob_datum_row.h"
#include "storage/ddl/ob_pipeline.h"
#include "storage/ddl/ob_ddl_tablet_context.h"

namespace oceanbase
{
using namespace sql;
namespace storage
{
/**
* -----------------------------------ObDDLRowFile-----------------------------------
*/
int ObDDLRowFile::open(const ObIArray<ObColumnSchemaItem> &all_column_schema_its,
                      const ObTabletID &tablet_id,
                      const int64_t slice_idx,
                      const int64_t max_batch_size,
                      const int64_t memory_limit,
                      const int64_t dir_id)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(is_opened_)) {
    ret = OB_INIT_TWICE;
    LOG_WARN("the ObDDLRowFile is opened already", K(ret));
  } else if (OB_UNLIKELY(all_column_schema_its.empty() ||
                         !tablet_id.is_valid() ||
                         slice_idx < 0 ||
                         max_batch_size <= 0 ||
                         memory_limit <= 0 ||
                         dir_id < 0)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("there are invalid argument", K(ret), K(all_column_schema_its.count()), K(tablet_id),
        K(slice_idx), K(max_batch_size), K(memory_limit), K(dir_id));
  }
  if (OB_SUCC(ret)) {
    ObCompressorType compressor_type = NONE_COMPRESSOR;
    const int64_t skip_size = ObBitVector::memory_size(max_batch_size);
    void *skip_mem = nullptr;
    if (OB_FAIL(ObTempColumnStore::init_vectors(all_column_schema_its, allocator_, bdrs_.vectors_))) {
      LOG_WARN("fail to initialize vectors", K(ret), K(all_column_schema_its));
    } else if (OB_FAIL(store_.init(bdrs_.vectors_,
                                   max_batch_size,
                                   ObMemAttr("DDLRowFileStore"),
                                   memory_limit,
                                   true/*enable_dump*/,
                                   compressor_type))) {
      LOG_WARN("fail to initialize temp column store", K(ret));
    } else if (OB_ISNULL(skip_mem = allocator_.alloc(skip_size))) {
      ret = OB_ALLOCATE_MEMORY_FAILED;
      LOG_WARN("fail to alloc skip buffer", K(ret), K(skip_size));
    } else {
      tablet_id_ = tablet_id;
      slice_idx_ = slice_idx;
      column_count_ = all_column_schema_its.count();
      store_.set_dir_id(dir_id);

      brs_.skip_ = to_bit_vector(skip_mem);
      brs_.skip_->reset(max_batch_size);
      brs_.size_ = 0;
      brs_.set_all_rows_active(true);
      bdrs_.row_flag_.set_flag(blocksstable::ObDmlFlag::DF_INSERT);
      is_opened_ = true;
    }
  }
  return ret;
}

int ObDDLRowFile::close()
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(!is_opened_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("the ObDDLRowFile is not opened", K(ret));
  } else {
    is_opened_ = false;
    column_count_ = 0;
    is_start_iterate_ = false;
    iter_.reset();
    store_.reset();
    bdrs_.reset();
    if (nullptr != brs_.skip_) {
      brs_.skip_->~ObBitVector();
      allocator_.free(brs_.skip_);
      brs_.skip_ = nullptr;
    }
    allocator_.reset();
  }
  return ret;
}

int ObDDLRowFile::append_batch(const blocksstable::ObBatchDatumRows &bdrs)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(!is_opened_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("the ObDDLRowFile is not opened", K(ret));
  } else if (OB_UNLIKELY(bdrs.vectors_.count() != column_count_)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("the column count is not equal to the batch datum rows's vector count",
        K(ret), K(bdrs.vectors_.count()), K(column_count_));
  } else {
    int64_t stored_row_count = 0;
    brs_.size_ = bdrs.row_count_;
    if (OB_FAIL(store_.add_batch(bdrs.vectors_, brs_, stored_row_count))) {
      LOG_WARN("fail to add batch", K(ret));
    } else if (OB_UNLIKELY(stored_row_count != bdrs.row_count_)) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("the stored row count is not equal to the brs's row count",
          K(ret), K(stored_row_count), K(bdrs.row_count_));
    } else {
      is_start_iterate_ = false;
    }
  }
  return ret;
}

int ObDDLRowFile::get_next_batch(blocksstable::ObBatchDatumRows *&bdrs)
{
  int ret = OB_SUCCESS;
  int64_t read_row_count = 0;
  bdrs = nullptr;
  if (OB_UNLIKELY(!is_opened_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("the ObDDLRowFile is not opened", K(ret));
  } else if (!is_start_iterate_ && begin(iter_)) {
    LOG_WARN("fail to begin iterating", K(ret));
  } else if (OB_FAIL(iter_.get_next_batch(bdrs_.vectors_, read_row_count))) {
    if (OB_ITER_END != ret) {
      LOG_WARN("fail to get next batch", KR(ret));
    }
  } else {
    bdrs_.row_count_ = read_row_count;
    bdrs = &bdrs_;
  }
  return ret;
}

int ObDDLRowFile::dump(const bool all_dump, const int64_t target_dump_size)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(!is_opened_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("the ObDDLRowFile is not opened", K(ret));
  } else if (OB_FAIL(store_.dump(all_dump, target_dump_size))) {
    LOG_WARN("fail to dump", K(ret), K(all_dump), K(target_dump_size));
  }
  return ret;
}

int ObDDLRowFile::finish_append_batch(bool need_dump)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(!is_opened_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("the ObDDLRowFile is not opened", K(ret));
  } else if (OB_FAIL(store_.finish_add_row(need_dump))) {
    LOG_WARN("fail to finish add row", K(ret));
  }
  return ret;
}

// private function
int ObDDLRowFile::begin(sql::ObTempColumnStore::Iterator &iter, const bool async)
{
  int ret = OB_SUCCESS;
  if (OB_FAIL(store_.begin(iter, async))) {
    LOG_WARN("fail to begin iterating", K(ret));
  } else {
    is_start_iterate_ = true;
  }
  return ret;
}

/**
* -----------------------------------ObDDLRowFileGenerator-----------------------------------
*/
void ObDDLRowFileGenerator::reset()
{
  is_inited_ = false;
  tablet_id_ = ObTabletID::INVALID_TABLET_ID;
  slice_idx_ = -1;
  max_batch_size_ = 0;
  row_file_memory_limit_ = 0;
  all_column_schema_its_.reset();
  for (int64_t i = 0; i < row_file_arr_.count(); ++i) {
    ObDDLRowFile *&row_file = row_file_arr_.at(i);
    if (nullptr != row_file) {
      row_file->~ObDDLRowFile();
      ob_free(row_file);
      row_file = nullptr;
    }
  }
  row_file_arr_.reset();
  if (is_generation_sync_output_ && nullptr != row_file_arr_for_output_) {
    for (int64_t i = 0; i < row_file_arr_for_output_->count(); ++i) {
      ObDDLRowFile *&row_file = row_file_arr_for_output_->at(i);
      if (nullptr != row_file) {
        row_file->~ObDDLRowFile();
        ob_free(row_file);
        row_file = nullptr;
      }
    }
    row_file_arr_for_output_->~ObArray<ObDDLRowFile *>();
    ob_free(row_file_arr_for_output_);
    row_file_arr_for_output_ = nullptr;
  }
  if (is_generation_sync_output_ && nullptr != sync_chunk_data_) {
    sync_chunk_data_->row_file_arr_ = nullptr;
    sync_chunk_data_->~ObChunk();
    ob_free(sync_chunk_data_);
    sync_chunk_data_ = nullptr;
  }
}

int ObDDLRowFileGenerator::init(
    const ObTabletID &tablet_id,
    const int64_t slice_idx,
    const int64_t max_batch_size,
    const int64_t row_file_memory_limit,
    const ObIArray<ObColumnSchemaItem> &all_column_schema_its,
    const bool is_generation_sync_output)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(is_inited_)) {
    ret = OB_INIT_TWICE;
    LOG_WARN("the ObDDLRowFileGenerator has been initialized", K(ret));
  } else if (OB_UNLIKELY(!tablet_id.is_valid() ||
                          slice_idx < 0 ||
                          max_batch_size <= 0 ||
                          row_file_memory_limit <= 0 ||
                          all_column_schema_its.empty())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("there are invalid argument",
        K(ret), K(tablet_id), K(slice_idx),
        K(max_batch_size), K(row_file_memory_limit), K(all_column_schema_its));
  } else {
    tablet_id_ = tablet_id;
    slice_idx_ = slice_idx;
    max_batch_size_ = max_batch_size;
    is_generation_sync_output_ = is_generation_sync_output;
    row_file_memory_limit_ = row_file_memory_limit;
    if (OB_FAIL(row_file_arr_.prepare_allocate(1))) {
      LOG_WARN("fail to prepare row file slot", K(ret));
    } else if (OB_FAIL(all_column_schema_its_.assign(all_column_schema_its))) {
      LOG_WARN("fail to assign all column schema its", K(ret), K(all_column_schema_its));
    } else {
      row_file_arr_.at(0) = nullptr;
      if (is_generation_sync_output) {
        row_file_arr_for_output_ = OB_NEW(ObArray<ObDDLRowFile *>, ObMemAttr("DDLRowFiles"));
        sync_chunk_data_ = OB_NEW(ObChunk, ObMemAttr("ChunkDataOutput"));
        if (OB_UNLIKELY(nullptr == row_file_arr_for_output_ || nullptr == sync_chunk_data_)) {
          ret = OB_ALLOCATE_MEMORY_FAILED;
          LOG_WARN("fail to allocate row file output",
              K(ret), KP(row_file_arr_for_output_), KP(sync_chunk_data_));
        }
        FLOG_INFO("the ObDDLRowFileGenerator is generation sync output mode",
            K(ret), K(is_generation_sync_output_));
      }
      if (OB_SUCC(ret)) {
        is_inited_ = true;
      }
    }
  }
  return ret;
}

int ObDDLRowFileGenerator::append_batch(
    const blocksstable::ObBatchDatumRows &bdrs,
    const bool is_slice_end,
    ObDDLChunk &output_chunk)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(!is_inited_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("the ObDDLRowFileGenerator is not initialized", K(ret));
  } else if (OB_UNLIKELY((!is_slice_end && bdrs.row_count_ <= 0) ||
                          bdrs.row_count_ > max_batch_size_)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("the are invalid arguments",
        K(ret), K(is_slice_end), K(bdrs), K(max_batch_size_));
  } else if (is_slice_end && bdrs.row_count_ <= 0) {
    // by pass
  } else {
    output_chunk.reset();
    ObDDLRowFile *&row_file = row_file_arr_.at(0);
    if (nullptr == row_file) {
      row_file = OB_NEW(ObDDLRowFile, ObMemAttr("DDLRowFile"));
      if (OB_UNLIKELY(nullptr == row_file)) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_WARN("fail to allocate row file", K(ret));
      } else if (OB_FAIL(row_file->open(all_column_schema_its_,
                                        tablet_id_,
                                        slice_idx_,
                                        max_batch_size_,
                                        row_file_memory_limit_))) {
        LOG_WARN("fail to open row file", K(ret), K(tablet_id_), K(slice_idx_));
      }
      if (OB_FAIL(ret) && nullptr != row_file) {
        row_file->~ObDDLRowFile();
        ob_free(row_file);
        row_file = nullptr;
      }
    }
    if (OB_SUCC(ret) && OB_FAIL(row_file->append_batch(bdrs))) {
      LOG_WARN("fail to append batch", K(ret));
    }
  }
  if (OB_SUCC(ret) && OB_FAIL(try_generate_output_chunk(is_slice_end, output_chunk))) {
    LOG_WARN("fail to generate ddl output chunk", K(ret));
  }
  return ret;
}

int ObDDLRowFileGenerator::try_generate_output_chunk(
    const bool is_slice_end,
    ObDDLChunk &output_chunk)
{
  int ret = OB_SUCCESS;
  ObChunk *chunk_data = nullptr;
  ObArray<ObDDLRowFile *> *row_files_ptr = nullptr;
  output_chunk.reset();
  if (OB_UNLIKELY(!is_inited_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("the ObDDLRowFileGenerator is not initialized", K(ret));
  } else {
    if (is_generation_sync_output_) {
      row_file_arr_for_output_->reuse();
    }
    for (int64_t i = 0; OB_SUCC(ret) && i < row_file_arr_.count(); ++i) {
      ObDDLRowFile *&row_file = row_file_arr_.at(i);
      if (nullptr != row_file &&
          (row_file->get_mem_hold() > row_file_memory_limit_ || is_slice_end)) {
        if (OB_FAIL(row_file->dump(true))) {
          LOG_WARN("fail to dump row file", K(ret), KPC(row_file));
        } else if (OB_FAIL(row_file->finish_append_batch(true/*need_dump*/))) {
          LOG_WARN("fail to finish add row", K(ret), KPC(row_file));
        } else if (is_generation_sync_output_) {
          if (OB_UNLIKELY(nullptr == row_file_arr_for_output_ ||
                          nullptr == sync_chunk_data_)) {
            ret = OB_ERR_UNEXPECTED;
            LOG_WARN("row file output is null",
                K(ret), KP(row_file_arr_for_output_), KP(sync_chunk_data_));
          } else {
            if (OB_FAIL(row_file_arr_for_output_->push_back(row_file))) {
              LOG_WARN("fail to push back row file", K(ret), KPC(row_file));
            } else {
              row_file = nullptr;
            }
          }
        } else {
          if (nullptr == chunk_data || nullptr == row_files_ptr) {
            chunk_data = OB_NEW(ObChunk, ObMemAttr("DDLRowChunk"));
            row_files_ptr = OB_NEW(ObArray<ObDDLRowFile *>, ObMemAttr("DDLRowFiles"));
            if (OB_UNLIKELY(nullptr == chunk_data || nullptr == row_files_ptr)) {
              ret = OB_ALLOCATE_MEMORY_FAILED;
              LOG_WARN("fail to allocate memory", K(ret), KP(chunk_data), KP(row_files_ptr));
            }
          }
          if (FAILEDx(row_files_ptr->push_back(row_file))) {
            LOG_WARN("fail to push back row file", K(ret), KPC(row_file));
          } else {
            row_file = nullptr;
          }
        }
      }
    }

    if (OB_SUCC(ret)) {
      output_chunk.tablet_id_ = tablet_id_;
      output_chunk.slice_idx_ = slice_idx_;
      output_chunk.is_slice_end_ = is_slice_end;
      if (is_generation_sync_output_ &&
          nullptr != sync_chunk_data_ &&
          nullptr != row_file_arr_for_output_ &&
          !row_file_arr_for_output_->empty()) { // has ouput data
        sync_chunk_data_->row_file_arr_ = row_file_arr_for_output_;
        sync_chunk_data_->type_ = ObChunk::DDL_ROW_TMP_FILES;
        output_chunk.chunk_data_ = sync_chunk_data_;
      } else if (!is_generation_sync_output_ &&
                 nullptr != chunk_data &&
                 nullptr != row_files_ptr &&
                 !row_files_ptr->empty()) { // has ouput data
        chunk_data->row_file_arr_ = row_files_ptr;
        chunk_data->type_ = ObChunk::DDL_ROW_TMP_FILES;
        output_chunk.chunk_data_ = chunk_data;
      }
    } else { // fail to generate output chunk
      if (nullptr != chunk_data) {
        chunk_data->~ObChunk();
        ob_free(chunk_data);
        chunk_data = nullptr;
      }
      if (nullptr != row_files_ptr) {
        for (int64_t i = 0; i < row_files_ptr->count(); ++i) {
          ObDDLRowFile *&row_file = row_files_ptr->at(i);
          if (OB_LIKELY(nullptr != row_file)) {
            row_file->~ObDDLRowFile();
            ob_free(row_file);
            row_file = nullptr;
          }
        }
        row_files_ptr->~ObArray<ObDDLRowFile *>();
        ob_free(row_files_ptr);
        row_files_ptr = nullptr;
      }
    }
  }
  return ret;
}

} // end namespace storage
} // end namespace oceanbase
