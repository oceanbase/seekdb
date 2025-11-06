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

#include "sql/resolver/ddl/ob_create_database_stmt.h"

using namespace oceanbase::common;
using namespace oceanbase::share::schema;

namespace oceanbase
{
namespace sql
{
ObCreateDatabaseStmt::ObCreateDatabaseStmt()
    : ObDDLStmt(stmt::T_CREATE_DATABASE),
    is_charset_specify_(false),
    is_collation_specify_(false),
    create_database_arg_()
  {
  }

ObCreateDatabaseStmt::ObCreateDatabaseStmt(common::ObIAllocator *name_pool)
    : ObDDLStmt(name_pool, stmt::T_CREATE_DATABASE),
    is_charset_specify_(false),
    is_collation_specify_(false),
    create_database_arg_()
{
}

ObCreateDatabaseStmt::~ObCreateDatabaseStmt()
{
}

void ObCreateDatabaseStmt::set_if_not_exists(bool if_not_exists)
{
  create_database_arg_.if_not_exist_ = if_not_exists;
}


int ObCreateDatabaseStmt::set_primary_zone(const common::ObString &zone)
{
  return OB_SUCCESS; // not supported
}

void ObCreateDatabaseStmt::set_read_only(const bool read_only)
{
  create_database_arg_.database_schema_.set_read_only(read_only);
}

int ObCreateDatabaseStmt::set_default_tablegroup_name(const common::ObString &tablegroup_name)
{
  return create_database_arg_.database_schema_.set_default_tablegroup_name(tablegroup_name);
}

void ObCreateDatabaseStmt::set_tenant_id(const uint64_t tenant_id)
{
  create_database_arg_.database_schema_.set_tenant_id(tenant_id);
}

int ObCreateDatabaseStmt::set_database_name(const ObString &database_name)
{
  return create_database_arg_.database_schema_.set_database_name(database_name);
}

void ObCreateDatabaseStmt::set_database_id(const uint64_t database_id)
{
  create_database_arg_.database_schema_.set_database_id(database_id);
}

const ObString &ObCreateDatabaseStmt::get_database_name() const
{
  return create_database_arg_.database_schema_.get_database_name_str();
}

void ObCreateDatabaseStmt::set_collation_type(const common::ObCollationType type)
{
  create_database_arg_.database_schema_.set_collation_type(type);
}

void ObCreateDatabaseStmt::set_charset_type(const common::ObCharsetType type)
{
  create_database_arg_.database_schema_.set_charset_type(type);
}


common::ObCharsetType ObCreateDatabaseStmt::get_charset_type() const
{
  return create_database_arg_.database_schema_.get_charset_type();
}

common::ObCollationType ObCreateDatabaseStmt::get_collation_type() const
{
  return create_database_arg_.database_schema_.get_collation_type();
}

obrpc::ObCreateDatabaseArg& ObCreateDatabaseStmt::get_create_database_arg()
{
  return create_database_arg_;
}
}//namespace sql
}//namespace oceanbase
