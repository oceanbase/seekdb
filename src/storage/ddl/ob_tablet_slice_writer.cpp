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
#include "storage/ddl/ob_tablet_slice_writer.h"
#include "storage/ddl/ob_ddl_storage_util.h"
#include "storage/ddl/ob_ddl_tablet_context.h"
#include "storage/ddl/ob_ddl_independent_dag.h"
#include "sql/engine/pdml/static/ob_px_sstable_insert_op.h"
#include "storage/ddl/ob_direct_load_struct.h"
#include "storage/ddl/ob_ddl_macro_block_writer.h"
#include "storage/ddl/ob_lob_macro_block_writer.h"
#include "storage/ddl/ob_ddl_vector_utils.h"
#include "sql/engine/ob_batch_rows.h"
#include "storage/ob_tablet_autoincrement_service.h"

#define USING_LOG_PREFIX STORAGE

using namespace oceanbase;
using namespace oceanbase::common;
using namespace oceanbase::share;
using namespace oceanbase::storage;
using namespace oceanbase::blocksstable;
using namespace oceanbase::sql;

ObTabletSliceWriter::ObTabletSliceWriter()
  : is_inited_(false), allocator_(ObMemAttr("ddl_mb_writer")), slice_idx_(-1), storage_column_count_(0), macro_block_writer_(nullptr), row_count_(0), unique_index_id_(0)
{

}

ObTabletSliceWriter::~ObTabletSliceWriter()
{
  reset();
}

void ObTabletSliceWriter::reset()
{
  is_inited_ = false;
  tablet_id_.reset();
  slice_idx_ = -1;
  storage_column_count_ = 0;
  OB_DELETEx(ObDDLMacroBlockWriter, &allocator_, macro_block_writer_);
  row_count_ = 0;
  unique_index_id_ = 0;
  allocator_.reset();
}

int ObTabletSliceWriter::init(const ObWriteMacroParam &param)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(is_inited_)) {
    ret = OB_INIT_TWICE;
    LOG_WARN("init twice", K(ret), K(is_inited_));
  } else if (OB_UNLIKELY(!param.is_valid())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", K(ret), K(param));
  } else {
    tablet_id_ = param.tablet_id_;
    slice_idx_ = param.slice_idx_;
    storage_column_count_ = param.ddl_table_schema_.column_items_.count();
    if (is_full_direct_load(param.direct_load_type_) && param.ddl_table_schema_.table_item_.is_unique_index_) {
      unique_index_id_ = param.ddl_table_schema_.table_id_;
    }
    if (OB_FAIL(ObDDLUtil::init_macro_block_writer(param, allocator_, macro_block_writer_))) {
      LOG_WARN("init macro block writer failed", K(ret));
    } else {
      row_count_ = 0;
      is_inited_ = true;
      FLOG_INFO("tablet slice writer init finished", K(ret), KPC(this), K(param.ddl_table_schema_.column_items_));
    }
  }
  return ret;
}

int ObTabletSliceWriter::append_row(const blocksstable::ObDatumRow &row)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(!is_inited_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", K(ret));
  } else if (OB_UNLIKELY(!row.is_valid() || row.get_column_count() != storage_column_count_)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", K(ret), K(storage_column_count_), K(row));
  } else {
    if (OB_FAIL(macro_block_writer_->append_row(row))) {
      if (OB_ERR_PRIMARY_KEY_DUPLICATE == ret && unique_index_id_ > 0) {
        int report_ret_code = OB_SUCCESS;
        LOG_USER_ERROR(OB_ERR_PRIMARY_KEY_DUPLICATE, "", static_cast<int>(sizeof("UNIQUE IDX") - 1), "UNIQUE IDX");
        (void) ObDirectLoadSliceWriter::report_unique_key_dumplicated(ret, unique_index_id_, row, tablet_id_, report_ret_code); // ignore ret
        if (OB_ERR_DUPLICATED_UNIQUE_KEY == report_ret_code) {
          // Report direct-load unique index conflicts with the dedicated duplicate-key code.
          ret = OB_ERR_DUPLICATED_UNIQUE_KEY;
        }
      } else {
        LOG_WARN("fail to append row", K(ret), K(row), KPC(macro_block_writer_));
      }
    }
    if (OB_SUCC(ret)) {
      ++row_count_;
    }
  }
  return ret;
}

int ObTabletSliceWriter::append_batch(const blocksstable::ObBatchDatumRows &batch_rows)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(!is_inited_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", K(ret));
  } else if (OB_UNLIKELY(batch_rows.get_column_count() != storage_column_count_ || batch_rows.row_count_ <= 0)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", K(ret), K(storage_column_count_), K(batch_rows));
  } else {
    if (OB_FAIL(macro_block_writer_->append_batch(batch_rows))) {
      if (OB_ERR_PRIMARY_KEY_DUPLICATE == ret && unique_index_id_ > 0) {
        int report_ret_code = OB_SUCCESS;
        LOG_USER_ERROR(OB_ERR_PRIMARY_KEY_DUPLICATE, "", static_cast<int>(sizeof("UNIQUE IDX") - 1), "UNIQUE IDX");
        (void) ObDirectLoadSliceWriter::report_unique_key_dumplicated(ret, unique_index_id_, batch_rows, tablet_id_, report_ret_code); // ignore ret
        if (OB_ERR_DUPLICATED_UNIQUE_KEY == report_ret_code) {
          // Report direct-load unique index conflicts with the dedicated duplicate-key code.
          ret = OB_ERR_DUPLICATED_UNIQUE_KEY;
        }
      } else {
        LOG_WARN("fail to append batch", K(ret), K(batch_rows), KPC(macro_block_writer_));
      }
    }
    if (OB_SUCC(ret)) {
      row_count_ += batch_rows.row_count_;
    }
  }
  return ret;
}

int ObTabletSliceWriter::close()
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(!is_inited_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", K(ret), K(is_inited_));
  }
  if (OB_SUCC(ret) && OB_FAIL(macro_block_writer_->close())) {
    LOG_WARN("fail to close macro block writer", K(ret), KPC(macro_block_writer_));
  }
  FLOG_INFO("tablet slice writer close finished", K(ret), KPC(this));
  return ret;
}

/**
 **************************************************    ObRsSliceWriter    ************************************************
 */

ObRsSliceWriter::ObRsSliceWriter()
  : is_inited_(false), rowkey_column_count_(0), sql_column_count_(0), lob_writer_(nullptr), storage_slice_writer_(nullptr),
    row_arena_(ObMemAttr("slice_row_arena")), current_row_()
{

}

ObRsSliceWriter::~ObRsSliceWriter()
{
  is_inited_ = false;
  tablet_id_.reset();
  slice_idx_ = -1;
  current_row_.reset();
  rowkey_column_count_ = 0;
  sql_column_count_ = 0;
  free_tablet_writer();
  free_lob_writer();
  current_row_.reset();
  row_arena_.reset();
}

