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

#include "sql/resolver/cmd/ob_alter_system_resolver.h"
#include "sql/resolver/cmd/ob_alter_system_stmt.h"
#include "sql/resolver/ddl/ob_create_table_resolver.h"
#include "sql/resolver/ddl/ob_drop_table_stmt.h"
#include "sql/resolver/cmd/ob_variable_set_stmt.h"
#include "observer/ob_server.h"
#include "observer/mysql/ob_query_response_time.h"

namespace oceanbase
{
using namespace common;
using namespace obcall;
using namespace share;
using namespace share::schema;
using namespace observer;
namespace sql
{
typedef ObAlterSystemResolverUtil Util;

int ObAlterSystemResolverUtil::sanity_check(const ParseNode *parse_tree, ObItemType item_type)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(NULL == parse_tree)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("parse tree should not be null");
  } else if (OB_UNLIKELY(item_type != parse_tree->type_)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("invalid type",
             "expect", get_type_name(item_type),
             "actual", get_type_name(parse_tree->type_));
  } else if (OB_UNLIKELY(parse_tree->num_child_ <= 0)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("invalid num_child", "num_child", parse_tree->num_child_);
  } else if (OB_UNLIKELY(NULL == parse_tree->children_)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("children should not be null");
  }

  return ret;
}

template <typename RPC_ARG>
int ObAlterSystemResolverUtil::resolve_server_or_zone(const ParseNode *parse_tree, RPC_ARG &arg)
{
  int ret = OB_SUCCESS;
  arg.server_.reset();
  arg.zone_.reset();
  if (NULL != parse_tree) {
    switch (parse_tree->type_) {
      case T_IP_PORT: {
        if (OB_FAIL(resolve_server(parse_tree, arg.server_))) {
          LOG_WARN("resolve server address failed", K(ret));
        }
        break;
      }
      case T_ZONE: {
        if (OB_FAIL(resolve_zone(parse_tree, arg.zone_))) {
          LOG_WARN("resolve zone failed", K(ret));
        }
        break;
      }
      default: {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("only server or zone type allowed here", "type", get_type_name(parse_tree->type_));
        break;
      }
    }
  }
  return ret;
}

int ObAlterSystemResolverUtil::resolve_server_value(const ParseNode *parse_tree, ObAddr &server)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(NULL == parse_tree)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("parse tree should not be null");
  } else if (OB_UNLIKELY(T_VARCHAR != parse_tree->type_)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("type is not T_VARCHAR", "type", get_type_name(parse_tree->type_));
  } else {
    char ip_port[128] = {0};
    snprintf(ip_port, 128, "%.*s", static_cast<int32_t>(parse_tree->str_len_), parse_tree->str_value_);
    if (OB_FAIL(server.parse_from_cstring(ip_port))) {
      LOG_WARN("string not in server address format", K(ip_port), K(ret));
    }
  }
  return ret;
}

int ObAlterSystemResolverUtil::resolve_replica_type(const ParseNode *parse_tree,
                                                    ObReplicaType &replica_type)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(NULL == parse_tree)) {
    replica_type = REPLICA_TYPE_FULL; // For compatibility with early commands, default to FULL type when replica_type is not specified
    LOG_INFO("resolve_replica_type without any value. default to FULL.");
  } else if (OB_UNLIKELY(T_VARCHAR != parse_tree->type_)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("type is not T_VARCHAR", "type", get_type_name(parse_tree->type_));
  } else {
    int64_t len = parse_tree->str_len_;
    const char *str = parse_tree->str_value_;
    if (OB_ISNULL(str)) {
      ret = OB_INVALID_ARGUMENT;
      LOG_WARN("invalid replica type string. null!", K(ret));
      LOG_USER_ERROR(OB_INVALID_ARGUMENT, "replica_type, replica_type should not be null");
    } else {
      replica_type = share::ObShareUtil::string_to_replica_type(str);
      if (REPLICA_TYPE_INVALID == replica_type) {
        ret = OB_INVALID_ARGUMENT;
        LOG_WARN("invalid replica type string", K(str), K(ret));
        LOG_USER_ERROR(OB_INVALID_ARGUMENT, "replica_type, unrecognized replica_type");
      } else if (! ObReplicaTypeCheck::is_replica_type_valid(replica_type)) {
        ret = OB_NOT_SUPPORTED;
        char err_msg[64] = {0};
        (void)snprintf(err_msg, sizeof(err_msg), "%s replica", ObShareUtil::replica_type_to_string(replica_type));
        LOG_USER_ERROR(OB_NOT_SUPPORTED, err_msg);
      } else {
        // good, valid replica_type
      }
    }
  }
  return ret;
}

int ObAlterSystemResolverUtil::check_compatibility_for_replica_type(const ObReplicaType replica_type)
{
  int ret = OB_SUCCESS;
  if (ObReplicaTypeCheck::is_columnstore_replica(replica_type)) {
  }
  return ret;
}


int ObAlterSystemResolverUtil::resolve_server(const ParseNode *parse_tree, ObAddr &server)
{
  int ret = OB_SUCCESS;
  if (NULL == parse_tree) {
    server.reset();
  } else if (OB_FAIL(sanity_check(parse_tree, T_IP_PORT))) {
    LOG_WARN("sanity check failed");
  } else {
    const ParseNode *node = parse_tree->children_[0];
    if (OB_UNLIKELY(NULL == node)) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("node should not be null");
    } else if (OB_FAIL(resolve_server_value(node, server))) {
      LOG_WARN("resolve server value failed", K(ret));
    }
  }
  return ret;
}

int ObAlterSystemResolverUtil::resolve_zone(const ParseNode *parse_tree, ObZone &zone)
{
  int ret = OB_SUCCESS;
  if (NULL == parse_tree) {
    zone.reset();
  } else if (OB_FAIL(sanity_check(parse_tree, T_ZONE))) {
    LOG_WARN("sanity check failed");
  } else {
    const ParseNode *node = parse_tree->children_[0];
    if (OB_UNLIKELY(NULL == node)) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("node should not be null");
    } else if (node->value_ <= 0) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("empty zone string");
    } else {
      ObString zone_name(static_cast<int32_t>(node->str_len_), node->str_value_);
      if (OB_FAIL(zone.assign(zone_name))) {
        LOG_WARN("assign zone string failed", K(zone_name), K(ret));
      }
    }
  }
  return ret;
}

int ObAlterSystemResolverUtil::resolve_tenant(const ParseNode *parse_tree,
                                              ObFixedLengthString < OB_MAX_TENANT_NAME_LENGTH + 1 > &tenant_name)
{
  int ret = OB_SUCCESS;
  if (NULL == parse_tree) {
    tenant_name.reset();
  } else if (OB_FAIL(sanity_check(parse_tree, T_TENANT_NAME))) {
    LOG_WARN("sanity check failed");
  } else {
    const ParseNode *node = parse_tree->children_[0];
    if (OB_UNLIKELY(NULL == node)) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("node should not be null");
    } else if (node->value_ <= 0) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("empty tenant string");
    } else {
      ObString tenant(static_cast<int32_t>(node->str_len_), node->str_value_);
      if (OB_FAIL(tenant_name.assign(tenant))) {
        LOG_WARN("assign tenant string failed", K(tenant), K(ret));
      }
    }
  }
  return ret;
}

int ObAlterSystemResolverUtil::resolve_ls_id(const ParseNode *parse_tree, int64_t &ls_id)
{
  int ret = OB_SUCCESS;
  if (NULL == parse_tree) {
    ret = OB_ERR_NULL_VALUE;
    LOG_WARN("node should not be null");
  } else if (OB_FAIL(sanity_check(parse_tree, T_LS))) {
    LOG_WARN("sanity check failed");
  } else {
    ls_id = parse_tree->children_[0]->value_;
    FLOG_INFO("resolve ls id", K(ls_id));
  }
  return ret;
}

int ObAlterSystemResolverUtil::resolve_tablet_id(const ParseNode *opt_tablet_id, ObTabletID &tablet_id)
{
  int ret = OB_SUCCESS;

  if (NULL == opt_tablet_id) {
    ret = OB_ERR_NULL_VALUE;
    LOG_WARN("opt_tablet_id should not be null");
  } else if (OB_FAIL(sanity_check(opt_tablet_id, T_TABLET_ID))) {
    LOG_WARN("sanity check failed");
  } else {
    tablet_id = opt_tablet_id->children_[0]->value_;
    FLOG_INFO("resolve tablet_id", K(tablet_id));
  }
  return ret;
}


int ObAlterSystemResolverUtil::resolve_string(const ParseNode *node, ObString &string)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(NULL == node)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("node should not be null");
  } else if (OB_UNLIKELY(T_VARCHAR != node->type_ && T_CHAR != node->type_)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("node type is not T_VARCHAR/T_CHAR", "type", get_type_name(node->type_));
  } else if (OB_UNLIKELY(node->str_len_ <= 0)) {
    ret = OB_ERR_PARSER_SYNTAX;
    LOG_WARN("empty string");
  } else {
    string = ObString(node->str_len_, node->str_value_);
  }
  return ret;
}

int ObAlterSystemResolverUtil::resolve_relation_name(const ParseNode *node, ObString &string)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(NULL == node)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("node should not be null");
  } else if (OB_UNLIKELY(T_IDENT != node->type_)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("node type is not T_IDENT", "type", get_type_name(node->type_));
  } else if (OB_UNLIKELY(node->str_len_ <= 0)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("empty string");
  } else {
    string = ObString(node->str_len_, node->str_value_);
  }
  return ret;
}

// resolve tenants
int ObAlterSystemResolverUtil::resolve_tenant(
    const ParseNode &tenants_node,
    int64_t &out_count,
    bool &affect_all,
    bool &affect_all_user,
    bool &affect_all_meta)
{
  int ret = OB_SUCCESS;
  ObSchemaGetterGuard schema_guard;
  // Filter duplicate tenant names.
  bool sys_tenant_added = false;

  const int64_t ERROR_MSG_LENGTH = 1024;
  char error_msg[ERROR_MSG_LENGTH] = "";

  int tmp_ret = OB_SUCCESS;
  int64_t pos = 0;

  if (OB_ISNULL(GCTX.schema_service_)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("schema service is empty", KR(ret));
  } else if (OB_FAIL(GCTX.schema_service_->get_tenant_schema_guard(schema_guard))) {
    LOG_WARN("get_schema_guard failed", KR(ret));
  } else {
    ObString tenant_name;

    affect_all = false;
    affect_all_user = false;
    affect_all_meta = false;

    for (int64_t i = 0; OB_SUCC(ret) && (i < tenants_node.num_child_); ++i) {
      ParseNode *node = tenants_node.children_[i];
      if (OB_ISNULL(node)) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("children of server_list should not be null", KR(ret));
      } else {
        tenant_name.assign_ptr(node->str_value_,
                               static_cast<ObString::obstr_size_t>(node->str_len_));

        if (tenant_name.empty()) {
          ret = OB_INVALID_ARGUMENT;
          LOG_WARN("invalid argument", KR(ret));
        } else if (0 == strcasecmp(tenant_name.ptr(), "all")) {
          affect_all = true;
        } else if (0 == strcasecmp(tenant_name.ptr(), "all_user")) {
          affect_all_user = true;
        } else if (0 == strcasecmp(tenant_name.ptr(), "all_meta")) {
          affect_all_meta = true;
        } else {
          if (sys_tenant_added) {
            // duplicate tenant name
            ret = OB_NOT_SUPPORTED;
            LOG_WARN("duplicate tanant name", K(tenant_name), KR(ret));
            if (OB_SUCCESS != (tmp_ret = databuff_printf(error_msg, ERROR_MSG_LENGTH,
                pos, "duplicate tenant %s is", tenant_name.ptr()))) {
              LOG_WARN("failed to set error msg", K(ret), K(tmp_ret), K(error_msg), K(pos));
            } else {
              LOG_USER_ERROR(OB_NOT_SUPPORTED, error_msg);
            }
          } else {
            out_count++;
            sys_tenant_added = true;
          }
        }
      }
      tenant_name.reset();
    }

    if (OB_SUCC(ret) && (affect_all || affect_all_user || affect_all_meta)) {
      if (tenants_node.num_child_ > 1) {
        ret = OB_NOT_SUPPORTED;
        LOG_WARN("all/all_user/all_meta must be used separately",
                 KR(ret), "tenant list count", tenants_node.num_child_);
        LOG_USER_ERROR(OB_NOT_SUPPORTED,
                       "all/all_user/all_meta in combination with other names is");
      }
    }
    FLOG_INFO("resolve tenants", K(affect_all), K(affect_all_user),
              K(affect_all_meta));
  }
  return ret;
}


