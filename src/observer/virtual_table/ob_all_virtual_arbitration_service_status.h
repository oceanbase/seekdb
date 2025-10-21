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

#ifndef OCEANBASE_OBSERVER_VIRTUAL_TABLE_OB_ALL_VIRTUAL_ARB_SERVICE_STATUS_
#define OCEANBASE_OBSERVER_VIRTUAL_TABLE_OB_ALL_VIRTUAL_ARB_SERVICE_STATUS_
#include "share/ob_virtual_table_scanner_iterator.h"
#include "share/ob_scanner.h"
#include "common/row/ob_row.h"

namespace oceanbase
{
namespace observer
{
class ObAllVirtualArbServiceStatus : public common::ObVirtualTableScannerIterator
{
public:
  ObAllVirtualArbServiceStatus();
  virtual ~ObAllVirtualArbServiceStatus();
public:
  virtual int inner_get_next_row(common::ObNewRow *&row);
  void destroy();
private:
#ifdef OB_BUILD_ARBITRATION
  int insert_row_(const common::ObAddr &self,
      const common::ObAddr &arb_service_addr,
      const bool is_in_blacklist,
      common::ObNewRow *row);
  void reset_buf_();
#endif
private:
  static const int64_t VARCHAR_32 = 32;
  static const int64_t VARCHAR_128 = 128;
private:
#ifdef OB_BUILD_ARBITRATION
  char ip_[common::OB_IP_PORT_STR_BUFF] = {'\0'};
  char arb_service_addr_buf_[VARCHAR_128] = {'\0'};
  char arb_service_status_buf_[VARCHAR_32] = {'\0'};
#endif
};
}//namespace observer
}//namespace oceanbase
#endif
