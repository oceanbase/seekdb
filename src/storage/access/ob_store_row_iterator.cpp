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

#define USING_LOG_PREFIX STORAGE
#include "ob_store_row_iterator.h"
#include "storage/access/ob_table_access_context.h"

namespace oceanbase
{
using namespace common;
using namespace blocksstable;
namespace storage
{

ObStoreRowIterator::~ObStoreRowIterator()
{
}

void ObStoreRowIterator::reuse()
{
  is_sstable_iter_ = false;
}

void ObStoreRowIterator::reset()
{
  is_sstable_iter_ = false;
  is_reclaimed_ = false;
  block_row_store_ = nullptr;
  long_life_allocator_ = nullptr;
}

void ObStoreRowIterator::reclaim()
{
  reset();
  is_reclaimed_ = true;
}

int ObStoreRowIterator::init(
    const ObTableIterParam &iter_param,
    storage::ObTableAccessContext &access_ctx,
    ObITable *table,
    const void *query_range)
{
  int ret = OB_SUCCESS;
  is_sstable_iter_ = table->is_sstable();
  is_reclaimed_ = false;
  if (is_sstable_iter_) {
    block_row_store_ = access_ctx.block_row_store_;
  }
  if (OB_ISNULL(long_life_allocator_ = access_ctx.get_long_life_allocator())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("Unexpected null long life allocator", K(ret));
  } else if (OB_FAIL(inner_open(iter_param, access_ctx, table, query_range))) {
    STORAGE_LOG(WARN, "Failed to inner open ObStoreRowIterator", K(ret), K(iter_param), K(access_ctx));
  }
  return ret;
}

int ObStoreRowIterator::get_next_row(const ObDatumRow *&store_row)
{
  int ret = OB_SUCCESS;
  if (OB_FAIL(inner_get_next_row(store_row))) {
  }
  return ret;
}

}
}
