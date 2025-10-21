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

#include "sql/resolver/ddl/ob_create_mlog_stmt.h"
namespace oceanbase
{
namespace sql
{
ObCreateMLogStmt::ObCreateMLogStmt(ObIAllocator *name_pool)
    : ObDDLStmt(name_pool, stmt::T_CREATE_MLOG),
    create_mlog_arg_()
{
}

ObCreateMLogStmt::ObCreateMLogStmt()
    : ObDDLStmt(stmt::T_CREATE_MLOG),
    create_mlog_arg_()
{
}

ObCreateMLogStmt::~ObCreateMLogStmt()
{
}

} // sql
} // oceanbase
