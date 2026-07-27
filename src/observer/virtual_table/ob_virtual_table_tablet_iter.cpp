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
#include "observer/virtual_table/ob_virtual_table_tablet_iter.h"
#include "share/rc/ob_module_provider.h"
#include "storage/tx_storage/ob_ls_service.h"

using namespace oceanbase;
using namespace common;
using namespace memtable;
using namespace storage;
using namespace observer;

ObVirtualTableTabletIter::ObVirtualTableTabletIter()
    : ObVirtualTableScannerIterator(),
      addr_(),
      tablet_iter_(nullptr),
      tablet_allocator_("VTTable"),
      tablet_handle_(),
      iter_buf_(nullptr)
{
}

ObVirtualTableTabletIter::~ObVirtualTableTabletIter()
{
  reset();
}

void ObVirtualTableTabletIter::reset()
{
  addr_.reset();
  if (OB_NOT_NULL(tablet_iter_)) {
    tablet_iter_->~ObTabletIterator();
    tablet_iter_ = nullptr;
  }
  if (OB_NOT_NULL(iter_buf_)) {
    allocator_->free(iter_buf_);
    iter_buf_ = nullptr;
  }
  tablet_handle_.reset();
  tablet_allocator_.reset();
  ObVirtualTableScannerIterator::reset();
}

int ObVirtualTableTabletIter::init(
    common::ObIAllocator *allocator,
    common::ObAddr &addr)
{
  int ret = OB_SUCCESS;
  if (start_to_read_) {
    ret = OB_INIT_TWICE;
    SERVER_LOG(WARN, "cannot init twice", K(ret));
  } else if (OB_ISNULL(allocator)) {
    ret = OB_INVALID_ARGUMENT;
    SERVER_LOG(WARN, "invalid argument", K(ret), KP(allocator));
  } else if (OB_ISNULL(iter_buf_ = allocator->alloc(sizeof(ObTabletIterator)))) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
    SERVER_LOG(WARN, "fail to alloc tablet iter buf", K(ret));
  } else {
    allocator_ = allocator;
    addr_ = addr;
    start_to_read_ = true;
  }
  return ret;
}

int ObVirtualTableTabletIter::get_next_tablet()
{
  int ret = OB_SUCCESS;

  tablet_handle_.reset();
  tablet_allocator_.reuse();
  if (nullptr == tablet_iter_) {
    
    ObStorageMetaMemMgr *t3m = share::g_mp->storage_meta_mem_mgr();
    if (OB_ISNULL(tablet_iter_ = new (iter_buf_) ObTabletIterator(*t3m, tablet_allocator_, nullptr/*no op*/))) {
      ret = OB_ERR_UNEXPECTED;
      SERVER_LOG(WARN, "fail to new tablet_iter_", K(ret));
    }
  }
  if (OB_FAIL(ret)) {
  } else if (OB_FAIL(tablet_iter_->get_next_tablet(tablet_handle_))) {
    if (OB_UNLIKELY(OB_ITER_END != ret)) {
      SERVER_LOG(WARN, "fail to get tablet iter", K(ret));
    }
  } else if (OB_UNLIKELY(!tablet_handle_.is_valid())) {
    ret = OB_ERR_UNEXPECTED;
    SERVER_LOG(WARN, "unexpected invalid tablet", K(ret), K(tablet_handle_));
  }

  return ret;
}
