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
#pragma once

#include "common/ob_tablet_id.h"
#include "storage/direct_load/ob_direct_load_data_block.h"
#include "storage/direct_load/ob_direct_load_data_block_writer.h"
#include "storage/direct_load/ob_direct_load_i_table.h"
#include "storage/direct_load/ob_direct_load_multiple_external_row.h"
#include "storage/direct_load/ob_direct_load_multiple_heap_table.h"
#include "storage/direct_load/ob_direct_load_multiple_heap_table_index_block_writer.h"
#include "storage/direct_load/ob_direct_load_table_data_desc.h"

namespace oceanbase
{
namespace storage
{
class ObDirectLoadTmpFileManager;

struct ObDirectLoadMultipleHeapTableBuildParam
{
public:
  ObDirectLoadMultipleHeapTableBuildParam();
  ~ObDirectLoadMultipleHeapTableBuildParam();
  bool is_valid() const;
  TO_STRING_KV(K_(table_data_desc), KP_(file_mgr), KP_(extra_buf), K_(extra_buf_size),
               K_(index_dir_id), K_(data_dir_id));
public:
  ObDirectLoadTableDataDesc table_data_desc_;
  ObDirectLoadTmpFileManager *file_mgr_;
  char *extra_buf_;
  int64_t extra_buf_size_;
  int64_t index_dir_id_;
  int64_t data_dir_id_;
};

class ObDirectLoadMultipleHeapTableBuilder : public ObIDirectLoadPartitionTableBuilder
{
  typedef ObDirectLoadMultipleExternalRow RowType;
  typedef ObDirectLoadMultipleHeapTableIndexBlockWriter IndexBlockWriter;
  typedef ObDirectLoadDataBlockWriter<ObDirectLoadDataBlock::Header, RowType> DataBlockWriter;
public:
  ObDirectLoadMultipleHeapTableBuilder();
  virtual ~ObDirectLoadMultipleHeapTableBuilder();
  int init(const ObDirectLoadMultipleHeapTableBuildParam &param);
  int append_row(const common::ObTabletID &tablet_id,
                 const ObDirectLoadDatumRow &datum_row) override;
  int append_row(const RowType &row);
  int close() override;
  int64_t get_row_count() const override { return row_count_; }
  int get_tables(ObDirectLoadTableHandleArray &table_array,
                 ObDirectLoadTableManager *table_manager) override;
private:
  class DataBlockFlushCallback : public ObIDirectLoadDataBlockFlushCallback
  {
  public:
    DataBlockFlushCallback() : data_block_offset_(0) {}
    virtual ~DataBlockFlushCallback() = default;
    int write(char *buf, int64_t buf_size, int64_t offset) override
    {
      data_block_offset_ = offset + buf_size;
      return common::OB_SUCCESS;
    }
    int64_t get_data_block_offset() const { return data_block_offset_; }
  private:
    int64_t data_block_offset_;
  };
private:
  ObDirectLoadMultipleHeapTableBuildParam param_;
  RowType row_;
  ObDirectLoadTmpFileHandle index_file_handle_;
  ObDirectLoadTmpFileHandle data_file_handle_;
  IndexBlockWriter index_block_writer_;
  DataBlockWriter data_block_writer_;
  DataBlockFlushCallback callback_;
  ObDirectLoadMultipleHeapTableTabletIndex last_tablet_index_;
  int64_t index_entry_count_;
  int64_t row_count_;
  bool is_closed_;
  bool is_inited_;
};

} // namespace storage
} // namespace oceanbase
