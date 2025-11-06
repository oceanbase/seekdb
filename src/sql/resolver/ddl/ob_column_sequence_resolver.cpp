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

#include "sql/resolver/ddl/ob_column_sequence_resolver.h"

namespace oceanbase
{
using namespace common;
using namespace share::schema;
namespace sql
{

/**
 *  CREATE SEQUENCE schema.sequence_name
 *      (create_sequence_option_list,...)
 */

ObColumnSequenceResolver::ObColumnSequenceResolver(ObResolverParams &params)
  : ObStmtResolver(params)
{
}

ObColumnSequenceResolver::~ObColumnSequenceResolver()
{
}
// Do nothing, belongs to identity_column
// Parse inside create_table_resolver or alter_table_resolver
int ObColumnSequenceResolver::resolve(const ParseNode &parse_tree)
{
  UNUSED(parse_tree);
  return OB_SUCCESS;
}

int ObColumnSequenceResolver::resolve_sequence_without_name(ObColumnSequenceStmt *&mystmt, ParseNode *&node)
{
  int ret = OB_SUCCESS;

  if (OB_ISNULL(session_info_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("session_info is null", K(ret));
  } else if (OB_ISNULL(mystmt = create_stmt<ObColumnSequenceStmt>())) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
    LOG_WARN("failed to create select stmt");
  } else {
    ObString db_name = session_info_->get_database_name();
    // inner sequnece, name as ISEQ$$_tableid_columnid, it can't comfirm when resolve
    mystmt->set_database_name(db_name);
    mystmt->set_tenant_id(session_info_->get_effective_tenant_id());
    mystmt->set_is_system_generated();

    if (OB_SUCC(ret) && OB_NOT_NULL(node)) {
      if (OB_UNLIKELY(T_SEQUENCE_OPTION_LIST != node->type_)) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("invalid option node type", K(node->type_), K(ret));
      } else {
        ObSequenceResolver<ObColumnSequenceStmt> resolver;
        if (OB_FAIL(resolver.resolve_sequence_options(session_info_->get_effective_tenant_id(),
                                                      mystmt, node))) {
          LOG_WARN("resolve sequence options failed", K(ret));
        }
      }
    }
  }
  return ret;
}

} /* sql */
} /* oceanbase */
