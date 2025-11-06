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

#ifndef OCEANBASE_SQL_OB_RENAME_TABLE_STMT_
#define OCEANBASE_SQL_OB_RENAME_TABLE_STMT_

#include "sql/resolver/ddl/ob_ddl_stmt.h"
#include "sql/resolver/ob_stmt_resolver.h"

namespace oceanbase
{
namespace sql
{

class ObRenameTableStmt : public ObDDLStmt
{
public:
  explicit ObRenameTableStmt(common::ObIAllocator *name_pool);
  ObRenameTableStmt();
  virtual ~ObRenameTableStmt();
  obrpc::ObRenameTableArg& get_rename_table_arg(){ return rename_table_arg_; }
  const obrpc::ObRenameTableArg& get_rename_table_arg() const { return rename_table_arg_; }
  int add_rename_table_item(const obrpc::ObRenameTableItem &rename_table_item);
  inline void set_tenant_id(const uint64_t tenant_id);
  inline void set_client_session_info(const uint32_t client_sessid,
                                      const int64_t create_ts);
  int set_lock_priority(sql::ObSQLSessionInfo *session);
  uint64_t get_tenant_id() const { return rename_table_arg_.tenant_id_; }
  virtual obrpc::ObDDLArg &get_ddl_arg() { return rename_table_arg_; }
  TO_STRING_KV(K_(stmt_type), K_(rename_table_arg));
private:
  obrpc::ObRenameTableArg rename_table_arg_;
  DISALLOW_COPY_AND_ASSIGN(ObRenameTableStmt);
};

inline void ObRenameTableStmt::set_tenant_id(const uint64_t tenant_id)
{
  rename_table_arg_.tenant_id_ = tenant_id;
}

inline void ObRenameTableStmt::set_client_session_info(const uint32_t client_sessid,
                                                       const int64_t create_ts)
{
  rename_table_arg_.client_session_id_ = client_sessid;
  rename_table_arg_.client_session_create_ts_ = create_ts;
}

} // namespace sql
} // namespace oceanbase


#endif //OCEANBASE_SQL_OB_RENAME_TABLE_STMT_

