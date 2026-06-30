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

#define USING_LOG_PREFIX SQL_ENG
#include "sql/engine/sequence/ob_sequence_op.h"
#include "sql/engine/ob_exec_context.h"

namespace oceanbase
{
using namespace common;
using namespace share;
using namespace share::schema;
using namespace common::sqlclient;
namespace sql
{

OB_SERIALIZE_MEMBER((ObSequenceSpec, ObOpSpec), nextval_seq_ids_);

ObSequenceSpec::ObSequenceSpec(ObIAllocator &alloc, const ObPhyOperatorType type)
    : ObOpSpec(alloc, type),
    nextval_seq_ids_(alloc)
{
}

int ObSequenceSpec::add_uniq_nextval_sequence_id(uint64_t seq_id)
{
  int ret = OB_SUCCESS;
  for (uint64_t i = 0; i < nextval_seq_ids_.count() && OB_SUCC(ret); ++i) {
    if (seq_id == nextval_seq_ids_.at(i)) {
      ret = OB_ENTRY_EXIST;
      LOG_WARN("should not add duplicated seq id to ObSequence operator",
              K(seq_id), K(ret));
    }
  }
  if (OB_SUCC(ret)) {
    if (OB_FAIL(nextval_seq_ids_.push_back(seq_id))) {
    }
  }
  return ret;
}

ObLocalSequenceExecutor::ObLocalSequenceExecutor()
  :ObSequenceExecutor(),
  sequence_cache_(nullptr)
{
  sequence_cache_ = &share::ObSequenceCache::get_instance();
  if (OB_ISNULL(sequence_cache_)) {
    LOG_ERROR_RET(OB_ALLOCATE_MEMORY_FAILED, "fail alloc memory for ObSequenceCache instance");
  }
}

ObLocalSequenceExecutor::~ObLocalSequenceExecutor()
{
  destroy();
}

int ObLocalSequenceExecutor::init(ObExecContext &ctx)
{
  int ret = OB_SUCCESS;
  ObTaskExecutorCtx *task_ctx = NULL;
  share::schema::ObMultiVersionSchemaService *schema_service = NULL;
  ObSQLSessionInfo *my_session = NULL;
  share::schema::ObSchemaGetterGuard schema_guard;
  if (OB_ISNULL(my_session = GET_MY_SESSION(ctx))) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("fail to get my session", K(ret));
  } else if (OB_ISNULL(task_ctx = GET_TASK_EXECUTOR_CTX(ctx))) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("task executor ctx is null", K(ret));
  } else if (OB_ISNULL(schema_service = task_ctx->schema_service_)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("schema service is null", K(ret));
  } else if (OB_FAIL(schema_service->get_tenant_schema_guard(
                      schema_guard))) {
  } else {
    
    ARRAY_FOREACH_X(seq_ids_, idx, cnt, OB_SUCC(ret)) {
      const uint64_t seq_id = seq_ids_.at(idx);
      const ObSequenceSchema *seq_schema = nullptr;
      if (OB_FAIL(schema_guard.get_sequence_schema(
                  seq_id,
                  seq_schema))) {
      } else if (OB_ISNULL(seq_schema)) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("null unexpected", K(ret));
      } else if (OB_FAIL(seq_schemas_.push_back(*seq_schema))) {
      }
    }
  }
  return ret;
}

void ObLocalSequenceExecutor::reset()
{
  ObSequenceExecutor::reset();
}

void ObLocalSequenceExecutor::destroy()
{
  sequence_cache_ = NULL;
  ObSequenceExecutor::destroy();
}