int ObRsSliceWriter::init(const ObWriteMacroParam &write_param)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(is_inited_)) {
    ret = OB_INIT_TWICE;
    LOG_WARN("init twice", K(ret), K(is_inited_));
  } else if (OB_UNLIKELY(!write_param.is_valid())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", K(ret), K(write_param));
  } else {
    writer_param_ = write_param;
    tablet_id_ = writer_param_.tablet_id_;
    slice_idx_ = writer_param_.slice_idx_;
    rowkey_column_count_ = writer_param_.ddl_table_schema_.table_item_.rowkey_column_num_;
    sql_column_count_ = writer_param_.ddl_table_schema_.column_items_.count() - ObMultiVersionRowkeyHelpper::get_extra_rowkey_col_cnt();
    const int64_t request_column_count = writer_param_.ddl_table_schema_.column_items_.count();
    if (OB_ISNULL(storage_slice_writer_ = OB_NEW(ObTabletSliceWriter, ObMemAttr("stor_slice_wrt")))) {
      ret = OB_ALLOCATE_MEMORY_FAILED;
      LOG_WARN("allocate memory failed", K(ret));
    } else if (OB_FAIL(static_cast<ObTabletSliceWriter *>(storage_slice_writer_)->init(writer_param_))) {
      LOG_WARN("init storage slice writer failed", K(ret), K(writer_param_));
    }
    if (FAILEDx(ObDDLStorageUtil::init_datum_row_with_snapshot(request_column_count, rowkey_column_count_, writer_param_.snapshot_version_, current_row_))) {
      LOG_WARN("init datum row failed", K(ret), K(request_column_count), K(rowkey_column_count_), K(writer_param_));
    }
    if (OB_SUCC(ret)) {
      is_inited_ = true;
    }
  }
  return ret;
}

int ObRsSliceWriter::append_current_row(const ObIArray<ObDatum *> &datums)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(!is_inited_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", K(ret));
  } else if (OB_UNLIKELY(datums.count() != sql_column_count_)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", K(ret), K(datums.count()), K(sql_column_count_));
  } else if (OB_FAIL(build_multi_version_row(datums))) {
    LOG_WARN("build multi version row failed", K(ret));
  } else if (FALSE_IT(row_arena_.reuse())) {
  } else if (OB_FAIL(ObDDLUtil::convert_to_storage_row(tablet_id_, slice_idx_, writer_param_, lob_writer_, row_arena_, current_row_))) {
    LOG_WARN("convert to storage row failed", K(ret), K(tablet_id_), K(slice_idx_), KP(lob_writer_));
  } else if (OB_FAIL(storage_slice_writer_->append_row(current_row_))) {
    LOG_WARN("append row failed", K(ret));
  }
  return ret;
}

int ObRsSliceWriter::close()
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(!is_inited_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", K(ret), K(is_inited_));
  } else if (OB_FAIL(storage_slice_writer_->close())) {
    LOG_WARN("close storage slice writer failed", K(ret));
  } else if (nullptr != lob_writer_) {
    if (OB_FAIL(lob_writer_->close())) {
      LOG_WARN("lob writer close failed", K(ret));
    }
  }
  FLOG_INFO("row slice writer closed", K(ret), KPC(this));
  return ret;
}

int ObRsSliceWriter::build_multi_version_row(const ObIArray<ObDatum *> &sql_datums)
{
  int ret = OB_SUCCESS;
  const int64_t extra_rowkey_column_count = ObMultiVersionRowkeyHelpper::get_extra_rowkey_col_cnt();
  for (int64_t i = 0; OB_SUCC(ret) && i < sql_datums.count(); i++) {
    ObDatum *datum = sql_datums.at(i);
    if (OB_ISNULL(datum)) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("expr is NULL", K(ret), K(i));
    } else {
      const int64_t store_position = i < rowkey_column_count_ ? i : i + extra_rowkey_column_count;
      current_row_.storage_datums_[store_position].shallow_copy_from_datum(*datum);
    }
  }
  return ret;
}

void ObRsSliceWriter::free_tablet_writer()
{
  if (nullptr != storage_slice_writer_) {
    storage_slice_writer_->~ObITabletSliceWriter();
    ob_free(storage_slice_writer_);
    storage_slice_writer_ = nullptr;
  }
}

void ObRsSliceWriter::free_lob_writer()
{
  if (nullptr != lob_writer_) {
    lob_writer_->~ObLobMacroBlockWriter();
    ob_free(lob_writer_);
    lob_writer_ = nullptr;
  }
}

int ObRsSliceWriter::switch_next_slice(ObHeapSliceInfo &heap_info)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(!is_inited_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", K(ret), K(is_inited_));
  } else if (OB_UNLIKELY(!heap_info.is_valid())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", K(ret), K(heap_info));
  } else if (OB_FAIL(close())) {
    LOG_WARN("close tablet writer failed", K(ret), K(tablet_id_), K(slice_idx_));
  } else {
    slice_idx_ += heap_info.get_parallel_count();
    free_lob_writer();
    writer_param_.slice_idx_ = slice_idx_;
    storage_slice_writer_->reset();
    if (OB_FAIL(heap_info.init_autoinc_interval(tablet_id_, slice_idx_))) {
      LOG_WARN("init autoinc interval failed", K(ret), K(tablet_id_), K(slice_idx_));
    } else if (OB_FAIL(storage_slice_writer_->init(writer_param_))) {
      LOG_WARN("init storage slice writer failed", K(ret));
    }
  }
  return ret;
}

/**
 **************************************************    ObHeapRsSliceWriter     ************************************************
 */

int ObHeapRsSliceWriter::init(
    const ObWriteMacroParam &write_param,
    const int64_t parallel_count,
    const int64_t autoinc_column_idx,
    const bool use_idempotent_autoinc)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(is_inited_)) {
    ret = OB_INIT_TWICE;
    LOG_WARN("init twice", K(ret), K(is_inited_));
  } else if (OB_UNLIKELY(!write_param.is_valid() || parallel_count <= 0 || autoinc_column_idx < 0)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", K(ret), K(write_param), K(parallel_count), K(autoinc_column_idx));
  } else {
    if (OB_FAIL(ObRsSliceWriter::init(write_param))) {
      LOG_WARN("init row slice writer failed", K(ret), K(write_param));
    } else {
      heap_info_.set_parallel_count(parallel_count);
      heap_info_.set_autoinc_column_idx(autoinc_column_idx);
      heap_info_.set_use_idempotent_autoinc(use_idempotent_autoinc);
      if (OB_FAIL(heap_info_.init_autoinc_interval(tablet_id_, slice_idx_)))   {
        LOG_WARN("init autoinc interval failed", K(ret), K(tablet_id_), K(slice_idx_));
      }
    }
  }
  return ret;
}

