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
#include "sql/resolver/dcl/ob_grant_resolver.h"

#include "sql/resolver/dcl/ob_set_password_resolver.h"
#include "sql/engine/ob_exec_context.h"

using namespace oceanbase::sql;
using namespace oceanbase::common;
using namespace oceanbase::share::schema;

ObGrantResolver::ObGrantResolver(ObResolverParams &params)
    : ObDCLResolver(params)
{
}

ObGrantResolver::~ObGrantResolver()
{
}

int ObGrantResolver::resolve_grantee_clause(
    const ParseNode *grantee_clause,
    ObSQLSessionInfo *session_info,
    ObIArray<ObString> &user_name_array,
    ObIArray<ObString> &host_name_array)
{
  int ret = OB_SUCCESS;
  ParseNode *grant_user  = NULL;
  if (OB_ISNULL(grantee_clause) || grantee_clause->num_child_ < 1
      || OB_ISNULL(grantee_clause->children_[0])) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("resolve grantee error", K(ret));
  } else {
    // Put every grant_user into grant_user_arry
    if (grantee_clause->type_ != T_USERS && grantee_clause->type_ != T_GRANT) {
      ret = OB_INVALID_ARGUMENT;
      LOG_WARN("invalid type", K(ret), K(grantee_clause->type_));
    } else if (grantee_clause->type_ == T_USERS) {
      const ParseNode *grant_user_list = grantee_clause->children_[0];
      for (int i = 0; OB_SUCC(ret) && i < grant_user_list->num_child_; ++i) {
        grant_user = grant_user_list->children_[i];
        if (OB_ISNULL(grant_user)) {
          ret = OB_ERR_PARSE_SQL;
          LOG_WARN("grant_user is NULL", K(ret));
        } else {
          ObString user_name;
          ObString host_name(OB_DEFAULT_HOST_NAME);
          LOG_DEBUG("grant_user", K(i), K(grant_user->str_value_), K(grant_user->type_));
          if (OB_FAIL(resolve_grant_user(grant_user, session_info, user_name, host_name))) {
            LOG_WARN("failed to resolve grant_user", K(ret), K(grant_user));
          } else {
            OZ(user_name_array.push_back(user_name));
            OZ(host_name_array.push_back(host_name));
          }
        }
      }
    } else if (grantee_clause->type_ == T_GRANT) {
      grant_user = grantee_clause->children_[0];
      if (OB_ISNULL(grant_user)) {
        ret = OB_ERR_PARSE_SQL;
        LOG_WARN("grant_user is NULL", K(ret));
      } else {
        ObString user_name;
        ObString host_name(OB_DEFAULT_HOST_NAME);
        if (OB_FAIL(resolve_grant_user(grant_user, session_info, user_name, host_name))) {
          LOG_WARN("failed to resolve grant_user", K(ret), K(grant_user));
        } else {
          OZ(user_name_array.push_back(user_name));
          OZ(host_name_array.push_back(host_name));
        }
      }
    }
  }
  return ret;
}

int ObGrantResolver::resolve_grant_user(
    const ParseNode *grant_user,
    ObSQLSessionInfo *session_info,
    ObString &user_name,
    ObString &host_name)
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(grant_user) || OB_ISNULL(session_info)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("resolve grant_user error", K(ret));
  } else {
    if (grant_user->type_ == T_CREATE_USER_SPEC) {
      if (OB_UNLIKELY(lib::is_mysql_mode() && 5 != grant_user->num_child_)) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("Parse node error in grentee ", K(ret));
      } else {
        if (grant_user->children_[0]->type_ == T_FUN_SYS_CURRENT_USER) {
          user_name = session_info->get_user_name();
          host_name = session_info->get_host_name();
        } else {
          user_name.assign_ptr(const_cast<char *>(grant_user->children_[0]->str_value_),
          static_cast<int32_t>(grant_user->children_[0]->str_len_));
        }
        if (NULL != grant_user->children_[3]) {
          // host name is not default 
          host_name.assign_ptr(const_cast<char *>(grant_user->children_[3]->str_value_),
            static_cast<int32_t>(grant_user->children_[3]->str_len_));
        }
        if (lib::is_mysql_mode() && NULL != grant_user->children_[4]) {
          /* here code is to mock a auth plugin check. */
          ObString auth_plugin(static_cast<int32_t>(grant_user->children_[4]->str_len_),
                                grant_user->children_[4]->str_value_);
          ObString default_auth_plugin;
          if (OB_FAIL(session_info->get_sys_variable(share::SYS_VAR_DEFAULT_AUTHENTICATION_PLUGIN,
                                                     default_auth_plugin))) {
            LOG_WARN("fail to get block encryption variable", K(ret));
          } else if (0 != auth_plugin.compare(default_auth_plugin)) {
            ret = OB_ERR_PLUGIN_IS_NOT_LOADED;
            LOG_USER_ERROR(OB_ERR_PLUGIN_IS_NOT_LOADED, auth_plugin.length(), auth_plugin.ptr());
          } else {/* do nothing */}
        }
      }
    } else {
      user_name.assign_ptr(const_cast<char *>(grant_user->str_value_), 
                           static_cast<int32_t>(grant_user->str_len_));
    }
  }

  return ret;
}

