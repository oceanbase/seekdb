/**
 * Copyright (c) 2021 OceanBase
 * OceanBase CE is licensed under Mulan PubL v2.
 * You can use this software according to the terms and conditions of the Mulan PubL v2.
 * You may obtain a copy of Mulan PubL v2 at:
 *          http://license.coscl.org.cn/MulanPubL-2.0
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND,
 * EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
 * MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
 * See the Mulan PubL v2 for more details.
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
