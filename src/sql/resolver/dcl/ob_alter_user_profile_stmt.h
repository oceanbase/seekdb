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

#ifndef OB_ALTER_USER_PROFILE_STMT_H_
#define OB_ALTER_USER_PROFILE_STMT_H_
#include "sql/resolver/ddl/ob_ddl_stmt.h"
#include "lib/string/ob_strings.h"
namespace oceanbase
{
namespace sql
{
class ObAlterUserProfileStmt: public ObDDLStmt
{
public:
  ObAlterUserProfileStmt();
  explicit ObAlterUserProfileStmt(common::ObIAllocator *name_pool);
  virtual ~ObAlterUserProfileStmt();

  obrpc::ObAlterUserProfileArg &get_ddl_arg() { return arg_; }
  // function members
  TO_STRING_KV(K_(stmt_type), K_(arg));
  enum {SET_ROLE = 1 << 0, SET_DEFAULT_ROLE = 1 << 1};
  void set_set_role_flag(int set_role_flag) { set_role_flag_ = set_role_flag; }
  int get_set_role_flag() const { return set_role_flag_; }
  virtual bool cause_implicit_commit() const { return !(lib::is_mysql_mode() && get_set_role_flag() == SET_ROLE); }
private:
  // data members
  obrpc::ObAlterUserProfileArg arg_;
  int set_role_flag_;
private:
  DISALLOW_COPY_AND_ASSIGN(ObAlterUserProfileStmt);
};
} // end namespace sql
} // end namespace oceanbase

#endif //OB_ALTER_USER_PROFILE_STMT_H_
