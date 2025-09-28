/**
 * Copyright (c) 2021 OceanBase
 * OceanBase CE is licensed under Mulan PubL v2.
 * You can use this software according to the terms and conditions of the Mulan PubL v2.
 * You may obtain a copy of Mulan PubL v2 at:
 *          http://license.coscl.org.cn/MulanPubL-2.0
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND,
 * EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
 * MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
 * See the Mulan PubL v2 for more details.
 */

#define USING_LOG_PREFIX SQL_RESV
#include "sql/resolver/dcl/ob_revoke_resolver.h"

#include "sql/resolver/dcl/ob_grant_resolver.h"
#include "sql/engine/ob_exec_context.h"


using namespace oceanbase::sql;
using namespace oceanbase::common;
using namespace oceanbase::share::schema;

ObRevokeResolver::ObRevokeResolver(ObResolverParams &params)
    : ObDCLResolver(params)
{
}

ObRevokeResolver::~ObRevokeResolver()
{
}

/* 解析revoke role from ur */
int ObRevokeResolver::resolve_revoke_role_inner(
    const ParseNode *revoke_role,
    ObRevokeStmt *revoke_stmt)
{
  int ret = OB_SUCCESS;
  uint64_t tenant_id = OB_INVALID_ID;
  ObSEArray<uint64_t, 4> role_id_array;
  ObArray<ObString> role_user_name;
  ObArray<ObString> role_host_name;

  CK (revoke_role != NULL && revoke_stmt != NULL);
  CK ((2 == revoke_role->num_child_ || 4 == revoke_role->num_child_) &&
      NULL != revoke_role->children_[0] &&
      NULL != revoke_role->children_[1]);
  bool ignore_unknown_role = false;
  bool ignore_unknown_user = false;
  bool ignore_error = false;
  tenant_id = revoke_stmt->get_tenant_id();
  if (lib::is_mysql_mode() && 4 == revoke_role->num_child_) {
    ignore_unknown_role = NULL != revoke_role->children_[2];
    ignore_unknown_user = NULL != revoke_role->children_[3];
  }
  if (lib::is_mysql_mode()) {
    OZ (ObSQLUtils::compatibility_check_for_mysql_role_and_column_priv(tenant_id));
  }
  // 1. resolve role list
  ParseNode *role_list = revoke_role->children_[0];
  for (int i = 0; OB_SUCC(ret) && i < role_list->num_child_; ++i) {
    const ObUserInfo *role_info = NULL;
    uint64_t role_id = OB_INVALID_ID;
    ParseNode *role = role_list->children_[i];
    if (NULL == role) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("role node is null", K(ret));
    } else {
      ObString role_name;
      ObString host_name(OB_DEFAULT_HOST_NAME);
      OZ (resolve_user_host(role, role_name, host_name));

      OZ (params_.schema_checker_->get_user_info(tenant_id,
                                                 role_name,
                                                 host_name,
                                                 role_info), role_name, host_name);
      if (OB_USER_NOT_EXIST == ret || OB_ISNULL(role_info)) {
        ret = OB_ERR_UNKNOWN_AUTHID;
        ignore_error = ignore_unknown_role;
        LOG_USER(ignore_unknown_role ? ObLogger::USER_WARN : ObLogger::USER_ERROR,
                  OB_ERR_UNKNOWN_AUTHID,
                  role_name.length(), role_name.ptr(),
                  host_name.length(), host_name.ptr());
      } else {
        role_id = role_info->get_user_id();
        if (OB_FAIL(revoke_stmt->add_role(role_id))) {
          if (OB_PRIV_DUP == ret && lib::is_mysql_mode()) {
            //ignored
            ret = OB_SUCCESS;
          } else {
            LOG_WARN("failed to add role", K(ret));
          }
        } else {
          OZ (role_id_array.push_back(role_id));
          OZ (role_user_name.push_back(role_name));
          OZ (role_host_name.push_back(host_name));
        }
      }
    }
  }
  // 2. check privilege
  if (OB_SUCC(ret)) {
    ObSqlCtx *sql_ctx = NULL;
    if (OB_ISNULL(params_.session_info_->get_cur_exec_ctx())
        || OB_ISNULL(sql_ctx = params_.session_info_->get_cur_exec_ctx()->get_sql_ctx())) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("unexpected ctx", K(ret), KP(params_.session_info_->get_cur_exec_ctx()));
    }
    OZ (params_.schema_checker_->check_mysql_grant_role_priv(*sql_ctx, role_id_array));
  }

  // 3. resolve grantee
  uint64_t user_id = OB_INVALID_ID;
  const ObUserInfo *user_info = NULL;
  ObSArray<ObString> user_name_array;
  ObSArray<ObString> host_name_array;
  OZ (ObGrantResolver::resolve_grantee_clause(revoke_role->children_[1], 
                                              params_.session_info_,
                                              user_name_array, 
                                              host_name_array));
  CK (user_name_array.count() == host_name_array.count());
  for (int i = 0; OB_SUCC(ret) && i < user_name_array.count(); ++i) {
    const ObString &user_name = user_name_array.at(i);
    const ObString &host_name = host_name_array.at(i);
    if (OB_FAIL(params_.schema_checker_->get_user_id(tenant_id, user_name, host_name, user_id))) {
      if (OB_USER_NOT_EXIST == ret) {
        ret = OB_ERR_UNKNOWN_AUTHID;
        ignore_error = ignore_unknown_user;
        LOG_USER(ignore_unknown_user ? ObLogger::USER_WARN : ObLogger::USER_ERROR,
                  OB_ERR_UNKNOWN_AUTHID,
                  user_name.length(), user_name.ptr(),
                  host_name.length(), host_name.ptr());
      }
      LOG_WARN("fail to get user id", K(ret), K(user_name), K(host_name));
    } else if (OB_FAIL(check_dcl_on_inner_user(revoke_role->type_,
                                               params_.session_info_->get_priv_user_id(),
                                               user_id))) {
      LOG_WARN("failed to check dcl on inner-user or unsupport to modify reserved user", K(ret),
               K(session_info_->get_priv_user_id()), K(user_name));
    }
    OZ (revoke_stmt->add_grantee(user_name));
    OZ (params_.schema_checker_->get_user_info(tenant_id, user_id, user_info), user_id);
    OZ (revoke_stmt->add_user(user_id));
    //4. check user has roles
    if (OB_SUCC(ret)) {
      CK (OB_NOT_NULL(user_info));
    }
  }
  // 5. set grant level
  if (OB_SUCC(ret)) {
    // roles_: 必须大于等于0
    if ((revoke_stmt->get_roles()).count() < 1) {
      ret = OB_INVALID_ARGUMENT;
      LOG_WARN("role argument is invalid", K(ret));
    } else {
      // role当作user处理
      revoke_stmt->set_grant_level(OB_PRIV_USER_LEVEL);
    }
  }

  if (OB_FAIL(ret) && OB_NOT_NULL(revoke_stmt) && ignore_error) {
    revoke_stmt->set_has_warning();
    ret = OB_SUCCESS;
  }

  return ret;
}

