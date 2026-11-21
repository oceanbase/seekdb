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

#ifndef OB_STORAGE_BLOCKSSTABLE_ENCODING_OB_MICRO_BLOCK_DECODER_H_
#define OB_STORAGE_BLOCKSSTABLE_ENCODING_OB_MICRO_BLOCK_DECODER_H_

#include "ob_imicro_block_decoder.h"
#include "storage/blocksstable/ob_row_reader.h"
#include "storage/ob_i_store.h"
#include "ob_new_column_decoder.h"
#include "lib/container/ob_bitmap.h"

namespace oceanbase
{
namespace blocksstable
{
struct ColumnReadInfo
{
  const ObColumnHeader *col_header_ = nullptr;
  const ObColumnParam *col_param_ = nullptr;
};

class ObIEncodeBlockReader
{
public:
  ObIEncodeBlockReader();
  virtual ~ObIEncodeBlockReader();
  void reset();
  void reuse();
  static int get_micro_metas(
      const ObMicroBlockHeader *&header,
      const ObColumnHeader *&col_header,
      const char *block,
      const int64_t block_size);
protected:
  int prepare(const int64_t column_cnt);
  int do_init(const ObMicroBlockData &block_data, const int64_t request_cnt);
protected:
  static const int64_t DEFAULT_DECODER_CNT = 16;

  int64_t request_cnt_; // request column count
  const ObMicroBlockHeader *header_;
  const ObColumnHeader *col_header_;
  const char *block_data_;
  int64_t block_size_;
  common::ObArenaAllocator decoder_allocator_;
  int64_t *store_id_array_;
  int64_t default_store_ids_[DEFAULT_DECODER_CNT];
};

class ObEncodeBlockGetReader : public ObIMicroBlockGetReader, public ObIEncodeBlockReader
{
public:
  void reuse();
  ObEncodeBlockGetReader() = default;
  virtual ~ObEncodeBlockGetReader() = default;
  virtual int get_row(
      const ObMicroBlockData &block_data,
      const ObDatumRowkey &rowkey,
      const ObITableReadInfo &read_info,
      ObDatumRow &row) final;
  virtual int exist_row(
      const ObMicroBlockData &block_data,
      const ObDatumRowkey &rowkey,
      const ObITableReadInfo &read_info,
      bool &exist,
      bool &found);
  virtual int get_row(
      const ObMicroBlockData &block_data,
      const ObITableReadInfo &read_info,
      const uint32_t row_idx,
      ObDatumRow &row) final;
  int get_row_id(
      const ObMicroBlockData &block_data,
      const ObDatumRowkey &rowkey,
      const ObITableReadInfo &read_info,
      int64_t &row_id) final;
protected:
  int get_all_columns(
      const int64_t row_id,
      ObDatumRow &row);
private:
  int get_columns_rust(
      const int64_t row_id,
      const int64_t column_count,
      ObStorageDatum *datums);
  int init_by_read_info(
      const ObMicroBlockData &block_data,
      const storage::ObITableReadInfo &read_info);
  int init_by_columns_desc(
      const ObMicroBlockData &block_data,
      const int64_t schema_rowkey_cnt,
      const ObColDescIArray &cols_desc,
      const int64_t request_cnt);
  int locate_row(
      const ObDatumRowkey &rowkey,
      const ObITableReadInfo &read_info,
      int64_t &row_id,
      bool &found);
};

class ObMicroBlockDecoder : public ObIMicroBlockDecoder
{
public:
  static const int64_t ROW_CACHE_BUF_SIZE = 64 * 1024;
  static const int64_t CPU_CACHE_LINE_SIZE = 64;
  ObMicroBlockDecoder();
  virtual ~ObMicroBlockDecoder();

  virtual void reset();
  virtual int init(
      const ObMicroBlockData &block_data,
      const storage::ObITableReadInfo &read_info) override;
  //when there is not read_info in input parameters, it indicates reading all columns from all rows
  //when the incoming datum_utils is nullptr, it indicates not calling locate_range or find_bound
  virtual int init(const ObMicroBlockData &block_data, const ObStorageDatumUtils *datum_utils) override;
  virtual int get_row(const int64_t index, ObDatumRow &row) override;
  virtual int get_row_header(
      const int64_t row_idx,
      const ObRowHeader *&row_header) override;
  virtual int get_row_count(int64_t &row_count) override;
  virtual int get_multi_version_info(
      const int64_t row_idx,
      const int64_t schema_rowkey_cnt,
      const ObRowHeader *&row_header,
      int64_t &trans_version,
      int64_t &sql_sequence);
  virtual int compare_rowkey(
      const ObDatumRowkey &rowkey,
      const int64_t index,
      int32_t &compare_result) override;
  virtual int compare_rowkey(
      const ObDatumRange &range,
      const int64_t index,
      int32_t &start_key_compare_result,
      int32_t &end_key_compare_result) override;
  virtual int find_bound(
      const ObDatumRowkey &key,
      const bool lower_bound,
      const int64_t begin_idx,
      int64_t &row_idx,
      bool &equal) override;
  // Filter interface for filter pushdown
  virtual int filter_truncate_evaluator(
      storage::ObTruncateFilterEvaluator &evaluator,
      const int64_t start,
      const int64_t count,
      const common::ObBitmap *candidate_rows,
      common::ObBitmap &result_bitmap) override;