int ObFreezeResolver::resolve(const ParseNode &parse_tree)
{
  int ret = OB_SUCCESS;
  ObFreezeStmt *freeze_stmt = NULL;
  if (OB_UNLIKELY(NULL == parse_tree.children_)
      || OB_UNLIKELY(parse_tree.num_child_ < 2)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("wrong freeze parse tree", KP(parse_tree.children_),
             K(parse_tree.num_child_));
  } else if (OB_ISNULL(session_info_)) {
    ret = OB_ERR_UNEXPECTED;
      LOG_WARN("session info should not be null", K(ret));
  } else if (NULL == (freeze_stmt = create_stmt<ObFreezeStmt>())) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
    LOG_ERROR("create ObFreezeStmt failed");
  } else if (OB_ISNULL(parse_tree.children_[0])) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("wrong freeze type", KP(parse_tree.children_[0]));
  } else if (T_INT != parse_tree.children_[0]->type_) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("wrong freeze type", K(parse_tree.children_[0]->type_));
  } else {
    stmt_ = freeze_stmt;
    if (1 == parse_tree.children_[0]->value_) { // MAJOR FREEZE
      freeze_stmt->set_major_freeze(true);
      if (OB_UNLIKELY(3 != parse_tree.num_child_)) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("wrong freeze parse tree", K(parse_tree.num_child_));
      } else {
        ParseNode *opt_tenant_list_or_tablet_id = parse_tree.children_[1];
        const ParseNode *opt_rebuild_column_group = parse_tree.children_[2];
        if (OB_FAIL(resolve_major_freeze_(freeze_stmt, opt_tenant_list_or_tablet_id, opt_rebuild_column_group))) {
          LOG_WARN("resolve major freeze failed", KR(ret), KP(opt_tenant_list_or_tablet_id));
        }
      }
    } else if (2 == parse_tree.children_[0]->value_) {  // MINOR FREEZE
      freeze_stmt->set_major_freeze(false);
      if (OB_UNLIKELY(4 != parse_tree.num_child_)) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("wrong freeze parse tree", K(parse_tree.num_child_));
      } else {
        ParseNode *opt_tenant_list_or_ls_or_tablet_id = parse_tree.children_[1];
        if (OB_FAIL(resolve_minor_freeze_(freeze_stmt, opt_tenant_list_or_ls_or_tablet_id))) {
          LOG_WARN("resolve minor freeze failed", KR(ret), KP(opt_tenant_list_or_ls_or_tablet_id));
        }
      }
    } else {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("unknown freeze type", K(parse_tree.children_[0]->value_));
    }
  }

  return ret;
}

int ObFreezeResolver::resolve_major_freeze_(ObFreezeStmt *freeze_stmt, ParseNode *opt_tenant_list_or_tablet_id, const ParseNode *opt_rebuild_column_group)
{
  int ret = OB_SUCCESS;


  if (NULL == opt_tenant_list_or_tablet_id) {
    // if opt_tenant_list_or_tablet_id == NULL, add owned tenant
    freeze_stmt->inc_tenant_count();
  } else if (OB_UNLIKELY(nullptr == opt_tenant_list_or_tablet_id->children_ || 0 == opt_tenant_list_or_tablet_id->num_child_)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("children of tenant should not be null", KR(ret), KP(opt_tenant_list_or_tablet_id));
  } else if (OB_FAIL(resolve_tenant_ls_tablet_(freeze_stmt, opt_tenant_list_or_tablet_id))) {
    LOG_WARN("fail to resolve tenant or tablet", KR(ret));
  } else if (OB_UNLIKELY(share::ObLSID::INVALID_LS_ID != freeze_stmt->get_ls_id())) {
    ret = OB_NOT_SUPPORTED;
    LOG_WARN("not support to specify ls to major freeze", K(ret), "ls_id", freeze_stmt->get_ls_id());
  } else if (freeze_stmt->get_tablet_id().is_valid()) { // tablet major freeze
    if (GCTX.is_shared_storage_mode()) {
      ret = OB_NOT_SUPPORTED;
      LOG_WARN("not allowed to schedule tablet major for shared storage mode", KR(ret));
    } else if (T_TABLET_ID == opt_tenant_list_or_tablet_id->type_) {
      if (OB_UNLIKELY(0 != freeze_stmt->get_tenant_count())) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("tenant ids should be empty for type T_TABLET_ID", K(ret));
      } else {
        freeze_stmt->inc_tenant_count(); // if tenant is not explicitly specified, add owned tenant
      }
    } else if (false) {
      ret = OB_ERR_NO_PRIVILEGE;
      LOG_WARN("Only sys tenant can add suffix opt of tablet_id after tenant name", KR(ret));
    } else if (1 != freeze_stmt->get_tenant_count()) {
      ret = OB_NOT_SUPPORTED;
      LOG_WARN("not suppport to specify several tenant ids or no tenant id for tablet major freeze", K(ret),
        "tenant_count", freeze_stmt->get_tenant_count());
    }
  } else if (false && 0 != freeze_stmt->get_tenant_count()) { // tenant major freeze
    ret = OB_ERR_NO_PRIVILEGE;
    LOG_WARN("Only sys tenant can add suffix opt(tenant=name)", KR(ret));
  }

  if (OB_FAIL(ret)) {
  } else if (opt_rebuild_column_group != nullptr) {
    if (OB_UNLIKELY(!freeze_stmt->get_tablet_id().is_valid())) {
      ret = OB_NOT_SUPPORTED;
      LOG_WARN("rebuild only supports tablet major freeze", KR(ret));
    } else {
      freeze_stmt->set_rebuild_column_group(true);
    }
  }
  return ret;
}

int ObFreezeResolver::resolve_minor_freeze_(ObFreezeStmt *freeze_stmt,
                                            ParseNode *opt_tenant_list_or_ls_or_tablet_id)
{
  int ret = OB_SUCCESS;


  if (OB_NOT_NULL(opt_tenant_list_or_ls_or_tablet_id)) {
    if (OB_FAIL(resolve_tenant_ls_tablet_(freeze_stmt, opt_tenant_list_or_ls_or_tablet_id))) {
      LOG_WARN("resolve tenant ls table failed", KR(ret));
    } else if (T_TABLET_ID == opt_tenant_list_or_ls_or_tablet_id->type_) {
      freeze_stmt->reset_tenant_count();
      freeze_stmt->get_ls_id() = share::ObLSID::INVALID_LS_ID;
      freeze_stmt->inc_tenant_count();  // if tenant is not explicitly specified, add owned tenant
    }
  }


  return ret;
}

int ObFreezeResolver::resolve_tenant_ls_tablet_(ObFreezeStmt *freeze_stmt,
                                                ParseNode *opt_tenant_list_or_ls_or_tablet_id)
{
  int ret = OB_SUCCESS;


  if (OB_ISNULL(opt_tenant_list_or_ls_or_tablet_id->children_)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("children of tenant should not be null", KR(ret));
  } else {
    bool affect_all = false;
    bool affect_all_user = false;
    bool affect_all_meta = false;
    const ParseNode *tenant_list_tuple = nullptr;
    const ParseNode *opt_tablet_id = nullptr;
    const ParseNode *ls_id = nullptr;

    switch (opt_tenant_list_or_ls_or_tablet_id->type_) {
      case T_TENANT_TABLET:
        if (opt_tenant_list_or_ls_or_tablet_id->num_child_ != 2) {
          ret = OB_ERR_UNEXPECTED;
          LOG_WARN("invalid child num", K(ret), K(opt_tenant_list_or_ls_or_tablet_id->num_child_));
        } else {
          tenant_list_tuple = opt_tenant_list_or_ls_or_tablet_id->children_[0];
          opt_tablet_id = opt_tenant_list_or_ls_or_tablet_id->children_[1];
          if (OB_ISNULL(tenant_list_tuple)) {
            ret = OB_ERR_UNEXPECTED;
            LOG_WARN("tenant list is nullptr", KR(ret), KP(tenant_list_tuple), KP(ls_id), KP(opt_tablet_id));
          }
        }
        break;
      case T_TENANT_LS_TABLET:
        if (opt_tenant_list_or_ls_or_tablet_id->num_child_ != 3) {
          ret = OB_ERR_UNEXPECTED;
          LOG_WARN("invalid child num", K(ret), K(opt_tenant_list_or_ls_or_tablet_id->num_child_));
        } else {
          tenant_list_tuple = opt_tenant_list_or_ls_or_tablet_id->children_[0];
          ls_id = opt_tenant_list_or_ls_or_tablet_id->children_[1];
          opt_tablet_id = opt_tenant_list_or_ls_or_tablet_id->children_[2];
          if (OB_ISNULL(tenant_list_tuple) || OB_ISNULL(ls_id)) {
            ret = OB_ERR_UNEXPECTED;
            LOG_WARN("tenant_list or ls_id is nullptr", KR(ret), KP(tenant_list_tuple), KP(ls_id), KP(opt_tablet_id));
          }
        }
        break;
      case T_TABLET_ID:
        if (opt_tenant_list_or_ls_or_tablet_id->num_child_ != 1) {
          ret = OB_ERR_UNEXPECTED;
          LOG_WARN("invalid child num", K(ret), K(opt_tenant_list_or_ls_or_tablet_id->num_child_));
        } else {
          opt_tablet_id = opt_tenant_list_or_ls_or_tablet_id->children_[0];
          if (OB_ISNULL(opt_tablet_id)) {
            ret = OB_ERR_UNEXPECTED;
            LOG_WARN("tenant_list or ls_id is nullptr", KR(ret), KP(opt_tablet_id));
          }
        }
        break;
      default:
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("invalid parse node type",
                 K(T_TENANT_TABLET),
                 K(T_TENANT_LS_TABLET),
                 K(opt_tenant_list_or_ls_or_tablet_id->type_));
        break;
    }

    if (OB_FAIL(ret)) {
    } else if (OB_NOT_NULL(tenant_list_tuple) &&
               OB_FAIL(Util::resolve_tenant(*tenant_list_tuple,
                                            freeze_stmt->tenant_count_ref(),
                                            affect_all, affect_all_user, affect_all_meta))) {
      LOG_WARN("fail to resolve tenant", KR(ret));
    } else if (OB_NOT_NULL(ls_id) && OB_FAIL(Util::resolve_ls_id(ls_id, freeze_stmt->get_ls_id()))) {
      LOG_WARN("fail to resolve tablet id", KR(ret));
    } else if (OB_NOT_NULL(opt_tablet_id) &&
               OB_FAIL(Util::resolve_tablet_id(opt_tablet_id, freeze_stmt->get_tablet_id()))) {
      LOG_WARN("fail to resolve tablet id", KR(ret));
    } else if (affect_all || affect_all_user || affect_all_meta) {
      if ((true == affect_all && true == affect_all_user) ||
          (true == affect_all && true == affect_all_meta) ||
          (true == affect_all_user && true == affect_all_meta)) {
        ret = OB_NOT_SUPPORTED;
        LOG_WARN("only one of affect_all,affect_all_user,affect_all_meta can be true",
                KR(ret), K(affect_all), K(affect_all_user), K(affect_all_meta));
        LOG_USER_ERROR(OB_NOT_SUPPORTED,
                       "all/all_user/all_meta in combination with other names is");
      } else {
        if (affect_all) {
          freeze_stmt->set_freeze_all();
        } else if (affect_all_user) {
          freeze_stmt->set_freeze_all_user();
        } else {
          freeze_stmt->set_freeze_all_meta();
        }
      }
    }
  }

  return ret;
}


  //
  // This node has six children_ and they are following:
  // cache_type_: parse_tree.children_[0]
  // opt_namespace: parse_tree.children_[1]
  // opt_sql_id: parse_tree.children_[2]
  // opt_databases: parse_tree.children_[3]
  // opt_tenant_list: parse_tree.children_[4]
  // flush_scope: parse_tree.children_[5]
  //
