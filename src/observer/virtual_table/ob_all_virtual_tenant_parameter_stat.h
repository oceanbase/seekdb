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

#ifndef OCEANBASE_OBSERVER_VIRTUAL_TABLE_OB_ALL_VIRTUAL_TENANT_PARAMETER_STAT_H_
#define OCEANBASE_OBSERVER_VIRTUAL_TABLE_OB_ALL_VIRTUAL_TENANT_PARAMETER_STAT_H_

#include "share/ob_virtual_table_iterator.h"
#include "share/config/ob_server_config.h"
#include "observer/omt/ob_tenant_config_mgr.h"

namespace oceanbase
{
namespace observer
{

// __all_tenant_parameter_stat
//
// show all server parameter's, for every server:
// 1. if effective tenant is SYS tenant: show all tenant parameter and cluster parameter
// 2. if effective tenant is USER tenant: show self tenant parameter and cluster parameter
class ObAllVirtualTenantParameterStat : public common::ObVirtualTableIterator
{
public:
  ObAllVirtualTenantParameterStat();
  virtual ~ObAllVirtualTenantParameterStat();

  // param[in] show_seed  whether to show seed config
  int init(const bool show_seed);

  virtual int inner_open();
  virtual void reset();
  virtual int inner_get_next_row(common::ObNewRow *&row);
private:
  typedef common::ObConfigContainer::const_iterator CfgIter;

  int inner_sys_get_next_row(common::ObNewRow *&row);
  int fill_row_(common::ObNewRow *&row,
      CfgIter &iter,
      const common::ObConfigContainer &cfg_container,
      const uint64_t *tenant_id_ptr);
  enum TENANT_PARAMETER_STAT_COLUMN {
    ZONE = common::OB_APP_MIN_COLUMN_ID,
    SERVER_TYPE,
    SERVER_IP,
    SERVER_PORT,
    NAME,
    DATA_TYPE,
    VALUE,
    INFO,
    SECTION,
    SCOPE,
    SOURCE,
    EDIT_LEVEL,
    TENANT_ID,
    DEFAULT_VALUE,
    ISDEFAULT,
  };

private:
  static const int64_t DEFAULT_TENANT_COUNT = 100;

  bool inited_;
  bool show_seed_;                // whether to show seed config

  CfgIter sys_iter_;              // iterator for cluster config
private:
  DISALLOW_COPY_AND_ASSIGN(ObAllVirtualTenantParameterStat);
};
} // namespace observer
} // namespace oceanbase

#endif

