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

#define USING_LOG_PREFIX SERVER
#include "sql/resolver/ddl/ob_create_tablegroup_resolver.h"

using namespace oceanbase::sql;
using namespace oceanbase::common;
using namespace oceanbase::share::schema;

ObCreateTablegroupResolver::ObCreateTablegroupResolver(ObResolverParams &params)
    : ObTableGroupResolver(params)
{
}

ObCreateTablegroupResolver::~ObCreateTablegroupResolver()
{
}

int ObCreateTablegroupResolver::resolve(const ParseNode &parse_tree)
{
  int ret = OB_SUCCESS;
  ParseNode *node = const_cast<ParseNode*>(&parse_tree);
  ObCreateTablegroupStmt *create_tablegroup_stmt = NULL;
  if (OB_ISNULL(session_info_) || OB_ISNULL(node) ||
      T_CREATE_TABLEGROUP != node->type_ ||
      OB_ISNULL(node->children_)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("session_info_ is null or parser is error", K(ret));
  }
  ObString tablegroup_name;
  if (OB_SUCC(ret)) {
    if (NULL == (create_tablegroup_stmt = create_stmt<ObCreateTablegroupStmt>())) {
      ret = OB_ALLOCATE_MEMORY_FAILED;
      SQL_RESV_LOG(ERROR, "failed to create create_tablegroup_stmt", K(ret));
    } else {
      stmt_ = create_tablegroup_stmt;
    }

    if (OB_SUCC(ret)) {
      if (NULL != node->children_[IF_NOT_EXIST]) {
        if (T_IF_NOT_EXISTS == node->children_[IF_NOT_EXIST]->type_) {
          create_tablegroup_stmt->set_if_not_exists(true);
        } else {
          ret = OB_ERR_UNEXPECTED;
          SQL_RESV_LOG(WARN, "node type is not T_IF_NOT_EXISTS", K(ret));
        }
      }
      if (OB_SUCC(ret)) {
        if (OB_ISNULL(node->children_[TG_NAME]) || T_IDENT != node->children_[TG_NAME]->type_) {
          ret = OB_ERR_UNEXPECTED;
          SQL_RESV_LOG(WARN, "node is null or node type is not T_IDENT", K(ret));
        }
      }
      if (OB_SUCC(ret)) {
        if (static_cast<int32_t>(node->children_[TG_NAME]->str_len_)
            >= OB_MAX_TABLEGROUP_NAME_LENGTH) {
          ret = OB_ERR_TOO_LONG_IDENT;
          LOG_USER_ERROR(OB_ERR_TOO_LONG_IDENT, static_cast<int32_t>(node->children_[TG_NAME]->str_len_),
                         node->children_[TG_NAME]->str_value_);
        } else {
          tablegroup_name.assign_ptr(node->children_[TG_NAME]->str_value_,
                                     static_cast<int32_t>(node->children_[TG_NAME]->str_len_));
          if (OB_FAIL(create_tablegroup_stmt->set_tablegroup_name(tablegroup_name))) {
            SQL_RESV_LOG(WARN, "set tablegroup name failed", K(ret));
          } else {
            create_tablegroup_stmt->set_tenant_id(session_info_->get_effective_tenant_id());
          }
        }
      }
    }
  }

  if (OB_FAIL(ret)) {
    //nothing todo
  } else if (OB_ISNULL(node->children_[TABLEGROUP_OPTION])) {
    //nothing todo
  } else {
    if (OB_FAIL(resolve_tablegroup_option(create_tablegroup_stmt,
                                          node->children_[TABLEGROUP_OPTION]))) {
      LOG_WARN("fail to resolve tabelgroup option", K(ret));
    }
  }

  if (OB_FAIL(ret)) {
    //nothing todo
  } else if (OB_NOT_NULL(node->children_[PARTITION_OPTION])) {
    //ignore partition after 4.2
    LOG_USER_WARN(OB_NOT_SUPPORTED, "create tablegroup with partition");
  }
  // set default sharding attribute
  if (OB_SUCC(ret)) {
    ObTablegroupSchema &tablegroup_schema = create_tablegroup_stmt->get_create_tablegroup_arg().tablegroup_schema_;
    ObString sharding_default(OB_PARTITION_SHARDING_ADAPTIVE);
    if (tablegroup_schema.get_sharding().empty()) {
      if (OB_FAIL(tablegroup_schema.set_sharding(sharding_default))) {
        LOG_WARN("set_default_sharding fail", K(ret));
      }
    }
  }
  SQL_RESV_LOG(INFO, "resolve create tablegroup finish", K(ret), K(tablegroup_name));
  return ret;
}
