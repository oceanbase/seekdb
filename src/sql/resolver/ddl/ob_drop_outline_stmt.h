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

#ifndef OCEANBASE_SQL_OB_DROP_OUTLINE_STMT_H_
#define OCEANBASE_SQL_OB_DROP_OUTLINE_STMT_H_

#include "lib/string/ob_string.h"
#include "sql/resolver/ddl/ob_ddl_stmt.h"
namespace oceanbase
{
namespace sql
{
class ObDropOutlineStmt : public ObDDLStmt
{
public:
  ObDropOutlineStmt() :
      ObDDLStmt(stmt::T_DROP_OUTLINE),
      drop_outline_arg_()
  {}
  ~ObDropOutlineStmt() { }
  void set_database_name(const common::ObString &database_name) { drop_outline_arg_.db_name_ = database_name; }
  void set_outline_name(const common::ObString &outline_name) { drop_outline_arg_.outline_name_ = outline_name; }
  void set_tenant_id(uint64_t tenant_id) { drop_outline_arg_.tenant_id_ = tenant_id; }
  void set_is_format(bool is_format) { drop_outline_arg_.is_format_ = is_format; }
  obrpc::ObDropOutlineArg &get_drop_outline_arg() { return drop_outline_arg_; }
  const obrpc::ObDropOutlineArg &get_drop_outline_arg() const { return drop_outline_arg_; }
  virtual obrpc::ObDDLArg &get_ddl_arg() { return drop_outline_arg_; }
  TO_STRING_KV(K_(drop_outline_arg));
private:
  obrpc::ObDropOutlineArg drop_outline_arg_;
  DISALLOW_COPY_AND_ASSIGN(ObDropOutlineStmt);
};
}//namespace sql
}//namespace oceanbase
#endif //OCEANBASE_SQL_OB_DROP_OUTLINE_STMT_H_
