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

#ifndef _OB_ALL_VIRTUAL_SYS_VARIABLE_DEFAULT_VALUE_H_
#define _OB_ALL_VIRTUAL_SYS_VARIABLE_DEFAULT_VALUE_H_
#include "share/ob_virtual_table_scanner_iterator.h"
namespace oceanbase
{
namespace common
{
class ObMySQLProxy;
}
namespace share
{
namespace schema
{
class ObTenantSchema;
class ObSysVariableSchema;
}
}
namespace sql
{
class ObSQLSessionInfo;
}
namespace observer
{
class ObSysVarDefaultValue : public common::ObVirtualTableScannerIterator
{
public:
  ObSysVarDefaultValue();
  virtual ~ObSysVarDefaultValue();
  virtual int inner_get_next_row(common::ObNewRow *&row);
  virtual void reset();
  int add_row();
private:
  enum COLUMN_NAME
  {
    NAME = common::OB_APP_MIN_COLUMN_ID,
    DEFAULT_VALUE
  };
  DISALLOW_COPY_AND_ASSIGN(ObSysVarDefaultValue);
};
}
}
#endif /* _OB_ALL_VIRTUAL_SYS_VARIABLE_DEFAULT_VALUE_H_*/
