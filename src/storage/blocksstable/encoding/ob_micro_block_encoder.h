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

  ObMicroBlockEncoder();
  virtual ~ObMicroBlockEncoder();

  int init(const ObMicroBlockEncodingCtx &ctx);
  virtual int append_row(const ObDatumRow &row) override;
  virtual int build_block(char *&buf, int64_t &size) override;
  virtual void reset() override;
  virtual void reuse() override;
  virtual int64_t get_row_count() const override { return row_count_; }
  virtual int64_t get_block_size() const override;
  virtual int64_t get_column_count() const override { return ctx_.column_cnt_;}
  virtual int64_t get_original_size() const override;
  virtual void dump_diagnose_info() override;
  virtual bool micro_block_row_data_buffered() const override { return false; }
private:
  int inner_init();
  int process_out_row_columns(const ObDatumRow &row);
  void update_estimate_size_limit(const ObMicroBlockEncodingCtx &ctx);
  void print_micro_block_encoder_status() const;

private:
  struct RustBlockBuffer;
  ObMicroBlockEncodingCtx ctx_;
  std::unique_ptr<RustBlockBuffer> rust_block_;
  common::ObArray<int64_t> column_checksums_;
  int64_t estimate_size_limit_;
  int64_t header_size_;
  int64_t expand_pct_;
  int64_t row_count_;
  bool encoder_freezed_;
  bool is_inited_;

  DISALLOW_COPY_AND_ASSIGN(ObMicroBlockEncoder);
};

} // end namespace blocksstable
} // end namespace oceanbase

#endif // OCEANBASE_ENCODING_OB_MICRO_BLOCK_ENCODER_H_
