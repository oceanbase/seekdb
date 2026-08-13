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

#ifndef _STORAGE_DDL_OB_TABLET_SLICE_WRITER_
#define _STORAGE_DDL_OB_TABLET_SLICE_WRITER_

#include "storage/ddl/ob_ddl_struct.h"
#include "storage/ddl/ob_ddl_batch_rows.h"
#include "share/ob_tablet_autoincrement_param.h"
#include "share/ob_batch_selector.h"
#include "storage/ddl/ob_ddl_row_tmp_file.h"

namespace oceanbase
{

namespace blocksstable
{
class ObBatchDatumRows;
}

namespace storage
{
class ObDDLIndependentDag;
class ObDDLMacroBlockWriter;
class ObLobMacroBlockWriter;

// write storage rows
class ObITabletSliceWriter
{
public:
  ObITabletSliceWriter() {}
  virtual ~ObITabletSliceWriter() {}
  virtual int init(const ObWriteMacroParam &param) = 0;
  virtual void reset() = 0;
  virtual int append_row(const blocksstable::ObDatumRow &row) = 0;
  virtual int append_batch(const blocksstable::ObBatchDatumRows &batch_rows) = 0;
  virtual int64_t get_row_count() const = 0;
  virtual int close() = 0;
  DECLARE_PURE_VIRTUAL_TO_STRING;
};

class ObTabletSliceWriter : public ObITabletSliceWriter
{
public:
  ObTabletSliceWriter();
  virtual ~ObTabletSliceWriter();
  int init(const ObWriteMacroParam &param);
  void reset();
  int append_row(const blocksstable::ObDatumRow &row);
  int append_batch(const blocksstable::ObBatchDatumRows &batch_rows);
  int64_t get_row_count() const { return row_count_; }
  int close();
  const ObTabletID &get_tablet_id() const { return tablet_id_; }
  int64_t get_slice_idx() const { return slice_idx_; }
  TO_STRING_KV(K(is_inited_), K(tablet_id_), K(slice_idx_), K(storage_column_count_), K(row_count_), KP(macro_block_writer_), K(unique_index_id_));
protected:
  bool is_inited_;
  ObArenaAllocator allocator_;
  common::ObTabletID tablet_id_;
  int64_t slice_idx_;
  int64_t storage_column_count_;
  ObDDLMacroBlockWriter *macro_block_writer_;
  int64_t row_count_;
  uint64_t unique_index_id_; // for report conflict key if need
};

class ObTabletSliceTempFileWriter : public ObITabletSliceWriter
{
public:
  explicit ObTabletSliceTempFileWriter(
      query::ObISpillBatchSpoolFactory &spool_factory) :
    is_inited_(false),
    ddl_dag_(nullptr),
    row_count_(0),
    row_file_generator_(),
    spool_factory_(&spool_factory) { }
  virtual ~ObTabletSliceTempFileWriter()
  {
    reset();
  }
  virtual int init(const ObWriteMacroParam &param) override;
  void reset();
  virtual int append_row(const blocksstable::ObDatumRow &row) { return OB_NOT_IMPLEMENT; }
  virtual int append_batch(const blocksstable::ObBatchDatumRows &batch_rows);
  virtual int64_t get_row_count() const { return row_count_; }
  virtual int close();
  TO_STRING_KV(K(is_inited_), KP(ddl_dag_), K(row_count_));

protected:
  bool is_inited_;
  ObDDLIndependentDag *ddl_dag_;
  int64_t row_count_;
  ObDDLRowFileGenerator row_file_generator_;
  query::ObISpillBatchSpoolFactory *spool_factory_;
};
// write sql rows
class ObISliceWriter
{
public:
  ObISliceWriter() : tablet_id_(), slice_idx_(-1) {}
  virtual ~ObISliceWriter() {}
  virtual int append_current_row(const ObIArray<ObDatum *> &datums) = 0;
  virtual int append_current_batch(const ObIArray<ObIVector *> &vectors, share::ObBatchSelector &selector) { return common::OB_NOT_SUPPORTED; }
  virtual int close() = 0;
  virtual int64_t get_row_count() const = 0;
  const ObTabletID &get_tablet_id() const { return tablet_id_; }
  int64_t get_slice_idx() const { return slice_idx_; }
  VIRTUAL_TO_STRING_KV(K(tablet_id_), K(slice_idx_));

protected:
  common::ObTabletID tablet_id_;
  int64_t slice_idx_;
};

class ObHeapSliceInfo
{
public:
  ObHeapSliceInfo()
    : use_idempotent_autoinc_(false), parallel_count_(0), autoinc_column_idx_(0), autoinc_interval_()
  {}
  int init_autoinc_interval(const ObTabletID &tablet_id, const int64_t slice_idx);
  bool is_valid() const { return parallel_count_ > 0; }