int ObHeapSliceInfo::init_autoinc_interval(const ObTabletID &tablet_id, const int64_t slice_idx)
{
  int ret = OB_SUCCESS;
  autoinc_interval_.reset();
  const int64_t AUTO_INC_CACHE_INTERVAL = 500L * 10000L;
  autoinc_interval_.tablet_id_ = tablet_id;
  if (use_idempotent_autoinc_) {
    const int64_t pk_start = slice_idx * AUTO_INC_CACHE_INTERVAL;
    autoinc_interval_.set(pk_start, pk_start + AUTO_INC_CACHE_INTERVAL - 1);
  } else {
    ObTabletAutoincrementService &auto_inc = ObTabletAutoincrementService::get_instance();
    autoinc_interval_.cache_size_ = AUTO_INC_CACHE_INTERVAL;
    if (OB_FAIL(auto_inc.get_tablet_cache_interval(autoinc_interval_))) {
      LOG_WARN("autoinc service get tablet cache failed", K(ret));
    }
  }
  return ret;
}

int ObHeapRsSliceWriter::append_current_row(const ObIArray<ObDatum *> &datums)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(!is_inited_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", K(ret));
  } else {
  // set autoinc val
    uint64_t current_pk = 0;
    if (OB_UNLIKELY(heap_info_.remain_count() < 1) && OB_FAIL(switch_next_slice(heap_info_))) {
      LOG_WARN("switch next slice failed", K(ret));
    } else if (OB_FAIL(heap_info_.get_next(current_pk))) {
      LOG_WARN("get next hidden pk failed", K(ret), K(heap_info_));
    } else {
      ObDatum *autoinc_datum = datums.at(heap_info_.get_autoinc_column_idx());
      autoinc_datum->set_uint(current_pk);
    }
  }
  if (OB_SUCC(ret)) {
    if (OB_FAIL(ObRsSliceWriter::append_current_row(datums))) {
      LOG_WARN("append row failed", K(ret));
    }
  }
  return ret;
}

int ObHeapRsSliceWriter::close()
{
  int ret = OB_SUCCESS;
  uint64_t last_autoinc_val = 0;
  if (OB_FAIL(ObRsSliceWriter::close())) {
    LOG_WARN("close row slice writer failed", K(ret));
  } else if (OB_FAIL(heap_info_.get_last_autoinc_val(last_autoinc_val))) {
    LOG_WARN("get last autoinc val failed", K(ret));
  } else if (OB_FAIL(ObDDLUtil::set_tablet_autoinc_seq(tablet_id_, last_autoinc_val))) {
    LOG_WARN("set tablet autoinc seq failed", K(ret));
  }
  return ret;
}

/**
* -----------------------------------ObTabletSliceBufferTempFileWriter::ObRowBuffer-----------------------------------
*/
int ObTabletSliceBufferTempFileWriter::ObDDLRowBuffer::init(
    const common::ObIArray<ObColumnSchemaItem> &column_schemas,
    const int64_t max_batch_size)
{
  int ret = OB_SUCCESS;
  ObDDLRowFlag row_flag;
  if (OB_UNLIKELY(is_inited_)) {
    ret = OB_INIT_TWICE;
    LOG_WARN("the ObDDLRowBuffer has been initialized", K(ret));
  } else if (OB_UNLIKELY(column_schemas.empty() || max_batch_size <= 0)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("the are invalid argument", K(ret), K(column_schemas), K(max_batch_size));
  } else if (OB_FAIL(buffer_.init(column_schemas, max_batch_size, row_flag))) {
    LOG_WARN("fail to initialize ddl row buffer",
        K(ret), K(column_schemas), K(max_batch_size), K(row_flag));
  } else {
    const ObIArray<ObDDLVector *> &vectors = buffer_.get_vectors();
    for (int64_t i = 0; OB_SUCC(ret) && i < vectors.count(); ++i) {
      if (OB_FAIL(bdrs_.vectors_.push_back(vectors.at(i)->get_vector()))) {
        LOG_WARN("fail to push back vector", K(ret));
      }
    }
    if (OB_SUCC(ret)) {
      is_inited_ = true;
    }
  }
  return ret;
}

void ObTabletSliceBufferTempFileWriter::ObDDLRowBuffer::reset()
{
  is_inited_ = false;
  buffer_.reset();
  bdrs_.reset();  
}

void ObTabletSliceBufferTempFileWriter::ObDDLRowBuffer::reuse()
{
  buffer_.reuse();
}

int ObTabletSliceBufferTempFileWriter::ObDDLRowBuffer::append_row(
    const blocksstable::ObDatumRow &datum_row)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(!is_inited_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("the ObDDLRowBuffer is not initialized");
  } else if (OB_UNLIKELY(!datum_row.is_valid())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("the datum row is invalid", K(ret), K(datum_row));
  } else if (OB_FAIL(buffer_.append_row(datum_row))) {
    LOG_WARN("fail to append row", K(ret), K(datum_row));
  }
  return ret;
}

