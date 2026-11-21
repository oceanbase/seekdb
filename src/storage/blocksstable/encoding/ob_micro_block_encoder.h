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

#ifndef OCEANBASE_ENCODING_OB_MICRO_BLOCK_ENCODER_H_
#define OCEANBASE_ENCODING_OB_MICRO_BLOCK_ENCODER_H_

#include <memory>

#include "lib/container/ob_array.h"
#include "storage/blocksstable/ob_block_sstable_struct.h"
#include "storage/blocksstable/ob_data_buffer.h"
#include "storage/blocksstable/ob_imicro_block_writer.h"
#include "storage/ob_i_store.h"
#include "ob_encoding_util.h"

namespace oceanbase
{
namespace blocksstable
{

class ObMicroBlockEncoder : public ObIMicroBlockWriter
{
public:
  static const int64_t MAX_ENCODING_META_LENGTH = UINT16_MAX;
  static const int64_t DEFAULT_ESTIMATE_REAL_SIZE_PCT = 150;
  static const int64_t MAX_MICRO_BLOCK_ROW_CNT = UINT16_MAX;
  static const int64_t DEFAULT_DATA_BUFFER_SIZE = common::OB_DEFAULT_MACRO_BLOCK_SIZE;

  struct CellCopyIndex
  {
    uint32_t index_;
    uint32_t offset_;
    TO_STRING_KV(K_(index), K_(offset));
  };

  ObMicroBlockEncoder();
  virtual ~ObMicroBlockEncoder();

  int init(const ObMicroBlockEncodingCtx &ctx);
  virtual int append_row(const ObDatumRow &row) override;
  virtual int build_block(char *&buf, int64_t &size) override;
  virtual void reset() override;
  virtual void reuse() override;
  virtual int64_t get_row_count() const override { return datum_rows_.count(); }
  virtual int64_t get_block_size() const override { return header_size_ + estimate_size_ * 100 / expand_pct_; }
  virtual int64_t get_column_count() const override { return ctx_.column_cnt_;}
  virtual int64_t get_original_size() const override { return estimate_size_; }
  virtual void dump_diagnose_info() override;
  virtual bool micro_block_row_data_buffered() const override { return true; }
  virtual int get_pre_agg_param(const int64_t col_idx, ObMicroDataPreAggParam &pre_agg_param) const override;
private:
  struct PreAggColumn
  {
    PreAggColumn() : col_datums_(nullptr), null_cnt_(0) {}
    const ObColDatums *col_datums_;
    uint64_t null_cnt_;
  };

  int inner_init();
  int reserve_header(const ObMicroBlockEncodingCtx &ctx);
  int pivot();
  int build_rust_block(char *&buf, int64_t &size);
  int try_to_append_row(const int64_t &store_size);
  int init_pre_agg_columns();
  int process_out_row_columns(const ObDatumRow &row);
  int copy_and_append_row(const ObDatumRow &src, int64_t &store_size);
  int copy_cell(
      const ObColDesc &col_desc,
      const ObStorageDatum &src,
      ObDatum &dest,
      int64_t &store_size,
      bool &is_large_row,
      bool &is_buffer_not_enough);
  int process_large_row(const ObDatumRow &src, ObDatum *&datum_arr, int64_t &store_size);
  int64_t calc_datum_row_size(const ObDatumRow &src, int64_t &var_len_column_cnt) const;
  void update_estimate_size_limit(const ObMicroBlockEncodingCtx &ctx);
  int init_all_col_values(const ObMicroBlockEncodingCtx &ctx);
  void print_micro_block_encoder_status() const;
  int set_datum_rows_ptr();

private:
  struct RustBlockBuffer;
  ObMicroBlockEncodingCtx ctx_;
  ObMicroBufferWriter data_buffer_;
  std::unique_ptr<RustBlockBuffer> rust_block_;
  ObConstDatumRowArray datum_rows_;
  common::ObArray<ObColDatums *> all_col_datums_;
  ObArenaAllocator pivot_allocator_;
  int64_t estimate_size_;
  int64_t estimate_size_limit_;
  int64_t header_size_;
  int64_t expand_pct_;
  ObMicroBufferWriter row_buf_holder_;
  common::ObArray<CellCopyIndex> deep_copy_indexes_;
  int64_t estimate_base_store_size_;
  common::ObArray<PreAggColumn> pre_agg_columns_;
  int64_t length_;
  bool encoder_freezed_;
  bool is_inited_;
  bool block_generated_;

  DISALLOW_COPY_AND_ASSIGN(ObMicroBlockEncoder);
};

} // end namespace blocksstable
} // end namespace oceanbase

#endif // OCEANBASE_ENCODING_OB_MICRO_BLOCK_ENCODER_H_
