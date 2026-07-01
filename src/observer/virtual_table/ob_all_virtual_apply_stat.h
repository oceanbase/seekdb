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

#ifndef OCEANBASE_OBSERVER_OB_ALL_VIRTUAL_APPLY_STAT_H_
#define OCEANBASE_OBSERVER_OB_ALL_VIRTUAL_APPLY_STAT_H_

#include "common/row/ob_row.h"
#include "observer/omt/ob_multi_tenant.h"
#include "share/ob_virtual_table_scanner_iterator.h"
#include "share/ob_scanner.h"
#include "logservice/applyservice/ob_log_apply_service.h"

namespace oceanbase
{
namespace observer
{
class ObAllVirtualApplyStat : public common::ObVirtualTableScannerIterator
{
public:
  explicit ObAllVirtualApplyStat(omt::ObMultiTenant *omt) : omt_(omt) {}
public:
  virtual int inner_get_next_row(common::ObNewRow *&row);
private:
  int insert_stat_(logservice::LSApplyStat &apply_stat);
private:
  static const int64_t VARCHAR_32 = 32;
  char role_str_[VARCHAR_32] = {'\0'};
  char ip_[common::OB_IP_PORT_STR_BUFF] = {'\0'};
  omt::ObMultiTenant *omt_;
};
} // namespace observer
} // namespace oceanbase
#endif /* OCEANBASE_OBSERVER_OB_ALL_VIRTUAL_APPLY_STAT_H_ */
