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

#ifndef OCEANBASE_SQL_OB_DROP_CCL_RULE_STMT_H_
#define OCEANBASE_SQL_OB_DROP_CCL_RULE_STMT_H_

#include "share/ob_rpc_struct.h"
#include "sql/resolver/ddl/ob_ddl_stmt.h"
namespace oceanbase
{
namespace sql
{
class ObDropCCLRuleStmt : public ObDDLStmt
{
public:
  ObDropCCLRuleStmt();
  explicit ObDropCCLRuleStmt(common::ObIAllocator *name_pool);
  virtual ~ObDropCCLRuleStmt();
  inline void set_if_exists(bool if_exists) { drop_ccl_rule_arg_.if_exist_ = if_exists; }
  inline void set_ccl_rule_name(const common::ObString &ccl_rule_name) { drop_ccl_rule_arg_.ccl_rule_name_ = ccl_rule_name; }
  inline void set_tenant_id(uint64_t tenant_id) { drop_ccl_rule_arg_.tenant_id_ = tenant_id; }
  obrpc::ObDropCCLRuleArg &get_drop_ccl_rule_arg() { return drop_ccl_rule_arg_; }
  virtual bool cause_implicit_commit() const
  {
    return true;
  }
  virtual obrpc::ObDDLArg &get_ddl_arg()
  {
    return drop_ccl_rule_arg_;
  }

  TO_STRING_KV(K_(drop_ccl_rule_arg));

private:
  obrpc::ObDropCCLRuleArg drop_ccl_rule_arg_;
};
}//namespace sql
}//namespace oceanbase
#endif //OCEANBASE_SQL_OB_DROP_CCL_RULE_STMT_H_
