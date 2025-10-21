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
#include "ob_savepoint_resolver.h"
#include "ob_savepoint_stmt.h"

namespace oceanbase
{
namespace sql
{
using namespace common;

int ObSavePointResolver::resolve(const ParseNode &parse_tree)
{
  int ret = OB_SUCCESS;
  ObSavePointStmt *stmt = NULL;
  if (OB_FAIL(create_savepoint_stmt(parse_tree.type_, stmt)) || OB_ISNULL(stmt)) {
    LOG_WARN("failed to create savepoint stmt", K(ret));
  } else if (OB_FAIL(stmt->set_sp_name(parse_tree.str_value_, parse_tree.str_len_))) {
    LOG_WARN("failed to set savepoint name", K(ret));
  } else {
    stmt_ = stmt;
  }
  return ret;
}

int ObSavePointResolver::create_savepoint_stmt(ObItemType stmt_type, ObSavePointStmt *&stmt)
{
  int ret = OB_SUCCESS;
  switch (stmt_type) {
  case T_CREATE_SAVEPOINT:
    stmt = create_stmt<ObCreateSavePointStmt>();
    break;
  case T_ROLLBACK_SAVEPOINT:
    stmt = create_stmt<ObRollbackSavePointStmt>();
    break;
  case T_RELEASE_SAVEPOINT:
    stmt = create_stmt<ObReleaseSavePointStmt>();
    break;
  default:
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid stmt type", K(ret), K(stmt_type));
  }
  return ret;
}

} // namespace sql
} // namespace oceanbase

