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

#ifndef OCEANBASE_QUERY_DDL_OB_DDL_SCHEMA_SERVICE_H_
#define OCEANBASE_QUERY_DDL_OB_DDL_SCHEMA_SERVICE_H_

namespace oceanbase
{
namespace common
{
class ObMySQLTransaction;
}
namespace obcall
{
struct ObCreateIndexArg;
}
namespace share
{
namespace schema
{
class ObColumnSchemaV2;
class ObSchemaGetterGuard;
class ObTableSchema;
}
}
namespace query
{

class ObIAuxIndexSchemaChecker
{
public:
  virtual ~ObIAuxIndexSchemaChecker() = default;
  virtual int check_aux_index_schema_exist(
      const obcall::ObCreateIndexArg &arg,
      share::schema::ObSchemaGetterGuard &schema_guard,
      const share::schema::ObTableSchema *data_schema,
      bool &is_exist,
      const share::schema::ObTableSchema *&index_schema) = 0;
};

class ObIColumnSchemaWriter
{
public:
  virtual ~ObIColumnSchemaWriter() = default;
  virtual int insert_single_column(
      common::ObMySQLTransaction &trans,
      const share::schema::ObTableSchema &new_table_schema,
      share::schema::ObColumnSchemaV2 &new_column) = 0;
};

int sort_table_partition_info(share::schema::ObTableSchema &table_schema);

} // namespace query
} // namespace oceanbase

#endif // OCEANBASE_QUERY_DDL_OB_DDL_SCHEMA_SERVICE_H_
