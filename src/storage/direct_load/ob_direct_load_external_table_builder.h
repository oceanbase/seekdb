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

#include "lib/allocator/page_arena.h"
#include "storage/direct_load/ob_direct_load_external_block_writer.h"
#include "storage/direct_load/ob_direct_load_external_row.h"
#include "storage/direct_load/ob_direct_load_i_table.h"
#include "storage/direct_load/ob_direct_load_table_data_desc.h"
#include "storage/direct_load/ob_direct_load_tmp_file.h"

namespace oceanbase
{
namespace storage
{

struct ObDirectLoadExternalTableBuildParam
{
public:
  ObDirectLoadExternalTableBuildParam();
  ~ObDirectLoadExternalTableBuildParam();
  bool is_valid() const;
  TO_STRING_KV(K_(tablet_id), K_(table_data_desc), KP_(datum_utils), KP_(file_mgr), KP_(extra_buf),
               K_(extra_buf_size));
public:
  common::ObTabletID tablet_id_;
  ObDirectLoadTableDataDesc table_data_desc_;
  const blocksstable::ObStorageDatumUtils *datum_utils_;
  ObDirectLoadTmpFileManager *file_mgr_;
  char *extra_buf_;
  int64_t extra_buf_size_;
};

class ObDirectLoadExternalTableBuilder : public ObIDirectLoadPartitionTableBuilder
{
  typedef ObDirectLoadExternalRow RowType;
  typedef ObDirectLoadExternalBlockWriter<RowType> ExternalWriter;
public:
  ObDirectLoadExternalTableBuilder();
  virtual ~ObDirectLoadExternalTableBuilder();
  int init(const ObDirectLoadExternalTableBuildParam &build_param);
  int append_row(const common::ObTabletID &tablet_id,
                 const ObDirectLoadDatumRow &datum_row) override;
  int close() override;
  int64_t get_row_count() const override { return row_count_; }
  int get_tables(ObDirectLoadTableHandleArray &table_array,
                 ObDirectLoadTableManager *table_manager) override;
private:
  ObDirectLoadExternalTableBuildParam build_param_;
  ObDirectLoadTmpFileHandle file_handle_;
  ExternalWriter external_writer_;
  RowType external_row_;
  int64_t row_count_;
  bool is_closed_;
  bool is_inited_;
  DISALLOW_COPY_AND_ASSIGN(ObDirectLoadExternalTableBuilder);
};

} // namespace storage
} // namespace oceanbase