int ObFlushCacheResolver::resolve(const ParseNode &parse_tree)
{
  int ret = OB_SUCCESS;
  ObFlushCacheStmt *stmt = NULL;
  ObSQLSessionInfo* sess = params_.session_info_;
  /* Whether setting tenant-level configuration items or system parameters, alter system permission is required.
       Modifications to tenant-level configuration items are considered an extension, and control is exercised using alter system permission */
  if (OB_ISNULL(sess)) {
    ret = OB_ERR_UNEXPECTED;
    SERVER_LOG(WARN, "invalid session");
  } else if (OB_UNLIKELY(T_FLUSH_CACHE != parse_tree.type_ || parse_tree.num_child_ != 6)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("invalid argument",
             "type", get_type_name(parse_tree.type_),
             "child_num", parse_tree.num_child_);
  } else if (NULL == parse_tree.children_[0]
             || NULL == parse_tree.children_[5]) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("invalid parse tree", K(ret));
  } else if (NULL == (stmt = create_stmt<ObFlushCacheStmt>())) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
    LOG_ERROR("create ObFlushCacheStmt failed");
  } else {
    ObSchemaGetterGuard schema_guard;

    // first child: resolve cache type
    ParseNode *cache_type_node = parse_tree.children_[0];
    if(T_IDENT == cache_type_node->type_) {
      common::ObString pltmp,plself("pl");
      pltmp.assign_ptr(cache_type_node->str_value_, static_cast<ObString::obstr_size_t>(cache_type_node->str_len_));
      if (0 == pltmp.case_compare(plself)) {
        stmt->flush_cache_arg_.cache_type_ = CACHE_TYPE_PL_OBJ;
      } else {
        ret = OB_NOT_SUPPORTED;
        LOG_WARN("only support pl cache's cache evict by identify as T_IDENT", K(ret));
      }
    } else {
      stmt->flush_cache_arg_.cache_type_ = (ObCacheType)cache_type_node->value_;
    }
    // second child: resolve namespace
    ParseNode *namespace_node = parse_tree.children_[1];
    // third child: resolve sql_id
    ParseNode *sql_id_node = parse_tree.children_[2];
    // for adds database id
    // fourth child: resolve db_list
    ParseNode *db_node = parse_tree.children_[3];
    // for adds tenant ids
    // fivth child: resolve tenant list
    ParseNode *t_node = parse_tree.children_[4];
    // sixth child: resolve application fields
    stmt->is_global_ = parse_tree.children_[5]->value_;
    // whether is coarse granularity plan cache evict.
    // tenant level(true) / pcv_set level(false)
    bool is_coarse_granularity = true;
    ObSEArray<common::ObString, 8> db_name_list;

    // namespace
    if (OB_FAIL(ret)) {
    } else if (NULL == namespace_node) {
      stmt->flush_cache_arg_.ns_type_ = ObLibCacheNameSpace::NS_INVALID;
    } else if (stmt->flush_cache_arg_.cache_type_ != CACHE_TYPE_LIB_CACHE) {
      ret = OB_NOT_SUPPORTED;
      LOG_WARN("only support lib cache's cache evict by namespace", K(stmt->flush_cache_arg_.cache_type_), K(ret));
      LOG_USER_ERROR(OB_NOT_SUPPORTED, "only support lib cache's cache evict by namespace, other type");
    } else {
      if (OB_UNLIKELY(NULL == namespace_node->children_)) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("children should not be null");
      } else {
        ParseNode *node = namespace_node->children_[0];
        if (OB_UNLIKELY(NULL == node)) {
          ret = OB_ERR_UNEXPECTED;
          LOG_WARN("node should not be null");
        } else {
          if (node->str_len_ <= 0) {
            ret = OB_ERR_UNEXPECTED;
            LOG_WARN("empty namespace name");
          } else {
            ObString namespce_name(node->str_len_, node->str_value_);
            ObLibCacheNameSpace ns_type = ObLibCacheRegister::get_ns_type_by_name(namespce_name);
            if (ns_type <= ObLibCacheNameSpace::NS_INVALID || ns_type >= ObLibCacheNameSpace::NS_MAX) {
              ret = OB_ERR_UNEXPECTED;
              LOG_WARN("invalid namespace type", K(ns_type));
            } else {
              stmt->flush_cache_arg_.ns_type_ = ns_type;
            }
          }
        }
      }
    }

    // sql_id
    if (OB_FAIL(ret)) {
      // do nothing
    } else if (OB_ISNULL(sql_id_node)) {
      // do nothing
    // currently, only support plan cache's fine-grained cache evict
    } else if (stmt->flush_cache_arg_.cache_type_ != CACHE_TYPE_PLAN &&
               stmt->flush_cache_arg_.cache_type_ != CACHE_TYPE_PL_OBJ) {
      ret = OB_NOT_SUPPORTED;
      LOG_WARN("only support plan cache's fine-grained cache evict", K(stmt->flush_cache_arg_.cache_type_), K(ret));
      LOG_USER_ERROR(OB_NOT_SUPPORTED, "only support plan cache's fine-grained cache evict, other type");
    } else if (OB_ISNULL(sql_id_node->children_)
               || OB_ISNULL(sql_id_node->children_[0])) {
      ret = OB_INVALID_ARGUMENT;
      LOG_WARN("invalid argument", K(ret));
    } else if (T_SQL_ID == sql_id_node->type_) {
      if (sql_id_node->children_[0]->str_len_ > (OB_MAX_SQL_ID_LENGTH+1)) {
        ret = OB_INVALID_ARGUMENT;
        LOG_WARN("invalid argument", K(ret));
      } else {
        stmt->flush_cache_arg_.sql_id_.assign_ptr(
            sql_id_node->children_[0]->str_value_,
            static_cast<ObString::obstr_size_t>(sql_id_node->children_[0]->str_len_));
        stmt->flush_cache_arg_.is_fine_grained_ = true;
      }
    } else if (T_SCHEMA_ID == sql_id_node->type_) {
      stmt->flush_cache_arg_.schema_id_ = sql_id_node->children_[0]->value_;
      stmt->flush_cache_arg_.is_fine_grained_ = true;
    } else {
      ret = OB_INVALID_ARGUMENT;
      LOG_WARN("invalid argument", K(ret));
    }

    // retrive schema guard
    if (OB_FAIL(ret)) {
    } else if (OB_ISNULL(GCTX.schema_service_)) {
      ret = OB_ERR_UNEXPECTED;
      SERVER_LOG(WARN, "invalid argument", K(GCTX.schema_service_));
    } else if (OB_ISNULL(session_info_)) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("session info should not be null", K(ret));
    } else if (OB_FAIL(GCTX.schema_service_->get_tenant_schema_guard(
                schema_guard))) {
      SERVER_LOG(WARN, "get_schema_guard failed", K(ret));
    } else {
      // do nothing
    }

    // db names
    if (OB_FAIL(ret)) {
      // do nothing
    } else if (!stmt->flush_cache_arg_.is_fine_grained_) {
      if (OB_ISNULL(db_node)) {
        // tenant level plan cache evict
        // and not needs to specify db_name
      } else {
        ret = OB_NOT_SUPPORTED;
        LOG_USER_ERROR(OB_NOT_SUPPORTED, "flushing cache in database level at coarse flushing");
      }
    } else if (NULL == db_node) { // db list is empty
      // empty db list means clear all db's in fine-grained cache evict
      // do nothing
    } else if (OB_ISNULL(db_node->children_)
               || OB_ISNULL(db_node->children_[0])
               || T_DATABASE_LIST != db_node->type_) {
      ret = OB_INVALID_ARGUMENT;
      LOG_WARN("invalid argument", K(ret));
    } else {
      uint64_t db_id = 0;
      ObString db_names;
      ObString db_name;
      db_names.assign_ptr(db_node->children_[0]->str_value_,
                          static_cast<ObString::obstr_size_t>(db_node->children_[0]->str_len_));
      while (OB_SUCC(ret) && !db_names.empty()) {
        db_name = db_names.split_on(',').trim();
        if(db_name.empty() && NULL == db_names.find(',')) {
          db_name = db_names;
          db_names.reset();
        }
        if(!db_name.empty()) {
          if (OB_FAIL(db_name_list.push_back(db_name))) {
            SERVER_LOG(WARN, "failed to add database name", K(ret));
          }
        }
      } // for database name end
    }

    /*
     * different database belongs to different tenant,
     * and we will use following logics to retrive db_id:
     * for (tenant list) {
     *    for (database_name_list) {
     *      // find db_id from schema
     *      args_.push_back(db_id);
     *    }
     * }
     * */
    // tenant list
    if (OB_FAIL(ret)) {
    } else if (NULL == t_node) { //tenant list is empty
      if (!stmt->flush_cache_arg_.is_fine_grained_) { // coarse grained cache evict
        // Notes:
        // tenant level evict, and no tenant list specified means all tenant
        // for system tenant: empty means flush all tenant's
        // for normal tenant: this node has been set as NULL in parse phase,
        //                    and already adds its tenant id to tenant list in above
        // Therefore, do nothing
        if (false
              && OB_FAIL(stmt->flush_cache_arg_.push_tenant())) {
            LOG_WARN("failed  to adds tenant for normal tenant", K(ret));
        }
      } else { // fine-grained cache evcit
        // for fine-grained plan evict, we must specify tenant list
        uint64_t t_id = OB_INVALID_ID;
        t_id = 1UL;
        if (t_id <= OB_MAX_RESERVED_TENANT_ID) {// system tenant will use this path.
          // system tenant must specify tenant_list;
          ret = OB_EMPTY_TENANT;
          SERVER_LOG(WARN, "invalid argument, fine-grained plan evict must specify tenant_list", K(ret));
        } else { // normal tenant
          if (OB_FAIL(stmt->flush_cache_arg_.push_tenant())) {
            LOG_WARN("failed  to adds tenant for normal tenant", K(ret));
          } else {
            // normal tenant will use it's tenant when t_node is empty
            for (uint64_t j=0; OB_SUCC(ret) && j<db_name_list.count(); j++) {
              uint64_t db_id = 0;
              if (OB_FAIL(schema_guard.get_database_id(db_name_list.at(j), db_id))
                  || (int64_t)db_id == OB_INVALID_ID) {
                ret = OB_ERR_BAD_DATABASE;
                SERVER_LOG(WARN, "database not exist", K(db_name_list.at(j)), K(ret));
              } else if (OB_FAIL(stmt->flush_cache_arg_.push_database(db_id))) {
                SERVER_LOG(WARN, "fail to push database id ",K(db_name_list.at(j)), K(db_id), K(ret));
              }
            } // for get db_id ends
            LOG_INFO("normal tenant flush plan cache ends", K(t_id), K(db_name_list));
          }
        } // normal tenant ends
      } // fine-grained plan evcit ends
    } else if (false) {
    // tenant node is not null and current tenant is not sys tenant
    // due to normal tenant cannot specify tenant, and only can purge
    // their own plan cache
      ret = OB_ERR_NO_PRIVILEGE;
      LOG_WARN("Only sys tenant can do this operation", K(ret));
    } else if (NULL == t_node->children_) {
      ret = OB_ERR_UNEXPECTED;
      SERVER_LOG(WARN, "invalid argument", K(ret));
    } else {
      ObString tenant_name;
      // adds tenants and get db_ids
      for (int64_t i = 0; OB_SUCC(ret) && i < t_node->num_child_; ++i) {
        if (OB_ISNULL(t_node->children_[i])) {
          ret = OB_ERR_UNEXPECTED;
          SERVER_LOG(WARN, "invalid argument", K(t_node->children_[i]), K(ret));
        } else {
          tenant_name.assign_ptr(t_node->children_[i]->str_value_,
                                 static_cast<ObString::obstr_size_t>(t_node->children_[i]->str_len_));
          if (OB_FAIL(stmt->flush_cache_arg_.push_tenant())) {
            SERVER_LOG(WARN, "fail to push tenant id ",K(tenant_name), K(ret));
          } else {
            ObSchemaGetterGuard schema_guard_db;
            if (OB_FAIL(GCTX.schema_service_->get_tenant_schema_guard(schema_guard_db))) {
              SERVER_LOG(WARN, "get_schema_guard failed", K(ret));
            } else {
              for (uint64_t j = 0; OB_SUCC(ret) && j < db_name_list.count(); j++) {
                uint64_t db_id = 0;
                if (OB_FAIL(schema_guard_db.get_database_id(db_name_list.at(j), db_id))) {
                  SERVER_LOG(WARN, "database not exist", K(db_name_list.at(j)), K(ret));
                } else if ((int64_t)db_id == OB_INVALID_ID) {
                  ret = OB_ERR_BAD_DATABASE;
                  SERVER_LOG(WARN, "database not exist", K(db_name_list.at(j)), K(ret));
                } else if (OB_FAIL(stmt->flush_cache_arg_.push_database(db_id))) {
                  SERVER_LOG(WARN, "fail to push database id ",K(db_name_list.at(j)), K(db_id), K(ret));
                }
              } // for get db_id ends
            }
          }
        } // for get tenant ends
        tenant_name.reset();
      } //for tenant end
    }
    LOG_INFO("resolve flush command finished!", K(ret),
                K(stmt->is_global_), K(stmt->flush_cache_arg_.cache_type_),
                K(stmt->flush_cache_arg_.sql_id_), K(stmt->flush_cache_arg_.is_fine_grained_),
                K(stmt->flush_cache_arg_.db_ids_));
  }
  return ret;
}

int ObFlushKVCacheResolver::resolve(const ParseNode &parse_tree)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(T_FLUSH_KVCACHE != parse_tree.type_)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("type is not T_FLUSH_KVCACHE", "type", get_type_name(parse_tree.type_));
  } else {
    ObFlushKVCacheStmt *stmt = create_stmt<ObFlushKVCacheStmt>();
    if (NULL == stmt) {
      ret = OB_ALLOCATE_MEMORY_FAILED;
      LOG_ERROR("create ObFlushKVCacheStmt failed");
    } else {
      stmt_ = stmt;
      if (OB_UNLIKELY(NULL == parse_tree.children_)) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("children should not be null");
      } else {
        ParseNode *node = parse_tree.children_[0];
        if (NULL == node) {
          stmt->tenant_name_.reset();
        } else {
          if (OB_UNLIKELY(NULL == node->children_)) {
            ret = OB_ERR_UNEXPECTED;
            LOG_WARN("children should not be null");
          } else {
            node = node->children_[0];
            if (OB_UNLIKELY(NULL == node)) {
              ret = OB_ERR_UNEXPECTED;
              LOG_WARN("node should not be null");
            } else {
              if (node->str_len_ <= 0) {
                ret = OB_ERR_UNEXPECTED;
                LOG_WARN("empty tenant name");
              } else {
                ObString tenant_name(node->str_len_, node->str_value_);
                if (OB_FAIL(stmt->tenant_name_.assign(tenant_name))) {
                  LOG_WARN("assign tenant name failed", K(tenant_name), K(ret));
                }
              }
            }
          }
        }
        if (OB_SUCC(ret)) {
          node = parse_tree.children_[1];
          if (NULL == node) {
            stmt->cache_name_.reset();
          } else {
            if (OB_UNLIKELY(NULL == node->children_)) {
              ret = OB_ERR_UNEXPECTED;
              LOG_WARN("children should not be null");
            } else {
              node = node->children_[0];
              if (OB_UNLIKELY(NULL == node)) {
                ret = OB_ERR_UNEXPECTED;
                LOG_WARN("node should not be null");
              } else {
                if (node->str_len_ <= 0) {
                  ret = OB_ERR_UNEXPECTED;
                  LOG_WARN("empty cache name");
                } else {
                  ObString cache_name(node->str_len_, node->str_value_);
                  if (OB_FAIL(stmt->cache_name_.assign(cache_name))) {
                    LOG_WARN("assign cache name failed", K(cache_name), K(ret));
                  }
                }
              }
            }
          }
        }
      }
    }
  }
  return ret;
}


