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

#ifndef OCEANBASE_SQL_OB_TRUNCATE_TABLE_STMT_
#define OCEANBASE_SQL_OB_TRUNCATE_TABLE_STMT_

#include "share/schema/ob_schema_service.h"
#include "sql/resolver/ddl/ob_ddl_stmt.h"
#include "sql/resolver/ob_stmt_resolver.h"

namespace oceanbase
{
namespace sql
{

class ObTruncateTableStmt : public ObDDLStmt
{
public:
  explicit ObTruncateTableStmt(common::ObIAllocator *name_pool);
  ObTruncateTableStmt();
  virtual ~ObTruncateTableStmt();
  void set_database_name(const common::ObString &db_name);
  void set_table_name(const common::ObString &table_name);
  void set_tenant_id(const uint64_t tenant_id);
  uint64_t get_tenant_id() const { return truncate_table_arg_.tenant_id_; }
  const common::ObString& get_database_name() const { return truncate_table_arg_.database_name_; }
  const common::ObString& get_table_name() const { return truncate_table_arg_.table_name_; }

  inline const obrpc::ObTruncateTableArg &get_truncate_table_arg() const;
  obrpc::ObTruncateTableArg &get_truncate_table_arg() { return truncate_table_arg_; }
  virtual obrpc::ObDDLArg &get_ddl_arg() { return truncate_table_arg_; }
  
  TO_STRING_KV(K_(stmt_type),K_(truncate_table_arg));
private:
  obrpc::ObTruncateTableArg truncate_table_arg_;
  DISALLOW_COPY_AND_ASSIGN(ObTruncateTableStmt);
};

inline void ObTruncateTableStmt::set_tenant_id(const uint64_t tenant_id)
{
  truncate_table_arg_.tenant_id_ = tenant_id;
}

inline const obrpc::ObTruncateTableArg &ObTruncateTableStmt::get_truncate_table_arg() const
{
  return truncate_table_arg_;
}

} // namespace sql
} // namespace oceanbase
#endif //OCEANBASE_SQL_OB_TRUNCATE_TABLE_STMT_
