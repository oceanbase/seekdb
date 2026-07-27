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

#ifndef SRC_OBSERVER_VIRTUAL_TABLE_OB_ALL_VIRTUAL_TABLET_POINTER_STATUS_H_
#define SRC_OBSERVER_VIRTUAL_TABLE_OB_ALL_VIRTUAL_TABLET_POINTER_STATUS_H_

#include "common/row/ob_row.h"
#include "lib/guard/ob_shared_guard.h"
#include "observer/omt/ob_server_runtime_controller.h"
#include "sql/ob_scanner.h"
#include "observer/virtual_table/ob_virtual_table_scanner_iterator.h"
#include "share/rc/ob_server_runtime.h"
#include "storage/meta_mem/ob_storage_meta_mem_mgr.h"
#include "storage/meta_mem/ob_tablet_pointer_handle.h"

namespace oceanbase
{
namespace observer
{

class ObAllVirtualTabletPtr : public common::ObVirtualTableScannerIterator
{
private:
  enum COLUMN_ID_LIST
  {
        TABLET_ID = common::OB_APP_MIN_COLUMN_ID,
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
  int init(common::ObIAllocator *allocator);
  virtual int inner_get_next_row(common::ObNewRow *&row);
  virtual void reset();
private:
  int get_next_tablet_pointer(
      ObTabletMapKey &tablet_key,
      ObTabletPointerHandle &pointer_handle,
      ObTabletHandle &tablet_handle);

private:
  static const int64_t STR_LEN = 128;
  static const int64_t ADDR_STR_LEN = 256;
private:
  char address_[ADDR_STR_LEN];
  char pointer_[STR_LEN];
  char old_chain_[STR_LEN];
  storage::ObTabletPtrWithInMemObjIterator *tablet_iter_;
  void *iter_buf_;
private:
  DISALLOW_COPY_AND_ASSIGN(ObAllVirtualTabletPtr);
};

}
}

#endif
