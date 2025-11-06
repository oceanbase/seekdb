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

#ifndef _OB_TABLE_PRE_SORT_WRITER_
#define _OB_TABLE_PRE_SORT_WRITER_

#include "share/table/ob_table_load_row_array.h"
#include "src/share/table/ob_table_load_row_array.h"
#include "storage/direct_load/ob_direct_load_datum_row.h"
#include "storage/direct_load/ob_direct_load_external_multi_partition_row.h"
#include "storage/direct_load/ob_direct_load_mem_context.h"

namespace oceanbase
{
namespace storage
{
class ObDirectLoadBatchRows;
} // namespace storage
namespace observer
{
class ObTableLoadPreSorter;
class ObTableLoadTransStoreWriter;
class ObTableLoadErrorRowHandler;
class ObTableLoadMemChunkManager;

class ObTableLoadPreSortWriter
{
public:
  using ChunkType = storage::ObDirectLoadExternalMultiPartitionRowChunk;
  using RowType = ObDirectLoadConstExternalMultiPartitionRow;
  ObTableLoadPreSortWriter();
  ~ObTableLoadPreSortWriter();
  int init(ObTableLoadPreSorter *pre_sorter,
           ObTableLoadTransStoreWriter *store_writer,
           ObTableLoadErrorRowHandler *error_row_handler);
  int write(int32_t session_id, const table::ObTableLoadTabletObjRowArray &row_array);
  int px_write(common::ObIVector *tablet_id_vector,
               const storage::ObDirectLoadBatchRows &batch_rows);
  int close();
private:
  int append_row(const ObTabletID &tablet_id,
                 const ObDirectLoadDatumRow &datum_row);
private:
  ObTableLoadPreSorter *pre_sorter_;
  ObTableLoadTransStoreWriter *store_writer_;
  ObTableLoadErrorRowHandler *error_row_handler_;
  ObDirectLoadMemContext *mem_ctx_;
  ObTableLoadMemChunkManager *chunks_manager_;
  int64_t chunk_node_id_;
  ChunkType *chunk_;
  ObDirectLoadDatumRow datum_row_;
  ObDirectLoadExternalMultiPartitionRow external_row_;
  bool is_inited_;
};

} // namespace observer
} // namespace oceanbase

#endif /* _OB_TABLE_LOAD_PRE_SORT_WRITER_ */
