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

#ifndef OCEANBASE_OBSERVER_VIRTUAL_TABLE_OB_TENANT_SHOW_TABLES_
#define OCEANBASE_OBSERVER_VIRTUAL_TABLE_OB_TENANT_SHOW_TABLES_

#include "share/ob_virtual_table_iterator.h"
#include "common/ob_range.h"
#include "lib/container/ob_se_array.h"
#include "lib/ob_define.h"
using oceanbase::common::OB_APP_MIN_COLUMN_ID;
namespace oceanbase
{
namespace sql
{
class ObSQLSessionInfo;
}
namespace common
{
class ObMySQLProxy;
}
namespace share
{
namespace schema
{
class ObSimpleTableSchemaV2;
}
}
namespace observer
{
class ObTenantShowTables : public common::ObVirtualTableIterator
{
  enum TENANT_ALL_TABLES_COLUMN
  {
    DATABASE_ID = OB_APP_MIN_COLUMN_ID,
    TABLE_NAME = OB_APP_MIN_COLUMN_ID + 1,
    TABLE_TYPE = OB_APP_MIN_COLUMN_ID + 2,
  };
public:
  ObTenantShowTables();
  virtual ~ObTenantShowTables();
  virtual int inner_open();
  virtual int inner_get_next_row(common::ObNewRow *&row);
  virtual void reset();
  inline void set_tenant_id(uint64_t tenant_id) { tenant_id_ = tenant_id; }
private:
  int inner_get_next_row();
  int fetch_catalog_table_schemas_(const uint64_t tenant_id,
                                   const uint64_t database_id,
                                   common::ObString &database_name,
                                   common::ObIArray<const share::schema::ObSimpleTableSchemaV2 *> &table_schemas);
private:
  uint64_t tenant_id_;
  uint64_t database_id_;
  common::ObString database_name_;
  common::ObSEArray<const share::schema::ObSimpleTableSchemaV2 *, 128> table_schemas_;
  int64_t table_schema_idx_;
  DISALLOW_COPY_AND_ASSIGN(ObTenantShowTables);
};

}// observer
}// oceanbase
#endif /* OCEANBASE_OBSERVER_VIRTUAL_TABLE_OB_TENANT_SHOW_TABLES_ */