int ObFlushIlogCacheResolver::resolve(const ParseNode &parse_tree)
{
  int ret = OB_SUCCESS;
  ObFlushIlogCacheStmt *stmt = NULL;
  if (OB_UNLIKELY(T_FLUSH_ILOGCACHE != parse_tree.type_)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("type not match T_FLUSH_ILOGCACHE", "type", get_type_name(parse_tree.type_));
  } else if (OB_ISNULL(stmt = create_stmt<ObFlushIlogCacheStmt>())) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
    LOG_WARN("create ObFlushCacheStmt error", K(ret));
  } else if (OB_ISNULL(parse_tree.children_)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("children of parse tree is null", K(ret));
  } else {
    ParseNode *opt_file_id_node = parse_tree.children_[0];
    ParseNode *file_id_val_node = NULL;
    if (OB_ISNULL(opt_file_id_node)) {
      stmt->file_id_ = 0;
    } else if (OB_ISNULL(opt_file_id_node->children_)) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("opt_file_id_node.children is null", K(ret));
    } else if (OB_ISNULL(file_id_val_node = opt_file_id_node->children_[0])) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("file_id_val_node is null", K(ret));
    } else {
      int64_t file_id_val = file_id_val_node->value_; // type of value_ is int64_t
      if (file_id_val <= 0 || file_id_val >= INT32_MAX) {
        ret = OB_INVALID_ARGUMENT;
        LOG_WARN("invalid file_id when flush ilogcache", K(ret), K(file_id_val));
      } else {
        stmt->file_id_ = (int32_t)file_id_val;
        stmt_ = stmt;
        LOG_INFO("flush ilogcache resolve succ", K(file_id_val));
      }
    }
  }
  return ret;
}

int ObFlushDagWarningsResolver::resolve(const ParseNode &parse_tree)
{
  int ret = OB_SUCCESS;
  ObFlushDagWarningsStmt *stmt = NULL;
  if (OB_UNLIKELY(T_FLUSH_DAG_WARNINGS != parse_tree.type_)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("type not match T_FLUSH_DAG_WARNINGS", "type", get_type_name(parse_tree.type_));
  } else if (OB_ISNULL(stmt = create_stmt<ObFlushDagWarningsStmt>())) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
    LOG_WARN("create ObFlushDagWarningsStmt error", K(ret));
  }
  return ret;
}


int ObAdminMergeResolver::resolve(const ParseNode &parse_tree)
{
  int ret = OB_SUCCESS;
  ObSQLSessionInfo* sess = params_.session_info_;
  if (OB_UNLIKELY(T_MERGE_CONTROL != parse_tree.type_)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("type is not T_MERGE_CONTROL", "type", get_type_name(parse_tree.type_));
  } else {
    ObAdminMergeStmt *stmt = create_stmt<ObAdminMergeStmt>();
    if (NULL == stmt) {
      ret = OB_ALLOCATE_MEMORY_FAILED;
      LOG_ERROR("create ObAdminMergeStmt failed");
    } else {
      stmt_ = stmt;
      if (OB_UNLIKELY(NULL == parse_tree.children_)) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("children should not be null");
      } else {
        ParseNode *node = parse_tree.children_[0];
        if (OB_UNLIKELY(NULL == node)) {
          ret = OB_ERR_UNEXPECTED;
          LOG_WARN("node should not be null");
        } else if (OB_UNLIKELY(T_INT != node->type_)) {
          ret = OB_ERR_UNEXPECTED;
          LOG_WARN("type is not T_INT", "type", get_type_name(node->type_));
        } else {
          switch (node->value_) {
            case 1: {
              stmt->get_rpc_arg().type_ = ObAdminMergeArg::START_MERGE;
              ret = OB_NOT_SUPPORTED;
              LOG_WARN("start merge zone is not supported", KR(ret));
              break;
            }
            case 2: {
              stmt->get_rpc_arg().type_ = ObAdminMergeArg::SUSPEND_MERGE;
              break;
            }
            case 3: {
              stmt->get_rpc_arg().type_ = ObAdminMergeArg::RESUME_MERGE;
              break;
            }
            default: {
              ret = OB_ERR_UNEXPECTED;
              LOG_WARN("unexpected merge admin type", "value", node->value_);
              break;
            }
          }
        }
      }

      if (OB_SUCC(ret)) {

        ParseNode *tenants_node = parse_tree.children_[1];
        if (NULL != tenants_node) {
          if (T_TENANT_LIST != tenants_node->type_) {
            ret = OB_ERR_UNEXPECTED;
            LOG_WARN("type is not T_TENANT_LIST", "type", get_type_name(tenants_node->type_));
          } else {
            bool affect_all = false;
            bool affect_all_user = false;
            bool affect_all_meta = false;
            int64_t ignored_count = 0;
            const int64_t child_num = tenants_node->num_child_;
            if (OB_UNLIKELY(nullptr == tenants_node->children_)
                || OB_UNLIKELY(0 == child_num)) {
              ret = OB_ERR_UNEXPECTED;
              LOG_WARN("children of tenant should not be null", KR(ret), K(child_num));
            } else if (OB_FAIL(Util::resolve_tenant(*tenants_node,
                                                    ignored_count, affect_all,
                                                    affect_all_user, affect_all_meta))) {
              LOG_WARN("fail to resolve tenant", KR(ret));
            } else if (affect_all || affect_all_user || affect_all_meta) {
              if ((true == affect_all && true == affect_all_user) ||
                  (true == affect_all && true == affect_all_meta) ||
                  (true == affect_all_user && true == affect_all_meta)) {
                ret = OB_NOT_SUPPORTED;
                LOG_WARN("only one of affect_all,affect_all_user,affect_all_meta can be true",
                        KR(ret), K(affect_all), K(affect_all_user), K(affect_all_meta));
                LOG_USER_ERROR(OB_NOT_SUPPORTED,
                               "all/all_user/all_meta in combination with other names is");
              } else {
                if (affect_all) {
                  stmt->get_rpc_arg().affect_all_ = true;
                } else if (affect_all_user) {
                  stmt->get_rpc_arg().affect_all_user_ = true;
                } else {
                  stmt->get_rpc_arg().affect_all_meta_ = true;
                }
              }
            }
          }
        }
      }
    }
  }
  return ret;
}




int ObRefreshMemStatResolver::resolve(const ParseNode &parse_tree)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(T_REFRESH_MEMORY_STAT != parse_tree.type_)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("type is not T_REFRESH_MEMORY_STAT", "type", get_type_name(parse_tree.type_));
  } else {
    ObRefreshMemStatStmt *stmt = create_stmt<ObRefreshMemStatStmt>();
    if (NULL == stmt) {
      ret = OB_ALLOCATE_MEMORY_FAILED;
      LOG_ERROR("create ObRefreshMemStatStmt failed");
    } else {
      stmt_ = stmt;
      if (OB_UNLIKELY(NULL == parse_tree.children_)) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("children should not be null");
      }
    }
  }
  return ret;
}

int ObRefreshFulltextDictResolver::resolve(const ParseNode &parse_tree)
{
  int ret = OB_SUCCESS;
  ObRefreshFulltextDictStmt *stmt = nullptr;
  if (T_REFRESH_FULLTEXT_DICT != parse_tree.type_ || 1 != parse_tree.num_child_
      || OB_ISNULL(parse_tree.children_) || OB_ISNULL(parse_tree.children_[0])) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("invalid refresh fulltext dictionary parse tree", K(ret));
  } else if (OB_ISNULL(stmt = create_stmt<ObRefreshFulltextDictStmt>())) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
  } else {
    const ParseNode *relation = parse_tree.children_[0];
    ObString database_name;
    ObString table_name;
    if (T_RELATION_FACTOR == relation->type_) {
      if (OB_FAIL(resolve_table_relation_node(relation, table_name, database_name))) {
        LOG_WARN("failed to resolve dictionary table relation", K(ret));
      }
    } else {
      ObString input(static_cast<int32_t>(relation->str_len_), relation->str_value_);
      const char *dot = input.find('.');
      const int64_t dot_pos = OB_ISNULL(dot) ? -1 : dot - input.ptr();
      if (dot_pos > 0 && dot_pos < input.length() - 1) {
        database_name.assign_ptr(input.ptr(), static_cast<int32_t>(dot_pos));
        table_name.assign_ptr(input.ptr() + dot_pos + 1,
                              static_cast<int32_t>(input.length() - dot_pos - 1));
      } else if (OB_ISNULL(session_info_) || session_info_->get_database_name().empty()) {
        ret = OB_ERR_NO_DB_SELECTED;
      } else {
        database_name = session_info_->get_database_name();
        table_name = input;
      }
    }
    ObSqlString qualified_name;
    if (OB_SUCC(ret)) {
      if (OB_FAIL(qualified_name.append_fmt("%.*s.%.*s", database_name.length(),
                                            database_name.ptr(), table_name.length(),
                                            table_name.ptr()))) {
        LOG_WARN("failed to build dictionary table name", K(ret));
      } else {
        ObString stored_name;
        if (OB_FAIL(ob_write_string(*allocator_, qualified_name.string(), stored_name))) {
          LOG_WARN("failed to copy dictionary table name", K(ret));
        } else {
          stmt->set_table_name(stored_name);
          stmt_ = stmt;
        }
      }
    }
  }
  if (OB_SUCC(ret) && OB_NOT_NULL(stmt_)) {
    const ObString &qualified = stmt->get_table_name();
    const char *dot = qualified.find('.');
    const int64_t dot_pos = OB_ISNULL(dot) ? -1 : dot - qualified.ptr();
    const ObTableSchema *dict_schema = nullptr;
    uint64_t database_id = OB_INVALID_ID;
    if (dot_pos <= 0 || dot_pos >= qualified.length() - 1) {
      ret = OB_INVALID_ARGUMENT;
      LOG_WARN("invalid qualified dictionary table name", K(ret), K(qualified));
    } else {
      ObString db_name(static_cast<int32_t>(dot_pos), qualified.ptr());
      ObString tb_name(static_cast<int32_t>(qualified.length() - dot_pos - 1),
                       qualified.ptr() + dot_pos + 1);
      if (OB_FAIL(schema_checker_->get_database_id(db_name, database_id))
          || OB_FAIL(schema_checker_->get_table_schema(database_id, tb_name, false, false,
                                                       false, dict_schema))) {
        LOG_WARN("failed to resolve dictionary table", K(ret), K(qualified));
      } else if (OB_ISNULL(dict_schema) || !dict_schema->is_fulltext_dict_table()) {
        ret = OB_INVALID_ARGUMENT;
        LOG_USER_ERROR(OB_INVALID_ARGUMENT,
                       "ALTER SYSTEM REFRESH FULLTEXT DICT requires a FULLTEXT_DICT table");
      }
    }
  }
  return ret;
}

int ObWashMemFragmentationResolver::resolve(const ParseNode &parse_tree)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(T_WASH_MEMORY_FRAGMENTATION != parse_tree.type_)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("type is not T_WASH_MEMORY_FRAGMENTATION", "type", get_type_name(parse_tree.type_));
  } else {
    ObWashMemFragmentationStmt *stmt = create_stmt<ObWashMemFragmentationStmt>();
    if (NULL == stmt) {
      ret = OB_ALLOCATE_MEMORY_FAILED;
      LOG_ERROR("create ObWashMemFragmentationStmt failed");
    } else {
      stmt_ = stmt;
      if (OB_UNLIKELY(NULL == parse_tree.children_)) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("children should not be null");
      }
    }
  }
  return ret;
}

int ObRefreshIOCalibrationResolver::resolve(const ParseNode &parse_tree)
{
  int ret = OB_SUCCESS;
  ObRefreshIOCalibraitonStmt *stmt = nullptr;
  obcall::ObAdminRefreshIOCalibrationArg *arg = nullptr;
  if (OB_UNLIKELY(T_REFRESH_IO_CALIBRATION != parse_tree.type_)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("type is not T_REFRESH_IO_CALIBRATION", "type", get_type_name(parse_tree.type_));
  } else if (OB_ISNULL(stmt = create_stmt<ObRefreshIOCalibraitonStmt>())) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
    LOG_ERROR("create ObRefreshIOCalibraitonStmt failed");
  } else if (FALSE_IT(stmt_ = stmt)) {
  } else if (OB_UNLIKELY(NULL == parse_tree.children_ || 3 != parse_tree.num_child_)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("parse tree children is invalid", K(ret), K(parse_tree.num_child_));
  } else {
    arg = &stmt->get_rpc_arg();
  }
  if (OB_SUCC(ret)) {
    // parse storage_name from child[0]
    const ParseNode *storage_name_node = parse_tree.children_[0];
    if (OB_ISNULL(storage_name_node) || storage_name_node->num_child_ <= 0) {
      // allow null, do nothing
    } else if (OB_FAIL(Util::resolve_string(storage_name_node->children_[0], arg->storage_name_))) {
      LOG_WARN("resolve storage name failed", K(ret));
    }
  }
  if (OB_SUCC(ret)) {
    // parse calibration_list from child[1]
    const ParseNode *calibration_list_node = parse_tree.children_[1];
    if (OB_ISNULL(calibration_list_node)) {
      // null means refresh
      arg->only_refresh_ = true;
    } else if (nullptr == calibration_list_node->children_ || calibration_list_node->num_child_ <= 0) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("calibration list node has no children", K(ret));
    } else {
      for (int64_t i = 0; OB_SUCC(ret) && i < calibration_list_node->num_child_; ++i) {
        common::ObIOBenchResult item;
        const ParseNode *calibration_info_node = calibration_list_node->children_[i];
        ObString calibration_string;
        if (OB_ISNULL(calibration_info_node)) {
          ret = OB_ERR_UNEXPECTED;
          LOG_WARN("children of calibration_list should not be null", K(ret), KP(calibration_info_node), K(i));
        } else if (OB_FAIL(Util::resolve_string(calibration_info_node, calibration_string))) {
          LOG_WARN("resolve calibration info node failed", K(ret));
          if (0 == i && calibration_info_node->str_len_ <= 0) {
            // empty means reset, do nothing
            arg->only_refresh_ = false;
            ret = OB_SUCCESS;
            break;
          }
        } else if (OB_FAIL(ObIOCalibration::parse_calibration_string(calibration_string, item))) {
          LOG_WARN("parse calibration info failed", K(ret), K(calibration_string), K(i));
        } else if (OB_FAIL(arg->calibration_list_.push_back(item))) {
          LOG_WARN("push back calibration item failed", K(ret), K(i), K(item));
        }
      }
    }
  }
  return ret;
}

