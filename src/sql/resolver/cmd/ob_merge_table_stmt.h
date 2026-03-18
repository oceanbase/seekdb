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

#ifndef OCEANBASE_SQL_RESOLVER_CMD_OB_MERGE_TABLE_STMT_H_
#define OCEANBASE_SQL_RESOLVER_CMD_OB_MERGE_TABLE_STMT_H_

#include "sql/resolver/cmd/ob_cmd_stmt.h"
#include "lib/string/ob_string.h"

namespace oceanbase
{
namespace sql
{

enum ObMergeTableStrategy
{
  MERGE_STRATEGY_FAIL = 0,
  MERGE_STRATEGY_THEIRS = 1,
  MERGE_STRATEGY_OURS = 2,
};

class ObMergeTableStmt : public ObCMDStmt
{
public:
  ObMergeTableStmt()
    : ObCMDStmt(stmt::T_MERGE_TABLE),
      strategy_(MERGE_STRATEGY_FAIL)
  {}
  virtual ~ObMergeTableStmt() {}

  void set_strategy(ObMergeTableStrategy s) { strategy_ = s; }
  ObMergeTableStrategy get_strategy() const { return strategy_; }

  void set_insert_sql(const common::ObString &sql) { insert_sql_ = sql; }
  const common::ObString &get_insert_sql() const { return insert_sql_; }

  void set_update_sql(const common::ObString &sql) { update_sql_ = sql; }
  const common::ObString &get_update_sql() const { return update_sql_; }

  void set_conflict_check_sql(const common::ObString &sql) { conflict_check_sql_ = sql; }
  const common::ObString &get_conflict_check_sql() const { return conflict_check_sql_; }

  void set_cur_db_name(const common::ObString &name) { cur_db_name_ = name; }
  const common::ObString &get_cur_db_name() const { return cur_db_name_; }
  void set_cur_table_name(const common::ObString &name) { cur_table_name_ = name; }
  const common::ObString &get_cur_table_name() const { return cur_table_name_; }
  void set_inc_db_name(const common::ObString &name) { inc_db_name_ = name; }
  const common::ObString &get_inc_db_name() const { return inc_db_name_; }
  void set_inc_table_name(const common::ObString &name) { inc_table_name_ = name; }
  const common::ObString &get_inc_table_name() const { return inc_table_name_; }

  TO_STRING_KV(K_(stmt_type), K_(strategy));

private:
  ObMergeTableStrategy strategy_;
  common::ObString insert_sql_;
  common::ObString update_sql_;
  common::ObString conflict_check_sql_;
  common::ObString cur_db_name_;
  common::ObString cur_table_name_;
  common::ObString inc_db_name_;
  common::ObString inc_table_name_;

  DISALLOW_COPY_AND_ASSIGN(ObMergeTableStmt);
};

} // namespace sql
} // namespace oceanbase

#endif /* OCEANBASE_SQL_RESOLVER_CMD_OB_MERGE_TABLE_STMT_H_ */