/* grant_system_privileges:
role_list TO grantee_clause opt_with_admin_option
[0]: role_list
[1]: grantee_clause
[2]: opt_with_admin_option

enum GrantParseOffset
{
  PARSE_GRANT_ROLE_LIST,
  PARSE_GRANT_ROLE_GRANTEE,
  PARSE_GRANT_ROLE_OPT_WITH,
  PARSE_GRANT_ROLE_MAX_IDX
};
Parse grantee and role_list from grant_role, and put grantee and role_list into role of grant_stmt.
role[0]: user_name of grantee
role[1]: host_name of grantee
...
*/
int ObGrantResolver::resolve_grant_role_to_ur(
    const ParseNode *grant_role,
    ObGrantStmt *grant_stmt)
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(grant_role) || OB_ISNULL(grant_stmt)
      || OB_ISNULL(params_.schema_checker_)
      || OB_ISNULL(params_.session_info_)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("grant_role and grant_stmt should not be NULL", K(grant_role), K(grant_stmt), K(ret));
  } else {
    ObSArray<ObString> user_name_array;
    ObSArray<ObString> host_name_array;
    ObSEArray<uint64_t, 4> role_id_array;
    ParseNode *grantee_clause = grant_role->children_[PARSE_GRANT_ROLE_GRANTEE];
    ParseNode *role_list = grant_role->children_[PARSE_GRANT_ROLE_LIST];
    obrpc::ObGrantArg &args = static_cast<obrpc::ObGrantArg &>(grant_stmt->get_ddl_arg());
    ObSchemaChecker *schema_ck = params_.schema_checker_;
    uint64_t tenant_id = params_.session_info_->get_effective_tenant_id();
    ObArray<uint64_t> grantee_ids;

    CK (role_list != NULL);
    CK (grantee_clause != NULL);

    OZ (resolve_grantee_clause(grantee_clause, params_.session_info_,
                               user_name_array, host_name_array));
    if (OB_SUCC(ret)) {
      if (user_name_array.count() != host_name_array.count()) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("user_name count is not equal to host_name count",
                 K(ret),
                 K(user_name_array.count()),
                 K(host_name_array.count()));
      } else {
        for (int i = 0; OB_SUCC(ret) && i < user_name_array.count(); ++i) {
          const ObUserInfo *grantee_info = NULL;
          ObString &user_name = user_name_array.at(i);
          ObString &host_name = host_name_array.at(i);
          OZ (schema_ck->get_user_info(tenant_id, user_name, host_name, grantee_info), user_name, host_name);
          if (OB_USER_NOT_EXIST == ret || OB_ISNULL(grantee_info)) {
            ret = OB_ERR_UNKNOWN_AUTHID;
            LOG_USER_ERROR(OB_ERR_UNKNOWN_AUTHID,
                            user_name.length(), user_name.ptr(),
                            host_name.length(), host_name.ptr());
          }
          if (OB_SUCC(ret) && !has_exist_in_array(grantee_ids, grantee_info->get_user_id())) {
            if (args.roles_.empty()) {
              OZ (args.roles_.push_back(user_name));
              OZ (args.roles_.push_back(host_name));
            } else {
              OZ (args.remain_roles_.push_back(user_name));
              OZ (args.remain_roles_.push_back(host_name));
            }
            OZ (grantee_ids.push_back(grantee_info->get_user_id()));
            OZ (grant_stmt->add_grantee(user_name));
          }
        }
      }
    }

    if (OB_SUCC(ret)) {
      ObSqlCtx *sql_ctx = NULL;
      const ObUserInfo *role_info = NULL;
      uint64_t option = NO_OPTION;

      if (OB_ISNULL(params_.session_info_->get_cur_exec_ctx())
          || OB_ISNULL(sql_ctx = params_.session_info_->get_cur_exec_ctx()->get_sql_ctx())) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("unexpected ctx", K(ret), KP(params_.session_info_->get_cur_exec_ctx()));
      }

      for (int i = 0; OB_SUCC(ret) && i < role_list->num_child_; ++i) {
        ParseNode *cur_role = role_list->children_[i];
        ObString user_name;
        ObString host_name;
        OZ (resolve_user_host(cur_role, user_name, host_name));
        OZ (schema_ck->get_user_info(tenant_id, user_name, host_name, role_info), user_name, host_name);
        if (OB_USER_NOT_EXIST == ret || OB_ISNULL(role_info)) {
          ret = OB_ERR_UNKNOWN_AUTHID;
          LOG_USER_ERROR(OB_ERR_UNKNOWN_AUTHID,
                         user_name.length(), user_name.ptr(),
                         host_name.length(), host_name.ptr());
        }
        if (OB_SUCC(ret)) {
          for (int j = 0; OB_SUCC(ret) && j < user_name_array.count(); j++) {
            if (user_name_array.at(j) == user_name && host_name_array.at(j) == host_name) {
              ret = OB_ERR_CIRCULAR_ROLE_GRANT_DETECTED;
              LOG_USER_ERROR(OB_ERR_CIRCULAR_ROLE_GRANT_DETECTED,
                     user_name.length(), user_name.ptr(),
                     host_name.length(), host_name.ptr(),
                     user_name_array.at(j).length(), user_name_array.at(j).ptr(),
                     host_name_array.at(j).length(), host_name_array.at(j).ptr());
            }
          }
        }
        if (OB_SUCC(ret) && !has_exist_in_array(role_id_array, role_info->get_user_id())) {
          OZ (role_id_array.push_back(role_info->get_user_id()));
          OZ (args.roles_.push_back(user_name));
          OZ (args.roles_.push_back(host_name));
        }
      }
      OZ (resolve_admin_option( grant_role->children_[PARSE_GRANT_ROLE_OPT_WITH], option));
      OX (grant_stmt->set_option(option));
      OZ (schema_ck->check_mysql_grant_role_priv(*sql_ctx, role_id_array));
    }
  }

  return ret;
}

