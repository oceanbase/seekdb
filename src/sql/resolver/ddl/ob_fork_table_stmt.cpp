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
#include "sql/resolver/ddl/ob_fork_table_stmt.h"
#include "lib/allocator/ob_allocator.h"

namespace oceanbase
{
using namespace common;
namespace sql
{

ObForkTableStmt::ObForkTableStmt(ObIAllocator *name_pool)
  : ObDDLStmt(name_pool, stmt::T_FORK_TABLE),
    fork_table_arg_()
{
}

ObForkTableStmt::ObForkTableStmt()
  : ObDDLStmt(stmt::T_FORK_TABLE),
    fork_table_arg_()
{
}

ObForkTableStmt::~ObForkTableStmt()
{
}

}
}