  virtual int get_rows(
      const common::ObIArray<int32_t> &cols,
      const common::ObIArray<const share::schema::ObColumnParam *> &col_params,
      const bool is_padding_mode,
      const int32_t *row_ids,
      const char **cell_datas,
      const int64_t row_cap,
      common::ObIArray<ObSqlDatumInfo> &datum_infos,
      const int64_t datum_offset = 0) override;
  virtual int get_row_count(
      int32_t col_id,
      const int32_t *row_ids,
      const int64_t row_cap,
      const bool contains_null,
      const share::schema::ObColumnParam *col_param,
      int64_t &count) override final;
  virtual int get_aggregate_result(
      const ObTableIterParam &iter_param,
      const ObTableAccessContext &context,
      const int32_t col_offset,
      const share::schema::ObColumnParam &col_param,
      const int32_t *row_ids,
      const int64_t row_cap,
      storage::ObAggDatumBuf &agg_datum_buf,
      storage::ObAggCell &agg_cell) override;
  virtual int64_t get_column_count() const override
  {
    OB_ASSERT(nullptr != header_);
    return header_->column_count_;
  }
  virtual int get_column_datum(
      const ObTableIterParam &iter_param,
      const ObTableAccessContext &context,
      const share::schema::ObColumnParam &col_param,
      const int32_t col_offset,
      const int64_t row_index,
      ObStorageDatum &datum) override;

  virtual void reserve_reader_memory(bool reserve) override
  {
    decoder_allocator_.set_reserve_memory(reserve);
  }
  virtual bool has_lob_out_row() const override final
  { return nullptr != header_ && header_->has_lob_out_row(); }

private:
  // use inner_reset to reuse the decoder buffer
  // the column count would not change in most cases
  void inner_reset();
  virtual int find_bound(
      const ObDatumRowkey &key,
      const bool lower_bound,
      const int64_t begin_idx,
      const int64_t end_idx,
      int64_t &row_idx,
      bool &equal) override;
  virtual int find_bound(
      const ObDatumRange &range,
      const int64_t begin_idx,
      int64_t &row_idx,
      bool &equal,
      int64_t &end_key_begin_idx,
      int64_t &end_key_end_idx) override;
  int do_init(const ObMicroBlockData &block_data);
  int init_columns();

  int get_col_datums(int32_t col_id,
                     const int32_t *row_ids,
                     const char **cell_datas,
                     const int64_t row_cap,
                     common::ObDatum *col_datums);
  int try_get_rows_rust(
      const common::ObIArray<int32_t> &cols,
      const int32_t *row_ids,
      const char **cell_datas,
      const int64_t row_cap,
      common::ObIArray<ObSqlDatumInfo> &datum_infos,
      const int64_t datum_offset,
      const bool preserve_nop = false,
      const bool reverse_trans_version = false);
  int get_row_impl(int64_t index, ObDatumRow &row);
  OB_INLINE static const ObRowHeader &get_major_store_row_header()
  {
    static ObRowHeader rh = init_major_store_row_header();
    return rh;
  }
  static const ObRowHeader init_major_store_row_header();
  static int get_micro_metas(
      const ObMicroBlockHeader *&header, const ObColumnHeader *&col_header,
      const char *block, const int64_t block_size);
private:
  const ObMicroBlockHeader *header_;
  int64_t request_cnt_; // request column count
  const ObColumnHeader *col_header_;
  const char *block_data_;
  int64_t block_size_;
  ColumnReadInfo *columns_;
  ObRowReader flat_row_reader_;
  ObBlockReaderAllocator decoder_allocator_;
  common::ObArenaAllocator buf_allocator_;
  char *column_buf_;
  int64_t column_buf_size_;
};

}
}
#endif //OB_STORAGE_BLOCKSSTABLE_ENCODING_OB_MICRO_BLOCK_DECODER_H_
