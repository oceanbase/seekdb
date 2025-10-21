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

#ifndef OCEANBASE_SQL_RESOLVER_DCL_OB_CREATE_ROLE_STMT_
#define OCEANBASE_SQL_RESOLVER_DCL_OB_CREATE_ROLE_STMT_

#include "sql/resolver/ddl/ob_ddl_stmt.h"
#include "lib/string/ob_strings.h"
#include "share/ob_define.h"

namespace oceanbase
{
namespace sql
{
class ObCreateRoleStmt: public ObDDLStmt
{
public:
  explicit ObCreateRoleStmt(common::ObIAllocator *name_pool);
  ObCreateRoleStmt();
  virtual ~ObCreateRoleStmt();

  void set_tenant_id(const uint64_t tenant_id) { tenant_id_ = tenant_id; }
  uint64_t get_tenant_id() { return tenant_id_; }
  void set_role_name(const common::ObString &role_name) { role_name_ = role_name; }
  const common::ObString &get_role_name() const { return role_name_; }
  void set_password(const common::ObString &password) { password_ = password; }
  const common::ObString &get_password() const { return password_; }
  void set_need_enc(bool need_enc) { need_enc_ = need_enc; }
  bool get_need_enc() const { return need_enc_; }
  void set_masked_sql(const common::ObString &masked_sql) { masked_sql_ = masked_sql; }
  const common::ObString &get_masked_sql() const { return masked_sql_; }
  virtual bool cause_implicit_commit() const { return true; }
  virtual obrpc::ObDDLArg &get_ddl_arg() { return create_role_arg_; }
  common::ObIArray<common::ObString> &get_user_names() { return user_names_; }
  common::ObIArray<common::ObString> &get_host_names() { return host_names_; }
  void set_if_not_exists() { if_not_exists_ = true; }
  bool get_if_not_exists() const { return if_not_exists_; }
  DECLARE_VIRTUAL_TO_STRING;
private:
  // data members
  uint64_t tenant_id_;
  common::ObString role_name_; 
  common::ObString password_;
  bool need_enc_;
  common::ObString masked_sql_;
  obrpc::ObCreateRoleArg create_role_arg_;
  //for mysql role
  bool if_not_exists_;
  ObArray<common::ObString, common::ModulePageAllocator, true /*auto_free*/> user_names_;
  ObArray<common::ObString, common::ModulePageAllocator, true /*auto_free*/> host_names_;
private:
  DISALLOW_COPY_AND_ASSIGN(ObCreateRoleStmt);
};
} // end namespace sql
} // end namespace oceanbase
#endif //OCEANBASE_SQL_RESOLVER_DCL_OB_CREATE_ROLE_STMT_
