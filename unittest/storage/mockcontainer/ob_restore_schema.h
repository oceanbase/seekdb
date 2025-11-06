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

#ifndef OB_RESTORE_SCHEMA_
#define OB_RESTORE_SCHEMA_
#include "share/schema/ob_schema_getter_guard.h"
#include "sql/parser/parse_node.h"
#include "../../share/schema/mock_schema_service.h"
namespace oceanbase
{
using namespace common;
#if 0
namespace share
{
namespace schema
{
class MockSchemaService;
}
}
#endif
using namespace share;
using namespace share::schema;
namespace sql
{
class ObStmt;
class ObCreateIndexStmt;
struct ObResolverParams;
class ObRestoreSchema
{
public:
  ObRestoreSchema();
  virtual ~ObRestoreSchema() = default;
  int parse_from_file(const char *filename,
                      share::schema::ObSchemaGetterGuard *&schema_guard);
  share::schema::ObSchemaGetterGuard &get_schema_guard() { return schema_guard_; }
  //share::schema::ObSchemaManager *get_schema_manager();
  int init();

public:
  static const int64_t RESTORE_SCHEMA_VERSION = 1;
private:
  // disallow copy
  DISALLOW_COPY_AND_ASSIGN(ObRestoreSchema);
protected:
  // function members
  int do_parse_line(common::ObArenaAllocator &allocator, const char *query);
  int do_create_table(ObStmt *stmt);
  int do_create_index(ObStmt *stmt);
  int gen_columns(ObCreateIndexStmt &stmt,
                  share::schema::ObTableSchema &index_schema);
  int do_resolve_single_stmt(ParseNode *node,
                             ObResolverParams &ctx);
  int add_database_schema(ObDatabaseSchema &database_schema);
  int add_table_schema(ObTableSchema &table_schema);
public:
  // data members
  //share::schema::ObSchemaManager schema_manager_;
  share::schema::MockSchemaService *schema_service_;
  share::schema::ObSchemaGetterGuard schema_guard_;
  //int64_t schema_version_;
  uint64_t table_id_;
  uint64_t tenant_id_;
  uint64_t database_id_;
};
}
}
#endif
