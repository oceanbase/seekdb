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
#include "sql/resolver/ddl/restore_database_resolver.h"
#include "sql/resolver/ddl/ob_recyclebin_restore_stmt.h"

namespace oceanbase::sql {
using namespace common;

int RestoreDatabaseResolver::resolve(const peg::Node &node)
{
  int ret = OB_SUCCESS;
  ObRecyclebinRestoreDatabaseStmt *statement = nullptr;
  if (OB_ISNULL(session_info_) || OB_ISNULL(allocator_)) {
    ret = OB_ERR_UNEXPECTED;
  } else if (OB_ISNULL(statement = create_stmt<ObRecyclebinRestoreDatabaseStmt>())) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
  } else {
    stmt_ = statement;
    const auto definition = peg::restore_database_definition(node);
    auto copy_name = [&](rust::Slice<const uint8_t> text, ObString &name) {
      int ret = OB_SUCCESS;
      if (text.size() > OB_MAX_DATABASE_NAME_LENGTH) {
        ret = OB_ERR_TOO_LONG_IDENT;
        LOG_USER_ERROR(OB_ERR_TOO_LONG_IDENT, static_cast<int>(text.size()), text.data());
      } else {
        ret = ob_write_string(*allocator_, syntax_text(text), name);
      }
      return ret;
    };
    ObString source;
    ObString target;
    OZ(copy_name(definition.source, source));
    OZ(copy_name(definition.target, target));
    OX(statement->set_origin_db_name(source));
    OX(statement->set_new_db_name(target));
  }
  return ret;
}

}
