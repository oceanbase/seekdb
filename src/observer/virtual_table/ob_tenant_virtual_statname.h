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

#ifndef OB_TENANT_VIRTUAL_STATNAME_H_
#define OB_TENANT_VIRTUAL_STATNAME_H_

#include "share/ob_virtual_table_scanner_iterator.h"
#include "lib/stat/ob_session_stat.h"

namespace oceanbase
{
namespace observer
{

class ObTenantVirtualStatname : public common::ObVirtualTableScannerIterator
{
public:
  ObTenantVirtualStatname();
  virtual ~ObTenantVirtualStatname();
  virtual int inner_get_next_row(common::ObNewRow *&row);
  inline void set_tenant_id(const uint64_t tenant_id) { tenant_id_ = tenant_id; }
  virtual void reset();
private:
  enum SYS_COLUMN
  {
    TENANT_ID = common::OB_APP_MIN_COLUMN_ID,
    STAT_ID,
    STATISTIC_NO,
    NAME,
    DISPLAY_NAME,
    CLASS
  };
  int32_t stat_iter_;
  uint64_t tenant_id_;
  common::ObObj cells_[common::OB_ROW_MAX_COLUMNS_COUNT];
  DISALLOW_COPY_AND_ASSIGN(ObTenantVirtualStatname);
};

} /* namespace observer */
} /* namespace oceanbase */

#endif /* OB_TENANT_VIRTUAL_STATNAME_H_ */
