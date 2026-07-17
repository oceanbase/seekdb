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

#ifndef OCEANBASE_ROOTSERVER_OB_SCHEMA2DDL_SQL_H_
#define OCEANBASE_ROOTSERVER_OB_SCHEMA2DDL_SQL_H_

#include <stdint.h>

namespace oceanbase
{

namespace share
{
namespace schema
{
class ObTableSchema;
class ObColumnSchemaV2;
}
}

namespace rootserver
{

// Convert table schema to create table sql for creating table in mysql server.
class ObSchema2DDLSql
{
public:
  ObSchema2DDLSql();
  virtual ~ObSchema2DDLSql();

  static int convert(const share::schema::ObTableSchema &table_schema,
                     char *sql_buf, const int64_t buf_size);

private:
  static int type2str(const share::schema::ObColumnSchemaV2 &column_schema,
                      char *buf, const int64_t buf_size);
};
} // end namespace common
} // end namespace oceanbase
#endif // OCEANBASE_ROOTSERVER_OB_SCHEMA2DDL_SQL_H_
