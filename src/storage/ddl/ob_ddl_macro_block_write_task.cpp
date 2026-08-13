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
#include "storage/ddl/ob_ddl_macro_block_write_task.h"
#include "storage/ddl/ob_ddl_storage_util.h"
#include "storage/ddl/ob_ddl_tablet_context.h"
#include "storage/ddl/ob_ddl_macro_block_writer.h"
#include "storage/ddl/ob_tablet_slice_row_iterator.h"
#include "src/storage/ddl/ob_ddl_insert_dag.h"
#include "storage/blocksstable/ob_logic_macro_id.h" // for ObMacroDataSeq
#include "storage/ddl/ob_ddl_batch_datum_rows.h"
#include "src/storage/ddl/ob_tablet_ddl_kv_mgr.h"

#define USING_LOG_PREFIX STORAGE

using namespace oceanbase;
using namespace oceanbase::common;
using namespace oceanbase::share;
using namespace oceanbase::storage;
using namespace oceanbase::blocksstable;
using namespace oceanbase::sql;

ObDDLScanTask::ObDDLScanTask(const ObITaskType type)
  : ObITask(type),
    ddl_dag_(nullptr)
{
}

ObDDLScanTask::ObDDLScanTask()
  : ObDDLScanTask(TASK_TYPE_DDL_PREPARE_SCAN)
{
}

ObDDLScanTask::~ObDDLScanTask()
{

}

int ObDDLScanTask::init(ObDDLIndependentDag *ddl_dag)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(nullptr == ddl_dag)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", K(ret));
  } else {
    ddl_dag_ = ddl_dag;
  }
  return ret;
}

ObITask::ObITaskPriority ObDDLScanTask::get_priority()
{
  int ret = OB_SUCCESS;
  ObITask::ObITaskPriority priority = ObITask::get_priority();
  if (OB_ISNULL(ddl_dag_)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("ddl dag is null", K(ret), KP(ddl_dag_));
  } else {
    priority = ddl_dag_->is_scan_finished() && 0 == ddl_dag_->get_pipeline_count() ? ObITask::TASK_PRIO_2 : ObITask::TASK_PRIO_0;
  }
  return priority;
}

int ObDDLScanTask::process()
{
  int ret = OB_SUCCESS;
  // do nothing
  return ret;
}

ObDDLTabletScanTask::ObDDLTabletScanTask()
  : ObITask(TASK_TYPE_DDL_PREPARE_SCAN)
{
}

bool ObWriteMacroBaseOperator::is_valid() const
{
  return is_inited_;
}

int ObWriteMacroBaseOperator::init(const ObWriteMacroParam &param)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(!param.is_valid())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid arguments", K(ret), K(param));
  } else if (OB_FAIL(slice_writer_.init(param))) {
  } else {
    is_inited_ = true;
  }
  return ret;
}

/**
* -----------------------------------ObDDLWriteMacroBlockOperator-----------------------------------
*/
int ObDDLWriteMacroBlockOperator::execute(
    const ObChunk &input_chunk,
    ResultState &result_state,
    ObChunk &output_chunk)
{
  int ret = OB_SUCCESS;
  output_chunk.reset();
  result_state = ObPipelineOperator::NEED_MORE_INPUT;
  if (OB_UNLIKELY(!is_inited_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("the ObDDLWriteMacroBlockOperator has been not initialized", K(ret));
  } else if (OB_UNLIKELY(!input_chunk.is_valid() ||
                         (!input_chunk.is_end_chunk() && !input_chunk.is_ddl_batch_datum_rows_type()))) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("there are invalid arguments", K(ret), K(input_chunk));
  } else if (input_chunk.is_end_chunk()) {
    if (OB_FAIL(slice_writer_.close())) {
    }
  } else if (OB_FAIL(slice_writer_.append_batch(input_chunk.ddl_batch_rows_->datum_rows_))) {
  }
  return ret;
}


