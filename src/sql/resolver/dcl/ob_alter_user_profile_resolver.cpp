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

#include "sql/resolver/dcl/ob_alter_user_profile_resolver.h"

#include "lib/encrypt/ob_encrypted_helper.h"
#include "sql/optimizer/ob_optimizer_util.h"
using namespace oceanbase::sql;
using namespace oceanbase::common;
using oceanbase::share::schema::ObUserInfo;

ObAlterUserProfileResolver::ObAlterUserProfileResolver(ObResolverParams &params)
    : ObDCLResolver(params)
{
}

ObAlterUserProfileResolver::~ObAlterUserProfileResolver()
{
}

int ObAlterUserProfileResolver::resolve_set_role(const ParseNode &parse_tree)
{
  int ret = OB_SUCCESS;
  ObAlterUserProfileStmt *stmt = NULL;

  if (OB_ISNULL(params_.session_info_)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("not init", K(ret));
  } else if (OB_ISNULL(params_.schema_checker_)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("not init", K(ret));
  } else if (T_SET_ROLE != parse_tree.type_
             || 1 != parse_tree.num_child_) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("wrong root", K(ret), K(parse_tree.type_), K(parse_tree.num_child_));
  } else if (OB_ISNULL(stmt = create_stmt<ObAlterUserProfileStmt>())) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
    LOG_WARN("Failed to create ObAlterUserProfileStmt", K(ret));
  } else {
    ObString user_name;
    ObString host_name(OB_DEFAULT_HOST_NAME);
    uint64_t session_user_id = lib::is_mysql_mode() ? params_.session_info_->get_priv_user_id()
                                                    : params_.session_info_->get_user_id();
    const ObUserInfo *user_info = NULL;
    if (OB_FAIL(params_.schema_checker_->get_user_info(
         params_.session_info_->get_effective_tenant_id(),
         session_user_id, user_info))) {
      LOG_WARN("get user info failed", K(ret));
    } else if (NULL == user_info) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("current user info is null", K(ret));
    } else {
      
      obrpc::ObAlterUserProfileArg &arg = stmt->get_ddl_arg();
      arg.tenant_id_ = params_.session_info_->get_effective_tenant_id();
      stmt->set_set_role_flag(ObAlterUserProfileStmt::SET_ROLE);

      /* 1. resolve default role */
      OZ (resolve_default_role_clause(parse_tree.children_[0], arg, 
                                      user_info->get_role_id_array(), false));

    }
  }
  return ret;
}

