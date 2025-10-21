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

#ifndef OCEANBASE_OBSERVER_VIRTUAL_TABLE_OB_MYSQL_DB_TABLE_
#define OCEANBASE_OBSERVER_VIRTUAL_TABLE_OB_MYSQL_DB_TABLE_

#include "share/ob_virtual_table_scanner_iterator.h"
namespace oceanbase
{
namespace share
{
namespace schema
{
class ObUserInfo;
}
}
namespace sql
{
class ObSQLSessionInfo;
}
namespace observer
{
class ObMySQLDBTable : public common::ObVirtualTableScannerIterator
{
private:
  enum MySQLDBTableColumns {
    HOST = 16,
    DB,
    USER,
    SELECT_PRIV,
    INSERT_PRIV,
    UPDATE_PRIV,
    DELETE_PRIV,
    CREATE_PRIV,
    DROP_PRIV,
    GRANT_PRIV,
    REFERENCES_PRIV,
    INDEX_PRIV,
    ALTER_PRIV,
    CREATE_TMP_TABLE_PRIV,
    LOCK_TABLE_PRIV,
    CREATE_VIEW_PRIV,
    SHOW_VIEW_PRIV,
    CREATE_ROUTINE_PRIV,
    ALTER_ROUTINE_PRIV,
    EXECUTE_PRIV,
    EVENT_PRIV,
    TRIGGER_PRIV,
  };

public:
  ObMySQLDBTable();
  virtual ~ObMySQLDBTable();
  virtual int inner_get_next_row(common::ObNewRow *&row);
  virtual void reset();

  inline void set_tenant_id(const uint64_t tenant_id) { tenant_id_ = tenant_id; }

private:
  int get_user_info(const uint64_t tenant_id,
                    const uint64_t user_id,
                    const share::schema::ObUserInfo *&user_info);
  uint64_t tenant_id_;
private:
  DISALLOW_COPY_AND_ASSIGN(ObMySQLDBTable);
};
}
}
#endif // OCEANBASE_OBSERVER_VIRTUAL_TABLE_OB_MYSQL_DB_TABLE_