static int alter_system_set_reset_constraint_check_and_add_item_mysql_mode(obcall::ObAdminSetConfigArg &rpc_arg, ObAdminSetConfigItem &item, ObSQLSessionInfo *& session_info)
{
  int ret = OB_SUCCESS;
  bool can_set_trace_control_info = false;
  if (OB_ISNULL(session_info)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("session_info is NULL", KP(session_info), K(ret));
  } else if (OB_FAIL(rpc_arg.items_.push_back(item))) {
    LOG_WARN("add config item failed", K(ret), K(item));
  } else if (0 == STRCMP(item.name_.ptr(), CLUSTER_ID)) {
    ret = OB_OP_NOT_ALLOW;
    LOG_WARN("cluster_id is not allowed to modify");
    LOG_USER_ERROR(OB_OP_NOT_ALLOW, "alter the parameter cluster_id");
  } else if (!can_set_trace_control_info &&
              session_info != NULL &&
              0 == STRCMP(item.name_.ptr(), OB_STR_TRC_CONTROL_INFO) &&
              !session_info->is_inner()) {
    ret = OB_OP_NOT_ALLOW;
    LOG_WARN("_trace_control_info is not allowed to modify");
    LOG_USER_ERROR(OB_OP_NOT_ALLOW, "alter the parameter _trace_control_info");
  }
  return ret;
}

static int alter_system_set_reset_constraint_check_and_add_item_oracle_mode(obcall::ObAdminSetConfigArg &rpc_arg, ObAdminSetConfigItem &item, ObSchemaChecker *& schema_checker)
{
  int ret = OB_SUCCESS;
  if (OB_FAIL(rpc_arg.items_.push_back(item))) {
    LOG_WARN("add config item failed", K(ret), K(item));
  }
  return ret;
}

/* for mysql mode */
int ObSetConfigResolver::resolve(const ParseNode &parse_tree)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(T_ALTER_SYSTEM_SET_PARAMETER != parse_tree.type_)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("type is not T_ALTER_SYSTEM_SET_PARAMETER", "type", get_type_name(parse_tree.type_));
  } else {
    if (OB_UNLIKELY(NULL == parse_tree.children_)) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("children should not be null");
    } else {
      const ParseNode *list_node = parse_tree.children_[0];
      if (OB_UNLIKELY(NULL == list_node)) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("list_node should not be null");
      } else {
        ObSetConfigStmt *stmt = create_stmt<ObSetConfigStmt>();
        if (OB_UNLIKELY(NULL == stmt)) {
          ret = OB_ALLOCATE_MEMORY_FAILED;
          LOG_ERROR("create stmt failed");
        } else {
          HEAP_VAR(ObCreateTableResolver, ddl_resolver, params_) {
            for (int64_t i = 0; OB_SUCC(ret) && i < list_node->num_child_; ++i) {
              if (OB_UNLIKELY(NULL == list_node->children_)) {
                ret = OB_ERR_UNEXPECTED;
                LOG_WARN("children should not be null");
                break;
              }

              const ParseNode *action_node = list_node->children_[i];
              if (NULL == action_node) {
                continue;
              }

              // config name
              HEAP_VAR(ObAdminSetConfigItem, item) {
                if (OB_LIKELY(session_info_ != NULL)) {

                } else {
                  LOG_WARN("session is null");

                }

                if (OB_UNLIKELY(NULL == action_node->children_)) {
                  ret = OB_ERR_UNEXPECTED;
                  LOG_WARN("children should not be null");
                  break;
                }

                if (OB_UNLIKELY(NULL == action_node->children_[0])) {
                  ret = OB_ERR_UNEXPECTED;
                  LOG_WARN("children[0] should not be null");
                  break;
                }

                ObString name(action_node->children_[0]->str_len_,
                              action_node->children_[0]->str_value_);
                ObCharset::casedn(CS_TYPE_UTF8MB4_GENERAL_CI, name);
                if (OB_FAIL(item.name_.assign(name))) {
                  LOG_WARN("assign config name failed", K(name), K(ret));
                  break;
                }

                // config value
                ObObjParam val;
                ObDefaultValueRes resolve_res(val);
                if (OB_UNLIKELY(NULL == action_node->children_[1])) {
                  ret = OB_ERR_UNEXPECTED;
                  LOG_WARN("children[1] should not be null");
                  break;
                } else if (OB_FAIL(ddl_resolver.resolve_default_value(action_node->children_[1], resolve_res))) {
                  LOG_WARN("resolve config value failed", K(ret));
                  break;
                } else if (!resolve_res.is_literal_) {
                  ret = OB_ERR_ILLEGAL_TYPE;
                  LOG_WARN("resolve config value failed", K(ret), K(resolve_res.is_literal_));
                  break;
                }
                ObString str_val;
                ObCollationType cast_coll_type = CS_TYPE_INVALID;
                if (OB_LIKELY(session_info_ != NULL)) {
                  if (OB_SUCCESS != session_info_->get_collation_connection(cast_coll_type)) {
                    LOG_WARN("fail to get collation_connection");
                    cast_coll_type = ObCharset::get_default_collation(ObCharset::get_default_charset());
                  } else {}
                } else {
                  LOG_WARN("session is null");
                  cast_coll_type = ObCharset::get_system_collation();
                }
                ObArenaAllocator allocator(ObModIds::OB_SQL_COMPILE);
                ObCastCtx cast_ctx(&allocator,
                                   NULL,//to varchar. this field will not be used.
                                   0,//to varchar. this field will not be used.
                                   CM_NONE,
                                   cast_coll_type,
                                   NULL);
                EXPR_GET_VARCHAR_V2(val, str_val);
                if (OB_FAIL(ret)) {
                  LOG_WARN("get varchar value failed", K(ret), K(val));
                  break;
                } else if (OB_FAIL(item.value_.assign(str_val))) {
                  LOG_WARN("assign config value failed", K(ret), K(str_val));
                  break;
                } else if (OB_FAIL(convert_param_value(item))) {
                  LOG_WARN("convert config value failed", K(ret));
                } else if (NULL != action_node->children_[2]) {
                  ObString comment(action_node->children_[2]->str_len_,
                                   action_node->children_[2]->str_value_);
                  if (OB_FAIL(item.comment_.assign(comment))) {
                    LOG_WARN("assign comment failed", K(comment), K(ret));
                    break;
                  }
                }

                // ignore config scope
                // server or zone
                if (OB_SUCC(ret) && NULL != action_node->children_[3]) {
                  const ParseNode *n = action_node->children_[3];
                  if (OB_FAIL(Util::resolve_server_or_zone(n, item))) {
                    LOG_WARN("resolve server or zone failed", K(ret));
                    break;
                  }
                } // if

                // tenant
                if (OB_SUCC(ret) && NULL != action_node->children_[4]) {
                  const ParseNode *n = action_node->children_[4];
                  if (T_TENANT_NAME == n->type_) {

                    {
                      uint64_t tenant_node_id = OB_INVALID_ID;
                      ObString tenant_name(n->children_[0]->str_len_,
                                           n->children_[0]->str_value_);
                      ObString config_name(item.name_.size(), item.name_.ptr());
                      if (OB_FAIL(item.tenant_name_.assign(tenant_name))) {
                        LOG_WARN("assign tenant name failed", K(tenant_name), K(ret));
                        break;
                      } else if (0 == config_name.case_compare(DEFAULT_TABLE_ORGANIZATION)) {
                        int64_t cfg_count = 0;
                        bool affect_all = false;
                        bool affect_all_user = false;
                        bool affect_all_meta = false;
                        if (OB_FAIL(ObAlterSystemResolverUtil::resolve_tenant(*n,
                                                                              cfg_count,
                                                                              affect_all,
                                                                              affect_all_user,
                                                                              affect_all_meta))) {
                          LOG_WARN("fail to get reslove tenant", K(ret), "exec_tid", 1UL);
                        } else if (affect_all_meta) {
                          ret = OB_NOT_SUPPORTED;
                          LOG_WARN("all_meta is not supported by ALTER SYSTEM SET DEFAULT_TABLE_ORGANIZATION",
                                  KR(ret), K(affect_all), K(affect_all_user), K(affect_all_meta));
                          LOG_USER_ERROR(OB_NOT_SUPPORTED,
                                        "use all_meta in 'ALTER SYSTEM SET DEFAULT_TABLE_ORGANIZATION' syntax is");
                        } else if ((0 == cfg_count)) {
                          if (!affect_all && !affect_all_user) {
                            ret = OB_ERR_UNEXPECTED;
                            LOG_WARN("error unexpected", "item", item, K(ret), K(i));
                          }
                        }
                        if (OB_SUCC(ret) && !(0 == cfg_count)) {
                          bool valid = true;
                          ObSchemaGetterGuard schema_guard;
                          if (OB_FAIL(GCTX.schema_service_->get_tenant_schema_guard(schema_guard))) {
                            LOG_WARN("get_schema_guard failed", K(ret));
                          }
                          for (int64_t i = 0; OB_SUCC(ret) && i < cfg_count && valid; i++) {

                            lib::Worker::CompatMode compat_mode;
                            valid = valid && ObConfigDefaultTableOrganizationChecker::check(item);
                            if (OB_FAIL(schema_guard.get_tenant_compat_mode(compat_mode))) {
                              LOG_WARN("fail to get compat mode", K(ret));
                            } else if (!valid) {
                              ret = OB_OP_NOT_ALLOW;
                              LOG_WARN("can not set default_table_organization", "item", item, K(ret));
                            }
                          }
                        }
                      }
                    }
                  } else {
                    ret = OB_ERR_UNEXPECTED;
                    LOG_WARN("resolve tenant name failed", K(ret));
                    break;
                  }
                } else if (OB_SUCC(ret) && (0 == STRCASECMP(item.name_.ptr(), DEFAULT_TABLE_ORGANIZATION))) {
                  bool valid = ObConfigDefaultTableOrganizationChecker::check(item);
                  if (!valid) {
                    ret = OB_OP_NOT_ALLOW;
                    LOG_WARN("can not set default_table_organization", "item", item, K(ret));
                  }
                }

                if (OB_SUCC(ret)) {
                  if (OB_FAIL(alter_system_set_reset_constraint_check_and_add_item_mysql_mode(stmt->get_rpc_arg(), item, session_info_))) {
                    LOG_WARN("constraint check failed", K(ret));
                  }
                }
              }
            }
          }
        }
      }
    }
  }
  return ret;
}

int ObSetConfigResolver::convert_param_value(ObAdminSetConfigItem &item)
{
  int ret = OB_SUCCESS;
  if (0 == item.name_.str().case_compare("audit_log_path")) {
    ObBackupDest dest;
    ObBackupPathString path;
    if (item.value_.str().empty()) {
      // do nothing
    } else if (OB_FAIL(dest.set(item.value_.str()))) {
      LOG_WARN("failed to set backup dest", K(ret));
      if (OB_INVALID_BACKUP_DEST == ret) {
        // let config checker return the actual error info
        ret = OB_SUCCESS;
      }
    } else if (OB_FAIL(dest.get_backup_dest_str(path.ptr(), path.capacity()))) {
      LOG_WARN("failed to get backup dest", K(ret));
    } else if (OB_FAIL(item.value_.assign(path.str()))) {
      LOG_WARN("failed to assign config value", K(ret));
    }
  }
  return ret;
}

int ObSetTPResolver::resolve(const ParseNode &parse_tree)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(T_ALTER_SYSTEM_SETTP != parse_tree.type_)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("type is not T_ALTER_SYSTEM", "type", get_type_name(parse_tree.type_));
  } else {
    if (OB_UNLIKELY(NULL == parse_tree.children_)) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("children should not be null");
    } else {
      const ParseNode *list_node = parse_tree.children_[0];
      if (OB_UNLIKELY(NULL == list_node)) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("list_node should not be null");
      } else {
        ObSetTPStmt *stmt = create_stmt<ObSetTPStmt>();
        if (OB_UNLIKELY(NULL == stmt)) {
          ret = OB_ALLOCATE_MEMORY_FAILED;
          LOG_ERROR("create stmt failed");
        } else {
          for (int64_t i = 0; OB_SUCC(ret) && i < list_node->num_child_; ++i) {
            if (OB_UNLIKELY(NULL == list_node->children_)) {
              ret = OB_ERR_UNEXPECTED;
              LOG_WARN("children should not be null");
              break;
            }

            const ParseNode *action_node = list_node->children_[i];
            if (OB_ISNULL(action_node)
                || OB_ISNULL(action_node->children_)
                || OB_ISNULL(action_node->children_[0])) {
              continue;
            }

            const ParseNode *value = action_node->children_[0];
            switch (action_node->type_)
            {
            case T_TP_NO: {        // event no
              if (stmt->get_rpc_arg().event_name_ != "") {
                ret = OB_NOT_SUPPORTED;
                SQL_RESV_LOG(WARN, "Setting tp_no and tp_name simultaneously is not supported.");
                LOG_USER_ERROR(OB_NOT_SUPPORTED, "Setting tp_no and tp_name simultaneously is");
              } else {
                stmt->get_rpc_arg().event_no_ = value->value_;
              }
              break;
            }
            case T_TP_NAME: {     // event name
              if (stmt->get_rpc_arg().event_no_ != 0) {
                ret = OB_NOT_SUPPORTED;
                SQL_RESV_LOG(WARN, "Setting tp_no and tp_name simultaneously is not supported.");
                LOG_USER_ERROR(OB_NOT_SUPPORTED, "Setting tp_no and tp_name simultaneously is");
              } else {
                stmt->get_rpc_arg().event_name_.assign_ptr(
                  value->str_value_, static_cast<ObString::obstr_size_t>(value->str_len_));
              }
              break;
            }
            case T_OCCUR: {        // occurrence
              if (value->value_ > 0) {
                stmt->get_rpc_arg().occur_ = value->value_;
              } else {
                ret = OB_INVALID_ARGUMENT;
              }
            } break;
            case T_TRIGGER_MODE: {      // trigger frequency
              if (T_INT == value->type_) {
                if (value->value_ < 0) {
                  ret = OB_INVALID_ARGUMENT;
                  SQL_RESV_LOG(WARN, "invalid argument", K(value->value_));
                } else {
                  stmt->get_rpc_arg().trigger_freq_ = value->value_;
                }
              }
            } break;
            case T_ERROR_CODE: {        // error code
              if (value->value_ > 0) {
                stmt->get_rpc_arg().error_code_ = -value->value_;
              } else {
                stmt->get_rpc_arg().error_code_ = value->value_;
              }
            } break;
            case T_TP_COND: {        // condition
              stmt->get_rpc_arg().cond_ = value->value_;
            } break;
            default:
              break;
            }
          }
        }
        LOG_INFO("set tp", K(stmt->get_rpc_arg()));
      }
    }
  }

  return ret;
}

