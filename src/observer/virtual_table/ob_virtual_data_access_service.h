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

#ifndef OCEANBASE_OB_VIRTUAL_DATA_ACCESS_SERVICE_H_
#define OCEANBASE_OB_VIRTUAL_DATA_ACCESS_SERVICE_H_

#include "share/ob_i_tablet_scan.h"
#include "ob_virtual_table_iterator_factory.h"

namespace oceanbase
{
namespace common
{
class ObVTableScanParam;
class ObNewRowIterator;
class ObServerConfig;
}
namespace rootserver
{
class ObRootService;
}
namespace observer
{
class ObVirtualDataAccessService : public common::ObITabletScan
{
public:
  ObVirtualDataAccessService(
      rootserver::ObRootService &root_service,
      common::ObAddr &addr,
      common::ObServerConfig *config)
      : vt_iter_factory_(root_service, addr, config)
  {
  }
  virtual ~ObVirtualDataAccessService() {}

  virtual int table_scan(common::ObVTableScanParam &param, common::ObNewRowIterator *&result);

  virtual int revert_scan_iter(common::ObNewRowIterator *iter);

  ObVirtualTableIteratorFactory &get_vt_iter_factory() { return vt_iter_factory_; }
  virtual int check_iter(common::ObVTableScanParam &param);
private:
  ObVirtualTableIteratorFactory vt_iter_factory_;
  DISALLOW_COPY_AND_ASSIGN(ObVirtualDataAccessService);
};
}
}
#endif
