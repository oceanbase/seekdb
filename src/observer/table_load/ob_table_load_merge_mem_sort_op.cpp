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

#include "observer/table_load/ob_table_load_merge_mem_sort_op.h"
#include "observer/table_load/ob_table_load_mem_compactor.h"
#include "observer/table_load/ob_table_load_multiple_heap_table_compactor.h"
#include "storage/direct_load/ob_direct_load_table_store.h"

namespace oceanbase
{
namespace observer
{
using namespace common;
using namespace storage;

ObTableLoadMergeMemSortOp::ObTableLoadMergeMemSortOp(ObTableLoadMergeTableBaseOp *parent)
  : ObTableLoadMergeTableBaseOp(parent),
    mem_compactor_(nullptr),
    multiple_heap_table_compactor_(nullptr)
{
}

ObTableLoadMergeMemSortOp::~ObTableLoadMergeMemSortOp()
{
  if (nullptr != mem_compactor_) {
    mem_compactor_->~ObTableLoadMemCompactor();
    allocator_->free(mem_compactor_);
    mem_compactor_ = nullptr;
  }
  if (nullptr != multiple_heap_table_compactor_) {
    multiple_heap_table_compactor_->~ObTableLoadMultipleHeapTableCompactor();
    allocator_->free(multiple_heap_table_compactor_);
    multiple_heap_table_compactor_ = nullptr;
  }
}

void ObTableLoadMergeMemSortOp::stop()
{
  if (nullptr != mem_compactor_) {
    mem_compactor_->stop();
  }
  if (nullptr != multiple_heap_table_compactor_) {
    multiple_heap_table_compactor_->stop();
  }
}

int ObTableLoadMergeMemSortOp::switch_next_op(bool is_parent_called)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(!is_parent_called)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("unexpected is not parent called", KR(ret), K(lbt()));
  } else if (OB_ISNULL(merge_table_ctx_)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("unexpected merge table ctx is null", KR(ret), KP(merge_table_ctx_));
  } else if (OB_UNLIKELY(merge_table_ctx_->table_store_->empty() ||
                         !merge_table_ctx_->table_store_->is_external_table())) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("unexpected table store", KR(ret), KPC(merge_table_ctx_->table_store_));
  } else if (merge_table_ctx_->table_store_->get_table_data_desc()
               .row_flag_.uncontain_hidden_pk_) { // without primary key
    if (OB_ISNULL(multiple_heap_table_compactor_ =
                    OB_NEWx(ObTableLoadMultipleHeapTableCompactor, allocator_))) {
      ret = OB_ALLOCATE_MEMORY_FAILED;
      LOG_WARN("fail to new ObTableLoadMultipleHeapTableCompactor", KR(ret));
    } else if (OB_FAIL(multiple_heap_table_compactor_->init(this))) {
      LOG_WARN("fail to init multiple heap table compactor", KR(ret));
    } else if (OB_FAIL(multiple_heap_table_compactor_->start())) {
      LOG_WARN("fail to start multiple heap table compactor", KR(ret));
    }
  } else {
    if (OB_ISNULL(mem_compactor_ = OB_NEWx(ObTableLoadMemCompactor, allocator_))) {
      ret = OB_ALLOCATE_MEMORY_FAILED;
      LOG_WARN("fail to new ObTableLoadMemCompactor", KR(ret));
    } else if (OB_FAIL(mem_compactor_->init(this))) {
      LOG_WARN("fail to init mem compactor", KR(ret));
    } else if (OB_FAIL(mem_compactor_->start())) {
      LOG_WARN("fail to start mem compactor", KR(ret));
    }
  }
  return ret;
}

int ObTableLoadMergeMemSortOp::on_success()
{
  int ret = OB_SUCCESS;
  if (OB_FAIL(switch_parent_op())) {
    LOG_WARN("fail to switch parent op", KR(ret));
  }
  return ret;
}

} // namespace observer
} // namespace oceanbase
