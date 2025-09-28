/**
 * Copyright (c) 2024 OceanBase
 * OceanBase CE is licensed under Mulan PubL v2.
 * You can use this software according to the terms and conditions of the Mulan PubL v2.
 * You may obtain a copy of Mulan PubL v2 at:
 *          http://license.coscl.org.cn/MulanPubL-2.0
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND,
 * EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
 * MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
 * See the Mulan PubL v2 for more details.
 */

#ifndef OB_ALL_VIRTUAL_LOG_TRANSPORT_DEST_STAT_H_
#define OB_ALL_VIRTUAL_LOG_TRANSPORT_DEST_STAT_H_

#include "observer/omt/ob_multi_tenant.h"
#include "share/ob_virtual_table_scanner_iterator.h"
#include "share/ob_scanner.h"

namespace oceanbase
{

namespace observer
{
class ObAllVirtualLogTransportDestStat: public common::ObVirtualTableScannerIterator
{
public:
  explicit ObAllVirtualLogTransportDestStat(omt::ObMultiTenant *omt);
  virtual ~ObAllVirtualLogTransportDestStat();
public:
  virtual int inner_get_next_row(common::ObNewRow *&row);
  void destroy();
};
}
}

#endif