int ObDDLRowFileWriteMacroBlockOperator::execute(const ObChunk &input_chunk,
                                                 ResultState &result_state,
                                                 ObChunk &output_chunk)
{
  int ret = OB_SUCCESS;
  output_chunk.reset();
  result_state = ObPipelineOperator::NEED_MORE_INPUT;
  if (OB_UNLIKELY(!is_inited_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("row file writer is not initialized", K(ret));
  } else if (OB_UNLIKELY(!input_chunk.is_valid() ||
                         (!input_chunk.is_ddl_row_tmp_files_type() && !input_chunk.is_end_chunk()))) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("there are invalid arguments", K(ret), K(input_chunk));
  } else if (input_chunk.is_end_chunk()) {
    if (OB_FAIL(slice_writer_.close())) {
    }
  } else {
    ObArray<ObDDLRowFile *> *row_files = input_chunk.row_file_arr_;
    for (int64_t i = 0; OB_SUCC(ret) && i < row_files->count(); ++i) {
      ObDDLRowFile *&row_file = row_files->at(i);
      blocksstable::ObBatchDatumRows *batch_rows = nullptr;
      if (OB_ISNULL(row_file)) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("row file is null", K(ret), K(i));
      }
      while (OB_SUCC(ret)) {
        if (OB_FAIL(row_file->get_next_batch(batch_rows))) {
          if (OB_ITER_END == ret) {
            ret = OB_SUCCESS;
            break;
          } else {
            LOG_WARN("fail to get next row batch", K(ret), KPC(row_file));
          }
        } else if (OB_FAIL(slice_writer_.append_batch(*batch_rows))) {
        }
      }
      if (OB_SUCC(ret) && OB_FAIL(row_file->close())) {
        LOG_WARN("fail to close row file", K(ret), KPC(row_file));
      }
      if (OB_SUCC(ret)) {
        row_file->~ObDDLRowFile();
        ob_free(row_file);
        row_file = nullptr;
      }
    }
  }
  return ret;
}

/**
* -----------------------------------ObDDLWriteMacroBlockBasePipeline-----------------------------------
*/
int ObDDLWriteMacroBlockBasePipeline::get_next_chunk(ObChunk *&next_chunk)
{
  int ret = OB_SUCCESS;
  next_chunk = nullptr;
  static const int64_t timeout_us = 1000L; // 1ms
  if (OB_ISNULL(ddl_slice_)) {
    ret = OB_ERR_SYS;
    LOG_WARN("ddl slice is null", K(ret), KPC(ddl_slice_));
  } else if (OB_FAIL(ddl_slice_->pop_chunk(next_chunk))) {
    if (OB_ENTRY_NOT_EXIST != ret) {
      LOG_WARN("pop ddl chunk failed", K(ret));
    }
  }
  return ret;
}

ObITask::ObITaskPriority ObDDLWriteMacroBlockBasePipeline::get_priority()
{
  ObITask::ObITaskPriority priority = ObITask::get_priority();
  if (nullptr != ddl_slice_ && nullptr != dag_) {
    if (ddl_slice_->get_queue_size() > 0 || ddl_slice_->has_end_chunk()) {
      priority = ObITask::TASK_PRIO_1;
    } else {
      priority = ObITask::TASK_PRIO_0;
    }
  }
  return priority;
}

int ObDDLWriteMacroBlockBasePipeline::finish_chunk(ObChunk *chunk)
{
  int ret = OB_SUCCESS;
  if (OB_NOT_NULL(chunk)) {
    chunk->~ObChunk();
    ob_free(chunk);
    chunk = nullptr;
  }
  return ret;
}

int ObDDLWriteMacroBlockBasePipeline::fill_writer_param(ObWriteMacroParam &param)
{
  int ret = OB_SUCCESS;
  ObDDLIndependentDag *dag = nullptr;
  if (OB_ISNULL(dag = static_cast<ObDDLIndependentDag *>(get_dag())) || OB_ISNULL(ddl_slice_)) {
    ret = OB_ERR_SYS;
    LOG_WARN("error sys, dag must not be nullptr", K(ret), KPC(get_dag()), KPC(ddl_slice_));
  } else if (OB_FAIL(ObDDLStorageUtil::fill_writer_param(ddl_slice_->get_tablet_id(),
                                                  ddl_slice_->get_slice_idx(),
                                                  dag,
                                                  0/*max_batch_size*/,
                                                  param))) {
  }
  return ret;
}

void ObDDLWriteMacroBlockBasePipeline::postprocess(int &ret_code)
{
  int ret = OB_SUCCESS;
  ObDDLIndependentDag *dag = static_cast<ObDDLIndependentDag *>(get_dag());
  if (OB_ITER_END != ret_code
      && OB_DAG_TASK_IS_SUSPENDED != ret_code) {
    FLOG_INFO("ret code not expected", K(ret_code), KPC(this), KPC(dag));
  } else if (OB_ITER_END == ret_code) {
    ret_code = OB_SUCCESS;
    ObDDLTabletContext *tablet_context = nullptr;
    if (OB_ISNULL(dag) || OB_ISNULL(ddl_slice_)) {
      ret = OB_ERR_SYS;
      LOG_WARN("get dag failed", K(ret), KPC(get_dag()), KPC(ddl_slice_));
    } else if (OB_FAIL(dag->get_tablet_context(ddl_slice_->get_tablet_id(), tablet_context))) {
    } else {
      LOG_INFO("not data any more, change ret to be success", K(ret), K(dag->get_ddl_task_param()));
      ret_code = OB_SUCCESS;
    }
  }
  if (OB_DAG_TASK_IS_SUSPENDED != ret_code) {
    // pipeline exit
    if (OB_NOT_NULL(dag)) {
      dag->dec_pipeline_count();
    }
  }
}

