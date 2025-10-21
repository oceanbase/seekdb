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

#ifndef OCEANBASE_SQL_OB_DROP_TABLEGROUP_STMT_
#define OCEANBASE_SQL_OB_DROP_TABLEGROUP_STMT_

#include "share/ob_rpc_struct.h"
#include "sql/resolver/ob_stmt_resolver.h"
#include "sql/resolver/ddl/ob_ddl_stmt.h"

namespace oceanbase
{
namespace sql
{
class ObDropTablegroupStmt : public ObDDLStmt
{
public:
  explicit ObDropTablegroupStmt(common::ObIAllocator *name_pool)
    :ObDDLStmt(name_pool, stmt::T_DROP_TABLEGROUP)
  {}
  ObDropTablegroupStmt() : ObDDLStmt(stmt::T_DROP_TABLEGROUP)
  {}

  virtual ~ObDropTablegroupStmt()
  {}

  void set_if_exist(const bool if_exist) { drop_tablegroup_arg_.if_exist_ = if_exist; }
  TO_STRING_KV(K_(drop_tablegroup_arg));
  inline obrpc::ObDropTablegroupArg &get_drop_tablegroup_arg();
  inline void set_tablegroup_name(const common::ObString &tablegroup_name);
  void set_tenant_id(const uint64_t tenant_id) { drop_tablegroup_arg_.tenant_id_ = tenant_id; }
  virtual obrpc::ObDDLArg &get_ddl_arg() { return drop_tablegroup_arg_; }

  const common::ObString &get_tablegroup_name() const
  { return drop_tablegroup_arg_.tablegroup_name_; }
private:
    obrpc::ObDropTablegroupArg drop_tablegroup_arg_;
    DISALLOW_COPY_AND_ASSIGN(ObDropTablegroupStmt);

};

inline obrpc::ObDropTablegroupArg &ObDropTablegroupStmt::get_drop_tablegroup_arg()
{
  return drop_tablegroup_arg_;
}

inline void ObDropTablegroupStmt::set_tablegroup_name(const common::ObString &tablegroup_name)
{
  drop_tablegroup_arg_.tablegroup_name_ = tablegroup_name;
}

}
}

#endif //OCEANBASE_SQL_OB_DROP_TABLEGROUP_STMT_H_
