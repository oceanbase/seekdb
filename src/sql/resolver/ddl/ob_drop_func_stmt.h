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

#ifndef _OB_DROP_FUNC_STMT_H
#define _OB_DROP_FUNC_STMT_H 1

namespace oceanbase
{
namespace sql
{

class ObDropFuncStmt : public ObDDLStmt
{
public:
  ObDropFuncStmt() :
      ObDDLStmt(stmt::T_DROP_FUNC),
      drop_func_arg_()
  {}
  ~ObDropFuncStmt() { }
  void set_func_name(const common::ObString &func_name) { drop_func_arg_.name_ = func_name; }
  void set_tenant_id(uint64_t tenant_id) { drop_func_arg_.tenant_id_ = tenant_id; }
  obrpc::ObDropUserDefinedFunctionArg &get_drop_func_arg() { return drop_func_arg_; }
  obrpc::ObDDLArg &get_ddl_arg() { return drop_func_arg_; }
  TO_STRING_KV(K_(drop_func_arg));
private:
  obrpc::ObDropUserDefinedFunctionArg drop_func_arg_;
  DISALLOW_COPY_AND_ASSIGN(ObDropFuncStmt);

};

}
}

#endif /* _OB_DROP_FUNC_STMT_H */


