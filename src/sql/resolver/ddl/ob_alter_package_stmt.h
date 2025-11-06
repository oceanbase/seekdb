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

#ifndef OCEANBASE_SRC_SQL_RESOLVER_DDL_OB_ALTER_PACKAGE_STMT_H_
#define OCEANBASE_SRC_SQL_RESOLVER_DDL_OB_ALTER_PACKAGE_STMT_H_

#include "share/ob_rpc_struct.h"
#include "sql/resolver/ddl/ob_ddl_stmt.h"
namespace oceanbase
{
namespace sql
{
class ObAlterPackageStmt : public ObDDLStmt
{
public:
  explicit ObAlterPackageStmt(common::ObIAllocator *name_pool)
      : ObDDLStmt(name_pool, stmt::T_ALTER_PACKAGE),
        alter_package_arg_() {}
  ObAlterPackageStmt()
      : ObDDLStmt(stmt::T_ALTER_PACKAGE),
        alter_package_arg_() {}
  virtual ~ObAlterPackageStmt() {}
  obrpc::ObAlterPackageArg &get_alter_package_arg() { return alter_package_arg_; }
  virtual obrpc::ObDDLArg &get_ddl_arg() { return alter_package_arg_; }
private:
  obrpc::ObAlterPackageArg alter_package_arg_;
  DISALLOW_COPY_AND_ASSIGN(ObAlterPackageStmt);
};
}
}

#endif /* OCEANBASE_SRC_SQL_RESOLVER_DDL_OB_ALTER_PACKAGE_STMT_H_ */
