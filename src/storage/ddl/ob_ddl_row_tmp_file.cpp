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
#include "storage/blocksstable/ob_datum_row.h"
#include "storage/ddl/ob_pipeline.h"
#include "storage/ddl/ob_ddl_tablet_context.h"

namespace oceanbase
{
namespace storage
{
/**
* -----------------------------------ObDDLRowFile-----------------------------------
*/
int ObDDLRowFile::open(const ObIArray<ObColumnSchemaItem> &all_column_schema_its,
                      const ObTabletID &tablet_id,
                      const int64_t slice_idx,
                      const int64_t max_batch_size,
                      query::ObISpillBatchSpoolFactory &spool_factory,
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
    ObArray<query::ObSpillColumnDesc> columns;
    query::ObSpillBatchSpoolOptions options;
    if (OB_FAIL(columns.prepare_allocate(all_column_schema_its.count()))) {
      LOG_WARN("fail to prepare spill column descriptions", K(ret),
          K(all_column_schema_its.count()));
    } else if (OB_FAIL(bdrs_.vectors_.prepare_allocate(all_column_schema_its.count()))) {
      LOG_WARN("fail to prepare output vector slots", K(ret),
          K(all_column_schema_its.count()));
    } else {
      for (int64_t i = 0; i < all_column_schema_its.count(); ++i) {
        const ObColumnSchemaItem &column = all_column_schema_its.at(i);
        query::ObSpillColumnDesc &desc = columns.at(i);
        desc.type_ = column.col_type_.get_type();
        desc.scale_ = column.col_type_.get_scale();
        desc.precision_ = column.col_accuracy_.get_precision();
        bdrs_.vectors_.at(i) = nullptr;
      }
      options.max_batch_size_ = max_batch_size;
      options.resident_memory_limit_ = memory_limit;
      options.rotation_threshold_ = memory_limit;
      options.dir_id_ = dir_id;
      options.compressor_type_ = NONE_COMPRESSOR;
      options.async_read_ = true;
      if (OB_FAIL(spool_factory.create(columns, options, spool_))) {
        LOG_WARN("fail to create spill batch spool", K(ret), K(options.max_batch_size_),
            K(options.resident_memory_limit_), K(options.dir_id_));
      }
    }
    if (OB_SUCC(ret)) {
      tablet_id_ = tablet_id;
      slice_idx_ = slice_idx;
      column_count_ = all_column_schema_its.count();
      spool_factory_ = &spool_factory;
      bdrs_.row_flag_.set_flag(blocksstable::ObDmlFlag::DF_INSERT);
      is_opened_ = true;
    } else if (OB_NOT_NULL(spool_)) {
      spool_factory.destroy(spool_);
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
    if (OB_NOT_NULL(spool_factory_)) {
      spool_factory_->destroy(spool_);
    }
    is_opened_ = false;
    column_count_ = 0;
    spool_factory_ = nullptr;
    spool_ = nullptr;
    rotation_recommended_ = false;
    bdrs_.reset();
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
    query::ObSpillBatchAppendResult result;
    const query::ObSpillBatchView batch(bdrs.vectors_, bdrs.row_count_);
    if (OB_FAIL(spool_->append_batch(batch, result))) {
      LOG_WARN("fail to append spill batch", K(ret), K(bdrs.row_count_));
    } else {
      rotation_recommended_ = result.rotation_recommended_;
    }
  }
  return ret;
}

int ObDDLRowFile::seal()
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(!is_opened_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("the ObDDLRowFile is not opened", K(ret));
  } else if (OB_FAIL(spool_->seal())) {
    LOG_WARN("fail to seal spill batch spool", K(ret));
  } else {
    rotation_recommended_ = false;
  }
  return ret;
}

int ObDDLRowFile::get_next_batch(blocksstable::ObBatchDatumRows *&bdrs)
{
  int ret = OB_SUCCESS;
  query::ObSpillBatchView batch;
  bdrs = nullptr;
  if (OB_UNLIKELY(!is_opened_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("the ObDDLRowFile is not opened", K(ret));
  } else if (OB_FAIL(spool_->next_batch(batch))) {
    if (OB_ITER_END != ret) {
      LOG_WARN("fail to get next batch", KR(ret));
    }
  } else if (OB_ISNULL(batch.vectors_) || batch.row_count_ <= 0 ||
             batch.vectors_->count() != column_count_) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("spill spool returned an invalid batch", K(ret), KP(batch.vectors_),
        K(batch.row_count_), K(column_count_));
  } else {
    for (int64_t i = 0; i < column_count_; ++i) {
      bdrs_.vectors_.at(i) = batch.vectors_->at(i);
    }
    bdrs_.row_count_ = batch.row_count_;
    bdrs = &bdrs_;
  }
  return ret;
}

query::ObSpillBatchSpoolStats ObDDLRowFile::get_stats() const
{
  return OB_NOT_NULL(spool_) ? spool_->get_stats() :
      query::ObSpillBatchSpoolStats();
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
  spool_factory_ = nullptr;
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
    const bool is_generation_sync_output,
    query::ObISpillBatchSpoolFactory &spool_factory)
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
    spool_factory_ = &spool_factory;
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
                                        *spool_factory_,
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
          (row_file->should_rotate() || is_slice_end)) {
        if (OB_FAIL(row_file->seal())) {
          LOG_WARN("fail to seal row file", K(ret), KPC(row_file));
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