int ObAlterUserProfileResolver::resolve_role_list(
  const ParseNode *role_list,
  obrpc::ObAlterUserProfileArg &arg,
  const ObIArray<uint64_t> &role_id_array,
  bool for_default_role_stmt)
{
  int ret = OB_SUCCESS;
  CK (OB_NOT_NULL(role_list));
  CK (OB_NOT_NULL(params_.session_info_));
  CK (OB_NOT_NULL(params_.schema_checker_));
  hash::ObHashMap<uint64_t, ObString> roleid_pwd_map;
  OZ (roleid_pwd_map.create(32, "HashRRLPwdMa"));
  if (OB_SUCC(ret)) {
    for (int i = 0; OB_SUCC(ret) && i < role_list->num_child_; ++i) {
      uint64_t role_id = OB_INVALID_ID;
      ParseNode *role = role_list->children_[i];
      ParseNode *pwd_node = NULL;
      if (NULL == role) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("role opt identified by node is null", K(ret));
      } else if (lib::is_mysql_mode()) {
      } else if (T_IDENT == role->type_) {
      } else if (T_SET_ROLE_PASSWORD != role->type_) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("role type is error", K(ret), K(role->type_));
      } else if (2 == role->value_ && NULL == role->children_[1]) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("passwd_node is NULL", K(ret));
      } else if (NULL == role->children_[0]) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("role node is null", K(ret));
      } else {
        if (2 == role->value_) {
          pwd_node = role->children_[1];
        }
        role = role->children_[0];
      }
      if (OB_SUCC(ret)) {
        ObString role_name;
        ObString host_name(OB_DEFAULT_HOST_NAME);
        const ObUserInfo *role_info = NULL;
        OZ (resolve_user_host(role, role_name, host_name));
        if (OB_FAIL(ret)) {
        } else if (OB_FAIL(params_.schema_checker_->get_user_info(arg.tenant_id_, role_name,
                                                                  host_name, role_info))) {
          if (OB_USER_NOT_EXIST == ret) {
            if (obrpc::OB_DEFAULT_ROLE_ALL_EXCEPT == arg.default_role_flag_) {
              ret = OB_SUCCESS; //ignore EXCEPTED ROLE not exist
            } else {
              ret = OB_ERR_UNKNOWN_AUTHID;
              LOG_USER_ERROR(OB_ERR_UNKNOWN_AUTHID, role_name.length(), role_name.ptr(),
                                                    host_name.length(), host_name.ptr());
            }
          }
          LOG_WARN("fail to get user id", K(ret), K(role_name), K(host_name));
        } else if (role_info == NULL) {
          if (for_default_role_stmt) {
            ret = OB_ROLE_NOT_EXIST;
            LOG_USER_ERROR(OB_ROLE_NOT_EXIST, 
                            role_name.length(), role_name.ptr());
            LOG_WARN("role not exists", K(ret), K(role_name));
          } else {
            ret = OB_ERR_ROLE_NOT_GRANTED_OR_DOES_NOT_EXIST;
            LOG_USER_ERROR(OB_ERR_ROLE_NOT_GRANTED_OR_DOES_NOT_EXIST, 
                            role_name.length(), role_name.ptr());
            LOG_WARN("role not granted or does not exists", K(ret), K(role_name));
          }
        } else {
          bool skip = false;
          role_id = role_info->get_user_id();
          if (has_exist_in_array(arg.role_id_array_, role_id)) {
            /* if role duplicate in role list, then raise error */
            if (lib::is_mysql_mode()) {
              skip = true;
            } else {
              ret = OB_PRIV_DUP;
            }
          } else {
            ObString cur_user_name;
            ObString cur_host_name;
            if (!for_default_role_stmt) {
              if (!has_exist_in_array(role_id_array, role_id)) {
                ret = OB_ERR_ROLE_NOT_GRANTED_TO;
                cur_user_name = params_.session_info_->get_user_name();
                cur_host_name = params_.session_info_->get_host_name();
              }
            } else {
              for (int j = 0; OB_SUCC(ret) && j < arg.user_ids_.count(); j++) {
                const ObUserInfo *cur_user_info = NULL;
                OZ (params_.schema_checker_->get_user_info(arg.tenant_id_, arg.user_ids_.at(j), cur_user_info));
                CK (OB_NOT_NULL(cur_user_info));
                if (OB_SUCC(ret) && !has_exist_in_array(cur_user_info->get_role_id_array(), role_id)) {
                  ret = OB_ERR_ROLE_NOT_GRANTED_TO;
                  cur_user_name = cur_user_info->get_user_name_str();
                  cur_host_name = cur_user_info->get_host_name_str();
                }
              }
            }
            if (OB_ERR_ROLE_NOT_GRANTED_TO == ret) {
              if (obrpc::OB_DEFAULT_ROLE_ALL_EXCEPT == arg.default_role_flag_) {
                skip = true;
                ret = OB_SUCCESS; //ignore EXCEPTED ROLE not granted to user
              } else {
                LOG_USER_ERROR(OB_ERR_ROLE_NOT_GRANTED_TO,
                              role_name.length(), role_name.ptr(),
                              host_name.length(), host_name.ptr(),
                              cur_user_name.length(), cur_user_name.ptr(),
                              cur_host_name.length(), cur_host_name.ptr());
              }
            }
          }
          if (OB_SUCC(ret) && !skip) {
            OZ (arg.role_id_array_.push_back(role_id));
            ObString pwd = ObString::make_string("");
            if (NULL != pwd_node) {
              pwd.assign_ptr(pwd_node->str_value_, static_cast<int32_t>(pwd_node->str_len_));
            }
            OZ (roleid_pwd_map.set_refactored(role_id, pwd));
          }
        }
      }
    }
  }
  return ret;
}

