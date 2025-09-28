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

#include "ob_all_virtual_log_transport_dest_stat.h"
#include "logservice/ob_log_service.h"

namespace oceanbase
{
namespace observer
{
ObAllVirtualLogTransportDestStat::ObAllVirtualLogTransportDestStat(omt::ObMultiTenant *omt) {}

ObAllVirtualLogTransportDestStat::~ObAllVirtualLogTransportDestStat()
{
  destroy();
}

int ObAllVirtualLogTransportDestStat::inner_get_next_row(common::ObNewRow *&row)
{
  return OB_ITER_END;
}

void ObAllVirtualLogTransportDestStat::destroy() {}
}
}