int ObTabletSliceBufferTempFileWriter::ObDDLRowBuffer::get_batch_datum_rows(
    blocksstable::ObBatchDatumRows *&bdrs)
{
  int ret = OB_SUCCESS;
  bdrs = nullptr;
  if (OB_UNLIKELY(!is_inited_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("the ObDDLRowBuffer is not initialized");
  } else {
    bdrs_.row_count_ = buffer_.size();
    bdrs = &bdrs_;
  }
  return ret;
}

/**
* -----------------------------------ObTabletSliceBufferTempFileWriter-----------------------------------
*/
int ObTabletSliceBufferTempFileWriter::init(const ObWriteMacroParam &param)
{
  int ret = OB_SUCCESS;
  ObDDLIndependentDag *ddl_dag = param.ddl_dag_;
  if (OB_UNLIKELY(is_inited_)) {
    ret = OB_INIT_TWICE;
    LOG_WARN("the ObTabletSliceBufferTempFileWriter has been initialized", K(ret));
  } else if (OB_FAIL(ObTabletSliceTempFileWriter::init(param))) {
    LOG_WARN("fail to initialize the ObTabletSliceTempFileWriter", K(ret), K(param));
  } else if (OB_UNLIKELY(nullptr == ddl_dag)) {
    ret = OB_ERR_SYS;
    LOG_WARN("the ddl dag is null", K(ret));
  } else if (OB_FAIL(buffer_.init(ddl_dag->get_ddl_table_schema().column_items_))) {
    LOG_WARN("fail to initialize the ddl row buffer", K(ret));
  } else {
    is_inited_ = true;
  }
  return ret;
}

int ObTabletSliceBufferTempFileWriter::append_row(const blocksstable::ObDatumRow &row)
{
  int ret = OB_SUCCESS;
   if (OB_UNLIKELY(!is_inited_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("the ObTabletSliceBufferTempFileWriter is not initialized");
  } else if (OB_UNLIKELY(!row.is_valid())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("the row is invalid", K(ret), K(row));
  } else if (OB_FAIL(buffer_.append_row(row))) {
    LOG_WARN("fail to append current row", K(ret), K(row));
  } else if (buffer_.is_full()) {
    ObBatchDatumRows *bdrs = nullptr;
    if (OB_FAIL(buffer_.get_batch_datum_rows(bdrs))) {
      LOG_WARN("fail to get batch datum rows", K(ret));
    } else if (OB_FAIL(ObTabletSliceTempFileWriter::append_batch(*bdrs))) {
      LOG_WARN("fail to append batch datum rows", K(ret), K(*bdrs));
    } else {
      buffer_.reuse();
    }
  }
  return ret;
}

int ObTabletSliceBufferTempFileWriter::close()
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(!is_inited_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("the ObTabletSliceBufferTempFileWriter is not initialized");
  } else if (buffer_.size() > 0) {
    ObBatchDatumRows *bdrs = nullptr;
    if (OB_FAIL(buffer_.get_batch_datum_rows(bdrs))) {
      LOG_WARN("fail to get batch datum rows", K(ret));
    } else if (OB_FAIL(ObTabletSliceTempFileWriter::append_batch(*bdrs))) {
      LOG_WARN("fail to append batch datum rows", K(ret), K(*bdrs));
    } else {
      buffer_.reuse();
    }
  }
  if (FAILEDx(ObTabletSliceTempFileWriter::close())) {
    LOG_WARN("fail to close temp file writer", K(ret));
  }
  return ret;
}

void ObTabletSliceBufferTempFileWriter::reset()
{
  is_inited_ = false;
  ObTabletSliceTempFileWriter::reset();
  buffer_.reset();
}

/**
 ***************************************************      ObBatchSliceWriter     **************************************************
 */

ObBatchSliceWriter::ObBatchSliceWriter()
  : arena_(ObMemAttr("ddl_batch_wrt")),
    need_convert_storage_value_(false), direct_write_macro_block_(false), row_buffer_size_(256),
    need_check_rowkey_order_(true), rowkey_arena_(ObMemAttr("ddl_ck_rowkey"))
{

}

ObBatchSliceWriter::~ObBatchSliceWriter()
{
  if (last_key_.is_valid()) {
    for (int64_t i = 0; i < last_key_.datum_cnt_; ++i) {
      ObStorageDatum &datum = last_key_.datums_[i];
      datum.~ObStorageDatum();
    }
    ob_free(last_key_.datums_);
    last_key_.reset();
  }
}

int ObBatchSliceWriter::init(
    const ObWriteMacroParam &write_param,
    const bool direct_write_macro_block,
    const bool is_append_batch,
    const int64_t max_batch_size)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(is_inited_)) {
    ret = OB_INIT_TWICE;
    LOG_WARN("init twice", K(ret), K(is_inited_));
  } else if (OB_UNLIKELY(!write_param.is_valid())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", K(ret), K(write_param));
  } else {
    writer_param_ = write_param;
    writer_param_.max_batch_size_ = OB_MAX(max_batch_size, row_buffer_size_);
    tablet_id_ = writer_param_.tablet_id_;
    slice_idx_ = writer_param_.slice_idx_;
    rowkey_column_count_ = writer_param_.ddl_table_schema_.table_item_.rowkey_column_num_;
    sql_column_count_ = writer_param_.ddl_table_schema_.column_items_.count() - ObMultiVersionRowkeyHelpper::get_extra_rowkey_col_cnt();
    need_convert_storage_value_ = writer_param_.ddl_table_schema_.lob_column_idxs_.count() > 0 || writer_param_.ddl_table_schema_.reshape_column_idxs_.count() > 0;
    direct_write_macro_block_ = direct_write_macro_block;
    need_check_rowkey_order_ = (need_check_rowkey_order_ && !(writer_param_.ddl_table_schema_.table_item_.vec_dim_ > 0)); // vector index not check order
    const int64_t request_column_count = writer_param_.ddl_table_schema_.column_items_.count();
    if (direct_write_macro_block_) {
      if (OB_ISNULL(storage_slice_writer_ = OB_NEW(ObTabletSliceWriter, ObMemAttr("slice_mb_writer")))) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_WARN("allocate memory failed", K(ret));
      } else if (OB_FAIL(static_cast<ObTabletSliceWriter *>(storage_slice_writer_)->init(writer_param_))) {
        LOG_WARN("init slice macro block writer failed", K(ret), K(writer_param_));
      }
    } else {
      if (OB_ISNULL(storage_slice_writer_ = OB_NEW(ObTabletSliceTempFileWriter, ObMemAttr("slice_tmp_writr")))) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_WARN("allocate memory failed", K(ret));
      } else if (OB_FAIL(static_cast<ObTabletSliceTempFileWriter *>(storage_slice_writer_)->init(writer_param_))) {
        LOG_WARN("init slice temp file writer failed", K(ret), K(tablet_id_), K(slice_idx_));
      }
    }
    if (OB_FAIL(ret)) {
    } else if (OB_FAIL(init_storage_batch_rows())) {
      LOG_WARN("init storage batch rows failed", K(ret));
    } else if (row_buffer_size_ > 0 && OB_FAIL(init_row_buffer(row_buffer_size_))) {
      LOG_WARN("init row buffer failed", K(ret));
    } else if (need_check_rowkey_order_ || (!is_append_batch && need_convert_storage_value_)) {
      if (OB_FAIL(ObDDLStorageUtil::init_datum_row_with_snapshot(
              request_column_count, rowkey_column_count_, writer_param_.snapshot_version_, current_row_))) {
        LOG_WARN("init datum row failed", K(ret), K(request_column_count), K(rowkey_column_count_), K(writer_param_));
      }
    }
    if (OB_SUCC(ret)) {
      is_inited_ = true;
    }
  }
  return ret;
}

// row -> buffer -> macro block
int ObBatchSliceWriter::append_current_row(const ObIArray<ObDatum *> &datums)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(!is_inited_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", K(ret));
  } else if (OB_UNLIKELY(datums.count() != sql_column_count_)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", K(ret), K(datums.count()), K(sql_column_count_));
  } else if (!need_convert_storage_value_) {
    if (OB_FAIL(row_buffer_.append_row(datums))) {
      LOG_WARN("append sql datums failed", K(ret), K(datums));
    }
  } else {
    row_arena_.reuse();
    ObArray<ObDatum *> storage_datums;
    if (OB_FAIL(build_multi_version_row(datums))) {
      LOG_WARN("build multi version row failed", K(ret));
    } else if (OB_FAIL(ObDDLUtil::convert_to_storage_row(tablet_id_, slice_idx_, writer_param_, lob_writer_, row_arena_, current_row_))) {
      LOG_WARN("convert to storage row failed", K(ret), K(tablet_id_), K(slice_idx_), KP(lob_writer_));
    } else if (OB_FAIL(storage_datums.reserve(sql_column_count_))) {
      LOG_WARN("reserve row failed", K(ret));
    } else {
      const int64_t multi_version_col_cnt = ObMultiVersionRowkeyHelpper::get_extra_rowkey_col_cnt();
      for (int64_t i = 0; OB_SUCC(ret) && i < rowkey_column_count_; ++i) {
        if (OB_FAIL(storage_datums.push_back(&current_row_.storage_datums_[i]))) {
          LOG_WARN("push back datum failed", K(ret));
        }
      }
      for (int64_t i = rowkey_column_count_; OB_SUCC(ret) && i < sql_column_count_; ++i) {
        if (OB_FAIL(storage_datums.push_back(&current_row_.storage_datums_[i + multi_version_col_cnt]))) {
          LOG_WARN("push back datum failed", K(ret));
        }
      }
      if (OB_SUCC(ret)) {
        if (OB_FAIL(row_buffer_.append_row(storage_datums))) {
          LOG_WARN("append converted datums failed", K(ret));
        }
      }
    }
  }

  if (OB_SUCC(ret) && OB_UNLIKELY(row_buffer_.full())) {
    if (OB_FAIL(flush_row_buffer())) {
      LOG_WARN("flush row buffer failed", K(ret));
    }
  }
  return ret;

}

