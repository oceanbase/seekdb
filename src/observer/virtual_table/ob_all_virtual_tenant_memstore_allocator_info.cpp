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

#include "ob_all_virtual_tenant_memstore_allocator_info.h"
#include "share/rc/ob_module_provider.h"
#include "observer/ob_server_utils.h"
#include "storage/allocator/ob_shared_memory_allocator_mgr.h"

namespace oceanbase
{
using namespace common;
using namespace share;

namespace observer
{
class MemstoreInfoFill
{
public:
  typedef ObMemstoreAllocatorInfo Item;
  typedef ObArray<Item> ItemArray;
  typedef ObMemstoreAllocator::AllocHandle Handle;
  MemstoreInfoFill(ItemArray& array): array_(array) {}
  ~MemstoreInfoFill() {}
  int operator()(ObDLink* link) {
    Item item;
    Handle* handle = CONTAINER_OF(link, Handle, total_list_);
    memtable::ObMemtable& mt = handle->mt_;
    item.protection_clock_ = handle->get_protection_clock();
    item.is_active_ = handle->is_active();
    item.tablet_id_ = mt.get_key().tablet_id_.id();
    item.scn_range_ = mt.get_scn_range();
    item.mt_addr_ = &mt;
    item.ref_cnt_ = mt.get_ref();
    return array_.push_back(item);
  }
  ItemArray& array_;
};

ObAllVirtualTenantMemstoreAllocatorInfo::ObAllVirtualTenantMemstoreAllocatorInfo()
    : ObVirtualTableIterator(),
      memstore_infos_(),
      memstore_infos_idx_(0),
      col_count_(0),
      retire_clock_(INT64_MAX)
{
}

ObAllVirtualTenantMemstoreAllocatorInfo::~ObAllVirtualTenantMemstoreAllocatorInfo()
{
  reset();
}

int ObAllVirtualTenantMemstoreAllocatorInfo::inner_open()
{
  int ret = OB_SUCCESS;
  reset();
  if (OB_UNLIKELY(NULL == GCTX.omt_)) {
    ret = OB_NOT_INIT;
    SERVER_LOG(WARN, "GCTX.omt_ shouldn't be NULL", K(ret));
  } else if (OB_FAIL(fill_memstore_infos())) {
    SERVER_LOG(WARN, "fail to fill memstore info", K(ret));
  } else {
    col_count_ = output_column_ids_.count();
  }
  return ret;
}

void ObAllVirtualTenantMemstoreAllocatorInfo::reset()

{
  memstore_infos_.reset();
  memstore_infos_idx_ = 0;
  col_count_ = 0;
}

int ObAllVirtualTenantMemstoreAllocatorInfo::fill_memstore_infos()
{
  int ret = OB_SUCCESS;
  memstore_infos_.reset();
  MOD_SCOPE
  {
    ObMemstoreAllocator &memstore_allocator = share::g_mp->shared_mem_alloc_mgr()->memstore_allocator();
    MemstoreInfoFill fill_func(memstore_infos_);
    if (OB_FAIL(memstore_allocator.for_each(fill_func))) {
      SERVER_LOG(WARN, "fill memstore info fail", K(ret));
    } else {
      retire_clock_ = memstore_allocator.get_retire_clock();
      memstore_infos_idx_ = 0;
    }
  }

  return ret;
}

int ObAllVirtualTenantMemstoreAllocatorInfo::inner_get_next_row(ObNewRow *&row)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(NULL == allocator_)) {
    ret = OB_NOT_INIT;
    SERVER_LOG(WARN, "allocator_ shouldn't be NULL", K(ret));
  } else {
    if (memstore_infos_idx_ >= memstore_infos_.count()) {
      // single sys tenant exhausted
      ret = OB_ITER_END;
    }

    if (OB_SUCC(ret)) {
      ObObj *cells = cur_row_.cells_;
      
      if (OB_ISNULL(cells)) {
        ret = OB_ERR_UNEXPECTED;
        SERVER_LOG(ERROR, "cur row cell is NULL", K(ret));
      } else {
                MemstoreInfo &info = memstore_infos_.at(memstore_infos_idx_);
        for (int64_t i = 0; OB_SUCC(ret) && i < col_count_; ++i) {
          const uint64_t col_id = output_column_ids_.at(i);
          switch (col_id) {
            case TABLET_ID: {
              cells[i].set_int(info.tablet_id_);
              break;
            }
            case START_TS: {
              //TODO:SCN
              cells[i].set_uint64(info.scn_range_.start_scn_.get_val_for_inner_table_field());
              break;
            }
            case END_TS: {
              cells[i].set_uint64(info.scn_range_.end_scn_.get_val_for_inner_table_field());
              break;
            }
            case IS_ACTIVE: {
              cur_row_.cells_[i].set_varchar(info.is_active_ ? "YES" : "NO");
              break;
            }
            case RETIRE_CLOCK: {
              cells[i].set_int(retire_clock_);
              break;
            }
            case PROTECTION_CLOCK: {
              cells[i].set_int(info.protection_clock_);
              break;
            }
            case ADDRESS: {
              snprintf(mt_addr_, sizeof(mt_addr_), "%p", info.mt_addr_);
              cells[i].set_varchar(mt_addr_);
              cells[i].set_collation_type(ObCharset::get_default_collation(ObCharset::get_default_charset()));
              break;
            }
            case REF_COUNT: {
              cells[i].set_int(info.ref_cnt_);
              break;
            }
            default: {
              ret = OB_ERR_UNEXPECTED;
              SERVER_LOG(WARN, "unexpected column id", K(col_id), K(i), K(ret));
              break;
            }
          }
        }
      }
      if (OB_SUCC(ret)) {
        row = &cur_row_;
        memstore_infos_idx_++;
      }
    }
  }
  return ret;
}

} // namespace observer
} // namespace oceanbase
