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

#include "observer/table_load/ob_table_load_merge_insert_sstable_op.h"
#include "observer/table_load/ob_table_load_parallel_merger.h"
#include "observer/table_load/ob_table_load_schema.h"
#include "observer/table_load/ob_table_load_store_ctx.h"
#include "observer/table_load/ob_table_load_store_table_ctx.h"
#include "storage/direct_load/ob_direct_load_table_store.h"

namespace oceanbase
{
namespace observer
{
using namespace common;
using namespace storage;

ObTableLoadMergeInsertSSTableOp::ObTableLoadMergeInsertSSTableOp(
  ObTableLoadMergeTableBaseOp *parent)
  : ObTableLoadMergeTableBaseOp(parent), parallel_merger_(nullptr)
{
}

ObTableLoadMergeInsertSSTableOp::~ObTableLoadMergeInsertSSTableOp()
{
  if (nullptr != parallel_merger_) {
    parallel_merger_->~ObTableLoadParallelMerger();
    allocator_->free(parallel_merger_);
    parallel_merger_ = nullptr;
  }
}

void ObTableLoadMergeInsertSSTableOp::stop()
{
  if (nullptr != parallel_merger_) {
    parallel_merger_->stop();
  }
}

int ObTableLoadMergeInsertSSTableOp::switch_next_op(bool is_parent_called)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(!is_parent_called)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("unexpected is not parent called", KR(ret), K(lbt()));
  } else if (OB_ISNULL(merge_table_ctx_)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("unexpected merge table ctx is null", KR(ret), KP(merge_table_ctx_));
  } else if (OB_ISNULL(parallel_merger_ = OB_NEWx(ObTableLoadParallelMerger, allocator_))) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
    LOG_WARN("fail to new ObTableLoadParallelMerger", KR(ret));
  } else if (OB_FAIL(parallel_merger_->init_merge_task(this))) {
    LOG_WARN("fail to init merge task", KR(ret));
  } else if (OB_FAIL(parallel_merger_->start())) {
    LOG_WARN("fail to start parallel merge", KR(ret));
  }
  return ret;
}

int ObTableLoadMergeInsertSSTableOp::on_success()
{
  int ret = OB_SUCCESS;
  if (OB_FAIL(switch_parent_op())) {
    LOG_WARN("fail to switch parent op", KR(ret));
  }
  return ret;
}

} // namespace observer
} // namespace oceanbase