int ObBatchSliceWriter::append_current_batch(const ObIArray<ObIVector *> &vectors, share::ObBatchSelector &selector)
{
  int ret = OB_SUCCESS;
  const ObIArray<ObIVector *> *ready_vectors = nullptr;
  ObArray<ObIVector *> copied_vectors;
  if (OB_UNLIKELY(!is_inited_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", K(ret));
  } else if (OB_UNLIKELY(vectors.count() != sql_column_count_ || !selector.is_valid() || ObBatchSelector::CONTINIOUS_LENGTH != selector.get_type())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", K(ret), K(vectors.count()), K(sql_column_count_), K(selector));
  } else if (!need_convert_storage_value_) {
    ready_vectors = &vectors;
  } else {
    if (OB_FAIL(copied_vectors.assign(vectors))) {
      LOG_WARN("copy vector pointers failed", K(ret));
    } else if (OB_FAIL(convert_to_storage_vector(copied_vectors, selector))) {
      LOG_WARN("convert to storage vector failed", K(ret));
    } else {
      ready_vectors = &copied_vectors;
    }
  }
  if (OB_SUCC(ret) && OB_NOT_NULL(ready_vectors)) {
    int64_t remain_row_count = selector.size();
    int64_t current_offset = selector.get_offset();
    while (OB_SUCC(ret) && remain_row_count > 0) {
      const int64_t append_size = min(row_buffer_.remain_size(), remain_row_count);
      if (OB_FAIL(row_buffer_.append_batch(*ready_vectors, current_offset, append_size))) {
        LOG_WARN("append batch failed", K(ret), K(current_offset), K(append_size));
      } else if (row_buffer_.full() && OB_FAIL(flush_row_buffer())) {
        LOG_WARN("flush row buffer failed", K(ret));
      } else {
        remain_row_count -= append_size;
        current_offset += append_size;
      }
    }
  }
  return ret;
}

int ObBatchSliceWriter::close()
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(!is_inited_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", K(ret));
  } else if (OB_FAIL(flush_row_buffer())) {
    LOG_WARN("flush row buffer failed", K(ret));
  } else if (OB_FAIL(ObRsSliceWriter::close())) {
    LOG_WARN("close writer failed", K(ret));
  }
  return ret;
}

int ObBatchSliceWriter::convert_to_storage_vector(ObIArray<ObIVector *> &vectors, ObBatchSelector &selector)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(!is_inited_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", K(ret));
  } else if (OB_UNLIKELY(vectors.count() != sql_column_count_ || !selector.is_valid())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", K(ret), K(vectors.count()), K(sql_column_count_), K(selector));
  } else {
    row_arena_.reuse();
    const ObDDLTableSchema &ddl_table_schema = writer_param_.ddl_table_schema_;
    for (int64_t i = 0; OB_SUCC(ret) && i < ddl_table_schema.reshape_column_idxs_.count(); ++i) {
      const int64_t idx = ddl_table_schema.reshape_column_idxs_.at(i);
      const ObColumnSchemaItem &column_schema_item = ddl_table_schema.column_items_.at(idx);
      const int64_t sql_column_idx = idx < rowkey_column_count_ ? idx : idx - ObMultiVersionRowkeyHelpper::get_extra_rowkey_col_cnt();
      ObIVector *&cur_vector = vectors.at(sql_column_idx);
      selector.rescan();
      if (OB_FAIL(ObDDLVectorUtils::reshape_storage_vector(column_schema_item.col_type_,
                                                           column_schema_item.col_accuracy_,
                                                           row_arena_,
                                                           cur_vector,
                                                           selector))) {
        LOG_WARN("fail to reshape vector value", K(ret), K(column_schema_item), K(idx));
      }
    }
    ObArray<ObArray<std::pair<char **, uint32_t *>>> column_lob_cells;
    if (OB_SUCC(ret)) {
      if (OB_FAIL(column_lob_cells.prepare_allocate(ddl_table_schema.lob_column_idxs_.count()))) {
        LOG_WARN("reserve column lob cells failed", K(ret));
      }
    }
    for (int64_t i = 0; OB_SUCC(ret) && !ddl_table_schema.table_item_.is_skip_lob() && i < ddl_table_schema.lob_column_idxs_.count(); ++i) {
      const int64_t idx = ddl_table_schema.lob_column_idxs_.at(i);
      const ObColumnSchemaItem &column_schema_item = ddl_table_schema.column_items_.at(idx);
      const int64_t sql_column_idx = idx < rowkey_column_count_ ? idx : idx - ObMultiVersionRowkeyHelpper::get_extra_rowkey_col_cnt();
      ObIVector *&cur_vector = vectors.at(sql_column_idx);
      selector.rescan();
      if (OB_FAIL(ObDDLStorageUtil::handle_lob_column(tablet_id_,
                                               slice_idx_,
                                               writer_param_,
                                               true, // need_all_cells
                                               column_lob_cells.at(i),
                                               row_arena_,
                                               column_schema_item,
                                               selector,
                                               cur_vector))) {
        LOG_WARN("fail to write lob vector value", K(ret), K(column_schema_item), K(idx));
      }
    }
    int64_t row_count = -1;
    // check row count of each column
    for (int64_t i = 0; OB_SUCC(ret) && i < column_lob_cells.count(); ++i) {
      const ObArray<std::pair<char **, uint32_t *>> &cur_lob_cells = column_lob_cells.at(i);
      if (0 == i) {
        row_count = cur_lob_cells.count();
      } else if (row_count != cur_lob_cells.count()) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("row count different", K(ret), K(tablet_id_), K(slice_idx_), K(i), K(row_count), K(cur_lob_cells.count()));
      }
    }
    if (OB_SUCC(ret) && row_count > 0) {
      if (OB_FAIL(ObDDLUtil::prepare_lob_writer(tablet_id_, slice_idx_, writer_param_, lob_writer_))) {
        LOG_WARN("prepare lob writer failed", K(ret), K(tablet_id_), K(slice_idx_), K(writer_param_));
      } else if (OB_ISNULL(lob_writer_)) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("lob writer is null", K(ret), K(tablet_id_), K(slice_idx_), KP(lob_writer_));
      }
    }
    // for idempotence, must write lob cells row by row
    ObStorageDatum temp_datum;
    for (int64_t i = 0; OB_SUCC(ret) && i < row_count; ++i) {
      for (int64_t j = 0; OB_SUCC(ret) && j < column_lob_cells.count(); ++j) {
        std::pair<char **, uint32_t *> &cur_cell = column_lob_cells.at(j).at(i);
        if (OB_UNLIKELY(nullptr == cur_cell.first || nullptr == cur_cell.second)) {
          if (nullptr == cur_cell.first && nullptr == cur_cell.second) {
            // null for const vector, skip
          } else {
            ret = OB_ERR_UNEXPECTED;
            LOG_WARN("current cell is null", K(ret), K(tablet_id_), K(slice_idx_), K(i), K(j), KP(cur_cell.first), K(cur_cell.second));
          }
        } else {
          temp_datum.ptr_ = *cur_cell.first;
          temp_datum.pack_ = *cur_cell.second;
          const int64_t lob_column_idx = ddl_table_schema.lob_column_idxs_.at(j);
          const ObColumnSchemaItem &column_schema = ddl_table_schema.column_items_.at(lob_column_idx);
          if (temp_datum.is_null() || temp_datum.is_nop()) {
            // skip
          } else if (OB_FAIL(lob_writer_->write(column_schema, row_arena_, temp_datum))) {
            LOG_WARN("write lob cell failed", K(ret), K(tablet_id_), K(slice_idx_), K(i), K(j), K(temp_datum));
          } else {
            *cur_cell.first = const_cast<char *>(temp_datum.ptr_);
            *cur_cell.second = temp_datum.len_;
          }
        }
      }
    }
  }
  return ret;
}