int ObRevokeResolver::resolve(const ParseNode &parse_tree)
{
  int ret = OB_SUCCESS;
  CHECK_COMPATIBILITY_MODE(session_info_);
  ret = resolve_mysql(parse_tree);
  return ret;
}

int ObRevokeResolver::resolve_mysql(const ParseNode &parse_tree)
{
  int ret = OB_SUCCESS;
  static const int REVOKE_NUM_CHILD = 6;
  static const int REVOKE_ALL_NUM_CHILD = 3;
  static const int REVOKE_ROLE_NUM_CHILD = 1;
  ParseNode *node = const_cast<ParseNode*>(&parse_tree);
  ObRevokeStmt *revoke_stmt = NULL;
  if (OB_ISNULL(params_.schema_checker_) || OB_ISNULL(params_.session_info_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("schema_checker or session info not inited",
        K(ret), "schema checker", params_.schema_checker_, "session info", params_.session_info_);
  } else if (node != NULL 
      && ((T_REVOKE == node->type_ && REVOKE_NUM_CHILD == node->num_child_)
        || (T_REVOKE_ALL == node->type_ && REVOKE_ALL_NUM_CHILD == node->num_child_)
        || (T_SYSTEM_REVOKE == node->type_ && REVOKE_ROLE_NUM_CHILD == node->num_child_))) {
    if (OB_ISNULL(revoke_stmt = create_stmt<ObRevokeStmt>())) {
      ret = OB_ALLOCATE_MEMORY_FAILED;
      LOG_ERROR("Failed to create ObCreateUserStmt", K(ret));
    } else {
      revoke_stmt->set_stmt_type(stmt::T_REVOKE);
      stmt_ = revoke_stmt;
      uint64_t tenant_id = params_.session_info_->get_effective_tenant_id();
      revoke_stmt->set_tenant_id(tenant_id);
      ParseNode *users_node =  NULL;
      ParseNode *privs_node = NULL;
      ObPrivLevel grant_level = OB_PRIV_INVALID_LEVEL;
      if (T_SYSTEM_REVOKE == node->type_ && REVOKE_ROLE_NUM_CHILD == node->num_child_) {
        // resolve oracle revoke
        // 0: role_list; 1: grantee
        ParseNode *revoke_role = node->children_[0];
        if (NULL == revoke_role) {
          ret = OB_INVALID_ARGUMENT;
          LOG_ERROR("revoke ParseNode error", K(ret));
        } else if (T_REVOKE_ROLE == revoke_role->type_) {
          revoke_stmt->set_stmt_type(stmt::T_REVOKE_ROLE);
          OZ (resolve_revoke_role_inner(revoke_role, revoke_stmt));
        } else {
          ret = OB_INVALID_ARGUMENT;
          LOG_ERROR("Revoke ParseNode error", K(ret));
        }
      } else {
        bool ignore_priv_not_exist = false;
        bool ignore_user_not_exist = false;
        bool ignore_error = false;
        // resolve mysql revoke
        if (T_REVOKE == node->type_ && REVOKE_NUM_CHILD == node->num_child_) {
          privs_node = node->children_[0];
          ParseNode *priv_object_node = node->children_[1];
          ParseNode *priv_level_node = node->children_[2];
          users_node = node->children_[3];
          ignore_priv_not_exist = NULL != node->children_[4];
          ignore_user_not_exist = NULL != node->children_[5];
          //resolve priv_level
          if (OB_ISNULL(priv_level_node) || OB_ISNULL(allocator_)) {
            ret = OB_ERR_PARSE_SQL;
            LOG_WARN("Priv level node should not be NULL", K(ret));
          } else {
            ObString db = ObString::make_string("");
            ObString table = ObString::make_string("");
            ObString catalog = ObString::make_string("");
            if (priv_object_node != NULL
                && OB_FAIL(ObGrantResolver::resolve_priv_level_with_object_type(session_info_,
                                                                                priv_object_node,
                                                                                grant_level))) {
              LOG_WARN("failed to resolve priv level with object", K(ret));
            } else if (OB_FAIL(ObGrantResolver::resolve_priv_level(
                        params_.schema_checker_->get_schema_guard(),
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
              revoke_stmt->set_grant_level(grant_level);
            }

            if (OB_SUCC(ret) && grant_level != OB_PRIV_CATALOG_LEVEL) {
              if (OB_FAIL(check_and_convert_name(db, table))) {
                LOG_WARN("Check and convert name error", K(db), K(table), K(ret));
              } else if (OB_FAIL(revoke_stmt->set_database_name(db))) {
                LOG_WARN("Failed to set database_name to revoke_stmt", K(ret));
              } else if (OB_FAIL(revoke_stmt->set_table_name(table))) {
                LOG_WARN("Failed to set table_name to revoke_stmt", K(ret));
              }
            }

            if (OB_SUCC(ret) && OB_FAIL(ObGrantResolver::resolve_priv_object(priv_object_node,
                                                                             revoke_stmt,
                                                                             params_.schema_checker_,
                                                                             db,
                                                                             table,
                                                                             catalog,
                                                                             tenant_id,
                                                                             allocator_,
                                                                             false))) {
              LOG_WARN("failed to resolve priv object", K(ret));
            }
          }

        } else if (T_REVOKE_ALL == node->type_ && REVOKE_ALL_NUM_CHILD == node->num_child_) {
          ignore_priv_not_exist = NULL != node->children_[1];
          ignore_user_not_exist = NULL != node->children_[2];
          users_node = node->children_[0];
          revoke_stmt->set_revoke_all(true);
          revoke_stmt->set_grant_level(OB_PRIV_USER_LEVEL);
          if (OB_SUCC(ret)) {
            ObSessionPrivInfo session_priv;
            const common::ObIArray<uint64_t> &enable_role_id_array = params_.session_info_->get_enable_role_array();
            ObArenaAllocator alloc;
            ObStmtNeedPrivs stmt_need_privs(alloc);
            ObNeedPriv need_priv("mysql", "", OB_PRIV_DB_LEVEL, OB_PRIV_UPDATE, false);
            OZ (stmt_need_privs.need_privs_.init(1));
            OZ (stmt_need_privs.need_privs_.push_back(need_priv));
            //check CREATE USER or UPDATE privilege on mysql
            params_.session_info_->get_session_priv_info(session_priv);
            if (OB_SUCC(ret) && OB_FAIL(schema_checker_->check_priv(session_priv, enable_role_id_array, stmt_need_privs))) {
              stmt_need_privs.need_privs_.at(0) =
                  ObNeedPriv("", "", OB_PRIV_USER_LEVEL, OB_PRIV_CREATE_USER, false);
              if (OB_FAIL(schema_checker_->check_priv(session_priv, enable_role_id_array, stmt_need_privs))) {
                LOG_WARN("no priv", K(ret));
              }
            }
          }
        }
        //resolve privileges
        if (OB_SUCC(ret) && (NULL != privs_node)) {
          ObPrivSet priv_set = 0;
          uint64_t compat_version = 0;
          const uint64_t tenant_id = params_.session_info_->get_effective_tenant_id();  
          if (OB_ISNULL(allocator_)) {
            ret = OB_ERR_UNEXPECTED;
            LOG_WARN("unexpected error", K(ret));
          } else if (OB_FAIL(ObGrantResolver::resolve_priv_set(tenant_id, privs_node, grant_level, priv_set, revoke_stmt, 
                                                        params_.schema_checker_, params_.session_info_,
                                                        *allocator_))) {
            LOG_WARN("Resolve priv set error", K(ret));
          } else if (OB_FAIL(GET_MIN_DATA_VERSION(tenant_id, compat_version))) {
            LOG_WARN("fail to get data version", K(tenant_id));
          } else if (!sql::ObSQLUtils::is_data_version_ge_422_or_431(compat_version)
                     && ((priv_set & OB_PRIV_EXECUTE) != 0 ||
                         (priv_set & OB_PRIV_ALTER_ROUTINE) != 0 ||
                         (priv_set & OB_PRIV_CREATE_ROUTINE) != 0)) {
            ret = OB_NOT_SUPPORTED;
            LOG_WARN("grammar is not support when MIN_DATA_VERSION is below DATA_VERSION_4_3_1_0 or 4_2_2_0", K(ret));
            LOG_USER_ERROR(OB_NOT_SUPPORTED, "revoke execute/alter routine/create routine privilege");
          } else if (!sql::ObSQLUtils::is_data_version_ge_423_or_432(compat_version)
                     && ((priv_set & OB_PRIV_CREATE_TABLESPACE) != 0 ||
                         (priv_set & OB_PRIV_SHUTDOWN) != 0 ||
                         (priv_set & OB_PRIV_RELOAD) != 0)) {
            ret = OB_NOT_SUPPORTED;
            LOG_WARN("grammar is not support when MIN_DATA_VERSION is below DATA_VERSION_4_2_3_0 or 4_3_2_0", K(ret));
            LOG_USER_ERROR(OB_NOT_SUPPORTED, "revoke create tablespace/shutdown/reload privilege");
          } else if (!sql::ObSQLUtils::is_data_version_ge_424_or_433(compat_version)
                     && ((priv_set & OB_PRIV_REFERENCES) != 0 ||
                         (priv_set & OB_PRIV_CREATE_ROLE) != 0 ||
                         (priv_set & OB_PRIV_DROP_ROLE) != 0 ||
                         (priv_set & OB_PRIV_TRIGGER) != 0)) {
            ret = OB_NOT_SUPPORTED;
            LOG_WARN("grammar is not support when MIN_DATA_VERSION is below DATA_VERSION_4_2_4_0 or 4_3_3_0", K(ret));
            LOG_USER_ERROR(OB_NOT_SUPPORTED, "revoke references/create role/drop role/trigger");
          } else if (!((MOCK_DATA_VERSION_4_2_5_1 <= compat_version && compat_version < DATA_VERSION_4_3_0_0) || compat_version >= DATA_VERSION_4_3_5_1)
                     && ((priv_set & OB_PRIV_ENCRYPT) != 0 || (priv_set & OB_PRIV_DECRYPT) != 0)) {
            ret = OB_NOT_SUPPORTED;
            LOG_WARN("grammar is not support when MIN_DATA_VERSION is below DATA_VERSION_4_2_5_1 or 4_3_5_1", K(ret));
            LOG_USER_ERROR(OB_NOT_SUPPORTED, "grant encrypt/decrypt privilege");
          } else if (!((MOCK_DATA_VERSION_4_2_5_2 <= compat_version && compat_version < DATA_VERSION_4_3_0_0) || compat_version >= DATA_VERSION_4_3_5_2)
                     && ((priv_set & OB_PRIV_EVENT) != 0)) {
            ret = OB_NOT_SUPPORTED;
            LOG_WARN("grammar is not support when MIN_DATA_VERSION is below DATA_VERSION_4_2_5_2 or 4_3_5_2", K(ret));
            LOG_USER_ERROR(OB_NOT_SUPPORTED, "revoke event");
          } else if (compat_version < DATA_VERSION_4_3_5_2
                     && ((priv_set & OB_PRIV_CREATE_CATALOG) != 0 ||
                         (priv_set & OB_PRIV_USE_CATALOG) != 0)) {
            ret = OB_NOT_SUPPORTED;
            LOG_WARN("grammar is not support when MIN_DATA_VERSION is below DATA_VERSION_4_3_5_2", K(ret));
            LOG_USER_ERROR(OB_NOT_SUPPORTED, "grant create catalog/use catalog privilege");
          }
          if (OB_FAIL(ret)) {
          } else {
            revoke_stmt->set_priv_set(priv_set);
          }
        }

        //resolve users_node
        if (OB_SUCC(ret)) {
          if (OB_ISNULL(users_node)) {
            ret = OB_ERR_PARSE_SQL;
            LOG_WARN("Users node should not be NULL", K(ret));
          } else {
            for (int i = 0; OB_SUCC(ret) && i < users_node->num_child_; ++i) {
              ParseNode *user_hostname_node = users_node->children_[i];
              if (OB_ISNULL(user_hostname_node)) {
                ret = OB_ERR_PARSE_SQL;
                LOG_WARN("the child of users should not be NULL", K(ret), K(i));
              } else if (2 != user_hostname_node->num_child_) {
                ret = OB_INVALID_ARGUMENT;
                LOG_WARN("sql_parser parse user error", K(ret));
              } else if (OB_ISNULL(user_hostname_node->children_[0])) {
                // 0: user, 1: hostname
                ret = OB_ERR_PARSE_SQL;
                LOG_WARN("The child of user node should not be NULL", K(ret), K(i));
              } else {
                uint64_t user_id = OB_INVALID_ID;
                //0: user name; 1: host name
                ObString user_name;
                ObString host_name;
                if (user_hostname_node->children_[0]->type_ == T_FUN_SYS_CURRENT_USER) {
                  user_name = params_.session_info_->get_user_name();
                } else {  
                  user_name = ObString(static_cast<int32_t>(user_hostname_node->children_[0]->str_len_),
                                   user_hostname_node->children_[0]->str_value_);
                }
                if (NULL == user_hostname_node->children_[1]) {
                  if (user_hostname_node->children_[0]->type_ == T_FUN_SYS_CURRENT_USER) {
                    host_name = params_.session_info_->get_host_name();
                  } else {
                    host_name.assign_ptr(OB_DEFAULT_HOST_NAME, 
                                       static_cast<int32_t>(STRLEN(OB_DEFAULT_HOST_NAME)));
                  }
                } else {
                  host_name.assign_ptr(user_hostname_node->children_[1]->str_value_,
                            static_cast<int32_t>(user_hostname_node->children_[1]->str_len_));
                }
                if (user_name.length() > OB_MAX_USER_NAME_LENGTH) {
                  ret = OB_WRONG_USER_NAME_LENGTH;
                  LOG_USER_ERROR(OB_WRONG_USER_NAME_LENGTH, user_name.length(), user_name.ptr());
                } else if (OB_FAIL(revoke_stmt->add_grantee(user_name))) {
                  SQL_RESV_LOG(WARN, "fail to add grantee", K(ret), K(user_name), K(host_name));
                } else if (OB_FAIL(
                    params_.schema_checker_->get_user_id(tenant_id, user_name, 
                                                         host_name, user_id))) {
                  if (OB_USER_NOT_EXIST == ret) {
                     ignore_error = ignore_user_not_exist;
                     LOG_USER(ignore_user_not_exist ? ObLogger::USER_WARN : ObLogger::USER_ERROR,
                              OB_ERR_UNKNOWN_AUTHID,
                              user_name.length(), user_name.ptr(),
                              host_name.length(), host_name.ptr());
                  }
                  SQL_RESV_LOG(WARN, "fail to get user id", K(ret), K(user_name), K(host_name));
                } else if (is_root_user(user_id) && OB_PRIV_USER_LEVEL == grant_level) {
                  ret = OB_NOT_SUPPORTED;
                  LOG_WARN("Revoke privilege from root at global level is not supported", K(ret));
                  LOG_USER_ERROR(OB_NOT_SUPPORTED, "Revoke privilege from root at global level");
                } else if (OB_FAIL(check_dcl_on_inner_user(node->type_,
                                                          params_.session_info_->get_priv_user_id(),
                                                          user_id))) {
                  LOG_WARN("failed to check dcl on inner-user or unsupport to modify reserved user",
                           K(ret), K(session_info_->get_priv_user_id()), K(user_name));
                } else if (OB_FAIL(revoke_stmt->add_user(user_id))) {
                  LOG_WARN("Add user to grant_stmt error", K(ret), K(user_id));
                } else {
                  //do nothing
                }
              }
            } //end for
          }
        }

        if (OB_FAIL(ret) && OB_NOT_NULL(revoke_stmt) && ignore_error) {
          revoke_stmt->set_has_warning();
          ret = OB_SUCCESS;
        }
      }
    }
  } else {
    ret = OB_INVALID_ARGUMENT;
    LOG_ERROR("Revoke ParseNode error", K(ret));
  }
  return ret;
}
