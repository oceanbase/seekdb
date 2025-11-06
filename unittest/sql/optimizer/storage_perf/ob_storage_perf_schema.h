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

#ifndef TOOLS_STORAGE_PERF_TEST_SCHEMA_H_
#define TOOLS_STORAGE_PERF_TEST_SCHEMA_H_
#include <fstream>
#include <iterator>

#include "sql/parser/ob_parser.h"
#include "sql/resolver/ob_resolver.h"
#include "lib/allocator/page_arena.h"
#include "lib/json/ob_json_print_utils.h"
#include "sql/resolver/ddl/ob_create_table_stmt.h"
#include "sql/resolver/ddl/ob_create_index_stmt.h"
#include "sql/session/ob_sql_session_info.h"
#include "share/system_variable/ob_system_variable.h"
#include "sql/parser/parse_node.h"
#include "storage/ob_partition_storage.h"
#include "storage/mockcontainer/ob_restore_schema.h"

namespace oceanbase
{
using namespace sql;
using namespace share::schema;

namespace storageperf
{

class MySchemaService : public oceanbase::share::schema::ObMultiVersionSchemaService
{
public:
  int init(const char *file_name);
  int add_schema(const char *file_name);
  void get_schema_guard(ObSchemaGetterGuard *&schema_guard) {schema_guard = schema_guard_;}
private:
  ObRestoreSchema restore_schema_;
  ObSchemaGetterGuard *schema_guard_;
};

}//end storageperf
}//end oceanbase

#endif //TOOLS_STORAGE_PERF_SCHEMA_H_