int ObLocalSequenceExecutor::get_nextval(ObExecContext &ctx)
{
  int ret = OB_SUCCESS;
  ObSQLSessionInfo *my_session = GET_MY_SESSION(ctx);
  if (seq_ids_.count() != seq_schemas_.count()) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("id count does not match schema count",
             "id_cnt", seq_ids_.count(),
             "schema_cnt", seq_schemas_.count(),
             K(ret));
  } else {
    
    ObArenaAllocator allocator; // nextval temporary calculation memory
    // When and only when there is nextval in select item, it is necessary to update nextval in cache
    // Otherwise directly use the value from the session
    ARRAY_FOREACH_X(seq_ids_, idx, cnt, OB_SUCC(ret)) {
      const uint64_t seq_id = seq_ids_.at(idx);
      // int64_t dummy_seq_value = 10240012435; // TODO: xiaochu, set number to session
      ObSequenceValue seq_value;
      // Note: Here the order of schema and the order of id in ids are one-to-one corresponding
      //       So you can directly use the index to address
      ObAutoincrementService &auto_service = ObAutoincrementService::get_instance();
      if (seq_schemas_.at(idx).get_order_flag()
          && seq_schemas_.at(idx).get_cache_order_mode() == NEW_ACTION) {
        if (OB_FAIL(auto_service.get_handle(seq_schemas_.at(idx), seq_value))) {
        }
      } else {
        if (OB_FAIL(sequence_cache_->nextval(seq_schemas_.at(idx), allocator, seq_value))) {
        }
      }
      if (OB_SUCC(ret) && OB_FAIL(my_session->set_sequence_value(seq_id, seq_value))) {
        LOG_WARN("save seq_value to session as currval for later read fail",
                 K(seq_id), K(seq_value), K(ret));
      }
    }
  }
  return ret;
}

ObSequenceOp::ObSequenceOp(ObExecContext &exec_ctx, const ObOpSpec &spec, ObOpInput *input)
  : ObOperator(exec_ctx, spec, input)
{
}

ObSequenceOp::~ObSequenceOp()
{
}

void ObSequenceOp::destroy()
{
  for (int64_t i = 0; i < seq_executors_.count(); ++i) {
    seq_executors_.at(i)->destroy();
  }
  seq_executors_.reset();
  ObOperator::destroy();
}

int ObSequenceOp::inner_open()
{
  int ret = OB_SUCCESS;
  ObSQLSessionInfo *my_session = NULL;
  if (OB_ISNULL(my_session = GET_MY_SESSION(ctx_))) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("fail to get my session", K(ret));
  } else if (OB_FAIL(init_op())) {
  } else if (get_child_cnt() > 1) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("should not have more than 1 child", K(ret));
  } else if (0 < MY_SPEC.filters_.count()) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("sequence operator should have no filter expr", K(ret));
  }
  ARRAY_FOREACH_X(MY_SPEC.nextval_seq_ids_, idx, cnt, OB_SUCC(ret)) {
    const uint64_t seq_id = MY_SPEC.nextval_seq_ids_.at(idx);
    ObSequenceExecutor *executor = NULL;
    if (NULL != executor) {
      if (OB_FAIL(executor->add_sequence_id(seq_id))) {
      }
    } else {
      //add local executor
      void *tmp = NULL;
      if (OB_ISNULL(tmp=ctx_.get_allocator().alloc(sizeof(ObLocalSequenceExecutor)))) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_WARN("failed to alloc memory", K(ret));
      } else {
        executor = new(tmp) ObLocalSequenceExecutor();
        if (OB_FAIL(executor->add_sequence_id(seq_id))) {
        } else if (OB_FAIL(seq_executors_.push_back(executor))) {
        }
      }
    }
  }
  ARRAY_FOREACH_X(seq_executors_, idx, cnt, OB_SUCC(ret)) {
    if (OB_FAIL(seq_executors_.at(idx)->init(ctx_))) {
    }
  }
  return ret;
}

int ObSequenceOp::inner_close()
{
  int ret = OB_SUCCESS;
  reset();
  return ret;
}

int ObSequenceOp::init_op()
{
  int ret = OB_SUCCESS;
  return ret;
}

int ObSequenceOp::inner_get_next_row()
{
  int ret = OB_SUCCESS;
  if (OB_FAIL(try_get_next_row())) {
    LOG_WARN_IGNORE_ITER_END(ret, "fail get next row", K(ret));
  } else {
    ARRAY_FOREACH_X(seq_executors_, idx, cnt, OB_SUCC(ret)) {
      ObSequenceExecutor *executor = seq_executors_.at(idx);
      if (OB_FAIL(executor->get_nextval(ctx_))) {
      }
    }
  }
  return ret;
}

int ObSequenceOp::try_get_next_row()
{
  int ret = OB_SUCCESS;
  clear_evaluated_flag();
  if (get_child_cnt() == 0) {
    // insert stmt, no child, give an empty row
    // Here whether to set all ObExpr to null
  } else if (OB_FAIL(child_->get_next_row())) {
    LOG_WARN_IGNORE_ITER_END(ret, "fail get next row", K(ret));
  }
  return ret;
}

}  // namespace sql
}  // namespace oceanbase