int ObGrantResolver::resolve_admin_option(
    const ParseNode *admin_option,
    uint64_t &option)
{
  int ret = OB_SUCCESS;
  if (admin_option == NULL) {
    option = NO_OPTION;
  } else {
    if (admin_option->type_ != T_WITH_ADMIN_OPTION) {
      ret = OB_ERR_PARSE_SQL;
    } else {
      option = ADMIN_OPTION;
    }
  }
  return ret;
}    

int ObGrantResolver::resolve_grant_role_mysql(
    const ParseNode *grant_role,
    ObGrantStmt *grant_stmt)
{
  int ret = OB_SUCCESS;
      // resolve grant role to user
  if (OB_ISNULL(grant_role)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_ERROR("Grant ParseNode error", K(ret));
  } else {
    grant_stmt->set_stmt_type(stmt::T_GRANT_ROLE);
    if (OB_FAIL(resolve_grant_role_to_ur(grant_role, grant_stmt))) {
      LOG_WARN("resolve_grant_role fail", K(ret));
    }
  }
  return ret;
}

int ObGrantResolver::resolve(const ParseNode &parse_tree)
{
  int ret = OB_SUCCESS;
  CHECK_COMPATIBILITY_MODE(session_info_);
  ret = resolve_mysql(parse_tree);
  return ret;
}

