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

#include "sql/resolver/ddl/ob_alter_table_stmt.h"
#include "query/session/ob_session_access.h"
#include "sql/session/ob_sql_session_info.h"
#include "sql/session/ob_local_session_var.h"

namespace oceanbase
{
using namespace common;
using namespace transaction::tablelock;
namespace sql
{

ObAlterTableStmt::ObAlterTableStmt(common::ObIAllocator *name_pool)
    : ObTableStmt(name_pool, stmt::T_ALTER_TABLE), is_comment_table_(false),
      is_alter_system_(false), fts_arg_allocator_(nullptr), is_alter_triggers_(false),
      alter_table_action_count_(0)
{
}

ObAlterTableStmt::ObAlterTableStmt()
    : ObTableStmt(stmt::T_ALTER_TABLE), is_comment_table_(false), is_alter_system_(false),
      fts_arg_allocator_(nullptr), is_alter_triggers_(false), alter_table_action_count_(0)
{
}

ObAlterTableStmt::~ObAlterTableStmt()
{
  for (int64_t i = 0; i < index_arg_list_.count(); ++i) {
    if (OB_NOT_NULL(index_arg_list_.at(i))) {
      index_arg_list_.at(i)->~ObCreateIndexArg();
      index_arg_list_.at(i) = nullptr;
    }
  }
  index_arg_list_.reset();
  fts_arg_allocator_ = nullptr;
}

int ObAlterTableStmt::add_column(const share::schema::AlterColumnSchema &column_schema)
{
  int ret = OB_SUCCESS;
  share::schema::AlterTableSchema &alter_table_schema =
      get_alter_table_arg().alter_table_schema_;
  if (OB_FAIL(alter_table_schema.add_alter_column(column_schema, true))){
    SQL_RESV_LOG(WARN, "failed to add column schema to alter table schema", K(ret));
  }
  return ret;
}

int ObAlterTableStmt::add_index_arg(obcall::ObIndexArg *index_arg)
{
  int ret = OB_SUCCESS;
  if (index_arg == NULL) {
    ret = OB_INVALID_ARGUMENT;
    SQL_RESV_LOG(WARN, "index arg should not be null!", K(ret));
  } else if (OB_FAIL(alter_table_arg_.index_arg_list_.push_back(index_arg))) {
    SQL_RESV_LOG(WARN, "failed to add index arg to alter table arg!", K(ret));
  }
  return ret;
}

int ObAlterTableStmt::check_drop_fk_arg_exist(
    obcall::ObDropForeignKeyArg *drop_fk_arg, bool &has_same_fk_arg)
{
  int ret = OB_SUCCESS;
  has_same_fk_arg = false;

  if (OB_ISNULL(drop_fk_arg)) {
    ret = OB_ERR_UNEXPECTED;
    SQL_RESV_LOG(WARN, "drop_fk_arg should not be null", K(ret));
  } else {
    for (int64_t i = 0;
         OB_SUCC(ret) && !has_same_fk_arg && i < alter_table_arg_.index_arg_list_.count();
         ++i) {
      if (OB_ISNULL(alter_table_arg_.index_arg_list_.at(i))) {
        ret = OB_ERR_UNEXPECTED;
        SQL_RESV_LOG(WARN, "index_arg from index_arg_list_ is null", K(ret), K(i));
      } else if (obcall::ObIndexArg::IndexActionType::DROP_FOREIGN_KEY
                 != alter_table_arg_.index_arg_list_.at(i)->index_action_type_) {
        continue; // skip
      } else if (0 == static_cast<obcall::ObDropForeignKeyArg*>(alter_table_arg_.index_arg_list_.at(i))->
                        foreign_key_name_.compare(drop_fk_arg->foreign_key_name_)) {
        has_same_fk_arg = true;
      }
    }
  }

  return ret;
}



int ObAlterTableStmt::set_origin_database_name(const ObString &origin_db_name)
{
  return alter_table_arg_.alter_table_schema_.set_origin_database_name(origin_db_name);
}


int ObAlterTableStmt::set_origin_table_name(const ObString &origin_table_name)
{
  return alter_table_arg_.alter_table_schema_.set_origin_table_name(origin_table_name);
}

void ObAlterTableStmt::set_table_id(const uint64_t table_id)
{
  alter_table_arg_.alter_table_schema_.set_table_id(table_id);
}

int ObAlterTableStmt::fill_session_vars(const ObBasicSessionInfo &session) {
  int ret = OB_SUCCESS;
  if (OB_FAIL(ObLocalSessionVarHelper::load_session_vars(&session, alter_table_arg_.local_session_var_))) {
    SQL_RESV_LOG(WARN, "load local session vars failed", K(ret));
  }
  return ret;
}

int ObAlterTableStmt::set_exchange_partition_arg(const obcall::ObExchangePartitionArg &exchange_partition_arg)
{
  int ret = OB_SUCCESS;
  if (OB_FAIL(exchange_partition_arg_.assign(exchange_partition_arg))) {
    SQL_RESV_LOG(WARN, "failed to assign", K(ret), K(exchange_partition_arg));
  }
  return ret;
}

void ObAlterTableStmt::set_lock_priority()
{
  int ret = OB_SUCCESS;

  if (GCONF.enable_lock_priority) {
    alter_table_arg_.lock_priority_ = ObTableLockPriority::HIGH1;
  }
}

} //namespace sql
} //namespace oceanbase
