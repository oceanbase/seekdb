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

#ifndef OCEANBASE_SQL_RESOLVER_DCL_OB_DROP_USE_STMT_
#define OCEANBASE_SQL_RESOLVER_DCL_OB_DROP_USE_STMT_
#include "sql/resolver/ddl/ob_ddl_stmt.h"
#include "lib/string/ob_strings.h"
#include "share/ob_define.h"

namespace oceanbase
{
namespace sql
{
class ObDropUserStmt: public ObDDLStmt
{
public:
  explicit ObDropUserStmt(common::ObIAllocator *name_pool);
  ObDropUserStmt();
  virtual ~ObDropUserStmt();
  int add_user(const common::ObString &user_name, const common::ObString &host_name);
  
  void set_if_exists(const bool if_exists) { if_exists_ = if_exists; }
  const common::ObStrings *get_users() const { return &users_; };
  bool get_if_exists() const { return if_exists_; };
  
  virtual bool cause_implicit_commit() const { return true; }
  virtual obcall::ObDDLArg &get_ddl_arg() { return drop_user_arg_; }
  DECLARE_VIRTUAL_TO_STRING;
private:
  // data members
  common::ObStrings users_;//user1,host1; usr2,host2;...
  bool if_exists_;
  obcall::ObDropUserArg drop_user_arg_; // used to return exec_tid_
private:
  DISALLOW_COPY_AND_ASSIGN(ObDropUserStmt);
};
} // end namespace sql
} // end namespace oceanbase

#endif //OCEANBAS_SQL_RESOLVER_DCL_OB_DROP_USER_STMT_
