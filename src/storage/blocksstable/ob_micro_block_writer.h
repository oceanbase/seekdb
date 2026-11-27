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

#ifndef OCEANBASE_STORAGE_BLOCKSSTABLE_OB_MICRO_BLOCK_WRITER_H_
#define OCEANBASE_STORAGE_BLOCKSSTABLE_OB_MICRO_BLOCK_WRITER_H_
#include "ob_block_sstable_struct.h"
#include "ob_imicro_block_writer.h"
#include "ob_micro_block_checksum_helper.h"
#include <memory>

namespace oceanbase
{
namespace common
{
class ObNewRow;
}
namespace blocksstable
{
class ObMicroBlockWriter : public ObIMicroBlockWriter
{
public:
  ObMicroBlockWriter();
  virtual ~ObMicroBlockWriter();
  int init(const ObDataStoreDesc *data_store_desc);

  int init(const int64_t micro_block_size_limit,
           const int64_t rowkey_column_count,
           const int64_t column_count);

  virtual int append_row(const ObDatumRow &row);
  virtual int build_block(char *&buf, int64_t &size);
  virtual void reuse();

  virtual int64_t get_block_size() const override;
  virtual int64_t get_row_count() const override;
  virtual int64_t get_column_count() const override;
  virtual int64_t get_original_size() const override;
  void reset();
private:
  struct RustBlockBuffer;
  int inner_init();
  int process_out_row_columns(const ObDatumRow &row);

  int init_hash_index_policy(const ObDataStoreDesc &data_store_desc);
  int append_row_to_hash_index(const ObDatumRow &row, bool hash_index_empty);

private:
  int64_t micro_block_size_limit_;
  int64_t column_count_;
  int64_t rowkey_column_count_;
  const common::ObIArray<share::schema::ObColDesc> *col_desc_array_;
  int64_t row_count_;
  std::unique_ptr<RustBlockBuffer> rust_block_;
  common::ObArray<int64_t> column_checksums_;
  const ObDataStoreDesc *hash_index_store_desc_;
  bool previous_row_ended_key_;
  bool is_major_;
  bool is_inited_;
};

inline int64_t ObMicroBlockWriter::get_row_count() const
{
  return row_count_;
}
inline int64_t ObMicroBlockWriter::get_column_count() const
{
  return column_count_;
}
}//end namespace blocksstable
}//end namespace oceanbase
#endif