int ObBatchSliceWriter::init_last_rowkey()
{
  int ret = OB_SUCCESS;
  const ObStorageSchema *storage_schema = nullptr;
  if (OB_UNLIKELY(last_key_.is_valid()
        || datum_utils_.is_valid()
        || rowkey_column_count_ <= 0
        || OB_ISNULL(storage_schema = writer_param_.tablet_param_.storage_schema_))) {
    ret = OB_ERR_SYS;
    LOG_WARN("invlaid rowkey or param", K(ret), K(last_key_), K(datum_utils_), K(rowkey_column_count_), KP(storage_schema), K(writer_param_));
  } else {
    ObArray<share::schema::ObColDesc> rowkey_column_descs;
    if (OB_FAIL(storage_schema->get_rowkey_column_ids(rowkey_column_descs))) {
      LOG_WARN("get column desc failed", K(ret));
    } else if (OB_FAIL(datum_utils_.init(rowkey_column_descs, rowkey_column_count_, arena_))) {
      LOG_WARN("init datum utils failed", K(ret), K(rowkey_column_count_), K(rowkey_column_descs));
    }
  }
  if (OB_SUCC(ret)) {
    void *buf = ob_malloc(sizeof(ObStorageDatum) * rowkey_column_count_, ObMemAttr("ddl_last_rk"));
    if (OB_ISNULL(buf)) {
      ret = OB_ALLOCATE_MEMORY_FAILED;
      LOG_WARN("allocate memory failed", K(ret), K(rowkey_column_count_));
    } else {
      ObStorageDatum *datums = new (buf) ObStorageDatum[rowkey_column_count_];
      if (OB_FAIL(last_key_.assign(datums, rowkey_column_count_))) {
        LOG_WARN("assign storage datum failed", K(ret));
        last_key_.reset();
        ob_free(buf);
      }
    }
  }
  return ret;
}

int ObBatchSliceWriter::check_order(const blocksstable::ObBatchDatumRows &batch_rows)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(!is_inited_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", K(ret), K(is_inited_));
  } else if (OB_UNLIKELY(batch_rows.row_count_ <= 0)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", K(ret), K(batch_rows));
  }
  ObDatumRowkey current_key;
  for (int64_t i = 0; OB_SUCC(ret) && i < batch_rows.row_count_; ++i) {
    if (OB_FAIL(batch_rows.to_datum_row(i, current_row_))) {
      LOG_WARN("to datum row failed", K(ret));
    } else if (OB_FAIL(current_key.assign(current_row_.storage_datums_, rowkey_column_count_))) {
      LOG_WARN("assign current key failed", K(ret));
    } else if (last_key_.is_valid()) {
      int cmp_ret = 0;
      if (OB_FAIL(current_key.compare(last_key_, datum_utils_, cmp_ret))) {
        LOG_WARN("compare rowkey failed", K(ret));
      } else if (OB_UNLIKELY(cmp_ret < 0)) {
        ret = OB_ROWKEY_ORDER_ERROR;
        LOG_ERROR("input rowkey is less then last rowkey", K(ret), K(current_key), K(last_key_), K(tablet_id_), K(slice_idx_), K(batch_rows));
      } else if (OB_UNLIKELY(0 == cmp_ret)) {
        ret = OB_ERR_PRIMARY_KEY_DUPLICATE;
        LOG_WARN("input rowkey is equal with last rowkey", K(ret), K(current_key), K(last_key_), K(tablet_id_), K(slice_idx_), K(batch_rows));

        if (OB_ERR_PRIMARY_KEY_DUPLICATE == ret && writer_param_.ddl_table_schema_.table_item_.is_unique_index_) {
          const uint64_t unique_index_id = writer_param_.ddl_table_schema_.table_id_;
          int report_ret_code = OB_SUCCESS;
          LOG_USER_ERROR(OB_ERR_PRIMARY_KEY_DUPLICATE, "", static_cast<int>(sizeof("UNIQUE IDX") - 1), "UNIQUE IDX");
          (void) ObDirectLoadSliceWriter::report_unique_key_dumplicated(ret, unique_index_id, batch_rows, tablet_id_, report_ret_code); // ignore ret
          if (OB_ERR_DUPLICATED_UNIQUE_KEY == report_ret_code) {
            // Report direct-load unique index conflicts with the dedicated duplicate-key code.
            ret = OB_ERR_DUPLICATED_UNIQUE_KEY;
          }
        }
      }
    } else {
      if (OB_FAIL(init_last_rowkey())) {
        LOG_WARN("init last rowkey failed", K(ret));
      }
    }
    if (OB_SUCC(ret)) {
      // shallow copy current key
      for (int64_t i = 0; OB_SUCC(ret) && i < rowkey_column_count_; ++i) {
        last_key_.datums_[i] = current_key.datums_[i];
      }
    }
  }

  if (OB_SUCC(ret) && last_key_.is_valid()) {
    // deep copy last rowkey
    rowkey_arena_.reuse();
    for (int64_t i = 0; OB_SUCC(ret) && i < rowkey_column_count_; ++i) {
      if (OB_FAIL(last_key_.datums_[i].deep_copy(current_key.datums_[i], rowkey_arena_))) {
        LOG_WARN("deep copy datum failed", K(ret), K(i), K(rowkey_column_count_));
      }
    }
  }

  return ret;
}