  int64_t get_parallel_count() const { return parallel_count_; }
  int64_t get_autoinc_column_idx() const { return autoinc_column_idx_; }
  uint64_t remain_count() const { return autoinc_interval_.remain_count(); }
  int get_next(uint64_t &next_value) { return autoinc_interval_.next_value(next_value); }
  int get_last_autoinc_val(uint64_t &last_autoinc_val) { return autoinc_interval_.get_value(last_autoinc_val); }

  void set_parallel_count(int64_t parallel_count) { parallel_count_ = parallel_count; }
  void set_autoinc_column_idx(int64_t autoinc_column_idx) { autoinc_column_idx_ = autoinc_column_idx; }
  void set_use_idempotent_autoinc(bool use_idempotent_autoinc) { use_idempotent_autoinc_ = use_idempotent_autoinc; }

  TO_STRING_KV(K(parallel_count_), K(autoinc_column_idx_), K(autoinc_interval_));

private:
  bool use_idempotent_autoinc_;
  int64_t parallel_count_;
  int64_t autoinc_column_idx_;
  share::ObTabletCacheInterval autoinc_interval_;
};

// row store slice writer
class ObRsSliceWriter : public ObISliceWriter
{
public:
  ObRsSliceWriter();
  virtual ~ObRsSliceWriter();
  int init(const ObWriteMacroParam &write_param);
  virtual int append_current_row(const ObIArray<ObDatum *> &datums);
  virtual int64_t get_row_count() const override { return OB_NOT_NULL(storage_slice_writer_) ? storage_slice_writer_->get_row_count() : 0; }
  virtual int close();
  VIRTUAL_TO_STRING_KV(K(is_inited_), K(tablet_id_), K(slice_idx_), K(rowkey_column_count_), K(sql_column_count_), KP(lob_writer_), KP(storage_slice_writer_));

protected:
  int build_multi_version_row(const ObIArray<ObDatum *> &sql_datums);
  void free_tablet_writer();
  void free_lob_writer();
  int switch_next_slice(ObHeapSliceInfo &heap_info);
protected:
  bool is_inited_;
  ObWriteMacroParam writer_param_;
  int64_t rowkey_column_count_;
  int64_t sql_column_count_;
  ObLobMacroBlockWriter *lob_writer_;
  ObITabletSliceWriter *storage_slice_writer_;
  ObArenaAllocator row_arena_;
  blocksstable::ObDatumRow current_row_;
};


class ObHeapRsSliceWriter : public ObRsSliceWriter
{
public:
  ObHeapRsSliceWriter() : heap_info_(), ready_datums_(), autoinc_value_(0), autoinc_datum_() {}
  virtual ~ObHeapRsSliceWriter() {}
  int init(const ObWriteMacroParam &write_param,
           const int64_t parallel_count,
           const int64_t autoinc_column_idx,
           const bool use_idempotent_autoinc);
  virtual int append_current_row(const ObIArray<ObDatum *> &datums) override;
  virtual int close() override;
  INHERIT_TO_STRING_KV("RowStoreSliceWriter", ObRsSliceWriter, K(heap_info_));

private:
  ObHeapSliceInfo heap_info_;
  ObArray<ObDatum *> ready_datums_;
  uint64_t autoinc_value_;
  ObDatum autoinc_datum_;
};


class ObTabletSliceBufferTempFileWriter : public ObTabletSliceTempFileWriter
{
public:
  class ObDDLRowBuffer
  {
  public:
    ObDDLRowBuffer() : is_inited_(false), buffer_(), bdrs_() { }
    ~ObDDLRowBuffer()
    {
      reset();
    }
    int init(const common::ObIArray<ObColumnSchemaItem> &column_schemas,
            const int64_t max_batch_size = DEFAULT_MAX_BATCH_SIZE);
    void reset();
    void reuse();
    int append_row(const blocksstable::ObDatumRow &datum_row);
    bool is_full() { return buffer_.full(); }
    int64_t size() { return buffer_.size(); }
    int get_batch_datum_rows(blocksstable::ObBatchDatumRows *&bdrs);

