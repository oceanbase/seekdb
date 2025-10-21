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

#ifndef OCEANBASE_OBSERVER_VIRTUAL_PROXY_SYS_VARIABLE_H_
#define OCEANBASE_OBSERVER_VIRTUAL_PROXY_SYS_VARIABLE_H_

#include "share/ob_virtual_table_projector.h"

namespace oceanbase
{
namespace share
{
namespace schema
{
class ObTableSchema;
class ObColumnSchemaV2;
class ObMultiVersionSchemaService;
class ObSysVariableSchema;
}
}
namespace common
{
class ObMySQLProxy;
class ObServerConfig;
}
namespace observer
{
class ObVirtualProxySysVariable : public common::ObVirtualTableScannerIterator
{
public:
  ObVirtualProxySysVariable();
  virtual ~ObVirtualProxySysVariable();

  int init(share::schema::ObMultiVersionSchemaService &schema_service, common::ObServerConfig *config);

  virtual int inner_get_next_row(common::ObNewRow *&row);
  virtual void reset();

private:
  enum PROXY_SYSVAR_COLUMN
  {
    NAME = common::OB_APP_MIN_COLUMN_ID,
    TENANT_ID,
    DATA_TYPE,
    VALUE,
    FLAGS,
    MODIFIED_TIME
  };

  int get_next_sys_variable();
  int get_all_sys_variable();

  bool is_inited_;
  const share::schema::ObTableSchema *table_schema_;
  const share::schema::ObTenantSchema *tenant_info_;
  common::ObServerConfig *config_;
  const share::schema::ObSysVariableSchema *sys_variable_schema_;

  DISALLOW_COPY_AND_ASSIGN(ObVirtualProxySysVariable);
};

}//end namespace observer
}//end namespace oceanbase
#endif  /*OCEANBASE_OBSERVER_VIRTUAL_PROXY_SYS_VARIABLE_H_*/
