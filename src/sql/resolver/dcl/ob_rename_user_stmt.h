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

#ifndef OCEANBASE_SQL_RESOLVER_DCL_OB_RENAME_USE_STMT_
#define OCEANBASE_SQL_RESOLVER_DCL_OB_RENAME_USE_STMT_
#include "sql/resolver/ddl/ob_ddl_stmt.h"
#include "lib/string/ob_strings.h"
namespace oceanbase
{
namespace sql
{
class ObRenameUserStmt: public ObDDLStmt
{
public:
  explicit ObRenameUserStmt(common::ObIAllocator *name_pool);
  ObRenameUserStmt();
  virtual ~ObRenameUserStmt();
  int add_rename_info(const common::ObString &from_user, const common::ObString &from_host,
                      const common::ObString &to_user, const common::ObString &to_host);
  const common::ObStrings *get_rename_infos() const;
  inline uint64_t get_tenant_id() { return tenant_id_; }
  inline void set_tenant_id(uint64_t tenant_id) { tenant_id_ = tenant_id; }
  virtual bool cause_implicit_commit() const { return true; }
  virtual obrpc::ObDDLArg &get_ddl_arg() { return rename_user_arg_; }
  DECLARE_VIRTUAL_TO_STRING;
private:
  // data members
  common::ObStrings rename_infos_; // (from1_user, from1_host, to1_user, to1_host), (from2_user, from2_host, to2_user, to2_host)
  uint64_t tenant_id_;
  obrpc::ObRenameUserArg rename_user_arg_; // used to return exec_tenant_id_
private:
  DISALLOW_COPY_AND_ASSIGN(ObRenameUserStmt);
};
} // end namespace sql
} // end namespace oceanbase

#endif //OCEANBASE_SQL_RESOLVER_DCL_OB_RENAME_USER_STMT_