int ObBatchSliceWriter::init_storage_batch_rows()
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(!writer_param_.is_valid())) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("invalid write param", K(ret));
  } else {
    const int64_t multi_version_col_cnt = ObMultiVersionRowkeyHelpper::get_extra_rowkey_col_cnt();
    const int64_t snapshot_version = writer_param_.snapshot_version_;
    const int64_t storage_column_count = writer_param_.ddl_table_schema_.column_items_.count();
    ObIVector *snapshot_version_vector;
    ObIVector *sql_seq_vector;
    if (OB_FAIL(ObDDLVectorUtils::make_const_multi_version_vector(-snapshot_version, arena_, snapshot_version_vector))) {
      LOG_WARN("init const vector for snapshot version failed", K(ret), K(snapshot_version));
    } else if (OB_FAIL(ObDDLVectorUtils::make_const_multi_version_vector(0, arena_, sql_seq_vector))) {
      LOG_WARN("init const vector for sql sequence failed", K(ret));
    } else {
      buffer_batch_rows_.row_flag_ = ObDmlFlag::DF_INSERT;
      for (int64_t i = 0; OB_SUCC(ret) && i < rowkey_column_count_; ++i) {
        if (OB_FAIL(buffer_batch_rows_.vectors_.push_back(nullptr))) {
          LOG_WARN("push back vector failed", K(ret));
        }
      }
      if (OB_SUCC(ret)) {
        if (OB_FAIL(buffer_batch_rows_.vectors_.push_back(snapshot_version_vector))) {
          LOG_WARN("push back snapshot version vector failed", K(ret));
        } else if (OB_FAIL(buffer_batch_rows_.vectors_.push_back(sql_seq_vector))) {
          LOG_WARN("push back sql sequence vector failed", K(ret));
        }
      }
      for (int64_t i = rowkey_column_count_ + multi_version_col_cnt; OB_SUCC(ret) && i < storage_column_count; ++i) {
        if (OB_FAIL(buffer_batch_rows_.vectors_.push_back(nullptr))) {
          LOG_WARN("push back vector failed", K(ret));
        }
      }
    }
  }
  return ret;
}

int ObBatchSliceWriter::init_row_buffer(const int64_t buffer_row_count)
{
  int ret = OB_SUCCESS;
  if (row_buffer_.is_inited()) {
    // do nothing
  } else if (OB_UNLIKELY(buffer_row_count <= 0)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invlaid argument", K(ret), K(buffer_row_count));
  } else {
    const int64_t multi_version_col_cnt = ObMultiVersionRowkeyHelpper::get_extra_rowkey_col_cnt();
    if (OB_FAIL(ObDDLUtil::init_batch_rows(writer_param_.ddl_table_schema_, buffer_row_count, row_buffer_))) {
      LOG_WARN("init row buffer failed", K(ret));
    } else if (buffer_batch_rows_.vectors_.empty() && OB_FAIL(init_storage_batch_rows())) {
      LOG_WARN("init storage batch rows failed", K(ret));
    } else {
      const ObIArray<ObDDLVector *> &vectors = row_buffer_.get_vectors();
      for (int64_t i = 0; OB_SUCC(ret) && i < rowkey_column_count_; ++i) {
        buffer_batch_rows_.vectors_.at(i) = vectors.at(i)->get_vector();
      }
      for (int64_t i = rowkey_column_count_; OB_SUCC(ret) && i < vectors.count(); ++i) {
        buffer_batch_rows_.vectors_.at(i + multi_version_col_cnt) = vectors.at(i)->get_vector();
      }
    }
  }
  return ret;
}

int ObBatchSliceWriter::flush_row_buffer()
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(!is_inited_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", K(ret), K(is_inited_));
  } else if (OB_LIKELY(row_buffer_.size() > 0)) {
    buffer_batch_rows_.row_count_ = row_buffer_.size();
    const ObDDLTableSchema &ddl_table_schema = writer_param_.ddl_table_schema_;
    if (OB_FAIL(ObDDLStorageUtil::check_null_and_length(ddl_table_schema.table_item_.is_index_table_,
                                                 ddl_table_schema.table_item_.has_lob_rowkey_,
                                                 ddl_table_schema.table_item_.rowkey_column_num_,
                                                 buffer_batch_rows_))) {
      LOG_WARN("check null and length failed", K(ret));
    } else if (need_check_rowkey_order_ && OB_FAIL(check_order(buffer_batch_rows_))) {
      LOG_WARN("check order failed", K(ret));
    } else if (OB_FAIL(storage_slice_writer_->append_batch(buffer_batch_rows_))) {
      LOG_WARN("append batch failed", K(ret), K(buffer_batch_rows_));
    } else {
      row_buffer_.reuse();
    }
  }
  return ret;
}

/**
 ***************************************************      ObHeapBatchSliceWriter     **************************************************
 */
int ObHeapBatchSliceWriter::init(
    const ObWriteMacroParam &write_param,
    const int64_t parallel_count,
    const int64_t autoinc_column_idx,
    const bool direct_write_macro_block,
    const int64_t max_batch_size,
    const bool use_idempotent_autoinc)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(is_inited_)) {
    ret = OB_INIT_TWICE;
    LOG_WARN("init twice", K(ret), K(is_inited_));
  } else if (OB_UNLIKELY(!write_param.is_valid() || parallel_count <= 0 || autoinc_column_idx < 0 || max_batch_size <= 0)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", K(ret), K(write_param), K(parallel_count), K(autoinc_column_idx), K(max_batch_size));
  } else {
    need_check_rowkey_order_ = false;
    if (OB_FAIL(ObBatchSliceWriter::init(write_param, direct_write_macro_block, max_batch_size > 1/*is_append_batch*/, max_batch_size))) {
      LOG_WARN("init batch slice writer failed", K(ret), K(write_param));
    } else {
      heap_info_.set_parallel_count(parallel_count);
      heap_info_.set_autoinc_column_idx(autoinc_column_idx);
      heap_info_.set_use_idempotent_autoinc(use_idempotent_autoinc);
      if (OB_FAIL(heap_info_.init_autoinc_interval(tablet_id_, slice_idx_))) {
        LOG_WARN("init autoinc interval failed", K(ret), K(tablet_id_), K(slice_idx_));
      }
    }
  }
  return ret;
}

