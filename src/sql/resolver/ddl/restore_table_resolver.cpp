// Copyright (c) 2026 OceanBase.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#define USING_LOG_PREFIX SQL_RESV
#include "sql/resolver/ddl/restore_table_resolver.h"
#include "sql/resolver/ddl/ob_recyclebin_restore_stmt.h"

namespace oceanbase::sql {
using namespace common;

int RestoreTableResolver::resolve(const peg::Node &node)
{
  int ret = OB_SUCCESS;
  ObRecyclebinRestoreTableStmt *statement = nullptr;
  if (OB_ISNULL(session_info_) || OB_ISNULL(allocator_)) {
    ret = OB_ERR_UNEXPECTED;
  } else if (OB_ISNULL(statement = create_stmt<ObRecyclebinRestoreTableStmt>())) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
  } else {
    stmt_ = statement;
    ObString source_table;
    ObString source_database;
    OZ(resolve_table_relation_node(&peg::restore_table_source(node),
                                   source_table, source_database, true));
    OX(statement->set_origin_table_name(source_table));
    OX(statement->set_origin_table_id(OB_INVALID_ID));
    if (OB_SUCC(ret) && peg::restore_table_has_target(node)) {
      ObString target_table;
      ObString target_database;
      if (OB_FAIL(resolve_table_relation_node(&peg::restore_table_target(node),
                                             target_table, target_database))) {
      } else if (ObString(OB_RECYCLEBIN_SCHEMA_NAME) == target_database
                 || ObString(OB_PUBLIC_SCHEMA_NAME) == target_database) {
        ret = OB_OP_NOT_ALLOW;
      } else {
        statement->set_new_db_name(target_database);
        statement->set_new_table_name(target_table);
      }
    }
    if (OB_SUCC(ret)) {
      if (source_database.empty()) {
        source_database = session_info_->get_database_name();
        if (source_database.empty()) {
          ret = OB_ERR_NO_DB_SELECTED;
        }
      }
      OX(statement->set_origin_db_name(source_database));
    }
  }
  return ret;
}

}
