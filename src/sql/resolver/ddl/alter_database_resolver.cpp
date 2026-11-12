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
#include "sql/resolver/ddl/alter_database_resolver.h"
#include "sql/resolver/ddl/ob_alter_database_stmt.h"
#include "sql/resolver/ddl/database_options.h"

namespace oceanbase::sql {
using namespace common;

int AlterDatabaseResolver::resolve(const peg::Node &node)
{
  int ret = OB_SUCCESS;
  ObAlterDatabaseStmt *statement = nullptr;
  if (OB_ISNULL(session_info_) || OB_ISNULL(allocator_)) {
    ret = OB_ERR_UNEXPECTED;
  } else if (OB_ISNULL(statement = create_stmt<ObAlterDatabaseStmt>())) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
  } else {
    stmt_ = statement;
    ObString database_name = session_info_->get_database_name();
    if (peg::database_has_name(node)) {
      const auto text = peg::database_name(node).text();
      ObNameCaseMode mode = OB_NAME_CASE_INVALID;
      ObCollationType collation = CS_TYPE_INVALID;
      OZ(ob_write_string(*allocator_, syntax_text(text), database_name));
      OZ(session_info_->get_name_case_mode(mode));
      OZ(session_info_->get_collation_connection(collation));
      OZ(ObSQLUtils::check_and_convert_db_name(collation, mode != OB_LOWERCASE_AND_INSENSITIVE, database_name));
      CK(OB_NOT_NULL(schema_checker_));
      CK(OB_NOT_NULL(schema_checker_->get_schema_guard()));
      OZ(ObSQLUtils::cvt_db_name_to_org(*schema_checker_->get_schema_guard(), session_info_, database_name, allocator_));
    }
    OZ(statement->set_database_name(database_name));
    OZ(resolve_database_options(*statement, peg::database_options(node), &statement->get_alter_option_set()));
  }
  return ret;
}
}