int ObReloadGtsResolver::resolve(const ParseNode &parse_tree)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(T_RELOAD_GTS != parse_tree.type_)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("type is not T_RELOAD_GTS", "type", get_type_name(parse_tree.type_));
  } else {
    ret = OB_NOT_SUPPORTED;
    LOG_WARN("reload gts not supported", KR(ret));
    LOG_USER_ERROR(OB_NOT_SUPPORTED, "reload gts command");
  }
  return ret;
}

int ObClearMergeErrorResolver::resolve(const ParseNode &parse_tree)
{
  int ret = OB_SUCCESS;
  ObSQLSessionInfo* sess = params_.session_info_;
  if (OB_UNLIKELY(T_CLEAR_MERGE_ERROR != parse_tree.type_)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("type is not T_CLEAR_MERGE_ERROR", "type", get_type_name(parse_tree.type_));
  } else {
    ObClearMergeErrorStmt *stmt = create_stmt<ObClearMergeErrorStmt>();
    if (NULL == stmt) {
      ret = OB_ALLOCATE_MEMORY_FAILED;
      LOG_ERROR("create ObClearMergeErrorStmt failed");
    } else {
      stmt_ = stmt;
      if (OB_UNLIKELY(NULL == parse_tree.children_)) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("children should not be null");
      } else {

        ParseNode *tenants_node = parse_tree.children_[0];
        if (NULL != tenants_node) {
          if (T_TENANT_LIST != tenants_node->type_) {
            ret = OB_ERR_UNEXPECTED;
            LOG_WARN("type is not T_TENANT_LIST", "type", get_type_name(tenants_node->type_));
          } else {
            bool affect_all = false;
            bool affect_all_user = false;
            bool affect_all_meta = false;
            int64_t ignored_count = 0;
            const int64_t child_num = tenants_node->num_child_;
            if (OB_UNLIKELY(nullptr == tenants_node->children_)
                || OB_UNLIKELY(0 == child_num)) {
              ret = OB_ERR_UNEXPECTED;
              LOG_WARN("children of tenant should not be null", KR(ret), K(child_num));
            } else if (OB_FAIL(Util::resolve_tenant(*tenants_node,
                                                    ignored_count, affect_all,
                                                    affect_all_user, affect_all_meta))) {
              LOG_WARN("fail to resolve tenant", KR(ret));
            } else if (affect_all || affect_all_user || affect_all_meta) {
              if ((true == affect_all && true == affect_all_user) ||
                  (true == affect_all && true == affect_all_meta) ||
                  (true == affect_all_user && true == affect_all_meta)) {
                ret = OB_NOT_SUPPORTED;
                LOG_WARN("only one of affect_all,affect_all_user,affect_all_meta can be true",
                        KR(ret), K(affect_all), K(affect_all_user), K(affect_all_meta));
                LOG_USER_ERROR(OB_NOT_SUPPORTED,
                               "all/all_user/all_meta in combination with other names is");
              } else {
                if (affect_all) {
                  stmt->get_rpc_arg().affect_all_ = true;
                } else if (affect_all_user) {
                  stmt->get_rpc_arg().affect_all_user_ = true;
                } else {
                  stmt->get_rpc_arg().affect_all_meta_ = true;
                }
              }
            }
          }
        }
      }
    }
  }
  return ret;
}

int ObAlterSystemResolverUtil::get_and_verify_tenant_name(
    const ParseNode* tenant_name_node,
    uint64_t &out_tgt_id)
{
  // get tenant id
  int ret = OB_SUCCESS;
  out_tgt_id = OB_INVALID_TENANT_ID;
  ObString tenant_name;
  ObSchemaGetterGuard schema_guard;
  const ObSimpleTenantSchema *tenant_schema = NULL;
  if (OB_UNLIKELY(!true)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("exec tenant id is invalid", KR(ret));
  } else if (OB_ISNULL(GCTX.schema_service_)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("GCTX.schema_service_ is null", KR(ret), KP(GCTX.schema_service_));
  } else if (OB_FAIL(GCTX.schema_service_->get_tenant_schema_guard(schema_guard))) {
    LOG_WARN("failed to get_tenant_schema_guard", KR(ret));
  } else if (NULL == tenant_name_node) {
    out_tgt_id = 1UL;
  } else if (OB_FAIL(resolve_tenant_name(tenant_name_node, tenant_name))) {
    LOG_WARN("fail to resolve target tenant id", KR(ret));
  } else if (FALSE_IT(out_tgt_id = 1UL)) {
    LOG_WARN("failed to get tenant id from schema guard", KR(ret), K(tenant_name));
    if (OB_TENANT_NOT_EXIST == ret || OB_ERR_INVALID_TENANT_NAME == ret) {
      ret = OB_TENANT_NOT_EXIST;
      LOG_USER_ERROR(OB_TENANT_NOT_EXIST, tenant_name.length(), tenant_name.ptr());
    }
  }
  // check tenant status
  if (OB_FAIL(ret)) {
  } else if (OB_FAIL(schema_guard.get_tenant_info(tenant_schema))) {
    LOG_WARN("fail to get tenant schema", KR(ret));
  } else if (OB_ISNULL(tenant_schema)) {
    ret = OB_TENANT_NOT_EXIST;
    LOG_USER_ERROR(OB_TENANT_NOT_EXIST, tenant_name.length(), tenant_name.ptr());
    LOG_WARN("tenant not exist", KR(ret), KP(tenant_schema), K(tenant_name));
  } else if (tenant_schema->is_creating() || tenant_schema->is_dropping()) {
    ret = OB_OP_NOT_ALLOW;
    LOG_USER_ERROR(OB_OP_NOT_ALLOW, "Tenant is creating or dropping, current operation is");
    LOG_WARN("tenant status not normal", KR(ret),
            K(tenant_schema->is_creating()), K(tenant_schema->is_dropping()));
  }
  return ret;
}

int ObUpgradeVirtualSchemaResolver::resolve(const ParseNode &parse_tree)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(T_UPGRADE_VIRTUAL_SCHEMA != parse_tree.type_)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("type is not T_UPGRADE_VIRTUAL_SCHEMA", "type", get_type_name(parse_tree.type_));
  } else {
    ObUpgradeVirtualSchemaStmt *stmt = create_stmt<ObUpgradeVirtualSchemaStmt>();
    if (NULL == stmt) {
      ret = OB_ALLOCATE_MEMORY_FAILED;
      LOG_ERROR("create ObUpgradeVirtualSchemaStmt failed");
    } else {
      stmt_ = stmt;
    }
  }
  return ret;
}





//
//                           /- T_INT(priority)
//                          /|
//  T_ENABLE_SQL_THROTTLE -<
//                          \|
//                           \- T_SQL_THROTTLE_METRICS -< [ T_RT -> (decimal)
//                                                        | T_CPU -> (decimal)
//                                                        | T_IO -> (int)
//                                                        | T_NETWORK -> (decimal)
//                                                        | T_LOGICAL_READS -> (int)
//                                                        ]+
//
int ObEnableSqlThrottleResolver::resolve(const ParseNode &parse_tree)
{
  int ret = OB_SUCCESS;
  ObEnableSqlThrottleStmt *stmt = nullptr;

  if (OB_UNLIKELY(T_ENABLE_SQL_THROTTLE != parse_tree.type_)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("type is not T_ENABLE_SQL_THROTTLE", "type", get_type_name(parse_tree.type_));
  } else if (OB_ISNULL(parse_tree.children_)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("parse_tree's children is null", K(ret));
  } else if (2 != parse_tree.num_child_) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("parse_tree's number of children doesn't match", K(ret));
  } else {
    stmt = create_stmt<ObEnableSqlThrottleStmt>();
    if (nullptr == stmt) {
      ret = OB_ALLOCATE_MEMORY_FAILED;
      LOG_ERROR("create ObEnableSqlThrottleStmt failed");
    }
  }
  if (OB_SUCC(ret)) {
    ParseNode *priority_node = parse_tree.children_[0];
    if (nullptr != priority_node) {
      stmt->set_priority(priority_node->value_);
    }
    ParseNode *metrics_node = parse_tree.children_[1];
    if (metrics_node != nullptr) {
      for (int i = 0; i < metrics_node->num_child_; i++) {
        ParseNode *node = metrics_node->children_[i];
        ParseNode *valNode = node->children_[0];
        switch (node->type_) {
        case T_RT:
          if (valNode->type_ == T_INT) {
            stmt->set_rt(static_cast<double>(valNode->value_));
          } else if (valNode->type_ == T_NUMBER) {
            stmt->set_rt(atof(valNode->str_value_));
          }
          break;
        case T_CPU:
          if (valNode->type_ == T_INT) {
            stmt->set_cpu(static_cast<double>(valNode->value_));
          } else if (valNode->type_ == T_NUMBER) {
            stmt->set_cpu(atof(valNode->str_value_));
          }
          break;
        case T_IO:
          stmt->set_io(valNode->value_);
          break;
        case T_NETWORK:
          if (valNode->type_ == T_INT) {
            stmt->set_network(static_cast<double>(valNode->value_));
          } else if (valNode->type_ == T_NUMBER) {
            stmt->set_network(atof(valNode->str_value_));
          }
          break;
        case T_LOGICAL_READS:
          stmt->set_logical_reads(valNode->value_);
          break;
        case T_QUEUE_TIME:
          if (valNode->type_ == T_INT) {
            stmt->set_queue_time(static_cast<double>(valNode->value_));
          } else if (valNode->type_ == T_NUMBER) {
            stmt->set_queue_time(atof(valNode->str_value_));
          }
          break;
        default:
          break;
        }
      }
    }
  }
  if (OB_SUCC(ret)) {
    stmt_ = stmt;
  }
  return ret;
}

int ObDisableSqlThrottleResolver::resolve(const ParseNode &parse_tree)
{
  UNUSED(parse_tree);
  int ret = OB_SUCCESS;
  stmt_ = create_stmt<ObDisableSqlThrottleStmt>();
  return ret;
}

int ObCancelTaskResolver::resolve(const ParseNode &parse_tree)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(T_CANCEL_TASK != parse_tree.type_)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("type is not T_CANCEL_TASK", "type", get_type_name(parse_tree.type_));
  } else if (OB_ISNULL(parse_tree.children_)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("parse_tree's children is null", K(ret));
  } else {
    ObCancelTaskStmt *cancel_task = create_stmt<ObCancelTaskStmt>();
    if (NULL == cancel_task) {
      ret = OB_ALLOCATE_MEMORY_FAILED;
      LOG_ERROR("create ObCancelTaskStmt failed");
    } else {
      stmt_ = cancel_task;
      ParseNode *cancel_type_node = parse_tree.children_[0];
      ParseNode *task_id = parse_tree.children_[1];
      share::ObSysTaskType task_type = MAX_SYS_TASK_TYPE;
      ObString task_id_str;
      if (OB_ISNULL(task_id)) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("task_id node is null", K(ret));
      } else if (OB_FAIL(Util::resolve_string(task_id, task_id_str))) {
        LOG_WARN("resolve string failed", K(ret));
      } else if (NULL == cancel_type_node) {
        task_type = MAX_SYS_TASK_TYPE;
      } else {
        switch (cancel_type_node->value_) {
        case 1: {
            task_type = GROUP_MIGRATION_TASK;
            break;
          }
        default: {
            ret = OB_ERR_UNEXPECTED;
            LOG_WARN("unexpected cancel task type", K(ret), "value", cancel_type_node->value_);
            break;
          }
        }
      }

      if (OB_SUCC(ret)) {
        if (OB_FAIL(cancel_task->set_param(task_type, task_id_str))) {
          LOG_WARN("failed to set cancel task param", K(ret), K(task_type), K(task_id_str));
        }
      }
    }
  }
  return ret;
}

