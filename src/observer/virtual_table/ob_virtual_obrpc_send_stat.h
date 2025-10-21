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