int ObGrantResolver::resolve_mysql(const ParseNode &parse_tree)
{
  int ret = OB_SUCCESS;

  ParseNode *node = const_cast<ParseNode*>(&parse_tree);
  ObGrantStmt *grant_stmt = NULL;
  if (OB_ISNULL(params_.schema_checker_) || OB_ISNULL(params_.session_info_) || OB_ISNULL(allocator_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("schema_checker or session_info not inited", "schema_checker", params_.schema_checker_,
                                                          "session_info", params_.session_info_,
                                                          K(ret));
  } else if (node == NULL 
      || (T_GRANT != node->type_ && T_SYSTEM_GRANT != node->type_ && T_GRANT_ROLE != node->type_)
      || ((1 != node->num_child_) && (4 != node->num_child_) && (3 != node->num_child_))) {
    ret = OB_INVALID_ARGUMENT;
    LOG_ERROR("Grant ParseNode error", K(ret));
  } else if (OB_ISNULL(grant_stmt = create_stmt<ObGrantStmt>())) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
    LOG_ERROR("Failed to create ObCreateUserStmt", K(ret));
  } else {
    grant_stmt->set_stmt_type(T_GRANT == node->type_ ? stmt::T_GRANT : stmt::T_SYSTEM_GRANT);
    stmt_ = grant_stmt;
    uint64_t tenant_id = params_.session_info_->get_effective_tenant_id();
    grant_stmt->set_tenant_id(tenant_id);
    if (T_GRANT_ROLE == node->type_) {
      if (OB_FAIL(resolve_grant_role_mysql(node, grant_stmt))) {
        LOG_WARN("resolve grant system privileges failed", K(ret));
      }
    } else {
      ParseNode *privs_node = node->children_[0];
      ParseNode *priv_object_node = node->children_[1];
      ParseNode *priv_level_node = node->children_[2];
      ParseNode *users_node = node->children_[3];
      if (privs_node != NULL && priv_level_node != NULL && users_node != NULL) {
        ObPrivLevel grant_level = OB_PRIV_INVALID_LEVEL;
        //resolve priv_level
        if (OB_SUCC(ret)) {
          ObString db = ObString::make_string("");
          ObString table = ObString::make_string("");
          ObString catalog = ObString::make_string("");
          if (priv_object_node != NULL
              && OB_FAIL(resolve_priv_level_with_object_type(session_info_,
                                                             priv_object_node,
                                                             grant_level))) {
            LOG_WARN("failed to resolve priv level with object", K(ret));
          } else if (OB_FAIL(resolve_priv_level(params_.schema_checker_->get_schema_guard(),
                                                session_info_,
                                                priv_level_node,
                                                params_.session_info_->get_database_name(),
                                                db,
                                                table,
                                                grant_level,
                                                *allocator_,
                                                catalog))) {
            LOG_WARN("Resolve priv_level node error", K(ret));
          } else {
            grant_stmt->set_grant_level(grant_level);
          }

          if (OB_SUCC(ret) && grant_level != OB_PRIV_CATALOG_LEVEL) {
            if (OB_FAIL(check_and_convert_name(db, table))) {
              LOG_WARN("Check and convert name error", K(db), K(table), K(ret));
            } else if (OB_FAIL(grant_stmt->set_database_name(db))) {
              LOG_WARN("Failed to set database_name to grant_stmt", K(ret));
            } else if (OB_FAIL(grant_stmt->set_table_name(table))) {
              LOG_WARN("Failed to set table_name to grant_stmt", K(ret));
            }
          }

          if (OB_SUCC(ret) && OB_FAIL(resolve_priv_object(priv_object_node,
                                                          grant_stmt,
                                                          params_.schema_checker_,
                                                          db,
                                                          table,
                                                          catalog,
                                                          tenant_id,
                                                          allocator_))) {
            LOG_WARN("failed to resolve priv object", K(ret));
          }
        }

        //resolve privileges
        if (OB_SUCC(ret)) {
          const uint64_t tenant_id = params_.session_info_->get_effective_tenant_id();  
          ObPrivSet priv_set = 0;
          if (OB_ISNULL(allocator_)) {
            ret = OB_ERR_UNEXPECTED;
            LOG_WARN("unexpected error", K(ret));
          } else if (OB_FAIL(resolve_priv_set(tenant_id, privs_node, grant_level, priv_set, grant_stmt, params_.schema_checker_,
                                                                                      params_.session_info_,
                                                                                      *allocator_))) {
            LOG_WARN("Resolve priv set error", K(ret));
          }
          if (OB_FAIL(ret)) {
          } else {
            grant_stmt->set_priv_set(priv_set);
          }
        }

        //check whether table exist.If table no exist, priv set should contain create priv.
        if (OB_SUCC(ret)) {
          if (OB_PRIV_TABLE_LEVEL == grant_level) { //need check if table exist
            bool exist = false;
            const bool is_index = false;
            const ObString &db = grant_stmt->get_database_name();
            const ObString &table = grant_stmt->get_table_name();
            if (OB_FAIL(params_.schema_checker_->check_table_exists(
                    tenant_id, db, table, is_index, false/*is_hidden*/, exist))) {
              LOG_WARN("Check table exist error", K(ret));
            } else if (!exist) {
              if (!(OB_PRIV_CREATE & grant_stmt->get_priv_set())
                  && !params_.is_restore_
                  && !params_.is_ddl_from_primary_) {
                ret = OB_TABLE_NOT_EXIST;
                LOG_WARN("table not exist", K(ret), K(table), K(db));
                ObCStringHelper helper;
                LOG_USER_ERROR(OB_TABLE_NOT_EXIST, helper.convert(db),helper.convert(table));
              }
            } else {
              //do nothing
            }
          }
        }

        //resolve users
        if (OB_SUCC(ret)) {
          // In oracle mode during grant, if user does not exist, do not allow creating this user; fix #17900015
          bool need_create_user = false;
          CHECK_COMPATIBILITY_MODE(session_info_);
          need_create_user = !is_no_auto_create_user(params_.session_info_->get_sql_mode());
          grant_stmt->set_need_create_user(need_create_user);
          if (users_node->num_child_ > 0) {
            ObString masked_sql;
            if (session_info_->is_inner()) {
              // do nothing in inner_sql
            } else if (OB_FAIL(mask_password_for_users(allocator_,
                session_info_->get_current_query_string(), users_node, 1, masked_sql))) {
              LOG_WARN("fail to mask_password_for_users", K(ret));
            } else {
              grant_stmt->set_masked_sql(masked_sql);
            }
            for (int i = 0; OB_SUCC(ret) && i < users_node->num_child_; ++i) {
              ParseNode *user_node = users_node->children_[i];
              ObString user_name;
              ObString host_name;
              ObString pwd;
              ObString need_enc = ObString::make_string("NO");
              if (OB_ISNULL(user_node)) {
                ret = OB_ERR_PARSE_SQL;
                LOG_WARN("Parse SQL error, user node should not be NULL", K(user_node), K(ret));
              } else if (OB_UNLIKELY(lib::is_mysql_mode() && 5 != user_node->num_child_)) {
                ret = OB_ERR_PARSE_SQL;
                LOG_WARN("User specification's child node num error", K(ret));
              } else if (OB_ISNULL(user_node->children_[0])) {
                ret = OB_ERR_PARSE_SQL;
                LOG_WARN("The child 0 should not be NULL", K(ret));
              } else {

                if (user_node->children_[0]->type_ == T_FUN_SYS_CURRENT_USER) {
                  user_name = session_info_->get_user_name();
                  host_name = session_info_->get_host_name();
                } else {
                  user_name.assign_ptr(const_cast<char *>(user_node->children_[0]->str_value_),
                      static_cast<int32_t>(user_node->children_[0]->str_len_));
                }
                if (NULL == user_node->children_[3]) {
                  if (user_node->children_[0]->type_ == T_FUN_SYS_CURRENT_USER) {
                  } else {
                    host_name.assign_ptr(OB_DEFAULT_HOST_NAME, static_cast<int32_t>(STRLEN(OB_DEFAULT_HOST_NAME)));
                  }
                } else {
                  host_name.assign_ptr(user_node->children_[3]->str_value_,
                      static_cast<int32_t>(user_node->children_[3]->str_len_));
                }
                if (lib::is_mysql_mode() && NULL != user_node->children_[4]) {
                  /* here code is to mock a auth plugin check. */
                  ObString auth_plugin(static_cast<int32_t>(user_node->children_[4]->str_len_),
                                      user_node->children_[4]->str_value_);
                  ObString default_auth_plugin;
                  if (OB_FAIL(params_.session_info_->get_sys_variable(
                                                       share::SYS_VAR_DEFAULT_AUTHENTICATION_PLUGIN,
                                                       default_auth_plugin))) {
                    LOG_WARN("fail to get block encryption variable", K(ret));
                  } else if (0 != auth_plugin.compare(default_auth_plugin)) {
                    ret = OB_ERR_PLUGIN_IS_NOT_LOADED;
                    LOG_USER_ERROR(OB_ERR_PLUGIN_IS_NOT_LOADED, auth_plugin.length(), auth_plugin.ptr());
                  } else {/* do nothing */}
                }
                if (OB_SUCC(ret) && user_node->children_[1] != NULL) {
                  if (0 != user_name.compare(session_info_->get_user_name())) {
                    grant_stmt->set_need_create_user_priv(true);
                  }
                  pwd.assign_ptr(const_cast<char *>(user_node->children_[1]->str_value_),
                      static_cast<int32_t>(user_node->children_[1]->str_len_));
                  if (OB_ISNULL(user_node->children_[2])) {
                    ret = OB_ERR_PARSE_SQL;
                    LOG_WARN("The child 2 of user_node should not be NULL", K(ret));
                  } else if (0 == user_node->children_[2]->value_) {
                    if (!ObSetPasswordResolver::is_valid_mysql41_passwd(pwd)) {
                      ret = OB_ERR_PASSWORD_FORMAT;
                      LOG_WARN("Wrong password hash format");
                    }
                  } else if (OB_FAIL(check_password_strength(pwd))) {
                    LOG_WARN("fail to check password strength", K(ret));
                  } else {
                    need_enc = ObString::make_string("YES");
                  }
                } else {
                  pwd = ObString("");
                }
              }
              if (OB_SUCC(ret)) {
                if (user_name.length() > OB_MAX_USER_NAME_LENGTH) {
                  ret = OB_WRONG_USER_NAME_LENGTH;
                  LOG_USER_ERROR(OB_WRONG_USER_NAME_LENGTH, user_name.length(), user_name.ptr());
                } else if (OB_FAIL(check_dcl_on_inner_user(node->type_,
                                                           session_info_->get_priv_user_id(),
                                                           user_name,
                                                           host_name))) {
                  LOG_WARN("failed to check dcl on inner-user or unsupport to modify reserved user",
                           K(ret), K(session_info_->get_user_name()), K(user_name));
                } else if (OB_FAIL(grant_stmt->add_grantee(user_name))) {
                  LOG_WARN("Add grantee error", K(user_name), K(ret));
                } else if (OB_FAIL(grant_stmt->add_user(user_name, host_name, pwd, need_enc))) {
                  LOG_WARN("Add user and pwd error", K(user_name), K(pwd), K(ret));
                } else {
                  //do nothing
                }
              }
            }
          }
        }//end of resolve users
      }
    }
  }
  return ret;
}

