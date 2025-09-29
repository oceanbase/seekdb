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

#ifndef SRC_OBSERVER_VIRTUAL_TABLE_OB_ALL_VIRTUAL_TABLET_POINTER_STATUS_H_
#define SRC_OBSERVER_VIRTUAL_TABLE_OB_ALL_VIRTUAL_TABLET_POINTER_STATUS_H_

#include "common/row/ob_row.h"
#include "lib/guard/ob_shared_guard.h"
#include "observer/omt/ob_multi_tenant.h"
#include "share/ob_scanner.h"
#include "share/ob_virtual_table_scanner_iterator.h"
#include "share/rc/ob_tenant_base.h"
#include "observer/omt/ob_multi_tenant_operator.h"
#include "storage/meta_mem/ob_tenant_meta_mem_mgr.h"
#include "storage/meta_mem/ob_tablet_pointer_handle.h"

namespace oceanbase
{
namespace storage
{
class ObTenantTabletIterator;
}

namespace observer
{

class ObAllVirtualTabletPtr : public common::ObVirtualTableScannerIterator,
                              public omt::ObMultiTenantOperator
{
private:
  enum COLUMN_ID_LIST
  {
    SVR_IP = common::OB_APP_MIN_COLUMN_ID,
    SVR_PORT,
    TENANT_ID,
    LS_ID,
    TABLET_ID,
    ADDRESS,
    POINTER_REF,
    IN_MEMORY,
    TABLET_REF,
    WASH_SCORE,
    TABLET_PTR,
    INITIAL_STATE,
    OLD_CHAIN,
    DATA_OCCUPIED,
    DATA_REQUIRED
  };
public:
  ObAllVirtualTabletPtr();
  virtual ~ObAllVirtualTabletPtr();
  int init(common::ObIAllocator *allocator, common::ObAddr &addr);
  virtual int inner_get_next_row(common::ObNewRow *&row);
  virtual void reset();
private:
  // Filter to get the tenants that need processing
  virtual bool is_need_process(uint64_t tenant_id) override;
  // Process the tenant of the current iteration
  virtual int process_curr_tenant(common::ObNewRow *&row) override;
  // Release the resources of the previous tenant
  virtual void release_last_tenant() override;
  int get_next_tablet_pointer(
      ObTabletMapKey &tablet_key,
      ObTabletPointerHandle &pointer_handle,
      ObTabletHandle &tablet_handle);

private:
  static const int64_t STR_LEN = 128;
  static const int64_t ADDR_STR_LEN = 256;
private:
  common::ObAddr addr_;
  char ip_buf_[common::OB_IP_STR_BUFF];
  char address_[ADDR_STR_LEN];
  char pointer_[STR_LEN];
  char old_chain_[STR_LEN];
  /* The resources accessed across tenants must be handled and released by ObMultiTenantOperator */
  storage::ObTenantTabletPtrWithInMemObjIterator *tablet_iter_;
  void *iter_buf_;
private:
  DISALLOW_COPY_AND_ASSIGN(ObAllVirtualTabletPtr);
};

}
}

#endif
