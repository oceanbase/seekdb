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

#ifndef OCEANBASE_OBSERVER_VIRTUAL_TABLE_OB_INFORMATION_TABLE_CONSTRAINTS_
#define OCEANBASE_OBSERVER_VIRTUAL_TABLE_OB_INFORMATION_TABLE_CONSTRAINTS_
#include "share/ob_virtual_table_scanner_iterator.h"

namespace oceanbase
{
namespace common
{
class ObObj;

}
namespace share
{
namespace schema
{
class ObTableSchema;
class ObDatabaseSchema;

}
}

namespace observer
{
static const common::ObString PRIMARY_KEY_CONSTRAINT_TYPE = "PRIMARY KEY";
static const common::ObString PRIMARY_KEY_CONSTRAINT_NAME = "PRIMARY";
static const common::ObString UNIQUE_CONSTRAINT_TYPE = "UNIQUE";
static const common::ObString CHECK_CONSTRAINT_TYPE = "CHECK";
static const common::ObString FOREIGN_KEY_CONSTRAINT_TYPE = "FOREIGN KEY";

class ObInfoSchemaTableConstraintsTable : public common::ObVirtualTableScannerIterator
{
public:
  ObInfoSchemaTableConstraintsTable();
  virtual ~ObInfoSchemaTableConstraintsTable();
  virtual int inner_get_next_row(common::ObNewRow *&row);
  virtual void reset();

  inline void set_tenant_id(uint64_t tenant_id)
  {
    tenant_id_ = tenant_id;
  }

private:
  int add_table_constraints(const share::schema::ObDatabaseSchema &database_schema,
                            common::ObObj *cells,
                            const int64_t col_count);
  int add_table_constraints(const share::schema::ObTableSchema &table_schema,
                           const common::ObString &database_name,
                           common::ObObj *cells,
                           const int64_t col_count);
  int add_rowkey_constraints(const share::schema::ObTableSchema &table_schema,
                             const common::ObString &database_name,
                             common::ObObj *cells,
                             const int64_t col_count);
  int add_index_constraints(const share::schema::ObTableSchema &table_schema,
                            const common::ObString &database_name,
                            common::ObObj *cells,
                            const int64_t col_count);
  int add_foreign_key_constraints(const share::schema::ObTableSchema &table_schema,
                                  const common::ObString &database_name,
                                  common::ObObj *cells,
                                  const int64_t col_count);
  int add_check_constraints(const share::schema::ObTableSchema &table_schema,
                            const common::ObString &database_name,
                            common::ObObj *cells,
                            const int64_t col_count);

  uint64_t tenant_id_;
private:
  enum TABLE_CONSTRAINTS_COLUMN
  {
    CONSTRAINT_CATALOG = common::OB_APP_MIN_COLUMN_ID,
    CONSTRAINT_SCHEMA,
    CONSTRAINT_NAME,
    TABLE_SCHEMA,
    TABLE_NAME,
    CONSTRAINT_TYPE,
    ENFORCED,
    MAX_TABLE_CONSTRAINTS_COLUMN
  };
  static const int64_t TABLE_CONSTRAINTS_COLUMN_COUNT = 7;
  DISALLOW_COPY_AND_ASSIGN(ObInfoSchemaTableConstraintsTable);
};


}
}
#endif /* OCEANBASE_OBSERVER_VIRTUAL_TABLE_OB_INFORMATION_TABLE_CONSTRAINTS_ */
