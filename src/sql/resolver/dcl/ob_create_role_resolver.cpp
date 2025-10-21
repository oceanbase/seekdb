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
#include "sql/resolver/dcl/ob_create_role_resolver.h"
#include "sql/resolver/dcl/ob_create_role_stmt.h"
#include "sql/resolver/dcl/ob_set_password_resolver.h"
#include "src/sql/resolver/ob_resolver_utils.h"

namespace oceanbase
{
using namespace common;

namespace sql
{
ObCreateRoleResolver::ObCreateRoleResolver(ObResolverParams &params)
    : ObDCLResolver(params)
{
}

ObCreateRoleResolver::~ObCreateRoleResolver()
{
}

int ObCreateRoleResolver::resolve(const ParseNode &parse_tree)
{
  int ret = OB_SUCCESS;
  CHECK_COMPATIBILITY_MODE(session_info_);
  ObCreateRoleStmt *create_role_stmt = NULL;
  if (T_CREATE_ROLE != parse_tree.type_
      || (2 != parse_tree.num_child_ && 3 != parse_tree.num_child_)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("expect 2 or 3 child, create role type",
             "actual_num", parse_tree.num_child_,
             "type", parse_tree.type_,
             K(ret));
  } else if (OB_ISNULL(params_.session_info_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("Session info should not be NULL", K(ret));
	} else if (OB_ISNULL(create_role_stmt = create_stmt<ObCreateRoleStmt>())) {
		ret = OB_ALLOCATE_MEMORY_FAILED;
		LOG_ERROR("Failed to create ObCreateRoleStmt", K(ret));
	} else if (NULL == parse_tree.children_[0]) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("role node is null", K(ret));
  } else { // Resolve role
    stmt_ = create_role_stmt;
    create_role_stmt->set_tenant_id(params_.session_info_->get_effective_tenant_id());
    ParseNode *role = const_cast<ParseNode*>(parse_tree.children_[0]);
    if (OB_ISNULL(role)) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("role should not be NULL", K(ret));
    } else {

      if (OB_SUCC(ret) && NULL != parse_tree.children_[1]) {
        if (T_IF_NOT_EXISTS != parse_tree.children_[1]->type_) {
          ret = OB_INVALID_ARGUMENT;
          LOG_WARN("invalid argument", K(parse_tree.children_[1]->type_), K(ret));
        } else {
          create_role_stmt->set_if_not_exists();
        }
      }

      OZ (create_role_stmt->get_user_names().reserve(role->num_child_));
      OZ (create_role_stmt->get_host_names().reserve(role->num_child_));

      for (int i = 0; OB_SUCC(ret) && i < role->num_child_; i++) {
        ParseNode *cur_role = role->children_[i];
        ObString user_name;
        ObString host_name;
        if (OB_ISNULL(cur_role)) {
          ret = OB_ERR_UNEXPECTED;
          LOG_WARN("invalid role", K(ret));
        } else {
          OZ (resolve_user_host(cur_role, user_name, host_name));
          user_name = user_name.trim_space_only();
          if (OB_SUCC(ret) && user_name.empty()) {
            ret = OB_CANNOT_USER;
            LOG_USER_ERROR(OB_CANNOT_USER, (int)strlen("CREATE ROLE"), "CREATE ROLE",
                           (int)strlen("anonymous user"), "anonymous user");
          }
          OZ (create_role_stmt->get_user_names().push_back(user_name));
          OZ (create_role_stmt->get_host_names().push_back(host_name));
        }
      }
    }
  }
  // resolve password
  ParseNode *pw_node = NULL;
  ParseNode *need_enc_node = NULL;
  if (OB_SUCC(ret)) {
    if (2 == parse_tree.num_child_) {
      // create role without password, do nothing
    } else if (OB_ISNULL(need_enc_node = parse_tree.children_[1])) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("need_enc_node is NULL", K(ret));
    } else if (OB_ISNULL(pw_node = parse_tree.children_[2])) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("pw_node is NULL", K(ret));
    } else {
      ObString password(pw_node->str_len_, pw_node->str_value_);
      if (1 == need_enc_node->value_) { // identified by 
        create_role_stmt->set_need_enc(true);
      } else {                          // identified by values
        create_role_stmt->set_need_enc(false);
        if (!ObSetPasswordResolver::is_valid_mysql41_passwd(password)) {
          ret = OB_ERR_PASSWORD_FORMAT;
          LOG_WARN("Wrong password format", K(password), K(ret));
        }
      }
      OX (create_role_stmt->set_password(password);)
    }
  }
  // replace password to *** in query_string for audit
  if (OB_SUCC(ret)) {
    ObString masked_sql;
    if (session_info_->is_inner()) {
    } else if (OB_FAIL(mask_password_for_passwd_node(allocator_,
                                                      session_info_->get_current_query_string(), 
                                                      pw_node, 
                                                      masked_sql))) {
      LOG_WARN("fail to mask_password_for_passwd_node", K(ret));
    } else {
      create_role_stmt->set_masked_sql(masked_sql);
    }
  }
  return ret;
}

} //namespace sql
} //namespace oceanbase