  public:
    static const int64_t DEFAULT_MAX_BATCH_SIZE = 256;

  private:
    bool is_inited_;
    ObDDLBatchRows buffer_;
    blocksstable::ObBatchDatumRows bdrs_;
  };

public:
  explicit ObTabletSliceBufferTempFileWriter(
      query::ObISpillBatchSpoolFactory &spool_factory)
    : ObTabletSliceTempFileWriter(spool_factory)
  { }
  virtual ~ObTabletSliceBufferTempFileWriter() { reset(); }
  virtual int init(const ObWriteMacroParam &param) override;
  void reset();
  virtual int append_row(const blocksstable::ObDatumRow &row);
  virtual int close() override;

protected:
  ObDDLRowBuffer buffer_;
};

// Buffered slice writer for vectorized DDL input.
class ObBatchSliceWriter : public ObRsSliceWriter
{
public:
  ObBatchSliceWriter();
  virtual ~ObBatchSliceWriter();
  // max_batch_size parameter for heap table path
  int init(const ObWriteMacroParam &write_param,
           const bool direct_write_macro_block,
           const bool is_append_batch,
           const int64_t max_batch_size,
           query::ObISpillBatchSpoolFactory &spool_factory);
  virtual int append_current_row(const ObIArray<ObDatum *> &datums);
  virtual int append_current_batch(const ObIArray<ObIVector *> &vectors, share::ObBatchSelector &selector);
  virtual int64_t get_row_count() const override { return row_buffer_.size() + (OB_NOT_NULL(storage_slice_writer_) ? storage_slice_writer_->get_row_count() : 0); }
  virtual int close();
  INHERIT_TO_STRING_KV("RowStoreSliceWriter", ObRsSliceWriter, K(need_convert_storage_value_), K(direct_write_macro_block_), K(row_buffer_size_), K(row_buffer_.size()));

protected:
  int convert_to_storage_vector(ObIArray<ObIVector *> &vectors, share::ObBatchSelector &selector);
  int init_last_rowkey();
  int check_order(const blocksstable::ObBatchDatumRows &batch_rows);
  int init_storage_batch_rows();
  int init_row_buffer(const int64_t buffer_row_count);
  int flush_row_buffer();

protected:
  ObArenaAllocator arena_;
  bool need_convert_storage_value_;
  bool direct_write_macro_block_;
  int64_t row_buffer_size_;
  ObDDLBatchRows row_buffer_;
  blocksstable::ObBatchDatumRows buffer_batch_rows_;
  bool need_check_rowkey_order_;
  ObArenaAllocator rowkey_arena_;
  blocksstable::ObDatumRowkey last_key_; // for check order row by row, consider macro block writer will not checkit when append_batch
  blocksstable::ObStorageDatumUtils datum_utils_;
};

class ObHeapBatchSliceWriter : public ObBatchSliceWriter
{
public:
  ObHeapBatchSliceWriter()
    : heap_info_(), active_array_(), ready_datums_(), autoinc_value_(0), autoinc_datum_()
  {}
  int init(const ObWriteMacroParam &write_param,
           const int64_t parallel_count,
           const int64_t autoinc_column_idx,
           const bool direct_write_macro_block,
           const int64_t max_batch_size,
           const bool use_idempotent_autoinc,
           query::ObISpillBatchSpoolFactory &spool_factory);
  virtual int append_current_row(const ObIArray<ObDatum *> &datums);
  virtual int append_current_batch(const ObIArray<ObIVector *> &vectors, share::ObBatchSelector &selector);
  virtual int close() override;

  int set_active_row(const uint16_t row_idx) { return active_array_.push_back(row_idx); }
  const ObIArray<uint16_t> &get_active_array() const { return active_array_; }
  void reuse_active_array() { active_array_.reuse(); }
  INHERIT_TO_STRING_KV("HeapBatchSliceWriter", ObBatchSliceWriter, K(heap_info_));

private:
  ObHeapSliceInfo heap_info_;
  ObArray<uint16_t> active_array_;
  ObArray<ObDatum *> ready_datums_;
  uint64_t autoinc_value_;
  ObDatum autoinc_datum_;
};


}// namespace storage
}// namespace oceanbase

#endif//_STORAGE_DDL_OB_TABLET_SLICE_WRITER_
