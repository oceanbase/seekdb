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

#ifndef _OB_CREATE_FUNC_STMT_H
#define _OB_CREATE_FUNC_STMT_H 1

#include "lib/string/ob_string.h"
#include "sql/resolver/ddl/ob_ddl_stmt.h"

namespace oceanbase
{
namespace sql
{

class ObCreateFuncStmt : public ObDDLStmt
{
public:
  ObCreateFuncStmt() :
      ObDDLStmt(stmt::T_CREATE_FUNC)
  {}
  ~ObCreateFuncStmt() {}

  obrpc::ObCreateUserDefinedFunctionArg &get_create_func_arg() { return create_func_arg_; }

  virtual obrpc::ObDDLArg &get_ddl_arg() { return create_func_arg_; }

private:
  obrpc::ObCreateUserDefinedFunctionArg create_func_arg_;
  DISALLOW_COPY_AND_ASSIGN(ObCreateFuncStmt);
};

}
}

#endif /* _OB_CREATE_FUNC_STMT_H */


