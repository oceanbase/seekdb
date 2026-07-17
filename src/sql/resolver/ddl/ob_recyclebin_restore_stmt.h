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

#ifndef OCEANBASE_SQL_OB_RECYCLEBIN_RESTORE_STMT_
#define OCEANBASE_SQL_OB_RECYCLEBIN_RESTORE_STMT_

#include "share/ob_rpc_struct.h"
#include "sql/resolver/ddl/ob_ddl_stmt.h"
#include "sql/resolver/ob_stmt_resolver.h"

namespace oceanbase
{
namespace sql
{
/**
 * Restore table from recyclebin.
 */

class ObRecyclebinRestoreTableStmt : public ObDDLStmt
{
public:
  ObRecyclebinRestoreTableStmt() : ObDDLStmt(stmt::T_RECYCLEBIN_RESTORE_TABLE) {}
  explicit ObRecyclebinRestoreTableStmt(common::ObIAllocator *name_pool)
    : ObDDLStmt(name_pool, stmt::T_RECYCLEBIN_RESTORE_TABLE)
  {}
  virtual ~ObRecyclebinRestoreTableStmt() {}
  const obcall::ObRecyclebinRestoreTableArg& get_restore_table_arg() const { return restore_table_arg_; }

  inline void set_origin_table_id(const uint64_t origin_table);
  uint64_t get_origin_table_id() const { return restore_table_arg_.origin_table_id_; }
  common::ObString get_origin_db_name() const { return restore_table_arg_.origin_db_name_; };
  common::ObString get_origin_table_name() const { return restore_table_arg_.origin_table_name_; };
  common::ObString get_new_db_name() const { return restore_table_arg_.new_db_name_; };
  void set_origin_table_name(const common::ObString &origin_table_name);
  void set_origin_db_name(const common::ObString &origin_db_name);
  void set_new_table_name(const common::ObString &new_table_name);
  void set_new_db_name(const common::ObString &new_db_name);
  virtual obcall::ObDDLArg &get_ddl_arg() { return restore_table_arg_; }
  TO_STRING_KV(K_(stmt_type),K_(restore_table_arg));
private:
  obcall::ObRecyclebinRestoreTableArg restore_table_arg_;
  DISALLOW_COPY_AND_ASSIGN(ObRecyclebinRestoreTableStmt);
};



inline void ObRecyclebinRestoreTableStmt::set_origin_db_name(
            const common::ObString &origin_db_name)
{
  restore_table_arg_.origin_db_name_ = origin_db_name;
}

inline void ObRecyclebinRestoreTableStmt::set_origin_table_id(const uint64_t origin_table_id)
{
  restore_table_arg_.origin_table_id_ = origin_table_id;
}

inline void ObRecyclebinRestoreTableStmt::set_origin_table_name(const common::ObString &origin_table_name)
{
  restore_table_arg_.origin_table_name_ = origin_table_name;
}

inline void ObRecyclebinRestoreTableStmt::set_new_table_name(const common::ObString &new_table_name)
{
  restore_table_arg_.new_table_name_ = new_table_name;
}

inline void ObRecyclebinRestoreTableStmt::set_new_db_name(const common::ObString &new_db_name)
{
  restore_table_arg_.new_db_name_ = new_db_name;
}

/**
 * Restore database from recyclebin.
 */

class ObRecyclebinRestoreDatabaseStmt : public ObDDLStmt
{
public:
  ObRecyclebinRestoreDatabaseStmt() : ObDDLStmt(stmt::T_RECYCLEBIN_RESTORE_DATABASE) {}
  explicit ObRecyclebinRestoreDatabaseStmt(common::ObIAllocator *name_pool)
    : ObDDLStmt(name_pool, stmt::T_RECYCLEBIN_RESTORE_DATABASE)
  {}
  virtual ~ObRecyclebinRestoreDatabaseStmt() {}
  const obcall::ObRecyclebinRestoreDatabaseArg& get_restore_database_arg() const { return restore_db_arg_; }

  const common::ObString &get_origin_db_name() const { return restore_db_arg_.origin_db_name_; }
  const common::ObString &get_new_db_name() const { return restore_db_arg_.new_db_name_; }
  void set_origin_db_name(const common::ObString origin_db_name);
  void set_new_db_name(const common::ObString &new_db_name);
  virtual obcall::ObDDLArg &get_ddl_arg() { return restore_db_arg_; }
  TO_STRING_KV(K_(stmt_type),K_(restore_db_arg));
private:
  obcall::ObRecyclebinRestoreDatabaseArg restore_db_arg_;
  DISALLOW_COPY_AND_ASSIGN(ObRecyclebinRestoreDatabaseStmt);
};



inline void ObRecyclebinRestoreDatabaseStmt::set_origin_db_name(const common::ObString origin_db_name)
{
  restore_db_arg_.origin_db_name_ = origin_db_name;
}

inline void ObRecyclebinRestoreDatabaseStmt::set_new_db_name(const common::ObString &new_db_name)
{
  restore_db_arg_.new_db_name_ = new_db_name;
}

} // namespace sql
} // namespace oceanbase


#endif // OCEANBASE_SQL_OB_RECYCLEBIN_RESTORE_STMT_