int ObAlterDiskgroupAddDiskResolver::resolve(const ParseNode &parse_tree)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(T_ALTER_DISKGROUP_ADD_DISK != parse_tree.type_)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("type is not T_ALTER_DISKGROUP_ADD_DISK", "type", get_type_name(parse_tree.type_));
  } else {
    ObAddDiskStmt *stmt = create_stmt<ObAddDiskStmt>();
    if (NULL == stmt) {
      ret = OB_ALLOCATE_MEMORY_FAILED;
      LOG_ERROR("create ObAddDiskStmt failed");
    } else {
      stmt_ = stmt;
      ObAdminAddDiskArg &arg = stmt->arg_;
      if (OB_UNLIKELY(NULL == parse_tree.children_)) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("children should not be null");
      } else if (OB_FAIL(Util::resolve_relation_name(parse_tree.children_[0], arg.diskgroup_name_))) {
        LOG_WARN("failed to resolve diskgroup_name", K(ret));
      } else if (OB_FAIL(Util::resolve_string(parse_tree.children_[1], arg.disk_path_))) {
        LOG_WARN("failed to resolve disk_path", K(ret));
      } else if (NULL != parse_tree.children_[2]
                    && OB_FAIL(Util::resolve_string(parse_tree.children_[2], arg.alias_name_))) {
        LOG_WARN("failed to resolve alias name", K(ret));
      } else if (OB_FAIL(Util::resolve_server(parse_tree.children_[3], arg.server_))) {
        LOG_WARN("failed to resolve server", K(ret));
      } else if (NULL != parse_tree.children_[4]
                    && OB_FAIL(Util::resolve_zone(parse_tree.children_[4], arg.zone_))) {
        LOG_WARN("failed to resolve zone", K(ret));
      } else {
        LOG_INFO("succeed to resolve add disk arg", K(arg));
      }
    }
  }
  return ret;
}

int ObAlterDiskgroupDropDiskResolver::resolve(const ParseNode &parse_tree)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(T_ALTER_DISKGROUP_DROP_DISK != parse_tree.type_)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("type is not T_ALTER_DISKGROUP_DROP_DISK", "type", get_type_name(parse_tree.type_));
  } else {
    ObDropDiskStmt *stmt = create_stmt<ObDropDiskStmt>();
    if (NULL == stmt) {
      ret = OB_ALLOCATE_MEMORY_FAILED;
      LOG_ERROR("create ObDropDiskStmt failed");
    } else {
      stmt_ = stmt;
      ObAdminDropDiskArg &arg = stmt->arg_;
      if (OB_UNLIKELY(NULL == parse_tree.children_)) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("children should not be null");
      } else if (OB_FAIL(Util::resolve_relation_name(parse_tree.children_[0], arg.diskgroup_name_))) {
        LOG_WARN("failed to resolve diskgroup_name", K(ret));
      } else if (OB_FAIL(Util::resolve_string(parse_tree.children_[1], arg.alias_name_))) {
        LOG_WARN("failed to resolve alias name", K(ret));
      } else if (OB_FAIL(Util::resolve_server(parse_tree.children_[2], arg.server_))) {
        LOG_WARN("failed to resolve server", K(ret));
      } else if (NULL != parse_tree.children_[3]
                    && OB_FAIL(Util::resolve_zone(parse_tree.children_[3], arg.zone_))) {
        LOG_WARN("failed to resolve zone", K(ret));
      } else {
        LOG_INFO("succeed to resolve drop disk arg", K(arg));
      }
    }
  }
  return ret;
}



// for oracle mode grammer: alter system set sys_var = val
int ObAlterSystemSetResolver::resolve(const ParseNode &parse_tree)
{
  int ret = OB_SUCCESS;
  bool set_parameters = false;

  if (OB_UNLIKELY(T_ALTER_SYSTEM_SET != parse_tree.type_)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("parse_tree.type_ must be T_ALTER_SYSTEM_SET", K(ret), K(parse_tree.type_));
  } else if (OB_ISNULL(session_info_) || OB_ISNULL(allocator_)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_ERROR("session_info_ or allocator_ is NULL", K(ret), K(session_info_), K(allocator_));
  } else {
    /* first round: detect set variables or parameters */
    for (int64_t i = 0; OB_SUCC(ret) && i < parse_tree.num_child_; ++i) {
      ParseNode *set_node = nullptr, *set_param_node = nullptr;
      if (OB_ISNULL(set_node = parse_tree.children_[i])) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("set_node should not be null", K(ret));
      } else if (T_ALTER_SYSTEM_SET_PARAMETER != set_node->type_) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("set_node->type_ must be T_ALTER_SYSTEM_SET_PARAMETER", K(ret),
                 K(set_node->type_));
      } else if (OB_ISNULL(set_param_node = set_node->children_[0])) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("set_node is null", K(ret));
      } else if (OB_UNLIKELY(T_VAR_VAL != set_param_node->type_)) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("type is not T_VAR_VAL", K(ret), K(set_param_node->type_));
      } else {
        ParseNode *var = nullptr;
        if (OB_ISNULL(var = set_param_node->children_[0])) {
          ret = OB_ERR_UNEXPECTED;
          LOG_WARN("var is NULL", K(ret));
        } else if (T_IDENT != var->type_) {
          ret = OB_NOT_SUPPORTED;
          LOG_USER_ERROR(OB_NOT_SUPPORTED,
              "Variable name isn't identifier type");
        } else {
          ObString name(var->str_len_, var->str_value_);
          {
            if (true &&
                nullptr != GCONF.get_container().get(ObConfigStringKey(name))) {
                set_parameters = true;
                break;
            }
          }
        }
      }
    } // for
  }
  /* second round: gen stmt */
  if (OB_SUCC(ret)) {
    if (set_parameters) {
      FLOG_WARN("set parameters");
      ObSetConfigStmt *setconfig_stmt = create_stmt<ObSetConfigStmt>();
      if (OB_ISNULL(setconfig_stmt)) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_WARN("create set config stmt failed", KR(ret));
      } else {
        HEAP_VAR(ObCreateTableResolver, ddl_resolver, params_) {
          for (int64_t i = 0; OB_SUCC(ret) && i < parse_tree.num_child_; ++i) {
            ParseNode *set_node = nullptr, *set_param_node = nullptr;
            if (OB_ISNULL(set_node = parse_tree.children_[i])) {
              ret = OB_ERR_UNEXPECTED;
              LOG_WARN("set_node should not be null", K(ret));
            } else if (T_ALTER_SYSTEM_SET_PARAMETER != set_node->type_) {
              ret = OB_ERR_UNEXPECTED;
              LOG_WARN("set_node->type_ must be T_ALTER_SYSTEM_SET_PARAMETER",
                       K(ret), K(set_node->type_));
            } else if (OB_ISNULL(set_param_node = set_node->children_[0])) {
              ret = OB_ERR_UNEXPECTED;
              LOG_WARN("set_node is null", K(ret));
            } else if (OB_UNLIKELY(T_VAR_VAL != set_param_node->type_)) {
              ret = OB_ERR_UNEXPECTED;
              LOG_WARN("type is not T_VAR_VAL", K(ret), K(set_param_node->type_));
            } else {
              ParseNode *name_node = nullptr, *value_node = nullptr;
              HEAP_VAR(ObAdminSetConfigItem, item) {

                /* name */
                if (OB_ISNULL(name_node = set_param_node->children_[0])) {
                  ret = OB_ERR_UNEXPECTED;
                  LOG_WARN("var is NULL", K(ret));
                } else if (T_IDENT != name_node->type_) {
                  ret = OB_NOT_SUPPORTED;
                  LOG_USER_ERROR(OB_NOT_SUPPORTED,
                      "Variable name isn't identifier type");
                } else {
                  ObString name(name_node->str_len_, name_node->str_value_);
                  ObCharset::casedn(CS_TYPE_UTF8MB4_GENERAL_CI, name);
                  if (OB_FAIL(item.name_.assign(name))) {
                    LOG_WARN("assign config name failed", K(name), K(ret));
                  }
                }
                if (OB_FAIL(ret)) {
                  break;
                }
                /* value */
                if (OB_ISNULL(value_node = set_param_node->children_[1])) {
                  ret = OB_INVALID_ARGUMENT;
                  LOG_WARN("value node is NULL", K(ret));
                } else {
                  ObObjParam val;
                  ObDefaultValueRes resolve_res(val);
                  if (OB_FAIL(ddl_resolver.resolve_default_value(value_node, resolve_res))) {
                    LOG_WARN("resolve config value failed", K(ret));
                    break;
                  } else if (!resolve_res.is_literal_) {
                    ret = OB_ERR_ILLEGAL_TYPE;
                    LOG_WARN("resolve config value failed", K(ret), K(resolve_res.is_literal_));
                    break;
                  }
                  ObString str_val;
                  ObCollationType cast_coll_type = CS_TYPE_INVALID;
                  if (OB_SUCCESS !=
                      session_info_->get_collation_connection(cast_coll_type)) {
                    LOG_WARN("fail to get collation_connection");
                    cast_coll_type = ObCharset::get_default_collation(
                        ObCharset::get_default_charset());
                  }
                  ObArenaAllocator allocator(ObModIds::OB_SQL_COMPILE);
                  ObCastCtx cast_ctx(&allocator,
                                     NULL, //to varchar. this field will not be used.
                                     0, //to varchar. this field will not be used.
                                     CM_NONE,
                                     cast_coll_type,
                                     NULL);
                  EXPR_GET_VARCHAR_V2(val, str_val);
                  if (OB_FAIL(ret)) {
                    LOG_WARN("get varchar value failed", K(ret), K(val));
                    break;
                  } else if (OB_FAIL(item.value_.assign(str_val))) {
                    LOG_WARN("assign config value failed", K(ret), K(str_val));
                    break;
                  }
                }
                if (OB_SUCC(ret)) {
                  if (OB_FAIL(alter_system_set_reset_constraint_check_and_add_item_oracle_mode(
                      setconfig_stmt->get_rpc_arg(), item, schema_checker_))) {
                    LOG_WARN("constraint check failed", K(ret));
                  } else if (OB_SUCC(ret) && (0 == STRCASECMP(item.name_.ptr(), DEFAULT_TABLE_ORGANIZATION))) {
                    LOG_WARN("can not set default_table_organization", "item", item);
                    LOG_USER_NOTE(OB_NOT_SUPPORTED, "'ALTER SYSTEM SET DEFAULT_TABLE_ORGANIZATION' syntax in the oracle tenant is");
                  }
                }
              }
            }
          } // for
        }
      }
    } else {
      ObVariableSetStmt *variable_set_stmt = create_stmt<ObVariableSetStmt>();
      if (OB_ISNULL(variable_set_stmt)) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("create variable set stmt failed", K(OB_ALLOCATE_MEMORY_FAILED));
      } else {
        ParseNode *set_node = nullptr, *set_param_node = NULL;
        ObVariableSetStmt::VariableSetNode var_node;
        for (int64_t i = 0; OB_SUCC(ret) && i < parse_tree.num_child_; ++i) {
          if (OB_ISNULL(set_node = parse_tree.children_[i])) {
            ret = OB_ERR_UNEXPECTED;
            LOG_WARN("set_node should not be null", K(ret));
          } else if (T_ALTER_SYSTEM_SET_PARAMETER != set_node->type_) {
            ret = OB_ERR_UNEXPECTED;
            LOG_WARN("set_node->type_ must be T_ALTER_SYSTEM_SET_PARAMETER",
                     K(ret), K(set_node->type_));
          } else if (OB_ISNULL(set_param_node = set_node->children_[0])) {
            ret = OB_ERR_UNEXPECTED;
            LOG_WARN("set_node is null", K(ret));
          } else if (OB_UNLIKELY(T_VAR_VAL != set_param_node->type_)) {
            ret = OB_ERR_UNEXPECTED;
            LOG_WARN("type is not T_VAR_VAL", K(ret), K(set_param_node->type_));
          } else {
            ParseNode *var = NULL;
            var_node.set_scope_ = ObSetVar::SET_SCOPE_GLOBAL;
            variable_set_stmt->set_has_global_variable(true);
            /* resolve var_name */
            if (OB_ISNULL(var = set_param_node->children_[0])) {
              ret = OB_ERR_UNEXPECTED;
              LOG_WARN("var is NULL", K(ret));
            } else {
              ObString var_name;
              if (T_IDENT != var->type_) {
                ret = OB_NOT_SUPPORTED;
                LOG_USER_ERROR(OB_NOT_SUPPORTED,
                    "Variable name isn't identifier type");
              } else {
                var_node.is_system_variable_ = true;
                var_name.assign_ptr(var->str_value_,
                    static_cast<int32_t>(var->str_len_));
              }
              if (OB_SUCC(ret)) {
                if (OB_FAIL(ob_write_string(*allocator_, var_name,
                                            var_node.variable_name_))) {
                  LOG_WARN("Can not malloc space for variable name", K(ret));
                } else {
                  ObCharset::casedn(CS_TYPE_UTF8MB4_GENERAL_CI,
                                    var_node.variable_name_);
                }
              }
              /* resolve value */
              if (OB_SUCC(ret)) {
                if (OB_ISNULL(set_param_node->children_[1])) {
                  ret = OB_INVALID_ARGUMENT;
                  LOG_WARN("value node is NULL", K(ret));
                } else if (var_node.is_system_variable_) {
                  ParseNode value_node;
                  MEMCPY(&value_node, set_param_node->children_[1], sizeof(ParseNode));
                  if (OB_FAIL(ObResolverUtils::resolve_const_expr(params_, value_node, var_node.value_expr_, NULL))) {
                    LOG_WARN("resolve variable value failed", K(ret));
                  }
                }
              }
              if (OB_SUCC(ret) && OB_FAIL(variable_set_stmt->add_variable_node(var_node))) {
                LOG_WARN("Add set entry failed", K(ret));
              }
            }
          } // end resolve variable and value
        } // end for
      }
    }
  } // if

  return ret;
}


