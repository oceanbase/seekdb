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

#ifndef _OCEANBASE_STORAGE_DDL_OB_PIPELINE_H_
#define _OCEANBASE_STORAGE_DDL_OB_PIPELINE_H_

#include "lib/utility/ob_print_utils.h"
#include "lib/container/ob_array.h"
#include "observer/scheduler/ob_tenant_dag_scheduler.h"
#include "storage/ddl/ob_ddl_batch_datum_rows.h"
#include "storage/ddl/ob_ddl_row_tmp_file.h"

namespace oceanbase
{
namespace blocksstable
{
class ObBatchDatumRows;
struct ObDatumRow;
}

namespace common 
{
class ObIVector;
}

namespace storage
{
class ObTaskBatchInfo;
class ObDDLTabletContext;
class ObDDLSlice;
class ObPipeline;

struct ObChunk
{
public:
  enum ChunkType
  {
    INVALID_TYPE = 0,
    ITER_END_TYPE,
    DATUM_ROW,
    DAG_TABLET_CONTEXT,
    DDL_BATCH_DATUM_ROWS,
    DDL_ROW_TMP_FILES,
    BATCH_DATUM_ROWS,
    TASK_BATCH_INFO,
    MAX_TYPE
  };
public:
  ObChunk() : type_(INVALID_TYPE), data_ptr_(nullptr) {}
  ~ObChunk();
  void reset();
  bool is_valid() const;
  void set_end_chunk() { type_ = ChunkType::ITER_END_TYPE; }
  bool is_end_chunk() const { return ChunkType::ITER_END_TYPE == type_; }
  int get_dag_tablet_context(ObDDLTabletContext *&tablet_context) const;
  OB_INLINE bool is_ddl_batch_datum_rows_type() const { return ChunkType::DDL_BATCH_DATUM_ROWS == type_; }
  OB_INLINE bool is_batch_datum_rows_type() const { return ChunkType::BATCH_DATUM_ROWS == type_; }
  OB_INLINE bool is_ddl_row_tmp_files_type() const { return ChunkType::DDL_ROW_TMP_FILES == type_; }
  OB_INLINE bool is_datum_row_type() const { return ChunkType::DATUM_ROW == type_; }
  OB_INLINE bool is_task_batch_info_type() const { return ChunkType::TASK_BATCH_INFO == type_; }
  TO_STRING_KV(K_(type), KP_(data_ptr));
public:
  ChunkType type_;
  union {
    void *data_ptr_;
    storage::ObDDLBatchDatumRows *ddl_batch_rows_;
    blocksstable::ObDatumRow *datum_row_;
    ObArray<ObDDLRowFile *> *row_file_arr_;
    blocksstable::ObBatchDatumRows *bdrs_;
    storage::ObTaskBatchInfo *batch_info_;
  };
};

class ObPipelineOperator
{
public:
  enum ResultState
  {
    INVALID_VALUE = 0, // deafult invalid val
    NEED_MORE_INPUT = 1, // means the input chunk is fully processed, the output chunk maybe valid
    HAVE_MORE_OUTPUT = 2, // means the input chunk is partial processed, the output chunk is valid and need execute again to get next output chunk
  };
public:
  explicit ObPipelineOperator(ObPipeline *pipeline):
    pipeline_(pipeline)
  {}
  virtual ~ObPipelineOperator() {}
  virtual bool is_valid() const { return false; }
  virtual int execute_op(const ObChunk &input_chunk,
                         ResultState &result_state,
                         ObChunk &output_chunk);
  share::ObIDag *get_dag();

  DECLARE_PURE_VIRTUAL_TO_STRING;

protected:
  virtual int execute(const ObChunk &input_chunk,
                      ResultState &result_state,
                      ObChunk &output_chunk) = 0;
  virtual int try_execute_finish(const ObChunk &input_chunk,
                                 ResultState &result_state,
                                 ObChunk &output_chunk);

protected:
  ObPipeline *pipeline_;
};

class ObPipeline: public share::ObITask
{
public:
  explicit ObPipeline(const share::ObITask::ObITaskType &task_type)
    : ObITask(task_type)
  {}
  ~ObPipeline() {}
  int add_op(ObPipelineOperator *op);
  int push(const ObChunk &chunk_data);
  virtual int process() override { return common::OB_NOT_IMPLEMENT; }

private:
  int execute_ops(const int64_t start_pos, const ObChunk &chunk_data);

protected:
  common::ObArray<ObPipelineOperator *> ops_;
};

}  // end namespace storage
}  // end namespace oceanbase
#endif//_OCEANBASE_STORAGE_DDL_OB_PIPELINE_H_
