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
#include "storage/ddl/ob_cg_macro_block_write_task.h"

namespace oceanbase
{
namespace storage
{
class ObDirectLoadIMergeTask;
} // namespace storage
namespace observer
{
class ObTableLoadInsertSSTableOp;
class ObTableLoadDagParallelMerger;

class ObTableLoadDagInsertSSTableOpTask final : public share::ObITask,
                                                public ObTableLoadDagOpTaskBase
{
  using ObTableLoadDagTaskBase::dag_;

public:
  ObTableLoadDagInsertSSTableOpTask(ObTableLoadDag *dag, ObTableLoadOp *op);
  virtual ~ObTableLoadDagInsertSSTableOpTask() = default;
  int process() override;
};

class ObTableLoadDagInsertSSTableOpFinishTask final : public share::ObITask,
                                                      public ObTableLoadDagOpTaskBase
{
  using ObTableLoadDagTaskBase::dag_;

public:
  ObTableLoadDagInsertSSTableOpFinishTask(ObTableLoadDag *dag, ObTableLoadOp *op);
  virtual ~ObTableLoadDagInsertSSTableOpFinishTask() = default;
  int process() override;
  static void reset_op(ObTableLoadInsertSSTableOp *op);
};

class ObTableLoadDagInsertSSTableTaskBase : public ObTableLoadDagTaskBase
{
  using ObTableLoadDagTaskBase::dag_;

public:
  ObTableLoadDagInsertSSTableTaskBase(ObTableLoadDag *dag,
                                      ObTableLoadDagParallelMerger *parallel_merger)
    : ObTableLoadDagTaskBase(dag), parallel_merger_(parallel_merger)
  {
  }
  ObTableLoadDagInsertSSTableTaskBase(ObTableLoadDagInsertSSTableTaskBase *parent)
    : ObTableLoadDagTaskBase(parent->dag_), parallel_merger_(parent->parallel_merger_)
  {
  }

protected:
  int handle_merge_task_finish(share::ObITask *parent_task,
                               storage::ObDirectLoadIMergeTask *merge_task);

protected:
  ObTableLoadDagParallelMerger *parallel_merger_;
};

class ObTableLoadInsertSSTableTask final : public share::ObITask,
                                           public ObTableLoadDagInsertSSTableTaskBase
{
  using ObTableLoadDagTaskBase::dag_;

public:
  ObTableLoadInsertSSTableTask(ObTableLoadDag *dag, ObTableLoadDagParallelMerger *parallel_merger);
  virtual ~ObTableLoadInsertSSTableTask() = default;
  int process() override;
};

class ObTableLoadInsertSSTableFinishTask final : public share::ObITask,
                                                 public ObTableLoadDagInsertSSTableTaskBase
{
  using ObTableLoadDagTaskBase::dag_;

public:
  ObTableLoadInsertSSTableFinishTask(ObTableLoadDagInsertSSTableTaskBase *parent)
    : ObITask(TASK_TYPE_DIRECT_LOAD_INSERT_SSTABLE_FINISH),
      ObTableLoadDagInsertSSTableTaskBase(parent)
  {
  }
  virtual ~ObTableLoadInsertSSTableFinishTask() = default;
  int process() override { return OB_SUCCESS; }
};

class ObTableLoadDagInsertSSTableClearTask final : public share::ObITask,
                                                   public ObTableLoadDagInsertSSTableTaskBase
{
  using ObTableLoadDagTaskBase::dag_;

public:
  ObTableLoadDagInsertSSTableClearTask(ObTableLoadDag *dag,
                                       ObTableLoadDagParallelMerger *parallel_merger,
                                       const int64_t thread_idx);
  ObTableLoadDagInsertSSTableClearTask(ObTableLoadDagInsertSSTableTaskBase *parent,
                                       const int64_t thread_idx);
  virtual ~ObTableLoadDagInsertSSTableClearTask() = default;
  int generate_next_task(share::ObITask *&next_task) override;
  int process() override;

private:
  int64_t thread_idx_;
};

class ObTableLoadMemoryFriendWriteMacroBlockPipeline
  : public ObDDLMemoryFriendWriteMacroBlockPipeline
{
public:
  ObTableLoadMemoryFriendWriteMacroBlockPipeline();
  // for unittest
  ObTableLoadMemoryFriendWriteMacroBlockPipeline(ObITabletSliceRowIterator *row_iterator);
  virtual ~ObTableLoadMemoryFriendWriteMacroBlockPipeline();
  virtual int init();
  virtual int get_next_chunk(ObChunk *&chunk) override;
  virtual void postprocess(int &ret_code);
  virtual int finish_chunk(ObChunk *chunk)
  {
    UNUSED(chunk);
    return OB_SUCCESS;
  }
  void reset();

protected:
  bool is_inited_;
  ObITabletSliceRowIterator *row_iterator_;
  ObChunk chunk_;
  ObBatchDatumRowsWriteOp batch_datum_rows_write_op_;
  ObCGRowFileWriterOp cg_row_file_writer_op_;
};

class ObTableLoadMemoryFriendWriteMacroBlockTask final
  : public ObTableLoadMemoryFriendWriteMacroBlockPipeline,
    public ObTableLoadDagInsertSSTableTaskBase
{
  using ObTableLoadDagTaskBase::dag_;

public:
  ObTableLoadMemoryFriendWriteMacroBlockTask(ObTableLoadDagInsertSSTableTaskBase *parent,
                                             storage::ObDirectLoadIMergeTask *merge_task);
  virtual ~ObTableLoadMemoryFriendWriteMacroBlockTask() = default;
  int init() override;
  int generate_next_task(share::ObITask *&next_task) override;
  void postprocess(int &ret_code) override;

protected:
  storage::ObDirectLoadIMergeTask *merge_task_;
};

// for the sorting path of direct laod, incremental direct load, row storage, and column storage
// replica writes all use ObTableLoadMacroBlockWriteTask
class ObTableLoadMacroBlockWriteTask final : public share::ObITask,
                                             public ObTableLoadDagInsertSSTableTaskBase
{
  using ObTableLoadDagTaskBase::dag_;

public:
  ObTableLoadMacroBlockWriteTask(ObTableLoadDagInsertSSTableTaskBase *parent,
                                 storage::ObDirectLoadIMergeTask *merge_task);
  virtual ~ObTableLoadMacroBlockWriteTask() = default;
  int process() override;

private:
  int generate_next_task(share::ObITask *&next_task) override;

private:
  storage::ObDirectLoadIMergeTask *merge_task_;
};

} // namespace observer
} // namespace oceanbase
