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

#include "sql/resolver/ddl/ob_drop_location_resolver.h"
#include "sql/resolver/ddl/ob_drop_location_stmt.h"

namespace oceanbase
{
namespace sql
{
ObDropLocationResolver::ObDropLocationResolver(ObResolverParams &params)
  : ObDDLResolver(params)
{
}

ObDropLocationResolver::~ObDropLocationResolver()
{
}

int ObDropLocationResolver::resolve(const ParseNode &parse_tree)
{
  int ret = OB_SUCCESS;
  ParseNode *node = const_cast<ParseNode*>(&parse_tree);
  ObDropLocationStmt *drop_location_stmt = NULL;
  uint64_t tenant_id = OB_INVALID_ID;
  if (OB_ISNULL(session_info_) || OB_ISNULL(schema_checker_)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("get unexpected null", K(ret));
  } else if (FALSE_IT(tenant_id = session_info_->get_effective_tenant_id())) {
  } else if (OB_ISNULL(node)
      || OB_UNLIKELY(node->type_ != T_DROP_LOCATION)
      || OB_UNLIKELY(node->num_child_ != LOCATION_NODE_COUNT)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("invalid parse tree", K(ret));
  } else if (OB_ISNULL(node->children_)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_ERROR("invalid node children", K(ret), K(node), K(node->children_));
  } else if (OB_ISNULL(drop_location_stmt = create_stmt<ObDropLocationStmt>())) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
    LOG_ERROR("failed to get drop location stmt", K(ret));
  } else {
    stmt_ = drop_location_stmt;
    drop_location_stmt->set_tenant_id(session_info_->get_effective_tenant_id());
  }

  // location name
  if (OB_FAIL(ret)) {
  } else if (OB_ISNULL(drop_location_stmt)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("unexpected error, drop location stmt is NULL", K(ret), KP(drop_location_stmt));
  } else {
    ObCollationType cs_type = CS_TYPE_UTF8MB4_BIN;
    ObNameCaseMode case_mode = OB_NAME_CASE_INVALID;
    int max_name_length = lib::is_oracle_mode() ? OB_MAX_LOCATION_NAME_LENGTH : OB_MAX_USER_TABLE_NAME_LENGTH_MYSQL;
    ObString location_name;
    ParseNode *child_node = node->children_[LOCATION_NAME];
    if (OB_ISNULL(child_node)) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("invalid parse tree", K(ret));
    } else if (child_node->str_len_ >= max_name_length) {
      ret = OB_ERR_TOO_LONG_IDENT;
      LOG_USER_ERROR(OB_ERR_TOO_LONG_IDENT, static_cast<int32_t>(child_node->str_len_), child_node->str_value_);
    } else if (FALSE_IT(location_name.assign_ptr(child_node->str_value_, static_cast<int32_t>(child_node->str_len_)))){

    } else if (OB_FAIL(session_info_->get_name_case_mode(case_mode))) {
      LOG_WARN("failed to get name case mode", K(ret));
    } else if (is_mysql_mode() && OB_LOWERCASE_AND_INSENSITIVE == case_mode
               && OB_FAIL(ObCharset::tolower(cs_type, location_name, location_name, *allocator_))) {
      LOG_WARN("failed to lower string", K(ret));
    } else {
      drop_location_stmt->set_location_name(location_name);
    }
  }

  return ret;
}
} // end namespace sql
} // end namespace oceanbase
