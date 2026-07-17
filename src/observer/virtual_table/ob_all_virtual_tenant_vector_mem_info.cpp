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

#include "ob_all_virtual_tenant_vector_mem_info.h"
#include "share/rc/ob_module_provider.h"
#include "lib/alloc/memory_dump.h"
#include "observer/vector_index/ob_plugin_vector_index_service.h"
#include "observer/vector_index/ob_plugin_vector_index_utils.h"
#include "storage/allocator/ob_shared_memory_allocator_mgr.h"

using namespace oceanbase::common;
namespace oceanbase
{
namespace observer
{

ObAllVirtualTenantVectorMemInfo::ObAllVirtualTenantVectorMemInfo()
    : ObVirtualTableScannerIterator()
{
}

ObAllVirtualTenantVectorMemInfo::~ObAllVirtualTenantVectorMemInfo()
{
}

int64_t ObAllVirtualTenantVectorMemInfo::fill_glibc_used_info()
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

int ObAllVirtualTenantVectorMemInfo::inner_get_next_row(ObNewRow *&row)
{
  int ret = OB_SUCCESS;
  if (start_to_read_) {
    ret = OB_ITER_END;
  } else {
    start_to_read_ = true;
    ObObj *cells = cur_row_.cells_;
    if (OB_FAIL(malloc_sample_map_.create(1000, "MallocInfoMap", "MallocInfoMap"))) {
      SERVER_LOG(WARN, "create memory info map failed", K(ret));
    } else if (OB_FAIL(ObMemoryDump::get_instance().load_malloc_sample_map(malloc_sample_map_))) {
      SERVER_LOG(WARN, "load memory info map failed", K(ret));
    } else {
      int64_t manage_used = 0;
      int64_t vector_hold = 0;
      int64_t vector_limit = 0;
      MOD_SCOPE {
        ObPluginVectorIndexService *service = share::g_mp->plugin_vector_index_service();
        ObSharedMemAllocMgr *shared_mem_mgr = share::g_mp->shared_mem_alloc_mgr();
        manage_used = service->get_allocator().used();
        vector_hold = shared_mem_mgr->vector_allocator().hold();
        const int64_t rb_used = shared_mem_mgr->vector_allocator().get_rb_mem_used();
        const int64_t vector_used = shared_mem_mgr->vector_allocator().used();
        int64_t pos = 0;
        const int64_t glibc_used = fill_glibc_used_info();
        MEMSET(vector_used_str_, 0, sizeof(vector_used_str_));
        complete_tablet_ids_.reset();
        partial_tablet_ids_.reset();
        cache_tablet_ids_.reset();
        if (OB_FAIL(service->get_snapshot_ids(complete_tablet_ids_, partial_tablet_ids_))) {
          SERVER_LOG(WARN, "failed to get snapshot_ids", K(ret));
        } else if (OB_FAIL(service->get_cache_ids(cache_tablet_ids_))) {
          SERVER_LOG(WARN, "failed to get cache_ids", K(ret));
        } else if (OB_FAIL(databuff_printf(vector_used_str_, OB_MAX_MYSQL_VARCHAR_LENGTH, pos,
                                          "{\"rb_used\":%lu", rb_used))) {
          SERVER_LOG(WARN, "failed to print total vector mem usage", K(ret), K(vector_hold));
        } else if (OB_FAIL(ObPluginVectorIndexUtils::get_mem_context_detail_info(
            service, complete_tablet_ids_, partial_tablet_ids_, cache_tablet_ids_, vector_used_str_,
            OB_MAX_MYSQL_VARCHAR_LENGTH, pos))) {
          SERVER_LOG(WARN, "failed to print vector mem usage detail", K(ret), K(vector_hold));
        } else if (OB_FAIL(databuff_printf(vector_used_str_, OB_MAX_MYSQL_VARCHAR_LENGTH, pos, "}"))) {
          SERVER_LOG(WARN, "failed to print total vector mem usage", K(ret));
        }
        vector_limit = shared_mem_mgr->share_resource_throttle_tool()
            .get_resource_limit<ObTenantVectorAllocator>();
        const int64_t tx_share_limit =
            shared_mem_mgr->share_resource_throttle_tool().get_resource_limit<FakeAllocatorForTxShare>();
        for (int64_t i = 0; OB_SUCC(ret) && i < output_column_ids_.count(); ++i) {
          const uint64_t col_id = output_column_ids_.at(i);
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
            ret = OB_ERR_UNEXPECTED;
            SERVER_LOG(WARN, "unexpected column id", K(ret));
            break;
          }
        }
      }
    }
    if (OB_SUCC(ret)) {
      row = &cur_row_;
    }
  }
  return ret;
}

}/* ns observer*/
}/* ns oceanbase */
