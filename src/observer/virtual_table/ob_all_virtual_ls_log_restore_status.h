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

#ifndef OCEANBASE_OBSERVER_OB_ALL_VIRTUAL_LS_LOG_RESTORE_STATUS_H_
#define OCEANBASE_OBSERVER_OB_ALL_VIRTUAL_LS_LOG_RESTORE_STATUS_H_

#include "share/ob_virtual_table_projector.h"

namespace oceanbase
{
namespace observer
{
class ObVirtualLSLogRestoreStatus : public common::ObVirtualTableProjector
{
public:
  ObVirtualLSLogRestoreStatus();
  virtual ~ObVirtualLSLogRestoreStatus();
  int init(omt::ObMultiTenant *omt);
  virtual int inner_get_next_row(common::ObNewRow *&row);
  void destroy(); 
};

}//end namespace observer
}//end namespace oceanbase
#endif //OCEANBASE_OBSERVER_OB_ALL_VIRTUAL_LS_LOG_RESTORE_STATUS_H_
