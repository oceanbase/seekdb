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

#ifndef OCEANBASE_SQL_OB_CREATE_DIRECTORY_STMT_H_
#define OCEANBASE_SQL_OB_CREATE_DIRECTORY_STMT_H_

#include "share/ob_rpc_struct.h"
#include "sql/resolver/ddl/ob_ddl_stmt.h"

namespace oceanbase
{
namespace sql
{
class ObCreateDirectoryStmt : public ObDDLStmt
{
public:
  ObCreateDirectoryStmt();
  explicit ObCreateDirectoryStmt(common::ObIAllocator *name_pool);
  virtual ~ObCreateDirectoryStmt();

  virtual obrpc::ObDDLArg &get_ddl_arg() { return arg_; }
  virtual bool cause_implicit_commit() const { return true; }

  obrpc::ObCreateDirectoryArg &get_create_directory_arg() { return arg_; }

  void set_tenant_id(const uint64_t tenant_id) { arg_.schema_.set_tenant_id(tenant_id); }
  void set_user_id(const uint64_t user_id) { arg_.user_id_ = user_id; }
  void set_or_replace(bool or_replace) { arg_.or_replace_ = or_replace; }
  int set_directory_name(const common::ObString &name) { return arg_.schema_.set_directory_name(name); }
  int set_directory_path(const common::ObString &path) { return arg_.schema_.set_directory_path(path); }

  bool is_or_replace() const {return arg_.or_replace_;}

  TO_STRING_KV(K_(arg));
private:
  obrpc::ObCreateDirectoryArg arg_;
private:
  DISALLOW_COPY_AND_ASSIGN(ObCreateDirectoryStmt);
};
} // namespace sql
} // namespace oceanbase

#endif // OCEANBASE_SQL_OB_CREATE_DIRECTORY_STMT_H_