int ObGrantResolver::resolve_priv_level_with_object_type(const ObSQLSessionInfo *session_info,
                                                         const ParseNode *priv_object_node,
                                                         ObPrivLevel &grant_level)
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(session_info)) {
    ret = OB_NOT_INIT;
    LOG_WARN("session_info not inited", K(ret));
  } else if (OB_ISNULL(priv_object_node)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("get unexpected parse node", K(ret));
  }
  if (OB_SUCC(ret)) {
    if (priv_object_node->value_ == 1) {
      // do nothing, compat with mysql
    } else if (priv_object_node->value_ == 2 || priv_object_node->value_ == 3) {
      grant_level = OB_PRIV_ROUTINE_LEVEL;
    } else if (priv_object_node->value_ == 4) {
      grant_level = OB_PRIV_CATALOG_LEVEL;
    } else if (priv_object_node->value_ == 5) {
        grant_level = OB_PRIV_OBJECT_LEVEL;
    } else {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("unexpected obj type", K(ret), K(priv_object_node->value_));
    }
  }
  return ret;
}

//0 == priv_level_node->num_child_ -> grant priv on * to user
//0 == priv_level_node->num_child_ -> grant priv on table to user
//2 == priv_level_node->num_child_ -> grant priv on db.table to user
int ObGrantResolver::resolve_priv_level(
    ObSchemaGetterGuard *guard,
    const ObSQLSessionInfo *session,
    const ParseNode *node,
    const ObString &session_db,
    ObString &db,
    ObString &table,
    ObPrivLevel &grant_level,
    ObIAllocator &allocator,
    ObString &catalog)
{
  int ret = OB_SUCCESS;
  bool is_grant_routine = (grant_level == OB_PRIV_ROUTINE_LEVEL);
  bool is_grant_object = (grant_level == OB_PRIV_OBJECT_LEVEL);
  if (OB_ISNULL(node)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("Invalid argument", K(node), K(ret));
  } else if (grant_level == OB_PRIV_CATALOG_LEVEL) {
    if (0 != node->num_child_ || T_IDENT != node->type_) {
      ret = OB_ERR_PARSE_SQL;
      LOG_WARN("sql_parser error", K(ret));
    } else {
      catalog.assign_ptr(node->str_value_, static_cast<const int32_t>(node->str_len_));
    }
  } else {
    CK (guard != NULL);
    db = ObString::make_string("");
    table = ObString::make_string("");
    //0 == priv_level_node->num_child_ -> grant priv on * to user
    //0 == priv_level_node->num_child_ -> grant priv on table to user
    //2 == priv_level_node->num_child_ -> grant priv on db.table to user
    if (0 == node->num_child_) {
      if (T_STAR == node->type_) {
        grant_level = OB_PRIV_DB_LEVEL;
      } else if (T_IDENT == node->type_) {
        grant_level = OB_PRIV_TABLE_LEVEL;
        table.assign_ptr(node->str_value_, static_cast<const int32_t>(node->str_len_));
      } else {
        ret = OB_ERR_PARSE_SQL;
        LOG_WARN("sql_parser error", K(ret));
      }
      if (OB_SUCC(ret)) {
        if (0 == session_db.length()) {
          ret = OB_ERR_NO_DB_SELECTED;
          LOG_WARN("No database selected", K(ret));
        } else {
          db = session_db;
        }
      }
    } else if (T_PRIV_LEVEL == node->type_ && 2 == node->num_child_) {
      if (OB_ISNULL(node->children_[0]) || OB_ISNULL(node->children_[1])) {
        ret = OB_ERR_PARSE_SQL;
        LOG_WARN("Parse priv level error",
            K(ret), "child 0", node->children_[0], "child 1", node->children_[1]);
      } else if (T_STAR == node->children_[0]->type_ && T_STAR == node->children_[1]->type_) {
        grant_level = OB_PRIV_USER_LEVEL;
      } else if (T_IDENT == node->children_[0]->type_ && T_STAR == node->children_[1]->type_) {
        grant_level = OB_PRIV_DB_LEVEL;
        db.assign_ptr(node->children_[0]->str_value_,
                      static_cast<const int32_t>(node->children_[0]->str_len_));
        OZ (ObSQLUtils::cvt_db_name_to_org(*guard, session, db, &allocator));
      } else if (T_IDENT == node->children_[0]->type_ && T_IDENT == node->children_[1]->type_) {
        grant_level = OB_PRIV_TABLE_LEVEL;
        db.assign_ptr(node->children_[0]->str_value_,
                      static_cast<const int32_t>(node->children_[0]->str_len_));
        table.assign_ptr(node->children_[1]->str_value_,
                         static_cast<const int32_t>(node->children_[1]->str_len_));
        OZ (ObSQLUtils::cvt_db_name_to_org(*guard, session, db, &allocator));
      } else {
        ret = OB_ERR_PARSE_SQL;
        LOG_WARN("sql_parser error", K(ret));
      }
    } else {
      ret = OB_ERR_PARSE_SQL;
      LOG_WARN("sql_parser parse grant_stmt error", K(ret));
    }
    if (OB_SUCC(ret)) {
      if (OB_PRIV_TABLE_LEVEL == grant_level && table.empty()) {
        ret = OB_WRONG_TABLE_NAME;
        LOG_USER_ERROR(OB_WRONG_TABLE_NAME, table.length(), table.ptr());
      } else if (!(OB_PRIV_USER_LEVEL == grant_level) && db.empty()) {
        //different with MySQL. MySQL may be error.
        ret = OB_WRONG_DB_NAME;
        LOG_USER_ERROR(OB_WRONG_DB_NAME, db.length(), db.ptr());
      } else {
        //do nothing
      }
    }
    if (OB_SUCC(ret) && (is_grant_routine || is_grant_object)) {
      if (grant_level != OB_PRIV_TABLE_LEVEL) {
        // tmp_grant_level == OB_PRIV_TABLE_LEVEL means sql is like:
        // grant priv on [object type] ident to user
        // or
        // grant priv on [object type] ident1.ident2 to user
        ret = OB_ILLEGAL_GRANT_FOR_TABLE;
        LOG_WARN("illegal grant", K(ret));
      } else {
        grant_level = is_grant_routine ? OB_PRIV_ROUTINE_LEVEL : OB_PRIV_OBJECT_LEVEL;
      }
    }
  }
  return ret;
}