int ObAlterUserProfileResolver::resolve_default_role_clause(
    const ParseNode *parse_tree,
    obrpc::ObAlterUserProfileArg &arg,
    const ObIArray<uint64_t> &role_id_array,
    bool for_default_role_stmt)
{
  int ret = OB_SUCCESS;
  CK (OB_NOT_NULL(parse_tree));
  if (T_DEFAULT_ROLE != parse_tree->type_ || (1 != parse_tree->num_child_
                                           && 2 != parse_tree->num_child_)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("wrong root", K(ret), K(parse_tree->type_), K(parse_tree->num_child_));
  } else {
    if (1 == parse_tree->num_child_) {
      CK (OB_NOT_NULL(parse_tree->children_[0]));
      if (OB_SUCC(ret)) {
        switch (parse_tree->children_[0]->value_) {
          case 1: {
            arg.default_role_flag_ = obrpc::OB_DEFAULT_ROLE_ALL;
            break;
          }
          case 3: {
            arg.default_role_flag_ = obrpc::OB_DEFAULT_ROLE_NONE;
            break;
          }
          case 4: {
            arg.default_role_flag_ = obrpc::OB_DEFAULT_ROLE_DEFAULT;
            break;
          }
          default: {
            ret = OB_ERR_UNDEFINED;
            LOG_WARN("invalid type", K(ret), K(parse_tree->children_[0]->value_));
          }
        }
      }
    } else {
      CK (2 == parse_tree->num_child_);
      if (OB_SUCC(ret)) {
        if (0 == parse_tree->children_[0]->value_) {
          OX (arg.default_role_flag_ = obrpc::OB_DEFAULT_ROLE_LIST);
        } else {
          CK (2 == parse_tree->children_[0]->value_);
          OX (arg.default_role_flag_ = obrpc::OB_DEFAULT_ROLE_ALL_EXCEPT);
        }
        OZ (resolve_role_list(parse_tree->children_[1], arg, role_id_array, for_default_role_stmt));
      }
    }
  }
  
  return ret;
}

