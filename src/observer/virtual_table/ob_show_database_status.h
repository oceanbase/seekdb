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

#ifndef OCEANBASE_OBSERVER_VIRTUAL_TABLE_OB_SHOW_DATABASE_STATUS_
#define OCEANBASE_OBSERVER_VIRTUAL_TABLE_OB_SHOW_DATABASE_STATUS_

#include "share/ob_virtual_table_scanner_iterator.h"
#include "share/ob_scanner.h"
#include "common/row/ob_row.h"

namespace oceanbase
{
namespace sql
{
class ObSQLSessionInfo;
}
namespace share
{
namespace schema
{
class ObDatabaseSchema;
}
}
namespace observer
{

class ObShowDatabaseStatus : public common::ObVirtualTableScannerIterator
{
public:
  ObShowDatabaseStatus();
  virtual ~ObShowDatabaseStatus();

  virtual int inner_get_next_row(common::ObNewRow *&row);
  virtual void reset();

  inline void set_tenant_id(const uint64_t tenant_id) { tenant_id_ = tenant_id; }

  int add_database_status(const common::ObAddr &server_addr,
                          const share::schema::ObDatabaseSchema &database_schema,
                          common::ObObj *cells,
                          const int64_t col_count);
  int add_all_database_status();
private:
  uint64_t tenant_id_;
private:
  enum DATABASE_STATUS_COLUMN
  {
    DATABASE_NAME = common::OB_APP_MIN_COLUMN_ID,
    HOST,
    PORT,
    READ_ONLY,
    MAX_DATABASE_STATUS_COLUMN
  };
  static const int64_t DATABASE_STATUS_COLUMN_COUNT = 4;
  DISALLOW_COPY_AND_ASSIGN(ObShowDatabaseStatus);
};

}
}
#endif /* OCEANBASE_OBSERVER_VIRTUAL_TABLE_OB_SHOW_DATABASE_STATUS_ */
