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

#define USING_LOG_PREFIX SQL_RESV
#include "sql/resolver/ddl/ob_drop_ccl_rule_resolver.h"

#include "sql/ob_sql_utils.h"
#include "sql/resolver/ob_resolver.h"
#include "sql/resolver/ob_stmt_resolver.h"
#include "sql/resolver/ddl/ob_drop_ccl_rule_stmt.h"
#include "sql/session/ob_sql_session_info.h"

namespace oceanbase
{
using namespace common;
namespace sql
{

ObDropCCLRuleResolver::ObDropCCLRuleResolver(ObResolverParams &params)
    : ObDDLResolver(params)
{
}

ObDropCCLRuleResolver::~ObDropCCLRuleResolver()
{
}

int ObDropCCLRuleResolver::resolve(const ParseNode &parse_tree)
{
  int ret = OB_SUCCESS;
  ParseNode *node = const_cast<ParseNode*>(&parse_tree);
  ObDropCCLRuleStmt *drop_ccl_rule_stmt = NULL;
  uint64_t tenant_id = OB_INVALID_ID;
  if (OB_ISNULL(node)
      || OB_UNLIKELY(node->type_ != T_DROP_CCL_RULE)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("invalid parse tree", K(ret));
  } else if (OB_ISNULL(session_info_)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("session info should not be null", K(ret));
  } else if (OB_ISNULL(node->children_)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_ERROR("invalid node children", K(node), K(node->children_));
  } else if (FALSE_IT(tenant_id = session_info_->get_effective_tenant_id())) {
  } else if (OB_ISNULL(drop_ccl_rule_stmt = create_stmt<ObDropCCLRuleStmt>())) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
    LOG_ERROR("failed to create create_database_stmt", K(ret));
  } else {
    stmt_ = drop_ccl_rule_stmt;
    drop_ccl_rule_stmt->set_tenant_id(session_info_->get_effective_tenant_id());
    // 1.resolve if exists
    if (OB_SUCC(ret) && lib::is_mysql_mode()) {
      if (node->children_[IF_EXIST] != NULL) {
        if (node->children_[IF_EXIST]->type_ != T_IF_EXISTS) {
          ret = OB_ERR_UNEXPECTED;
          LOG_WARN("invalid parse tree", K(ret), K(node->children_[IF_EXIST]->type_));
        } else {
          drop_ccl_rule_stmt->set_if_exists(true);
        }
      }
    }
    
    // 2. resolve ccl rule name
    if (OB_SUCC(ret)) {
      ObString ccl_rule_name;
      ParseNode *ccl_rule_name_node = node->children_[CCL_RULE_NAME];
      if (OB_ISNULL(ccl_rule_name_node)) {
        ret = OB_ERR_PARSE_SQL;
        LOG_WARN("ccl rule name parse node should not be null", K(ccl_rule_name_node));
      } else {
        ccl_rule_name.assign_ptr(ccl_rule_name_node->str_value_,
                                 static_cast<int32_t>(ccl_rule_name_node->str_len_));
        drop_ccl_rule_stmt->set_ccl_rule_name(ccl_rule_name);
      }
    }
  }
  return ret;
}

} //end namespace sql
} //end namespace oceanbase
