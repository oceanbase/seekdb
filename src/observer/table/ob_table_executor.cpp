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

#define USING_LOG_PREFIX SERVER
#include "ob_table_executor.h"
#include "ob_table_executor_factory.h"

using namespace oceanbase::sql;

namespace oceanbase
{
namespace table
{

// NOTE: In fact, the table api executor/spec tree is just a doubly linked list
int ObTableApiSpec::create_executor(ObTableCtx &ctx, ObTableApiExecutor *&executor)
{
  int ret = OB_SUCCESS;
  ObTableApiSpec *cur_spec = this;
  ObTableApiExecutor *pre_executor = nullptr;
  ObTableApiExecutor *cur_executor = nullptr;
  ObTableApiExecutor *root_executor = nullptr;
  while(cur_spec != nullptr && OB_SUCC(ret)) {
    if (OB_FAIL(ObTableExecutorFactory::alloc_executor(ctx.get_allocator(),
                                                       ctx,
                                                       *cur_spec,
                                                       cur_executor))) {
      LOG_WARN("fail to alloc executor", K(ret));
    } else {
      if (root_executor == nullptr) {
        root_executor = cur_executor;
      }
      if (pre_executor != nullptr) {
        pre_executor->set_child(cur_executor);
        cur_executor->set_parent(pre_executor);
      }
      pre_executor = cur_executor;
      cur_spec = cur_spec->child_;
    }
  }

  if (OB_SUCC(ret)) {
    executor = root_executor;
  }

  return ret;
}

void ObTableApiExecutor::set_parent(ObTableApiExecutor *parent)
{
  parent_ = parent;
}

void ObTableApiExecutor::set_child(ObTableApiExecutor *child)
{
  child_ = child;
}

void ObTableApiExecutor::clear_evaluated_flag()
{
  if (tb_ctx_.has_generated_column() || 
      tb_ctx_.is_inc_or_append() || 
      tb_ctx_.has_global_index() ||
      tb_ctx_.is_global_index_back()) {
    ObExprFrameInfo *expr_info = const_cast<ObExprFrameInfo *>(tb_ctx_.get_expr_frame_info());
    if (OB_NOT_NULL(expr_info)) {
      for (int64_t i = 0; i < expr_info->rt_exprs_.count(); i++) {
        const ObExpr &expr = expr_info->rt_exprs_.at(i);
        if (expr.type_ != T_FUN_SYS_AUTOINC_NEXTVAL) {
          expr_info->rt_exprs_.at(i).clear_evaluated_flag(eval_ctx_);
        }
      }
    }
  }
}

}  // namespace table
}  // namespace oceanbase