int ObHeapBatchSliceWriter::append_current_row(const ObIArray<ObDatum *> &datums)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(!is_inited_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", K(ret));
  } else {
  // set autoinc val
    uint64_t current_pk = 0;
    if (OB_UNLIKELY(heap_info_.remain_count() < 1) && OB_FAIL(switch_next_slice(heap_info_))) {
      LOG_WARN("switch next slice failed", K(ret));
    } else if (OB_FAIL(heap_info_.get_next(current_pk))) {
      LOG_WARN("get next hidden pk failed", K(ret), K(heap_info_));
    } else {
      ObDatum *autoinc_datum = datums.at(heap_info_.get_autoinc_column_idx());
      autoinc_datum->set_uint(current_pk);
    }
  }
  if (OB_SUCC(ret)) {
    if (OB_FAIL(ObBatchSliceWriter::append_current_row(datums))) {
      LOG_WARN("append row failed", K(ret));
    }
  }
  return ret;
}

int ObHeapBatchSliceWriter::append_current_batch(const ObIArray<ObIVector *> &vectors, share::ObBatchSelector &selector)
{
  int ret = OB_SUCCESS;
  const ObIArray<ObIVector *> *ready_vectors = &vectors;
  ObArray<ObIVector *> copied_vectors;
  if (OB_UNLIKELY(!is_inited_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", K(ret));
  } else if (OB_UNLIKELY(vectors.count() != sql_column_count_) || !selector.is_valid()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", K(ret), K(vectors.count()), K(sql_column_count_), K(selector));
  } else {
  // set autoinc val
    uint64_t current_pk = 0;
    ObIVector *autoinc_vector = vectors.at(heap_info_.get_autoinc_column_idx());
    if (OB_UNLIKELY(nullptr == autoinc_vector)) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("autoinc vector is not valid", K(ret), KPC(autoinc_vector));
    } else if (OB_UNLIKELY(heap_info_.remain_count() < selector.size()) && OB_FAIL(switch_next_slice(heap_info_))) {
      LOG_WARN("switch next slice failed", K(ret));
    }
    int64_t i = 0;
    while (OB_SUCC(ret) && OB_SUCC(selector.get_next(i))) {
      if (OB_FAIL(heap_info_.get_next(current_pk))) {
        LOG_WARN("get next hidden pk failed", K(ret), K(heap_info_));
      } else {
        autoinc_vector->set_uint(i, current_pk);
      }
    }
    if (OB_ITER_END == ret) {
      ret = OB_SUCCESS;
    }
  }
  if (OB_SUCC(ret) && need_convert_storage_value_) {
    selector.rescan();
    if (OB_FAIL(copied_vectors.assign(vectors))) {
      LOG_WARN("copy vector pointers failed", K(ret));
    } else if (OB_FAIL(convert_to_storage_vector(copied_vectors, selector))) {
      LOG_WARN("convert to storage vector failed", K(ret));
    } else {
      ready_vectors = &copied_vectors;
    }
  }
  if (OB_SUCC(ret)) {
    selector.rescan();
    int64_t i = 0;
    while (OB_SUCC(ret) && OB_SUCC(selector.get_next(i))) {
      if (OB_FAIL(row_buffer_.append_batch(*ready_vectors, i, 1))) {
        LOG_WARN("append row buffer failed", K(ret));
      } else if (row_buffer_.full() && OB_FAIL(flush_row_buffer())) {
        LOG_WARN("flush row buffer failed", K(ret));
      }
    }
    if (OB_ITER_END == ret) {
      ret = OB_SUCCESS;
    }
  }
  return ret;
}

int ObHeapBatchSliceWriter::close()
{
  int ret = OB_SUCCESS;
  uint64_t last_autoinc_val = 0;
  if (OB_FAIL(ObBatchSliceWriter::close())) {
    LOG_WARN("close row slice writer failed", K(ret));
  } else if (OB_FAIL(heap_info_.get_last_autoinc_val(last_autoinc_val))) {
    LOG_WARN("get last autoinc val failed", K(ret));
  } else if (OB_FAIL(ObDDLUtil::set_tablet_autoinc_seq(tablet_id_, last_autoinc_val))) {
    LOG_WARN("set tablet autoinc seq failed", K(ret));
  }
  return ret;
}

/**
* -----------------------------------ObTabletSliceTempFileWriter-----------------------------------
*/
int ObTabletSliceTempFileWriter::init(const ObWriteMacroParam &param)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(is_inited_)) {
    ret = OB_INIT_TWICE;
    LOG_WARN("the ObTabletSliceTempFileWriter has been initialized", K(ret));
  } else if (OB_UNLIKELY(!param.is_valid() ||
                         nullptr == param.ddl_dag_ ||
                         param.max_batch_size_ <= 0)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("the are invalid argument", K(ret), K(param));
  } else {
    ddl_dag_ = param.ddl_dag_;
    if (OB_FAIL(row_file_generator_.init(param.tablet_id_,
                                         param.slice_idx_,
                                         param.max_batch_size_,
                                         ObDDLRowFileGenerator::ROW_FILE_MEMORY_LIMIT,
                                         param.ddl_table_schema_.column_items_,
                                         false/*is_generation_sync_output*/))) {
      LOG_WARN("fail to initialize row file generator", K(ret));
    } else {
      is_inited_ = true;
    }
  }
  return ret;
}

void ObTabletSliceTempFileWriter::reset()
{
  is_inited_ = false;
  ddl_dag_ = nullptr;
  row_count_ = 0;
  row_file_generator_.reset();
}

int ObTabletSliceTempFileWriter::append_batch(
    const blocksstable::ObBatchDatumRows &batch_rows)
{
  int ret = OB_SUCCESS;
  ObDDLChunk output_ddl_chunk;
  if (OB_UNLIKELY(!is_inited_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("the ObTabletSliceTempFileWriter has not been initialized", K(ret));
  } else if (OB_FAIL(row_file_generator_.append_batch(batch_rows,
                                                         false/*is_slice_end*/,
                                                         output_ddl_chunk))) {
    LOG_WARN("fail to append batch rows",
        K(ret), K(batch_rows), K(output_ddl_chunk));
  } else if (output_ddl_chunk.is_valid()) {
    if (OB_FAIL(ddl_dag_->add_scan_chunk(output_ddl_chunk))) {
      LOG_WARN("fail to add scan chunk", K(ret), K(output_ddl_chunk));
    }
  }
  if (OB_SUCC(ret)) {
    row_count_ += batch_rows.row_count_;
  }
  return ret;
}

int ObTabletSliceTempFileWriter::close()
{
  int ret = OB_SUCCESS;
  ObDDLChunk output_ddl_chunk;
  if (OB_UNLIKELY(!is_inited_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("the ObTabletSliceTempFileWriter has not been initialized", K(ret));
  } else if (OB_FAIL(row_file_generator_.try_generate_output_chunk(true/*is_slice_end*/,
                                                                      output_ddl_chunk))) {
    LOG_WARN("fail to try generate output chunk", K(ret));
  } else if (output_ddl_chunk.is_valid()) {
    if (OB_FAIL(ddl_dag_->add_scan_chunk(output_ddl_chunk))) {
      LOG_WARN("fail to add scan chunk", K(ret), K(output_ddl_chunk));
    }
  }
  return ret;
}
