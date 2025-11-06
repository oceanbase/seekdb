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

#include "observer/table_load/dag/ob_table_load_dag_parallel_heap_table_compactor.h"
#include "observer/table_load/ob_table_load_store_ctx.h"
#include "observer/table_load/ob_table_load_table_ctx.h"
#include "observer/table_load/plan/ob_table_load_table_op.h"
#include "storage/direct_load/ob_direct_load_table_store.h"
#include "storage/direct_load/ob_direct_load_multiple_heap_table.h"

namespace oceanbase
{
namespace observer
{
using namespace common;
using namespace blocksstable;
using namespace storage;
using namespace lib;

/**
 * ObTableLoadDagHeapTableCompare
 */

ObTableLoadDagHeapTableCompare::ObTableLoadDagHeapTableCompare() : result_code_(OB_SUCCESS) {}

ObTableLoadDagHeapTableCompare::~ObTableLoadDagHeapTableCompare() {}

bool ObTableLoadDagHeapTableCompare::operator()(const ObDirectLoadTableHandle lhs,
                                             const ObDirectLoadTableHandle rhs)
{
  int ret = OB_SUCCESS;
  int cmp_ret = 0;
  if (OB_UNLIKELY(!lhs.is_valid() || !lhs.get_table()->is_multiple_heap_table() ||
                  !rhs.is_valid() || !rhs.get_table()->is_multiple_heap_table())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid args", KR(ret), K(lhs), K(rhs));
  } else {
    ObDirectLoadMultipleHeapTable *lhs_multi_sstable =
      static_cast<ObDirectLoadMultipleHeapTable *>(lhs.get_table());
    ObDirectLoadMultipleHeapTable *rhs_multi_sstable =
      static_cast<ObDirectLoadMultipleHeapTable *>(rhs.get_table());
    cmp_ret = lhs_multi_sstable->get_meta().index_entry_count_ -
              rhs_multi_sstable->get_meta().index_entry_count_;
  }
  if (OB_FAIL(ret)) {
    result_code_ = ret;
  }
  return cmp_ret < 0;
}

/**
 * ObTableLoadDagParallelHeapTableCompactor
 */

ObTableLoadDagParallelHeapTableCompactor::ObTableLoadDagParallelHeapTableCompactor()
  : store_ctx_(nullptr),
    op_ctx_(nullptr),
    is_inited_(false)
{
}

ObTableLoadDagParallelHeapTableCompactor::~ObTableLoadDagParallelHeapTableCompactor()
{
}

int ObTableLoadDagParallelHeapTableCompactor::init(ObTableLoadStoreCtx *store_ctx,
                                                ObTableLoadTableOpCtx *op_ctx)
{
  int ret = OB_SUCCESS;
  if (IS_INIT) {
    ret = OB_INIT_TWICE;
    LOG_WARN("ObTableLoadDagParallelHeapTableCompactor init twice", KR(ret), KP(this));
  } else if (OB_UNLIKELY(store_ctx == nullptr || op_ctx == nullptr)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid args", KR(ret), KP(store_ctx), KP(op_ctx));
  } else {
    store_ctx_ = store_ctx;
    op_ctx_ = op_ctx;
    is_inited_ = true;
  }
  return ret;
}

int ObTableLoadDagParallelHeapTableCompactor::close()
{
  int ret = OB_SUCCESS;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("ObTableLoadDagParallelHeapTableCompactor not init", KR(ret), KP(this));
  } else {
    ObDirectLoadTableStore &table_store = op_ctx_->table_store_;
    table_store.clear();
    table_store.set_multiple_heap_table();
    if (OB_FAIL(table_store.add_tables(tables_handle_))) {
      LOG_WARN("fail to add tables", KR(ret));
    }
  }
  return ret;
}

} // namespace observer
} // namespace oceanbase
