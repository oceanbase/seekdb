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

#ifndef OCEANBASE_OBSERVER_VIRTUAL_TABLE_OB_ALL_VIRTUAL_SYS_PARAMETER_STAT_H_
#define OCEANBASE_OBSERVER_VIRTUAL_TABLE_OB_ALL_VIRTUAL_SYS_PARAMETER_STAT_H_

#include "share/ob_virtual_table_iterator.h"
#include "share/config/ob_server_config.h"
#include "share/config/ob_config_helper.h"
#include "observer/omt/ob_tenant_config_mgr.h"

namespace oceanbase
{
namespace observer
{
class ObAllVirtualSysParameterStat : public common::ObVirtualTableIterator
{
public:
  ObAllVirtualSysParameterStat();
  virtual ~ObAllVirtualSysParameterStat();
  virtual int inner_open();
  virtual void reset();
  virtual int inner_get_next_row(common::ObNewRow *&row);
private:
  int inner_sys_get_next_row(common::ObNewRow *&row);

  enum SYS_PARAMETER_STAT_COLUMN {
    ZONE = common::OB_APP_MIN_COLUMN_ID,
    SERVER_TYPE,
    SERVER_IP,
    SERVER_PORT,
    NAME,
    DATA_TYPE,
    VALUE,
    VALUE_STRICT,
    INFO,
    NEED_REBOOT,
    SECTION,
    VISIBLE_LEVEL,
    SCOPE,
    SOURCE,
    EDIT_LEVEL,
    DEFAULT_VALUE,
    ISDEFAULT,
  };
  common::ObConfigContainer::const_iterator sys_iter_;
  omt::ObTenantConfigGuard tenant_config_;
  common::ObConfigContainer::const_iterator tenant_iter_;
  DISALLOW_COPY_AND_ASSIGN(ObAllVirtualSysParameterStat);
};
} // namespace observer
} // namespace oceanbase

#endif // OCEANBASE_OBSERVER_VIRTUAL_TABLE_OB_ALL_VIRTUAL_SYS_PARAMETER_STAT_H_
