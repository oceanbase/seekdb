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

#ifndef OCEANBASE_OBSERVER_VIRTUAL_TABLE_OB_TABLE_PRIVILEGES_
#define OCEANBASE_OBSERVER_VIRTUAL_TABLE_OB_TABLE_PRIVILEGES_

#include "share/ob_virtual_table_scanner_iterator.h"
#include "share/schema/ob_priv_type.h"

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
class ObTablePriv;
}
}
namespace observer
{
class ObInfoSchemaTablePrivilegesTable : public common::ObVirtualTableScannerIterator
{
public:
  class StaticInit {
  public:
    StaticInit();
  };
  friend class ObInfoSchemaTablePrivilegesTable::StaticInit;

  ObInfoSchemaTablePrivilegesTable();
  virtual ~ObInfoSchemaTablePrivilegesTable();

  virtual void reset();
  virtual int inner_get_next_row(common::ObNewRow *&row);

  inline void set_tenant_id(uint64_t tenant_id) { tenant_id_ = tenant_id; }
  inline void set_user_id(uint64_t user_id) { user_id_ = user_id; }

private:
  enum TABLE_PRIVS_COLUMN
  {
    GRANTEE = 16,
    TABLE_CATALOG,
    TABLE_SCHEMA,
    TABLE_NAME,
    PRIVILEGE_TYPE,
    IS_GRANTABLE,
    MAX_TABLE_PRIVS_COLUMN
  };
  enum {
    MAX_COL_COUNT = 6,
    USERNAME_AUX_LEN = 6// "''@''" + '\0'
  };

  int get_table_privs(const uint64_t tenant_id,
                      const uint64_t user_id,
                      common::ObArray<const share::schema::ObTablePriv *> &table_privs);
  int get_user_name_from_table_priv(const share::schema::ObTablePriv *table_priv,
                                    common::ObString &user_name,
                                    common::ObString &host_name);
  int fill_row_with_table_priv(const share::schema::ObTablePriv *table_priv);

  static const char *priv_type_strs[OB_PRIV_MAX_SHIFT + 1];
  uint64_t tenant_id_;
  uint64_t user_id_;

  DISALLOW_COPY_AND_ASSIGN(ObInfoSchemaTablePrivilegesTable);
};
}
}
#endif /* OCEANBASE_OBSERVER_VIRTUAL_TABLE_OB_TABLE_PRIVILEGES_ */
