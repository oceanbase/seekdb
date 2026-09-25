/*
 * Copyright (c) 2026 OceanBase.
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
#include "sql/engine/graph/graph_binding_store.h"
#include "lib/utility/ob_macro_utils.h"
#include "share/ob_errno.h"

#include <algorithm>

namespace oceanbase
{
using namespace common;
namespace sql
{

int GraphBindingMemoryAllocator::init(int64_t memory_limit)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(memory_limit_ > 0)) {
    ret = OB_INIT_TWICE;
    LOG_WARN("graph binding allocator is already initialized", K(ret),
             K_(memory_limit));
  } else if (OB_UNLIKELY(memory_limit <= 0)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid graph binding allocator limit", K(ret),
             K(memory_limit));
  } else {
    memory_limit_ = memory_limit;
  }
  return ret;
}

void *GraphBindingMemoryAllocator::alloc(int64_t size)
{
  return alloc(size, attr_);
}

void *GraphBindingMemoryAllocator::alloc(
    int64_t size,
    const lib::ObMemAttr &attr)
{
  void *ptr = nullptr;
  const int64_t used = tracker_.used();
  UNUSED(attr);
  if (size <= 0 || memory_limit_ <= 0) {
  } else if (used < 0 || used > memory_limit_
             || size > memory_limit_ - used) {
    limit_exceeded_ = true;
  } else {
    ptr = tracked_allocator_.alloc(size, attr_);
  }
  return ptr;
}

void GraphBindingMemoryAllocator::free(void *ptr)
{
  tracked_allocator_.free(ptr);
}

int GraphBindingStore::init(int64_t memory_limit)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(memory_limit_ > 0)) {
    ret = OB_INIT_TWICE;
    LOG_WARN("graph binding store is already initialized", K(ret),
             K_(memory_limit));
  } else if (OB_UNLIKELY(memory_limit <= 0)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid graph binding memory limit", K(ret), K(memory_limit));
  } else if (OB_FAIL(memory_allocator_.init(memory_limit))) {
    LOG_WARN("failed to initialize graph binding allocator", K(ret),
             K(memory_limit));
  } else {
    memory_limit_ = memory_limit;
  }
  return ret;
}

int64_t GraphBindingStore::used_memory() const
{
  return memory_allocator_.used();
}

int GraphBindingStore::check_memory_limit()
{
  int ret = OB_SUCCESS;
  const int64_t used = used_memory();
  if (OB_UNLIKELY(memory_limit_ <= 0)) {
    ret = OB_NOT_INIT;
    LOG_WARN("graph binding store is not initialized", K(ret));
  } else if (OB_UNLIKELY(used > memory_limit_)) {
    ret = OB_EXCEED_QUERY_MEM_LIMIT;
    LOG_WARN("graph binding store exceeded query memory limit", K(ret),
             K(used), K_(memory_limit));
  } else {
    peak_memory_bytes_ = std::max(peak_memory_bytes_, used);
  }
  return ret;
}

int GraphBindingStore::add_binding(const ObIArray<ObExpr *> &exprs,
                                   ObEvalCtx &eval_ctx,
                                   int64_t &binding_id)
{
  int ret = OB_SUCCESS;
  int64_t row_size = 0;
  ObChunkDatumStore::StoredRow *row = nullptr;
  binding_id = -1;
  memory_allocator_.clear_limit_exceeded();
  if (OB_UNLIKELY(memory_limit_ <= 0)) {
    ret = OB_NOT_INIT;
    LOG_WARN("graph binding store is not initialized", K(ret));
  } else if (OB_UNLIKELY(exprs.count() <= 0
                         || exprs.count() > UINT32_MAX
                         || rows_.count() == INT64_MAX)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid graph binding row", K(ret),
             "column_count", exprs.count(), "binding_count", rows_.count());
  } else if (OB_FAIL(ObChunkDatumStore::Block::row_store_size(
                 exprs, eval_ctx, row_size))) {
    LOG_WARN("failed to size graph binding row", K(ret));
  } else if (OB_UNLIKELY(row_size > INT32_MAX)) {
    ret = OB_SIZE_OVERFLOW;
    LOG_WARN("graph binding row is too large", K(ret), K(row_size));
  } else if (OB_UNLIKELY(row_size > memory_limit_)) {
    ret = OB_EXCEED_QUERY_MEM_LIMIT;
    LOG_WARN("graph binding row exceeds query memory limit", K(ret),
             K(row_size), K_(memory_limit));
  } else if (OB_FAIL(ObChunkDatumStore::StoredRow::build(
                 row, exprs, eval_ctx, row_allocator_))) {
    if (memory_allocator_.limit_exceeded()) {
      ret = OB_EXCEED_QUERY_MEM_LIMIT;
    }
    LOG_WARN("failed to copy graph binding row", K(ret), K(row_size));
  } else if (OB_ISNULL(row)
             || OB_UNLIKELY(row->cnt_ != exprs.count())) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("graph binding row is inconsistent", K(ret), K(row),
             "column_count", exprs.count());
  } else if (OB_FAIL(rows_.push_back(row))) {
    if (memory_allocator_.limit_exceeded()) {
      ret = OB_EXCEED_QUERY_MEM_LIMIT;
    }
    LOG_WARN("failed to index graph binding row", K(ret));
  } else if (OB_FAIL(check_memory_limit())) {
  } else {
    binding_id = rows_.count() - 1;
  }
  if (OB_SUCCESS != ret) {
    ret = fail(ret);
  }
  return ret;
}

bool GraphBindingStore::is_valid_binding_id(int64_t binding_id) const
{
  return binding_id >= 0 && binding_id < rows_.count();
}

int GraphBindingStore::get_binding(
    int64_t binding_id,
    const ObChunkDatumStore::StoredRow *&row) const
{
  int ret = OB_SUCCESS;
  row = nullptr;
  if (OB_UNLIKELY(!is_valid_binding_id(binding_id))) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid graph binding id", K(ret), K(binding_id),
             "binding_count", rows_.count());
  } else if (OB_ISNULL(row = rows_.at(binding_id))) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("graph binding row is null", K(ret), K(binding_id));
  }
  return ret;
}

int GraphBindingStore::restore_binding(
    int64_t binding_id,
    const ObIArray<ObExpr *> &exprs,
    ObEvalCtx &eval_ctx) const
{
  int ret = OB_SUCCESS;
  const ObChunkDatumStore::StoredRow *row = nullptr;
  if (OB_FAIL(get_binding(binding_id, row))) {
  } else if (OB_UNLIKELY(row->cnt_ != exprs.count())) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("graph binding output shape changed", K(ret), K(binding_id),
             "stored_count", row->cnt_, "output_count", exprs.count());
  } else if (OB_FAIL(row->to_expr(exprs, eval_ctx))) {
    LOG_WARN("failed to restore graph binding row", K(ret), K(binding_id));
  }
  return ret;
}

int GraphBindingStore::fail(int error)
{
  reset();
  return error;
}

void GraphBindingStore::reset()
{
  rows_.reuse();
  row_allocator_.reuse();
  memory_allocator_.clear_limit_exceeded();
  peak_memory_bytes_ = 0;
}

} // namespace sql
} // namespace oceanbase
