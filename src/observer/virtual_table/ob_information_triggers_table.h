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

#ifndef OCEANBASE_SRC_OBSERVER_VIRTUAL_TABLE_OB_INFORMATION_TRIGGERS_TABLE_H_
#define OCEANBASE_SRC_OBSERVER_VIRTUAL_TABLE_OB_INFORMATION_TRIGGERS_TABLE_H_

#include "share/ob_virtual_table_scanner_iterator.h"

namespace oceanbase
{
namespace sql
{
  class ObSQLSessionInfo;
}
namespace observer
{
class ObInfoSchemaTriggersTable : public common::ObVirtualTableScannerIterator
{
private:
  enum MySQLTriggersTableColumns {
    TRIGGER_CATALOG = 16,
    TRIGGER_SCHEMA,
    TRIGGER_NAME,
    EVENT_MANIPULATION,
    EVENT_OBJECT_CATALOG,
    EVENT_OBJECT_SCHEMA,
    EVENT_OBJECT_TABLE,
    ACTION_ORDER,
    ACTION_CONDITION,
    ACTION_STATEMENT,
    ACTION_ORIENTATION,
    ACTION_TIMING,
    ACTION_REFERENCE_OLD_TABLE,
    ACTION_REFERENCE_NEW_TABLE,
    ACTION_REFERENCE_OLD_ROW,
    ACTION_REFERENCE_NEW_ROW,
    CREATED,
    SQL_MODE,
    DEFINER,
    CHARACTER_SET_CLIENT,
    COLLATION_CONNECTION,
    DATABASE_COLLATION,
  };
public:
  ObInfoSchemaTriggersTable();
  virtual ~ObInfoSchemaTriggersTable();

  virtual int inner_get_next_row(common::ObNewRow *&row);
  virtual void reset();
  inline void set_tenant_id(const uint64_t tenant_id) { tenant_id_ = tenant_id; }

private:
  uint64_t tenant_id_;
private:
  DISALLOW_COPY_AND_ASSIGN(ObInfoSchemaTriggersTable);
};
}
}

#endif // OCEANBASE_SRC_OBSERVER_VIRTUAL_TABLE_OB_INFORMATION_TRIGGERS_TABLE_H_
