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

#ifndef OCEANBASE_STORAGE_DDL_OB_DDL_MACRO_BLOCK_WRITE_TASK_H_
#define OCEANBASE_STORAGE_DDL_OB_DDL_MACRO_BLOCK_WRITE_TASK_H_

#include "observer/scheduler/ob_tenant_dag_scheduler.h"
#include "storage/ddl/ob_ddl_pipeline.h"
#include "storage/ddl/ob_tablet_slice_writer.h"
#include "observer/table_load/dag/ob_table_load_dag_task.h"

namespace oceanbase
{
namespace blocksstable
{
struct ObDatumRow;
struct ObMacroDataSeq;
}

namespace observer
{
class ObTableLoadDag;
}

namespace storage
{
class ObDDLIndependentDag;
class ObDDLInsertDag;
struct ObWriteMacroParam;
class ObITabletSliceRowIterator;
class ObDDLSlice;

class ObDDLScanTask : public share::ObITask
{
public:
  ObDDLScanTask(const ObITaskType type);
  ObDDLScanTask();
  virtual ~ObDDLScanTask();
  int init(ObDDLIndependentDag *ddl_dag);
  virtual share::ObITask::ObITaskPriority get_priority() override;
  int process();
private:
  ObDDLIndependentDag *ddl_dag_;
};

class ObDDLTabletScanTask final : public share::ObITask
{
public:
  ObDDLTabletScanTask();
  virtual ~ObDDLTabletScanTask() = default;
  int process() override { return OB_SUCCESS; }
};

class ObWriteMacroBaseOperator : public ObPipelineOperator
{
public:
  explicit ObWriteMacroBaseOperator(ObPipeline *pipeline)
    : ObPipelineOperator(pipeline), is_inited_(false), slice_writer_()
  {}
  virtual ~ObWriteMacroBaseOperator() = default;
  int init(const ObWriteMacroParam &param);
  virtual bool is_valid() const override;
  VIRTUAL_TO_STRING_KV(K_(slice_writer));
protected:
  bool is_inited_;
  ObTabletSliceWriter slice_writer_;
};

class ObDDLWriteMacroBlockOperator : public ObWriteMacroBaseOperator
{
public:
  explicit ObDDLWriteMacroBlockOperator(ObPipeline *pipeline)
    : ObWriteMacroBaseOperator(pipeline)
  {}
  virtual ~ObDDLWriteMacroBlockOperator() = default;
  INHERIT_TO_STRING_KV("ObWriteMacroBaseOperator", ObWriteMacroBaseOperator, KP_(pipeline));

protected:
  virtual int execute(
      const ObChunk &input_chunk,
      ResultState &result_state,
      ObChunk &output_chunk) override;
  virtual int try_execute_finish(
      const ObChunk &input_chunk,
      ResultState &result_state,
      ObChunk &output_chunk) override
  {
    return OB_SUCCESS;
  }
};

class ObDDLRowFileWriteMacroBlockOperator : public ObWriteMacroBaseOperator
{
public:
  explicit ObDDLRowFileWriteMacroBlockOperator(ObPipeline *pipeline)
    : ObWriteMacroBaseOperator(pipeline)
  {}
  virtual ~ObDDLRowFileWriteMacroBlockOperator() = default;
  INHERIT_TO_STRING_KV("ObWriteMacroBaseOperator", ObWriteMacroBaseOperator, KP_(pipeline));

protected:
  virtual int execute(const ObChunk &input_chunk,
                      ResultState &result_state,
                      ObChunk &output_chunk) override;
};

class ObDDLMemoryFriendWriteMacroBlockPipeline : public ObDDLWriteMacroBlockBasePipeline
{
public:
  ObDDLMemoryFriendWriteMacroBlockPipeline() :
    ObDDLWriteMacroBlockBasePipeline(TASK_TYPE_DDL_WRITE_USING_TMP_FILE_PIPELINE),
    write_op_(this) { }
  ObDDLMemoryFriendWriteMacroBlockPipeline(const share::ObITask::ObITaskType &task_type) :
    ObDDLWriteMacroBlockBasePipeline(task_type),
    write_op_(this) { }
  virtual ~ObDDLMemoryFriendWriteMacroBlockPipeline() = default;
  int init(ObDDLSlice *ddl_slice);

protected:
  ObDDLRowFileWriteMacroBlockOperator write_op_;
};


class ObBatchDatumRowsWriteOp : public ObPipelineOperator
{
public:
  ObBatchDatumRowsWriteOp(ObPipeline *pipeline) :
    ObPipelineOperator(pipeline),
    is_inited_(false),
    buffer_need_reuse_(false),
    tablet_id_(ObTabletID::INVALID_TABLET_ID),
    slice_idx_(-1),
    buffer_(),
    bdrs_() { }
  virtual ~ObBatchDatumRowsWriteOp() = default;
  int init(const ObTabletID &tablet_id, const int64_t slice_idx);
  virtual bool is_valid() const
  {
    return is_inited_;
  }
  VIRTUAL_TO_STRING_KV(K(is_inited_), K(buffer_need_reuse_), K(tablet_id_), K(slice_idx_));

public:
  static const int64_t MAX_BATCH_SIZE = 256;

protected:
 virtual int execute(const ObChunk &input_chunk,
                     ResultState &result_state,
                     ObChunk &output_chunk) override;
  int generate_data_chunk(ObChunk &output_chunk);

protected:
  bool is_inited_;
  bool buffer_need_reuse_;
  ObTabletID tablet_id_;
  int64_t slice_idx_;
  ObDirectLoadBatchRows buffer_;
  blocksstable::ObBatchDatumRows bdrs_;
};

} // end namespace storage
} // end namespace oceanbase

#endif//OCEANBASE_STORAGE_DDL_OB_DDL_MACRO_BLOCK_WRITE_TASK_H_
