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

#include "observer/table_load/dag/ob_table_load_dag_task.h"
#include "observer/table_load/ob_table_load_row_array.h"
#include "storage/ddl/ob_pipeline.h"

namespace oceanbase
{
namespace storage
{
class ObDirectLoadDatumRow;
class ObDirectLoadBatchRows;
} // namespace storage
namespace observer
{
struct ObTableLoadStoreTrans;
class ObTableLoadTransStoreWriter;
class ObTableLoadDagChunkWriter;

// not thread-safe
// for PX paths, px_write and close are both called synchronously
// for non-PX paths, the control node checks trans state to confirm background write and close completion
class ObTableLoadDagWriter
{
public:
  ObTableLoadDagWriter() = default;
  virtual ~ObTableLoadDagWriter() = default;
  virtual int write(const table::ObTableLoadTabletObjRowArray &row_array) = 0;
  virtual int px_write(common::ObIVector *tablet_id_vector,
                       const storage::ObDirectLoadBatchRows &batch_rows) = 0;
  virtual int close() = 0;
};

class ObTableLoadDagWriteChannel
{
public:
  ObTableLoadDagWriteChannel();
  virtual ~ObTableLoadDagWriteChannel() = default;
  int create_writer(ObTableLoadStoreTrans *trans, ObTableLoadTransStoreWriter *store_writer,
                    const int32_t session_id, ObTableLoadDagWriter *&writer,
                    ObIAllocator &allocator);
  // the control node calls flush only after all writers are closed
  int flush();
  int close();

  bool is_flushed() const { return is_flushed_; }

protected:
  int inner_init();
  int inner_flush();
  virtual int create_writer(ObTableLoadDagChunkWriter *&writer, ObIAllocator &allocator) = 0;
  virtual int do_flush() { return OB_SUCCESS; }
  virtual int do_close() = 0;

protected:
  class FlushTask final : public share::ObITask
  {
  public:
    FlushTask(ObTableLoadDagWriteChannel *write_channel)
      : ObITask(TASK_TYPE_DIRECT_LOAD_WRITE_CHANNEL_FLUSH), write_channel_(write_channel)
    {
    }
    virtual ~FlushTask() = default;
    int process() override { return write_channel_->inner_flush(); }

  private:
    ObTableLoadDagWriteChannel *write_channel_;
  };

public:
  class FinishTask : public share::ObITask
  {
  public:
    FinishTask(ObTableLoadDagWriteChannel *write_channel)
      : ObITask(TASK_TYPE_DIRECT_LOAD_WRITE_CHANNEL_FINISH), write_channel_(write_channel)
    {
    }
    virtual ~FinishTask() = default;
    ObITaskPriority get_priority() override
    {
      return write_channel_->is_flushed() ? TASK_PRIO_1 : TASK_PRIO_0;
    }
    int process() override { return OB_SUCCESS; }

  private:
    ObTableLoadDagWriteChannel *write_channel_;
  };

public:
  ObTableLoadStoreCtx *store_ctx_;
  ObTableLoadDag *dag_;

protected:
  FlushTask *flush_task_;
  bool is_flushed_;
  bool is_closed_;
  bool is_inited_;
};

class ObTableLoadDagChunkWriter : public ObTableLoadDagWriter
{
public:
  ObTableLoadDagChunkWriter();
  virtual ~ObTableLoadDagChunkWriter() = default;
  virtual int init(ObTableLoadDagWriteChannel *write_channel, ObTableLoadStoreTrans *trans,
                   ObTableLoadTransStoreWriter *store_writer, const int32_t session_id) = 0;
  int write(const table::ObTableLoadTabletObjRowArray &row_array) override;
  int px_write(common::ObIVector *tablet_id_vector,
               const storage::ObDirectLoadBatchRows &batch_rows) override;
  int close() override { return close(trans_, session_id_); }
  virtual int append_row(const ObTabletID &tablet_id, const ObDirectLoadDatumRow &datum_row) = 0;
  virtual int append_batch(common::ObIVector *tablet_id_vector,
                           const storage::ObDirectLoadBatchRows &batch_rows, int64_t &start) = 0;
  virtual int close(ObTableLoadStoreTrans *trans, const int32_t session_id) = 0;
  virtual int finish_chunk() { return OB_SUCCESS; }

protected:
  ObTableLoadDag *dag_;
  ObTableLoadStoreTrans *trans_;
  ObTableLoadTransStoreWriter *store_writer_;
  int32_t session_id_;
  bool is_inited_;
};

} // namespace observer
} // namespace oceanbase
