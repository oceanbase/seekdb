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

#ifndef OCEANBASE_SQL_RESOLVER_DML_OB_LOCK_TABLE_STMT_
#define OCEANBASE_SQL_RESOLVER_DML_OB_LOCK_TABLE_STMT_
#include "sql/resolver/dml/ob_dml_stmt.h"
#include "sql/resolver/ob_cmd.h"
#include "share/schema/ob_schema_struct.h"
#include "lib/container/ob_se_array.h"

namespace oceanbase
{
namespace sql
{

struct ObMySQLLockNode
{
public:
  ObMySQLLockNode() : table_item_(NULL), lock_mode_(0) {}
  ~ObMySQLLockNode() { table_item_ = NULL; }
  bool is_valid() const
  {
    return (NULL != table_item_
            && 0 != lock_mode_);
  }
  TO_STRING_KV(KPC_(table_item), K_(lock_mode));
public:
  TableItem *table_item_;
  int64_t lock_mode_;
};

class ObLockTableStmt : public ObDMLStmt, public ObICmd
{
public:
  enum {
        INVALID_STMT_TYPE =       0,
        ORACLE_LOCK_TABLE_STMT =  1,
        MYSQL_LOCK_TABLE_STMT =   2,
        MYSQL_UNLOCK_TABLE_STMT = 3
  };
  explicit ObLockTableStmt()
    : ObDMLStmt(stmt::T_LOCK_TABLE),
      lock_stmt_type_(0),
      lock_mode_(0),
      wait_lock_seconds_(-1),
      mysql_lock_list_()
  {}
  virtual ~ObLockTableStmt()
  {}
  virtual int get_cmd_type() const { return get_stmt_type(); }

  void set_lock_mode(const int64_t lock_mode) { lock_mode_ = lock_mode; }
  void set_wait_lock_seconds(const int64_t wait_lock_seconds) { wait_lock_seconds_ = wait_lock_seconds; }
  void set_lock_stmt_type(const int64_t stmt_type) { lock_stmt_type_ = stmt_type; }
  int64_t get_lock_mode() const { return lock_mode_; }
  int64_t get_wait_lock_seconds() const { return wait_lock_seconds_; }
  virtual int check_is_simple_lock_stmt(bool &is_valid) const override { 
    is_valid = true;
    return common::OB_SUCCESS;  
  };
  int64_t get_lock_stmt_type() const { return lock_stmt_type_; }
  const ObIArray<ObMySQLLockNode> &get_mysql_lock_list() const { return mysql_lock_list_; }
  int add_mysql_lock_node(const ObMySQLLockNode &node);
private:
  // for oracle lock
  int64_t lock_stmt_type_;
  int64_t lock_mode_;
  int64_t wait_lock_seconds_;

  // for mysql lock
  common::ObSEArray<ObMySQLLockNode, 2> mysql_lock_list_;
  DISALLOW_COPY_AND_ASSIGN(ObLockTableStmt);
};
} // namespace sql
} // namespace oceanbase

#endif
