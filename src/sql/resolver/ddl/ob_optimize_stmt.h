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

#ifndef OCEANBASE_SQL_RESOLVER_DDL_OPTIMIZE_TABLE_STMT_H_
#define OCEANBASE_SQL_RESOLVER_DDL_OPTIMIZE_TABLE_STMT_H_


#include "share/ob_rpc_struct.h"
#include "sql/resolver/ddl/ob_ddl_stmt.h"

namespace oceanbase
{
namespace sql
{
class ObOptimizeTableStmt : public ObDDLStmt
{
public:
  explicit ObOptimizeTableStmt(common::ObIAllocator *name_pool);
  ObOptimizeTableStmt();
  virtual ~ObOptimizeTableStmt() = default;

  const obrpc::ObOptimizeTableArg &get_optimize_table_arg() const { return optimize_table_arg_; }
  obrpc::ObOptimizeTableArg &get_optimize_table_arg() { return optimize_table_arg_; }
  virtual bool cause_implicit_commit() const { return true; }
  int add_table_item(const obrpc::ObTableItem &table_item);
  virtual obrpc::ObDDLArg &get_ddl_arg() { return optimize_table_arg_; }
  inline void set_tenant_id(const uint64_t tenant_id) { optimize_table_arg_.tenant_id_ = tenant_id; }
  uint64_t get_tenant_id() const { return optimize_table_arg_.tenant_id_; }
  TO_STRING_KV(K_(stmt_type),K_(optimize_table_arg));
private:
  obrpc::ObOptimizeTableArg optimize_table_arg_;
  DISALLOW_COPY_AND_ASSIGN(ObOptimizeTableStmt);
};

class ObOptimizeTenantStmt : public ObDDLStmt
{
public:
  explicit ObOptimizeTenantStmt(common::ObIAllocator *name_pool);
  ObOptimizeTenantStmt();
  virtual ~ObOptimizeTenantStmt() = default;
  const obrpc::ObOptimizeTenantArg &get_optimize_tenant_arg() const { return optimize_tenant_arg_; }
  obrpc::ObOptimizeTenantArg &get_optimize_tenant_arg() { return optimize_tenant_arg_; }
  virtual bool cause_implicit_commit() const { return true; }
  void set_tenant_name(const common::ObString &tenant_name);
  virtual obrpc::ObDDLArg &get_ddl_arg() { return optimize_tenant_arg_; }
private:
  obrpc::ObOptimizeTenantArg optimize_tenant_arg_;
  DISALLOW_COPY_AND_ASSIGN(ObOptimizeTenantStmt);
};

class ObOptimizeAllStmt : public ObDDLStmt
{
public:
  explicit ObOptimizeAllStmt(common::ObIAllocator *name_pool);
  ObOptimizeAllStmt();
  virtual ~ObOptimizeAllStmt() = default;
  const obrpc::ObOptimizeAllArg &get_optimize_all_arg() const { return optimize_all_arg_; }
  obrpc::ObOptimizeAllArg &get_optimize_all_arg() { return optimize_all_arg_; }
  virtual bool cause_implicit_commit() const { return true; }
  virtual obrpc::ObDDLArg &get_ddl_arg() { return optimize_all_arg_; }
private:
  obrpc::ObOptimizeAllArg optimize_all_arg_;
  DISALLOW_COPY_AND_ASSIGN(ObOptimizeAllStmt);
};

}
}

#endif  // OCEANBASE_SQL_RESOLVER_DDL_OPTIMIZE_TABLE_STMT_H_
