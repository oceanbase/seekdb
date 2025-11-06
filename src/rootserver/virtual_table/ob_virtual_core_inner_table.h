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

#ifndef OCEANBASE_ROOTSERVER_OB_ALL_VIRTUAL_CORE_INNER_TABLE_H_
#define OCEANBASE_ROOTSERVER_OB_ALL_VIRTUAL_CORE_INNER_TABLE_H_

#include "share/ob_virtual_table_projector.h"
#include "lib/mysqlclient/ob_mysql_proxy.h"

namespace oceanbase
{
namespace share
{
class ObCoreTableProxy;
namespace schema
{
class ObTableSchema;
class ObColumnSchemaV2;
class ObSchemaGetterGuard;
}
}
namespace rootserver
{
class ObVritualCoreInnerTable : public common::ObVirtualTableProjector
{
public:
  ObVritualCoreInnerTable();
  virtual ~ObVritualCoreInnerTable();

  int init(common::ObMySQLProxy &sql_proxy,
           const char *table_name, const uint64_t table_id,
           share::schema::ObSchemaGetterGuard *schema_guard);

  virtual int inner_open();
  virtual int inner_get_next_row(common::ObNewRow *&row);
private:
  int get_full_row(const uint64_t tenant_id,
                   const share::schema::ObTableSchema *table,
                   const share::ObCoreTableProxy &core_table,
                   common::ObIArray<Column> &columns);

  bool inited_;
  common::ObMySQLProxy *sql_proxy_;
  const char *table_name_;
  uint64_t table_id_;
  share::schema::ObSchemaGetterGuard *schema_guard_;
private:
  DISALLOW_COPY_AND_ASSIGN(ObVritualCoreInnerTable);
};

}//end namespace rootserver
}//end namespace oceanbase

#endif //OCEANBASE_ROOTSERVER_OB_ALL_VIRTUAL_CORE_INNER_TABLE_H_
