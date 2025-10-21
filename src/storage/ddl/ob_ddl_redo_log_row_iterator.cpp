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

#include "storage/ddl/ob_ddl_redo_log_row_iterator.h"

namespace oceanbase
{
namespace storage
{
using namespace common;
using namespace blocksstable;
using namespace transaction;
using namespace share::schema;

ObDDLRedoLogRowIterator::ObDDLRedoLogRowIterator(ObIAllocator &allocator, const uint64_t tenant_id)
  : allocator_(allocator),
    iter_(allocator, tenant_id), 
    rowkey_obobj_(nullptr),
    schema_rowkey_column_count_(0),
    is_inited_(false)
{ 
}

ObDDLRedoLogRowIterator::~ObDDLRedoLogRowIterator()
{ 
  reset();
}


void ObDDLRedoLogRowIterator::reset()
{
  iter_.reset();
  rowkey_.reset();
  if (rowkey_obobj_ != nullptr) {
    allocator_.free(rowkey_obobj_);
    rowkey_obobj_ = nullptr;
  }
  schema_rowkey_column_count_ = 0;
  is_inited_ = false;
}



} // namespace storage
} // namespace oceanbase
