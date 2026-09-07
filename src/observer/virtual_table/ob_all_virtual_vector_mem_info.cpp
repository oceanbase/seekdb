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

#include "ob_all_virtual_vector_mem_info.h"
#include "share/rc/ob_server_runtime.h"
#include "lib/alloc/memory_dump.h"
#include "observer/vector_index/ob_plugin_vector_index_service.h"
#include "observer/vector_index/ob_plugin_vector_index_utils.h"
#include "storage/allocator/ob_shared_memory_allocator_mgr.h"

using namespace oceanbase::common;
namespace oceanbase
{
namespace observer
{

ObAllVirtualVectorMemInfo::ObAllVirtualVectorMemInfo()
    : ObVirtualTableScannerIterator()
{
}

ObAllVirtualVectorMemInfo::~ObAllVirtualVectorMemInfo()
{
  reset();
}

void ObAllVirtualVectorMemInfo::reset()
{
  ObVirtualTableScannerIterator::reset();
}

int64_t ObAllVirtualVectorMemInfo::fill_glibc_used_info()
{
  int64_t used_size = 0;
  for (it_ = malloc_sample_map_.begin(); it_ != malloc_sample_map_.end(); ++it_) {
    if (0 == STRNCMP("VIndex", it_->first.label_, strlen("VIndex")) &&
        0 == STRNCMP("GLIBC", get_global_ctx_info().get_ctx_name(it_->first.ctx_id_), strlen("GLIBC"))) {
      used_size += it_->second.alloc_bytes_;
    }
  }
  return used_size;
}

int ObAllVirtualVectorMemInfo::inner_get_next_row(ObNewRow *&row)
{
  int ret = OB_SUCCESS;
  if (NULL == allocator_) {
    ret = OB_NOT_INIT;
    SERVER_LOG(WARN, "allocator_ shouldn't be NULL", K(allocator_), K(ret));
  } else if (!start_to_read_) {
    ObObj *cells = NULL;
    // allocator_ is allocator of PageArena type, no need to free
    if (NULL == (cells = cur_row_.cells_)) {
      ret = OB_ERR_UNEXPECTED;
      SERVER_LOG(ERROR, "cur row cell is NULL", K(ret));
    } else if (OB_FAIL(malloc_sample_map_.create(1000, "MallocInfoMap", "MallocInfoMap"))) {
    } else if (OB_FAIL(ObMemoryDump::get_instance().load_malloc_sample_map(malloc_sample_map_))) {
    } else {
      {
        int64_t manage_used = 0;
        int64_t vector_hold = 0;
        int64_t vector_limit = 0;
        SERVER_MODULE_SCOPE {
          ObPluginVectorIndexService *service = ::oceanbase::share::server_service<::oceanbase::share::ObPluginVectorIndexService>();
          ObSharedMemAllocMgr *shared_mem_mgr = ::oceanbase::share::server_service<::oceanbase::share::ObSharedMemAllocMgr>();
          manage_used = service->get_allocator().used();
          vector_hold = shared_mem_mgr->vector_allocator().hold();
          int64_t rb_used = shared_mem_mgr->vector_allocator().get_rb_mem_used();
          int64_t vector_used = shared_mem_mgr->vector_allocator().used();
          int64_t pos = 0;
          int64_t glibc_used = fill_glibc_used_info();
          MEMSET(vector_used_str_, 0, sizeof(vector_used_str_));
          complete_tablet_ids_.reset();
          partial_tablet_ids_.reset();
          cache_tablet_ids_.reset();
          if (OB_FAIL(service->get_snapshot_ids(complete_tablet_ids_, partial_tablet_ids_))) {
          } else if (OB_FAIL(service->get_cache_ids(cache_tablet_ids_))) {
          } else if (OB_FAIL(databuff_printf(vector_used_str_, OB_MAX_MYSQL_VARCHAR_LENGTH, pos, "{\"rb_used\":%lu", rb_used))) {
          } else if (OB_FAIL(ObPluginVectorIndexUtils::get_mem_context_detail_info(service, complete_tablet_ids_,
             partial_tablet_ids_, cache_tablet_ids_, vector_used_str_, OB_MAX_MYSQL_VARCHAR_LENGTH, pos))) {
          } else if (OB_FAIL(databuff_printf(vector_used_str_, OB_MAX_MYSQL_VARCHAR_LENGTH, pos, "}"))) {
          }
          vector_limit = GMEMCONF.get_vector_memory_limit();
          int64_t tx_share_limit = shared_mem_mgr->share_resource_throttle_tool().get_resource_limit<FakeAllocatorForTxShare>();
          for (int64_t i = 0; OB_SUCC(ret) && i < output_column_ids_.count(); ++i) {
            uint64_t col_id = output_column_ids_.at(i);
            switch (col_id) {
              case RAW_MALLOC_SIZE:
                cells[i].set_int(glibc_used);
                break;
              case INDEX_METADATA_SIZE:
                cells[i].set_int(manage_used);
                break;
              case VECTOR_MEM_HOLD:
                cells[i].set_int(vector_hold);
                break;
              case VECTOR_MEM_USED:
                cells[i].set_int(vector_used);
                break;
              case VECTOR_MEM_LIMIT:
                cells[i].set_int(vector_limit);
                break;
              case TX_SHARE_LIMIT:
                cells[i].set_int(tx_share_limit);
                break;
              case VECTOR_MEM_DETAIL_INFO:
                cells[i].set_varchar(vector_used_str_);
                cells[i].set_collation_type(ObCharset::get_default_collation(ObCharset::get_default_charset()));
                break;
              default:
                // abnormal column id
                ret = OB_ERR_UNEXPECTED;
                SERVER_LOG(WARN, "unexpected column id", K(ret));
                break;
            }
          }
          if (OB_SUCCESS == ret
              && OB_SUCCESS != (ret = scanner_.add_row(cur_row_))) {
            SERVER_LOG(WARN, "fail to add row", K(ret), K(cur_row_));
          }
        }
      }
    }
    scanner_it_ = scanner_.begin();
    start_to_read_ = true;
  }
  if (start_to_read_) {
    if (OB_FAIL(scanner_it_.get_next_row(cur_row_))) {
      if (OB_ITER_END != ret) {
        SERVER_LOG(WARN, "fail to get next row", K(ret));
      }
    } else {
      row = &cur_row_;
    }
  }
  return ret;
}

}/* ns observer*/
}/* ns oceanbase */
