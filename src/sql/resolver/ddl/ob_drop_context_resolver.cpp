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

#define USING_LOG_PREFIX  SQL_RESV

#include "sql/resolver/ddl/ob_drop_context_resolver.h"


namespace oceanbase
{
using namespace common;
using namespace obrpc;
using namespace share::schema;
namespace sql
{
ObDropContextResolver::ObDropContextResolver(ObResolverParams &params) : ObDDLResolver(params)
{
}

ObDropContextResolver::~ObDropContextResolver()
{
}

int ObDropContextResolver::resolve(const ParseNode &parse_tree)
{
  int ret = OB_SUCCESS;
  ObDropContextStmt *stmt = NULL;
  bool is_sync_ddl_user = false;
  if (OB_UNLIKELY(T_DROP_CONTEXT != parse_tree.type_)
      || OB_UNLIKELY(ROOT_NUM_CHILD != parse_tree.num_child_)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("unexpected parse_tree", K(parse_tree.type_), K(parse_tree.num_child_), K(ret));
  } else if (OB_ISNULL(parse_tree.children_) || OB_ISNULL(parse_tree.children_[CONTEXT_NAMESPACE])
             || OB_ISNULL(allocator_) || OB_ISNULL(session_info_)
             || OB_ISNULL(params_.query_ctx_)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("get unexpected null", K(ret), K(parse_tree.children_),
                                    K(parse_tree.children_[CONTEXT_NAMESPACE]),
                                    K(allocator_), K(session_info_),
                                    K(params_.query_ctx_));
  } else if (OB_FAIL(ObResolverUtils::check_sync_ddl_user(session_info_, is_sync_ddl_user))) {
    LOG_WARN("Failed to check sync_dll_user", K(ret));
  } else if (OB_UNLIKELY(NULL == (stmt = create_stmt<ObDropContextStmt>()))) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
    LOG_ERROR("create context stmt failed", K(ret));
  } else {
    ObString ctx_namespace;
    stmt_ = stmt;
    ObContextDDLArg &drop_arg = stmt->get_arg();
    ObContextSchema &ctx_schema = drop_arg.ctx_schema_;
    ctx_schema.set_tenant_id(session_info_->get_effective_tenant_id());
    //ctx_schema.set_database_id(session_info_->get_database_id());
    // check namesapce && package_name
    if (OB_FAIL(resolve_context_namespace(*parse_tree.children_[CONTEXT_NAMESPACE],
                                            ctx_namespace))) {
      LOG_WARN("failed to resolve namespace", K(ret));
    } else if (OB_FAIL(ctx_schema.set_namespace(ctx_namespace))) {
      LOG_WARN("failed to set context info", K(ret));
    }
  }

  return ret;
}

int ObDropContextResolver::resolve_context_namespace(const ParseNode &namespace_node,
                                                       ObString &ctx_namespace)
{
  int ret = OB_SUCCESS;
  int32_t name_len = static_cast<int32_t>(namespace_node.str_len_);
  ctx_namespace.assign_ptr(const_cast<char *>(namespace_node.str_value_), name_len);
  ObCollationType cs_type = CS_TYPE_INVALID;
  if (OB_ISNULL(session_info_)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("session is NULL", K(ret));
  } else if (OB_FAIL(session_info_->get_collation_connection(cs_type))) {
    LOG_WARN("fail to get collation_connection", K(ret));
  } else if (OB_FAIL(ObSQLUtils::check_and_convert_context_namespace(cs_type, ctx_namespace))) {
    LOG_WARN("failed to check ctx namespace", K(ret));
  }
  return ret;
}

}  // namespace sql
}  // namespace oceanbase
