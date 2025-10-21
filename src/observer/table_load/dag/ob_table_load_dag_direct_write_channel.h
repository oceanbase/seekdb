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

#include "lib/hash/ob_hashmap.h"
#include "observer/table_load/dag/ob_table_load_dag_write_channel.h"

namespace oceanbase
{
namespace storage
{
class ObDirectLoadDagInsertTableBatchRowDirectWriter;
} // namespace storage
namespace observer
{
class ObTableLoadDirectWriteOp;

class ObTableLoadDagDirectWriteChannel final : public ObTableLoadDagWriteChannel
{
public:
  ObTableLoadDagDirectWriteChannel();
  virtual ~ObTableLoadDagDirectWriteChannel() = default;
  int init(ObTableLoadDag *dag, ObTableLoadDirectWriteOp *op);

protected:
  int create_writer(ObTableLoadDagChunkWriter *&writer, ObIAllocator &allocator) override;
  int do_close() override;

public:
  ObTableLoadDirectWriteOp *op_;
};

class ObTableLoadDagDirectChunkWriter final : public ObTableLoadDagChunkWriter
{
  typedef storage::ObDirectLoadDagInsertTableBatchRowDirectWriter BatchWriter;

public:
  ObTableLoadDagDirectChunkWriter();
  virtual ~ObTableLoadDagDirectChunkWriter();
  int init(ObTableLoadDagWriteChannel *write_channel, ObTableLoadStoreTrans *trans,
           ObTableLoadTransStoreWriter *store_writer, const int32_t session_id) override;
  int append_row(const ObTabletID &tablet_id, const ObDirectLoadDatumRow &datum_row) override;
  int append_batch(common::ObIVector *tablet_id_vector,
                   const storage::ObDirectLoadBatchRows &batch_rows, int64_t &start) override;
  int close(ObTableLoadStoreTrans *trans, const int32_t session_id) override;

private:
  int new_batch_writer(const common::ObTabletID &tablet_id, BatchWriter *&batch_writer);
  int get_batch_writer(const common::ObTabletID &tablet_id, BatchWriter *&batch_writer);

private:
  typedef common::hash::ObHashMap<common::ObTabletID, BatchWriter *,
                                  common::hash::NoPthreadDefendMode>
    BatchWriterMap;

  ObTableLoadStoreCtx *store_ctx_;
  ObTableLoadDagDirectWriteChannel *write_channel_;
  ObArenaAllocator batch_writer_allocator_;
  ObSEArray<BatchWriter *, 1> batch_writers_;
  BatchWriterMap batch_writer_map_;
  ObArenaAllocator allocator_;
  int64_t max_batch_size_;
  uint16_t *selector_;
  uint16_t *tablet_offsets_;
  ObTabletID single_tablet_id_;
  bool is_single_part_;
  bool is_closed_;
};

} // namespace observer
} // namespace oceanbase