//for mysql mode
int ObResetConfigResolver::resolve(const ParseNode &parse_tree)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(T_ALTER_SYSTEM_RESET_PARAMETER != parse_tree.type_)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("type is not T_ALTER_SYSTEM_RESET_PARAMETER", "type", get_type_name(parse_tree.type_));
  } else {
    if (OB_UNLIKELY(NULL == parse_tree.children_)) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("children should not be null");
    } else {
      const ParseNode *list_node = parse_tree.children_[0];
      if (OB_UNLIKELY(NULL == list_node)) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("list_node should not be null");
      } else if (OB_UNLIKELY(NULL == list_node->children_)) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("children should not be null");
      } else {
        ObResetConfigStmt *stmt = create_stmt<ObResetConfigStmt>();
        if (OB_UNLIKELY(NULL == stmt)) {
          ret = OB_ALLOCATE_MEMORY_FAILED;
          LOG_ERROR("create stmt failed", K(ret));
        } else {
          for (int64_t i = 0; OB_SUCC(ret) && i < list_node->num_child_; i++) {
            const ParseNode *action_node = list_node->children_[i];
            if (NULL == action_node) {
              continue;
            } else {
              HEAP_VAR(ObAdminSetConfigItem, item) {
                if (OB_LIKELY(NULL != session_info_)) {

                } else {
                  LOG_WARN("session is null");

                }
                if (OB_UNLIKELY(NULL == action_node->children_)) {
                  ret = OB_ERR_UNEXPECTED;
                  LOG_WARN("children should not be null");
                } else if (OB_UNLIKELY(NULL == action_node->children_[0])) {
                  ret = OB_ERR_UNEXPECTED;
                  LOG_WARN("children[0] should not be null");
                } else {
                  // config name
                  ObString name(action_node->children_[0]->str_len_,
                                action_node->children_[0]->str_value_);
                  ObCharset::casedn(CS_TYPE_UTF8MB4_GENERAL_CI, name);
                  if (OB_FAIL(item.name_.assign(name))) {
                    LOG_WARN("assign config name failed", K(name), K(ret));
                  } else {
                    ObConfigItem *ci = NULL;
                    ObConfigItem * const *sys_ci_ptr = NULL;
                    ObConfigItem * const *tenant_ci_ptr = NULL;
                    sys_ci_ptr = GCONF.get_container().get(ObConfigStringKey(item.name_.ptr()));
                    if (OB_NOT_NULL(sys_ci_ptr)) {
                      ci = *sys_ci_ptr;
                    } else {
                      int tmp_ret = OB_SUCCESS;
                      if (!true) {
                        tmp_ret = OB_ERR_UNEXPECTED;
                        LOG_WARN("failed to get tenant config", KR(tmp_ret));
                      } else if (OB_ISNULL(tenant_ci_ptr = (GCONF.get_container().get(
                                                ObConfigStringKey(item.name_.ptr()))))) {
                        tmp_ret = OB_ERR_SYS_CONFIG_UNKNOWN;
                        LOG_WARN("can't found config item", KR(tmp_ret), "item", item);
                      } else {
                        ci = *tenant_ci_ptr;
                      }
                    }
                    if (OB_FAIL(ret)) {
                      LOG_WARN("error ret", KR(ret));
                    } else {
                      if (OB_NOT_NULL(ci)) {
                        if (OB_FAIL(item.value_.assign(ci->default_str()))) {
                          LOG_WARN("assign config value failed", K(ret));
                        } else {
                          //ignore config scope
                          //tenant
                          if (NULL != action_node->children_[1]) {
                            const ParseNode *n = action_node->children_[1];
                            if (T_TENANT_NAME == n->type_) {

                              {
                                ObString tenant_name(n->children_[0]->str_len_,
                                                      n->children_[0]->str_value_);
                                if (OB_FAIL(item.tenant_name_.assign(tenant_name))) {
                                  LOG_WARN("assign tenant name failed", K(tenant_name), K(ret));
                                }
                              }
                            } else {
                              ret = OB_ERR_UNEXPECTED;
                              LOG_WARN("resolve tenant name failed", K(ret));
                            }
                          }
                          if (OB_SUCC(ret)) {
                            if (OB_FAIL(alter_system_set_reset_constraint_check_and_add_item_mysql_mode(stmt->get_rpc_arg(), item, session_info_))) {
                              LOG_WARN("constraint check failed", K(ret));
                            }
                          }
                        }
                      } else {
                        ret = OB_ERR_SYS_CONFIG_UNKNOWN;
                        LOG_WARN("unknown config", K(ret), K(item));
                      }
                    }
                  }
                }
              }
            }
          }
        }
      }
    }
  }
  return ret;
}

int ObAlterSystemResetResolver::resolve(const ParseNode &parse_tree)
{
  int ret = OB_SUCCESS;
  bool set_parameters = false;

  if (OB_UNLIKELY(T_ALTER_SYSTEM_RESET != parse_tree.type_)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("parse_tree.type_ must be T_ALTER_SYSTEM_RESET", K(ret), K(parse_tree.type_));
  } else if (OB_ISNULL(session_info_) || OB_ISNULL(allocator_)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_ERROR("session_info_ or allocator_ is NULL", K(ret), K(session_info_), K(allocator_));
  } else {
    /* first round: detect set variables or parameters */
    for (int64_t i = 0; OB_SUCC(ret) && i < parse_tree.num_child_; ++i) {
      ParseNode *set_node = nullptr, *set_param_node = nullptr;
      if (OB_ISNULL(set_node = parse_tree.children_[i])) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("set_node should not be null", K(ret));
      } else if (T_ALTER_SYSTEM_RESET_PARAMETER != set_node->type_) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("set_node->type_ must be T_ALTER_SYSTEM_RESET_PARAMETER", K(ret),
                 K(set_node->type_));
      } else if (OB_ISNULL(set_param_node = set_node->children_[0])) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("set_node is null", K(ret));
      } else if (OB_UNLIKELY(T_VAR_VAL != set_param_node->type_)) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("type is not T_VAR_VAL", K(ret), K(set_param_node->type_));
      } else {
        ParseNode *var = nullptr;
        if (OB_ISNULL(var = set_param_node->children_[0])) {
          ret = OB_ERR_UNEXPECTED;
          LOG_WARN("var is NULL", K(ret));
        } else if (T_IDENT != var->type_) {
          ret = OB_NOT_SUPPORTED;
          LOG_USER_ERROR(OB_NOT_SUPPORTED,
              "Variable name isn't identifier type");
        } else {
          ObString name(var->str_len_, var->str_value_);
          {
            if (true &&
              nullptr != GCONF.get_container().get(ObConfigStringKey(name))) {
              set_parameters = true;
              break;
            }
          }
        }
      }
    } // for
  }
  /* second round: gen stmt */
  if (OB_SUCC(ret)) {
    if (set_parameters) {
      ObSetConfigStmt *setconfig_stmt = create_stmt<ObSetConfigStmt>();
      if (OB_ISNULL(setconfig_stmt)) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_WARN("create set config stmt failed", KR(ret));
      } else {
        for (int64_t i = 0; OB_SUCC(ret) && i < parse_tree.num_child_; ++i) {
          ParseNode *set_node = nullptr, *set_param_node = nullptr;
          if (OB_ISNULL(set_node = parse_tree.children_[i])) {
            ret = OB_ERR_UNEXPECTED;
            LOG_WARN("set_node should not be null", K(ret));
          } else if (T_ALTER_SYSTEM_RESET_PARAMETER != set_node->type_) {
            ret = OB_ERR_UNEXPECTED;
            LOG_WARN("set_node->type_ must be T_ALTER_SYSTEM_RESET_PARAMETER",
                      K(ret), K(set_node->type_));
          } else if (OB_ISNULL(set_param_node = set_node->children_[0])) {
            ret = OB_ERR_UNEXPECTED;
            LOG_WARN("set_node is null", K(ret));
          } else if (OB_UNLIKELY(T_VAR_VAL != set_param_node->type_)) {
            ret = OB_ERR_UNEXPECTED;
            LOG_WARN("type is not T_VAR_VAL", K(ret), K(set_param_node->type_));
          } else {
            ParseNode *name_node = nullptr, *value_node = nullptr;
            HEAP_VAR(ObAdminSetConfigItem, item) {

              /* name */
              if (OB_ISNULL(name_node = set_param_node->children_[0])) {
                ret = OB_ERR_UNEXPECTED;
                LOG_WARN("var is NULL", K(ret));
              } else if (T_IDENT != name_node->type_) {
                ret = OB_NOT_SUPPORTED;
                LOG_USER_ERROR(OB_NOT_SUPPORTED,
                    "Variable name isn't identifier type");
              } else {
                ObString name(name_node->str_len_, name_node->str_value_);
                ObCharset::casedn(CS_TYPE_UTF8MB4_GENERAL_CI, name);
                if (OB_FAIL(item.name_.assign(name))) {
                  LOG_WARN("assign config name failed", K(name), K(ret));
                }
              }
              if (OB_FAIL(ret)) {
                continue;
              }
              //value
              ObConfigItem *ci = NULL;
              ObConfigItem * const *tenant_ci_ptr = NULL;
              {
                int tmp_ret = OB_SUCCESS;
                if (!true) {
                  tmp_ret = OB_ERR_UNEXPECTED;
                  LOG_WARN("failed to get tenant config", KR(tmp_ret));
                } else if (OB_ISNULL(tenant_ci_ptr = (GCONF.get_container().get(
                                          ObConfigStringKey(item.name_.ptr()))))) {
                  tmp_ret = OB_ERR_SYS_CONFIG_UNKNOWN;
                  LOG_WARN("can't found config item", KR(tmp_ret), "item", item);
                } else {
                  ci = *tenant_ci_ptr;
                }
              }
              if (OB_FAIL(ret)) {
                LOG_WARN("error ret", KR(ret));
              } else {
                if (OB_NOT_NULL(ci)) {
                  if (OB_FAIL(item.value_.assign(ci->default_str()))) {
                    LOG_WARN("assign config value failed", K(ret));
                  }
                } else {
                  ret = OB_ERR_SYS_CONFIG_UNKNOWN;
                }
              }
              if (OB_SUCC(ret)) {
                if (OB_FAIL(alter_system_set_reset_constraint_check_and_add_item_oracle_mode(
                    setconfig_stmt->get_rpc_arg(), item, schema_checker_))) {
                  LOG_WARN("constraint check failed", KR(ret));
                }
              }
            }
          }
        } // for
      }
    } else {
      ret = OB_ERR_SYS_CONFIG_UNKNOWN;
      LOG_WARN("variables do not support reset or unknown config item", KR(ret));
    }
  } // if
  return ret;
}

int get_and_verify_tenant_name(
    const ParseNode *parse_node,
    uint64_t &out_tgt_id,
    const char * const op_str)
{
  int ret = OB_SUCCESS;
  ObString tenant_name;
  ObSchemaGetterGuard schema_guard;
  int tmp_ret = OB_SUCCESS;
  const int64_t COMMENT_LENGTH = 512;
  char comment[COMMENT_LENGTH] = {0};
  int64_t pos = 0;
  if (OB_UNLIKELY(!true)
      || OB_ISNULL(op_str)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("exec tenant id is invalid", KR(ret), K(op_str));
  } else if (NULL == parse_node) {
    {
      ret = OB_NOT_SUPPORTED;
      LOG_WARN("no specified tenant in the sys tenant's session", KR(ret));
      if (OB_TMP_FAIL(databuff_printf(comment, COMMENT_LENGTH, pos,
              "%s of SYS tenant is", op_str))) {
        LOG_WARN("failed to printf to comment", KR(ret), KR(tmp_ret), K(op_str));
      } else {
        LOG_USER_ERROR(OB_NOT_SUPPORTED, comment);
      }
    }
  } else if (OB_FAIL(resolve_tenant_name(parse_node, tenant_name))) {
    LOG_WARN("fail to resolve target tenant id", KR(ret));
  } else if (OB_ISNULL(GCTX.schema_service_)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("GCTX.schema_service_ is null", KR(ret), KP(GCTX.schema_service_));
  } else if (OB_FAIL(GCTX.schema_service_->get_tenant_schema_guard(schema_guard))) {
    LOG_WARN("failed to get_tenant_schema_guard", KR(ret));
  } else if (FALSE_IT(out_tgt_id = 1UL)) {
    LOG_WARN("failed to get tenant id from schema guard", KR(ret), K(tenant_name));
    if (OB_TENANT_NOT_EXIST == ret || OB_ERR_INVALID_TENANT_NAME == ret) {
      ret = OB_TENANT_NOT_EXIST;
      LOG_USER_ERROR(OB_TENANT_NOT_EXIST, tenant_name.length(), tenant_name.ptr());
    }
  } else {
    ret = OB_NOT_SUPPORTED;
    LOG_WARN("only support user tenant", KR(ret));
    if (OB_TMP_FAIL(databuff_printf(comment, COMMENT_LENGTH, pos,
            "%s of META or SYS tenant is", op_str))) {
      LOG_WARN("failed to printf to comment", KR(ret), KR(tmp_ret), K(op_str));
    } else {
      LOG_USER_ERROR(OB_NOT_SUPPORTED, comment);
    }
  }
  return ret;
}

int resolve_tenant_name(
    const ParseNode *node,
    ObString &tenant_name)
{
  int ret = OB_SUCCESS;
  tenant_name.reset();
  if (OB_ISNULL(node)) {
    {
      ret = OB_INVALID_ARGUMENT;
      LOG_WARN("invalid argument", KR(ret));
      LOG_USER_ERROR(OB_INVALID_ARGUMENT, "tenant name, should specify tenant name");
    }
  } else if (OB_UNLIKELY(T_TENANT_NAME != node->type_)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid type", K(node->type_));
  } else if (OB_UNLIKELY(node->num_child_ <= 0)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid num_child", "num_child", node->num_child_);
  } else if (OB_ISNULL(node->children_)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("node should not be null");
  } else {
    const ParseNode *tenant_name_node = node->children_[0];
    if (OB_ISNULL(tenant_name_node)) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("tenant_name_node should not be null");
    } else if (tenant_name_node->value_ <= 0) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("empty tenant string");
    } else {
      tenant_name.assign_ptr((char *)(tenant_name_node->str_value_),
                            static_cast<int32_t>(tenant_name_node->str_len_));
    }
  }
  return ret;
}

} // end namespace sql
} // end namespace oceanbase
