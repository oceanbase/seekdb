/**
 * Copyright (c) 2021 OceanBase
 * OceanBase CE is licensed under Mulan PubL v2.
 * You can use this software according to the terms and conditions of the Mulan PubL v2.
 * You may obtain a copy of Mulan PubL v2 at:
 *          http://license.coscl.org.cn/MulanPubL-2.0
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND,
 * EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
 * MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
 * See the Mulan PubL v2 for more details.
 */

#ifndef _OCEABASE_OBSERVER_VIRTUAL_TABLE_OB_VIRTUAL_OBRPC_SEND_STAT_H_
#define _OCEABASE_OBSERVER_VIRTUAL_TABLE_OB_VIRTUAL_OBRPC_SEND_STAT_H_

#include "share/ob_virtual_table_iterator.h"
#include "observer/omt/ob_multi_tenant.h"

namespace oceanbase
{
namespace observer
{

class ObVirtualObRpcSendStat
    : public common::ObVirtualTableIterator
{
public:
  ObVirtualObRpcSendStat();
  virtual ~ObVirtualObRpcSendStat();

  virtual int inner_get_next_row(common::ObNewRow *&row);
  virtual void reset();
private:
}; // end of class ObVirtualObRpcSendStat


} // end of namespace observer
} // end of namespace oceanbase

#endif /* _OCEABASE_OBSERVER_VIRTUAL_TABLE_OB_VIRTUAL_RPC_SEND_STAT_H_ */
