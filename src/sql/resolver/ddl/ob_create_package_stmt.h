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

#ifndef OCEANBASE_SRC_SQL_RESOLVER_DDL_OB_CREATE_PACKAGE_STMT_H_
#define OCEANBASE_SRC_SQL_RESOLVER_DDL_OB_CREATE_PACKAGE_STMT_H_

#include "share/ob_rpc_struct.h"
#include "sql/resolver/ddl/ob_ddl_stmt.h"
namespace oceanbase
{
namespace sql
{
class ObCreatePackageStmt : public ObDDLStmt
{
public:
  explicit ObCreatePackageStmt(common::ObIAllocator *name_pool)
      : ObDDLStmt(name_pool, stmt::T_CREATE_PACKAGE),
        create_package_arg_() {}
  ObCreatePackageStmt()
      : ObDDLStmt(stmt::T_CREATE_PACKAGE),
        create_package_arg_() {}
  virtual ~ObCreatePackageStmt() {}
  obrpc::ObCreatePackageArg &get_create_package_arg() { return create_package_arg_; }
  virtual obrpc::ObDDLArg &get_ddl_arg() { return create_package_arg_; }
private:
  obrpc::ObCreatePackageArg create_package_arg_;
  DISALLOW_COPY_AND_ASSIGN(ObCreatePackageStmt);
};
} //namespace sql
} //namespace oceanbase

#endif /* OCEANBASE_SRC_SQL_RESOLVER_DDL_OB_CREATE_PACKAGE_STMT_H_ */