/**
* -----------------------------------ObDDLMemoryFriendWriteMacroBlockPipeline-----------------------------------
*/
int ObDDLMemoryFriendWriteMacroBlockPipeline::init(ObDDLSlice *ddl_slice)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(nullptr == ddl_slice || !ddl_slice->is_inited())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("there are invalid arguments", K(ret), KPC(ddl_slice));
  } else {
    ddl_slice_ = ddl_slice;
    if (OB_FAIL(fill_writer_param(write_param_))) {
    } else if (OB_FAIL(write_op_.init(write_param_))) {
    } else if (OB_FAIL(add_op(&write_op_))) {
    }
  }
  return ret;
}

/**
* -----------------------------------ObBatchDatumRowsWriteOp-----------------------------------
*/
int ObBatchDatumRowsWriteOp::init(const ObTabletID &tablet_id, const int64_t slice_idx)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(!tablet_id.is_valid() || slice_idx < 0)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("there are invalid arguments", K(ret), K(tablet_id), K(slice_idx));
  } else if (OB_UNLIKELY(nullptr == get_dag() ||
                         share::ObDagType::DAG_TYPE_DDL != get_dag()->get_type())) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("the dag is null or dag type is not ddl dag", K(ret), KP(get_dag()));
  } else {
    ObDDLRowFlag row_flag;
    ObDDLIndependentDag *ddl_dag = dynamic_cast<ObDDLIndependentDag *>(get_dag());
    tablet_id_ = tablet_id;
    slice_idx_ = slice_idx;
    if (OB_UNLIKELY(nullptr == ddl_dag)) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("ddl dag is null", K(ret));
    } else if (OB_FAIL(buffer_.init(ddl_dag->get_ddl_table_schema().column_items_,
                                    MAX_BATCH_SIZE,
                                    row_flag))) {
    } else {
      const ObIArray<ObDDLVector *> &vectors = buffer_.get_vectors();
      for (int64_t i = 0; OB_SUCC(ret) && i < vectors.count(); ++i) {
        if (OB_FAIL(bdrs_.vectors_.push_back(vectors.at(i)->get_vector()))) {
        }
      }
      if (OB_SUCC(ret)) {
        is_inited_ = true;
      }
    }
  }
  return ret;
}

int ObBatchDatumRowsWriteOp::execute(
    const ObChunk &input_chunk,
    ResultState &result_state,
    ObChunk &output_chunk)
{
  int ret = OB_SUCCESS;
  output_chunk.reset();
  result_state = ObPipelineOperator::NEED_MORE_INPUT;
  if (OB_UNLIKELY(!is_inited_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("the ObBatchDatumRowsWriteOp is not initialized", K(ret));
  } else if (OB_UNLIKELY(!input_chunk.is_valid() ||
                         (!input_chunk.is_end_chunk() && !input_chunk.is_datum_row_type()))) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("the input chunk is invalid", K(ret), K(input_chunk));
  } else {
    if (buffer_need_reuse_) {
      buffer_.reuse();
      buffer_need_reuse_ = false;
    }
    if (input_chunk.is_end_chunk()) {
      if (OB_FAIL(generate_data_chunk(output_chunk))) {
      }
    } else {
      if (OB_FAIL(buffer_.append_row(*input_chunk.datum_row_))) {
      } else if (buffer_.full() && OB_FAIL(generate_data_chunk(output_chunk))) {
        LOG_WARN("fail to generate output chunk", K(ret));
      }
    }
  }
  return ret;
}

int ObBatchDatumRowsWriteOp::generate_data_chunk(ObChunk &output_chunk)
{
  int ret = OB_SUCCESS;
  output_chunk.reset();
  if (OB_LIKELY(buffer_.size() > 0)) {
    buffer_need_reuse_ = true;
    bdrs_.row_count_ = buffer_.size();
    output_chunk.type_ = ObChunk::BATCH_DATUM_ROWS;
    output_chunk.bdrs_ = &bdrs_;
  }
  return ret;
}
