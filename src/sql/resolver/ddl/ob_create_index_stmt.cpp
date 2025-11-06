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

#define USING_LOG_PREFIX SQL_RESV
#include "sql/resolver/ddl/ob_create_index_stmt.h"

namespace oceanbase
{
using namespace oceanbase::obrpc;
using namespace oceanbase::common;
namespace sql
{
ObCreateIndexStmt::ObCreateIndexStmt(ObIAllocator *name_pool)
    : ObPartitionedStmt(name_pool, stmt::T_CREATE_INDEX), create_index_arg_() ,table_id_(OB_INVALID_ID)
{
}

ObCreateIndexStmt::ObCreateIndexStmt()
    : ObPartitionedStmt(NULL, stmt::T_CREATE_INDEX), create_index_arg_(), table_id_(OB_INVALID_ID)
{
}

ObCreateIndexStmt::~ObCreateIndexStmt()
{
}

ObCreateIndexArg &ObCreateIndexStmt::get_create_index_arg()
{
  return create_index_arg_;
}

int ObCreateIndexStmt::add_sort_column(const ObColumnSortItem &sort_column)
{
  int ret = OB_SUCCESS;
  if (OB_USER_MAX_ROWKEY_COLUMN_NUMBER == create_index_arg_.index_columns_.count()) {
    ret = OB_ERR_TOO_MANY_ROWKEY_COLUMNS;
    LOG_USER_ERROR(OB_ERR_TOO_MANY_ROWKEY_COLUMNS, OB_USER_MAX_ROWKEY_COLUMN_NUMBER);
  } else if (OB_FAIL(create_index_arg_.index_columns_.push_back(sort_column))) {
    LOG_WARN("add index column failed", K(ret));
  } else {}
  return ret;
}

int ObCreateIndexStmt::add_storing_column(const ObString &column_name)
{
  return create_index_arg_.store_columns_.push_back(column_name);
}

int ObCreateIndexStmt::add_hidden_storing_column(const ObString &column_name)
{
  return create_index_arg_.hidden_store_columns_.push_back(column_name);
}


void ObCreateIndexStmt::set_comment(const ObString &comment)
{
  create_index_arg_.index_option_.comment_ = comment;
}

void ObCreateIndexStmt::set_storage_cache_policy(const ObString &storage_cache_policy)
{
  create_index_arg_.index_option_.storage_cache_policy_ = storage_cache_policy;
}

void ObCreateIndexStmt::set_index_name(const ObString &index_name)
{
  create_index_arg_.index_name_ = index_name;
}


}  // namespace sql
}  // namespace oceanbase
