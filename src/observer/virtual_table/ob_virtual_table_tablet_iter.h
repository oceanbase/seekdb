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

#ifndef SRC_OBSERVER_VIRTUAL_TABLE_TABLET_ITER_H_
#define SRC_OBSERVER_VIRTUAL_TABLE_TABLET_ITER_H_

#include "common/row/ob_row.h"
#include "lib/guard/ob_shared_guard.h"
#include "observer/omt/ob_multi_tenant.h"
#include "share/ob_scanner.h"
#include "share/ob_virtual_table_scanner_iterator.h"
#include "share/rc/ob_tenant_base.h"
#include "observer/omt/ob_multi_tenant_operator.h"
#include "storage/meta_mem/ob_tablet_handle.h"

namespace oceanbase
{
namespace storage
{
class ObTenantTabletIterator;
}
namespace observer
{
class ObVirtualTableTabletIter : public common::ObVirtualTableScannerIterator,
                                         public omt::ObMultiTenantOperator
{
public:
  ObVirtualTableTabletIter();
  virtual ~ObVirtualTableTabletIter();
  int init(common::ObIAllocator *allocator, common::ObAddr &addr);
public:
  virtual int inner_get_next_row(common::ObNewRow *&row);
  virtual void reset();
protected:
  int get_next_tablet();
  virtual bool is_need_process(uint64_t tenant_id) override;
  virtual int process_curr_tenant(common::ObNewRow *&row) = 0;
  virtual void release_last_tenant() override;
protected:
  common::ObAddr addr_;
  storage::ObTenantTabletIterator *tablet_iter_;
  common::ObArenaAllocator tablet_allocator_;
  ObTabletHandle tablet_handle_;
  int64_t ls_id_;
  char ip_buf_[common::OB_IP_STR_BUFF];
  void *iter_buf_;
private:
  DISALLOW_COPY_AND_ASSIGN(ObVirtualTableTabletIter);
};

}
}
#endif /* SRC_OBSERVER_VIRTUAL_TABLE_TABLET_ITER_H_ */
