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
#include "sql/resolver/ddl/ob_drop_tablegroup_resolver.h"
#include "sql/resolver/ddl/ob_drop_tablegroup_stmt.h"
using namespace oceanbase::sql;
using namespace oceanbase::common;
using namespace oceanbase::share::schema;

ObDropTablegroupResolver::ObDropTablegroupResolver(ObResolverParams &params)
    : ObDDLResolver(params)
{
}

ObDropTablegroupResolver::~ObDropTablegroupResolver()
{
}

int ObDropTablegroupResolver::resolve(const ParseNode &parse_tree)
{
  int ret = OB_SUCCESS;
  ParseNode *node = const_cast<ParseNode*>(&parse_tree);
  ObDropTablegroupStmt *drop_tablegroup_stmt = NULL;
  if (OB_ISNULL(session_info_) || OB_ISNULL(node) ||
      T_DROP_TABLEGROUP != node->type_ ||
      TG_NODE_COUNT != node->num_child_ ||
      OB_ISNULL(node->children_)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("session_info_ is null or parser error", K(ret));
  }
  ObString tablegroup_name;
  if (OB_SUCC(ret)) {
    if (NULL == (drop_tablegroup_stmt = create_stmt<ObDropTablegroupStmt>())) {
      ret = OB_ALLOCATE_MEMORY_FAILED;
      SQL_RESV_LOG(ERROR, "failed to create stmt", K(ret));
    } else {
      stmt_ = drop_tablegroup_stmt;
    }

    if(OB_SUCC(ret)) {
      if (NULL != node->children_[IF_NOT_EXIST]) {
        if (T_IF_EXISTS == node->children_[IF_NOT_EXIST]->type_) {
          drop_tablegroup_stmt->set_if_exist(true);
        } else {
          ret = OB_ERR_UNEXPECTED;
          LOG_WARN("node type is not T_IF_EXISTS", K(ret));
        }
      } else {
        drop_tablegroup_stmt->set_if_exist(false);
      }
      if (OB_SUCC(ret)) {
        if (NULL != node->children_[TG_NAME] && T_IDENT == node->children_[TG_NAME]->type_) {
          tablegroup_name.assign_ptr((char *)(node->children_[TG_NAME]->str_value_),
                                    static_cast<int32_t>(node->children_[TG_NAME]->str_len_));
          drop_tablegroup_stmt->set_tablegroup_name(tablegroup_name);
          drop_tablegroup_stmt->set_tenant_id(session_info_->get_effective_tenant_id());
        } else {
          ret = OB_ERR_UNEXPECTED;
          LOG_WARN("node type is not T_IDENT", K(ret));
        }
      }
    }
  }

  return ret;
}

