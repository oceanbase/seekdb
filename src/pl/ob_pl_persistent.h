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
#ifndef OCEANBASE_PL_ROUTINE_STORAGE_H_
#define OCEANBASE_PL_ROUTINE_STORAGE_H_

#include "share/ob_define.h"
#include "ob_pl_stmt.h"
#include "pl/ob_pl_allocator.h"
#include "sql/resolver/expr/ob_raw_expr_util.h"
#include "sql/resolver/ob_stmt_resolver.h"
namespace oceanbase
{

namespace common
{
class ObIAllocator;
class ObMySQLTransaction;
}

namespace share
{
class ObDMLSqlSplicer;
}

namespace pl
{
class ObRoutinePersistentInfo
{
public:
  template<typename DependencyTable>
  static int check_dep_schema(ObSchemaGetterGuard &schema_guard,
                              const DependencyTable &dep_schema_objs,
                              int64_t merge_version,
                              bool &match);
  static int has_same_name_dependency_with_public_synonym(
                            schema::ObSchemaGetterGuard &schema_guard,
                            const ObPLDependencyTable &dep_schema_objs,
                            bool& exist,
                            ObSQLSessionInfo &session_info);                  

};

}

}
#endif