int ObAlterUserProfileResolver::resolve_default_role(const ParseNode &parse_tree)
{
  int ret = OB_SUCCESS;
  ObAlterUserProfileStmt *stmt = NULL;
  uint64_t tenant_id = OB_INVALID_ID;

  if (OB_ISNULL(params_.session_info_)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("not init", K(ret));
  } else if (OB_ISNULL(params_.schema_checker_)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("not init", K(ret));
  } else if (T_ALTER_USER_DEFAULT_ROLE != parse_tree.type_
             || 2 != parse_tree.num_child_) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("wrong root", K(ret), K(parse_tree.type_), K(parse_tree.num_child_));
  } else if (OB_ISNULL(stmt = create_stmt<ObAlterUserProfileStmt>())) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
    LOG_WARN("Failed to create ObAlterUserProfileStmt", K(ret));
  } else {
    ObString user_name;
    ObString host_name;
    const ObUserInfo *user_info = NULL;
    obrpc::ObAlterUserProfileArg &arg = stmt->get_ddl_arg();
    stmt->set_set_role_flag(ObAlterUserProfileStmt::SET_DEFAULT_ROLE);

    /* 1. resolve user */
    tenant_id = params_.session_info_->get_effective_tenant_id();
    arg.tenant_id_ = tenant_id;
    if (T_USER_WITH_HOST_NAME == parse_tree.children_[0]->type_) {
      ParseNode *user_with_host_name = parse_tree.children_[0];
      // Get user_name and host_name
      if (OB_ISNULL(user_with_host_name)) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("user_with_host_name is NULL");
      } else {
        ParseNode *user_name_node = user_with_host_name->children_[0];
        ParseNode *host_name_node = user_with_host_name->children_[1];
        if (OB_ISNULL(user_name_node)) {
          ret = OB_ERR_UNEXPECTED;
          LOG_WARN("user_name is NULL", K(ret), K(user_name));
        } else {
          user_name = ObString(user_name_node->str_len_, user_name_node->str_value_);
        }
        if (NULL != host_name_node) {
          host_name = ObString(host_name_node->str_len_, host_name_node->str_value_);
        } else {
          host_name = ObString(OB_DEFAULT_HOST_NAME);
        }
      }
      OZ (params_.schema_checker_->get_user_info(tenant_id, user_name, host_name, user_info),
            tenant_id, user_name, host_name);
      if (ret == OB_USER_NOT_EXIST) {
        ret = OB_ERR_UNKNOWN_AUTHID;
        LOG_USER_ERROR(OB_ERR_UNKNOWN_AUTHID, user_name.length(), user_name.ptr(), host_name.length(), host_name.ptr());
      }
      if (OB_SUCC(ret)) {
        if (user_info == NULL) {
          ret = OB_USER_NOT_EXIST;
          LOG_USER_ERROR(OB_USER_NOT_EXIST, user_name.length(), user_name.ptr());
        } else if (lib::is_mysql_mode()) {
          OZ (arg.user_ids_.push_back(user_info->get_user_id()));
        } else if (OB_FAIL(check_dcl_on_inner_user(parse_tree.type_,
                                                    session_info_->get_priv_user_id(),
                                                    user_info->get_user_id()))) {
          LOG_WARN("failed to check dcl on inner-user or unsupport to modify reserved user", K(ret),
                  K(session_info_->get_user_name()), K(user_name));
        } else {
          arg.user_id_ = user_info->get_user_id();
        }
      }
      
    } else {
      ParseNode *user_list_node = parse_tree.children_[0];
      for (int i = 0; OB_SUCC(ret) && i < user_list_node->num_child_; i++) {
        OZ (ObDCLResolver::resolve_user_list_node(user_list_node->children_[i], user_list_node, user_name, host_name));
        OZ (params_.schema_checker_->get_user_info(tenant_id, user_name, host_name, user_info),
              tenant_id, user_name, host_name);
        if (OB_USER_NOT_EXIST == ret || OB_ISNULL(user_info)) {
          ret = OB_ERR_UNKNOWN_AUTHID;
          LOG_USER_ERROR(OB_ERR_UNKNOWN_AUTHID, user_name.length(), user_name.ptr(), host_name.length(), host_name.ptr());
        }
        OZ (arg.user_ids_.push_back(user_info->get_user_id()));
      }
    }

    /* 2. resolve default role */
    OZ (resolve_default_role_clause(parse_tree.children_[1], arg, 
                                    user_info->get_role_id_array(), true));

    if (OB_SUCC(ret) && lib::is_mysql_mode()) {
      ObSqlCtx *sql_ctx = NULL;
      if (OB_ISNULL(params_.session_info_->get_cur_exec_ctx())
          || OB_ISNULL(sql_ctx = params_.session_info_->get_cur_exec_ctx()->get_sql_ctx())) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("unexpected ctx", K(ret), KP(params_.session_info_->get_cur_exec_ctx()));
      }
      for (int i = 0; OB_SUCC(ret) && i < arg.user_ids_.count(); i++) {
        if (arg.user_ids_.at(i) != params_.session_info_->get_priv_user_id()) {
          OZ (schema_checker_->check_set_default_role_priv(*sql_ctx));
        }
      }
    }

  }
  return ret;
}

int ObAlterUserProfileResolver::resolve(const ParseNode &parse_tree)
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(params_.session_info_)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("not init", K(ret));
  } else if (T_SET_ROLE == parse_tree.type_) {
    OZ (resolve_set_role(parse_tree));
  } else if (T_ALTER_USER_DEFAULT_ROLE == parse_tree.type_) {
    OZ (resolve_default_role(parse_tree));
  } else {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("wrong root", K(ret), K(parse_tree.type_), K(parse_tree.num_child_));
  }
  return ret;
}
