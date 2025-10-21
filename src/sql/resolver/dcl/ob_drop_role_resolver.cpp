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
#include "sql/resolver/dcl/ob_drop_role_resolver.h"
#include "sql/resolver/dcl/ob_drop_role_stmt.h"

namespace oceanbase
{
using namespace common;

namespace sql
{
ObDropRoleResolver::ObDropRoleResolver(ObResolverParams &params)
    : ObDCLResolver(params)
{
}

ObDropRoleResolver::~ObDropRoleResolver()
{
}

int ObDropRoleResolver::resolve(const ParseNode &parse_tree)
{
  int ret = OB_SUCCESS;
  CHECK_COMPATIBILITY_MODE(session_info_);
  ObDropRoleStmt *drop_role_stmt = NULL;
  if (2 != parse_tree.num_child_ || T_DROP_ROLE != parse_tree.type_) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("expect 2 child, drop role type",
             "actual_num", parse_tree.num_child_,
             "type", parse_tree.type_,
             K(ret));
  } else if (OB_ISNULL(params_.session_info_)
             || OB_ISNULL(params_.schema_checker_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("Session info should not be NULL", K(ret));
	} else if (OB_ISNULL(drop_role_stmt = create_stmt<ObDropRoleStmt>())) {
		ret = OB_ALLOCATE_MEMORY_FAILED;
		LOG_ERROR("Failed to drop ObDropRoleStmt", K(ret));
	} else if (NULL == parse_tree.children_[0]) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("role node is null", K(ret));
  } else {
    //mysql mode
    stmt_ = drop_role_stmt;
    drop_role_stmt->set_tenant_id(params_.session_info_->get_effective_tenant_id());
    ParseNode *users_node = const_cast<ParseNode*>(parse_tree.children_[0]);

    if (OB_SUCC(ret) && NULL != parse_tree.children_[1]) {
      if (T_IF_EXISTS != parse_tree.children_[1]->type_) {
        ret = OB_INVALID_ARGUMENT;
        LOG_WARN("invalid argument", K(parse_tree.children_[1]->type_), K(ret));
      } else {
        drop_role_stmt->set_if_exists();
      }
    }

    for (int i = 0; OB_SUCC(ret) && i < users_node->num_child_; i++) {
      ParseNode *cur_role = users_node->children_[i];
      ObString user_name;
      ObString host_name;
      if (OB_ISNULL(cur_role)) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("invalid role", K(ret));
      } else {
        OZ (resolve_user_list_node(cur_role, users_node, user_name, host_name));
        OZ (drop_role_stmt->get_user_names().push_back(user_name));
        OZ (drop_role_stmt->get_host_names().push_back(host_name));
      }
    }
  }
  return ret;
}

} //namespace sql
} //namespace oceanbase
