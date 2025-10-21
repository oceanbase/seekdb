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

#ifndef SRC_OBSERVER_VIRTUAL_TABLE_OB_ALL_VIRTUAL_TABLE_MGR_H_
#define SRC_OBSERVER_VIRTUAL_TABLE_OB_ALL_VIRTUAL_TABLE_MGR_H_

#include "common/row/ob_row.h"
#include "lib/guard/ob_shared_guard.h"
#include "observer/omt/ob_multi_tenant.h"
#include "share/ob_scanner.h"
#include "share/ob_virtual_table_scanner_iterator.h"
#include "share/rc/ob_tenant_base.h"
#include "storage/meta_mem/ob_tablet_handle.h"
#include "storage/ob_i_table.h"
#include "storage/meta_mem/ob_tablet_handle.h"
#include "observer/omt/ob_multi_tenant_operator.h"

namespace oceanbase
{
namespace storage
{
class ObTenantTabletIterator;
}
namespace observer
{
class ObAllVirtualTableMgr : public common::ObVirtualTableScannerIterator,
                             public omt::ObMultiTenantOperator
{
  enum COLUMN_ID_LIST
  {
    SVR_IP = common::OB_APP_MIN_COLUMN_ID,
    SVR_PORT,
    TENANT_ID,
    LS_ID,
    TABLE_TYPE,
    TABLET_ID,
    START_LOG_SCN,
    END_LOG_SCN,
    UPPER_TRANS_VERSION,
    SIZE,
    DATA_BLOCK_CNT,
    INDEX_BLOCK_CNT,
    LINKED_BLOCK_CNT,
    REF,
    IS_ACTIVE,
    CONTAIN_UNCOMMITTED_ROW,
    NESTED_OFFSET,
    NESTED_SIZE,
    CG_IDX,
    DATA_CHECKSUM,
    TABLE_FLAG,
    REC_SCN
  };
public:
  ObAllVirtualTableMgr();
  virtual ~ObAllVirtualTableMgr();
  int init(common::ObIAllocator *allocator);
public:
  virtual int inner_get_next_row(common::ObNewRow *&row);
  virtual void reset();
  inline void set_addr(common::ObAddr &addr) { addr_ = addr; }
private:
  // Filter to get the tenants that need processing
  virtual bool is_need_process(uint64_t tenant_id) override;
  // Process the tenant of the current iteration
  virtual int process_curr_tenant(common::ObNewRow *&row) override;
  // Release the resources of the previous tenant
  virtual void release_last_tenant() override;

  int get_next_tablet();
  int get_next_table(storage::ObITable *&table);
private:
  common::ObAddr addr_;
  storage::ObTenantTabletIterator *tablet_iter_;
  common::ObArenaAllocator tablet_allocator_;
  ObTabletHandle tablet_handle_;
  int64_t ls_id_;
  char ip_buf_[common::OB_IP_STR_BUFF];
  storage::ObTableStoreIterator table_store_iter_;
  void *iter_buf_;
private:
  DISALLOW_COPY_AND_ASSIGN(ObAllVirtualTableMgr);
};

}
}
#endif /* SRC_OBSERVER_VIRTUAL_TABLE_OB_ALL_VIRTUAL_TABLE_MGR_H_ */
