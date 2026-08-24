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

#include "ob_all_virtual_memory_info.h"
#include "lib/alloc/memory_dump.h"
#include "observer/vector_index/ob_plugin_vector_index_service.h"
#include "share/cache/ob_kv_storecache.h"
#include "share/config/ob_server_config.h"
#include "share/rc/ob_server_runtime.h"
#include "share/schema/ob_multi_version_schema_service.h"
#include "share/schema/ob_schema_runtime_service.h"
#include "sql/dtl/ob_dtl_fc_server.h"
#include "sql/engine/ob_sql_memory_manager.h"
#include "sql/plan_cache/ob_plan_cache.h"
#include "sql/plan_cache/ob_ps_cache.h"
#include "storage/allocator/ob_shared_memory_allocator_mgr.h"
#include "storage/meta_mem/ob_storage_meta_mem_mgr.h"
#include "storage/tmp_file/ob_tmp_file_manager.h"
#include "storage/tx_storage/ob_memstore_freezer.h"

namespace oceanbase
{
using namespace common;

namespace observer
{
namespace
{
int collect_kv_cache_memory(int64_t &hold, int64_t &used, int64_t &mem_limit)
{
  int ret = OB_SUCCESS;
  ObKVGlobalCache &kv_cache = ObKVGlobalCache::get_instance();
  ObSEArray<ObKVCacheInstHandle, 100> inst_handles;
  hold = 0;
  used = 0;
  mem_limit = 0;
  if (OB_FAIL(kv_cache.get_cache_inst_info(inst_handles))) {
    SERVER_LOG(WARN, "fail to get kv cache instances", K(ret));
  } else {
    // Keep CACHE_SIZE as logical used memory and include cache-map overhead in hold.
    hold = kv_cache.get_managed_used();
    mem_limit = GMEMCONF.get_kvcache_memory_limit();
    for (int64_t i = 0; OB_SUCC(ret) && i < inst_handles.count(); ++i) {
      ObKVCacheInst *inst = inst_handles.at(i).get_inst();
      if (OB_ISNULL(inst)) {
        ret = OB_ERR_UNEXPECTED;
        SERVER_LOG(WARN, "kv cache instance is null", K(ret), K(i));
      } else {
        used += ATOMIC_LOAD(&inst->status_.store_size_);
      }
    }
  }
  return ret;
}

int collect_plan_cache_memory(int64_t &hold, int64_t &used, int64_t &mem_limit)
{
  int ret = OB_SUCCESS;
  sql::ObPlanCache *plan_cache = share::server_service<sql::ObPlanCache>();
  hold = 0;
  used = 0;
  mem_limit = 0;
  if (OB_ISNULL(plan_cache)) {
    ret = OB_ERR_UNEXPECTED;
    SERVER_LOG(WARN, "plan cache service is null", K(ret));
  } else {
    hold = plan_cache->get_mem_hold();
    used = plan_cache->get_mem_used();
    mem_limit = plan_cache->get_mem_limit();
  }
  return ret;
}

int collect_ps_cache_memory(int64_t &hold, int64_t &used, int64_t &mem_limit)
{
  int ret = OB_SUCCESS;
  sql::ObPsCache *ps_cache = share::server_service<sql::ObPsCache>();
  hold = 0;
  used = 0;
  mem_limit = 0;
  if (OB_ISNULL(ps_cache)) {
    ret = OB_ERR_UNEXPECTED;
    SERVER_LOG(WARN, "ps cache service is null", K(ret));
  } else {
    ps_cache->get_memory_info(hold, used, mem_limit);
  }
  return ret;
}

int collect_memstore_memory(int64_t &hold, int64_t &used, int64_t &mem_limit)
{
  int ret = OB_SUCCESS;
  storage::ObMemstoreFreezer *freezer =
      share::server_service<storage::ObMemstoreFreezer>();
  int64_t freeze_trigger = 0;
  int64_t freeze_cnt = 0;
  hold = 0;
  used = 0;
  mem_limit = 0;
  // The module getter returns active memory first and total memory second.
  if (OB_ISNULL(freezer)) {
    ret = OB_ERR_UNEXPECTED;
    SERVER_LOG(WARN, "memstore freezer service is null", K(ret));
  } else if (OB_FAIL(freezer->get_memstore_condition(used, hold, freeze_trigger,
                                                     mem_limit, freeze_cnt))) {
    SERVER_LOG(WARN, "fail to get memstore memory condition", K(ret));
  }
  return ret;
}

int collect_tx_data_memory(int64_t &hold, int64_t &used, int64_t &mem_limit)
{
  int ret = OB_SUCCESS;
  share::ObSharedMemAllocMgr *shared_mem_mgr =
      share::server_service<share::ObSharedMemAllocMgr>();
  hold = 0;
  used = 0;
  mem_limit = 0;
  if (OB_ISNULL(shared_mem_mgr)) {
    ret = OB_ERR_UNEXPECTED;
    SERVER_LOG(WARN, "shared memory allocator manager is null", K(ret));
  } else {
    shared_mem_mgr->get_tx_data_memory_info(hold, used, mem_limit);
  }
  return ret;
}

int collect_mds_memory(int64_t &hold, int64_t &used, int64_t &mem_limit)
{
  int ret = OB_SUCCESS;
  share::ObSharedMemAllocMgr *shared_mem_mgr =
      share::server_service<share::ObSharedMemAllocMgr>();
  hold = 0;
  used = 0;
  mem_limit = 0;
  if (OB_ISNULL(shared_mem_mgr)) {
    ret = OB_ERR_UNEXPECTED;
    SERVER_LOG(WARN, "shared memory allocator manager is null", K(ret));
  } else {
    shared_mem_mgr->get_mds_memory_info(hold, used, mem_limit);
  }
  return ret;
}

int collect_storage_meta_memory(int64_t &hold, int64_t &used)
{
  int ret = OB_SUCCESS;
  storage::ObStorageMetaMemMgr *meta_mem_mgr =
      share::server_service<storage::ObStorageMetaMemMgr>();
  ObSEArray<storage::ObStorageMetaMemStatus, 10> status_array;
  hold = 0;
  used = 0;
  if (OB_ISNULL(meta_mem_mgr)) {
    ret = OB_ERR_UNEXPECTED;
    SERVER_LOG(WARN, "storage meta memory manager is null", K(ret));
  } else if (OB_FAIL(meta_mem_mgr->get_meta_mem_status(status_array))) {
    SERVER_LOG(WARN, "fail to get storage meta memory status", K(ret));
  } else {
    for (int64_t i = 0; i < status_array.count(); ++i) {
      hold += status_array.at(i).total_size_;
      used += status_array.at(i).used_size_;
    }
  }
  return ret;
}

int collect_schema_memory(int64_t &hold, int64_t &used)
{
  int ret = OB_SUCCESS;
  share::schema::ObSchemaRuntimeService *schema_runtime =
      share::server_service<share::schema::ObSchemaRuntimeService>();
  share::schema::ObMultiVersionSchemaService *schema_service = nullptr;
  ObSEArray<ObSchemaMemory, 2> memory_info;
  hold = 0;
  used = 0;
  if (OB_ISNULL(schema_runtime) ||
      OB_ISNULL(schema_service = schema_runtime->get_schema_service())) {
    ret = OB_ERR_UNEXPECTED;
    SERVER_LOG(WARN, "schema service is null", K(ret), KP(schema_runtime),
               KP(schema_service));
  } else if (OB_FAIL(schema_service->get_runtime_mem_info(1UL, memory_info))) {
    SERVER_LOG(WARN, "fail to get schema memory info", K(ret));
  } else {
    for (int64_t i = 0; i < memory_info.count(); ++i) {
      hold += memory_info.at(i).get_mem_total();
      used += memory_info.at(i).get_mem_used();
    }
  }
  return ret;
}

int collect_vector_raw_malloc_used(int64_t &raw_malloc_used)
{
  int ret = OB_SUCCESS;
  lib::ObMallocSampleMap malloc_sample_map;
  raw_malloc_used = 0;
  if (OB_FAIL(
          malloc_sample_map.create(1000, "MallocInfoMap", "MallocInfoMap"))) {
    SERVER_LOG(WARN, "fail to create malloc sample map", K(ret));
  } else {
    if (OB_FAIL(ObMemoryDump::get_instance().load_malloc_sample_map(
            malloc_sample_map))) {
      SERVER_LOG(WARN, "fail to load malloc sample map", K(ret));
    } else {
      for (lib::ObMallocSampleMap::const_iterator it =
               malloc_sample_map.begin();
           it != malloc_sample_map.end(); ++it) {
        if (0 == STRNCMP("VIndex", it->first.label_, STRLEN("VIndex")) &&
            ObCtxIds::GLIBC == it->first.ctx_id_) {
          raw_malloc_used += it->second.alloc_bytes_;
        }
      }
    }
    malloc_sample_map.destroy();
  }
  return ret;
}

int collect_vector_memory(int64_t &hold, int64_t &used, int64_t &mem_limit)
{
  int ret = OB_SUCCESS;
  share::ObPluginVectorIndexService *vector_service =
      share::server_service<share::ObPluginVectorIndexService>();
  share::ObSharedMemAllocMgr *shared_mem_mgr =
      share::server_service<share::ObSharedMemAllocMgr>();
  int64_t raw_malloc_used = 0;
  hold = 0;
  used = 0;
  mem_limit = 0;
  if (OB_ISNULL(vector_service) || OB_ISNULL(shared_mem_mgr)) {
    ret = OB_ERR_UNEXPECTED;
    SERVER_LOG(WARN, "vector memory service is null", K(ret),
               KP(vector_service), KP(shared_mem_mgr));
  } else if (OB_FAIL(collect_vector_raw_malloc_used(raw_malloc_used))) {
  } else {
    // Match the composition exposed by V$OB_VECTOR_MEMORY.
    const int64_t metadata_used = vector_service->get_allocator().used();
    hold = shared_mem_mgr->vector_allocator().hold() + raw_malloc_used +
           metadata_used;
    used = shared_mem_mgr->vector_allocator().used() + raw_malloc_used +
           metadata_used;
    mem_limit = shared_mem_mgr->share_resource_throttle_tool()
                    .get_resource_limit<share::ObVectorAllocator>();
  }
  return ret;
}

int collect_sql_workarea_memory(int64_t &hold, int64_t &used, int64_t &mem_limit)
{
  int ret = OB_SUCCESS;
  sql::ObSqlMemoryManager *memory_manager =
      share::server_service<sql::ObSqlMemoryManager>();
  sql::ObSqlWorkareaCurrentMemoryInfo memory_info;
  hold = 0;
  used = 0;
  mem_limit = 0;
  if (OB_ISNULL(memory_manager)) {
    ret = OB_ERR_UNEXPECTED;
    SERVER_LOG(WARN, "sql memory manager is null", K(ret));
  } else if (OB_FAIL(memory_manager->get_workarea_memory_info(memory_info))) {
    SERVER_LOG(WARN, "fail to get sql workarea memory info", K(ret));
  } else {
    used = memory_info.get_total_mem_used();
    // WORKAREA_HOLD_SIZE is a periodically refreshed snapshot, while
    // TOTAL_MEM_USED is read from the live tracker. Keep the summary invariant
    // when allocations happen between those two refresh points.
    hold = MAX(memory_info.get_workarea_hold_size(), used);
    mem_limit = memory_info.get_max_workarea_size();
  }
  return ret;
}

int collect_dtl_memory(int64_t &hold, int64_t &used)
{
  int ret = OB_SUCCESS;
  sql::dtl::ObDfc *dfc = share::server_service<sql::dtl::ObDfc>();
  hold = 0;
  used = 0;
  if (OB_ISNULL(dfc)) {
    ret = OB_ERR_UNEXPECTED;
    SERVER_LOG(WARN, "dtl flow control service is null", K(ret));
  } else {
    dfc->get_memory_info(hold, used);
  }
  return ret;
}

int collect_tmp_file_memory(int64_t &hold, int64_t &used, int64_t &mem_limit)
{
  int ret = OB_SUCCESS;
  tmp_file::ObTmpFileManager *tmp_file_manager =
      share::server_service<tmp_file::ObTmpFileManager>();
  hold = 0;
  used = 0;
  mem_limit = 0;
  if (OB_ISNULL(tmp_file_manager)) {
    ret = OB_ERR_UNEXPECTED;
    SERVER_LOG(WARN, "tmp file manager is null", K(ret));
  } else {
    tmp_file_manager->get_sn_file_manager()
        .get_page_cache_controller()
        .get_write_buffer_pool()
        .get_memory_info(hold, used, mem_limit);
  }
  return ret;
}
} // namespace

ObAllVirtualMemoryInfo::ObAllVirtualMemoryInfo()
    : ObVirtualTableScannerIterator(),
      col_count_(0),
      has_start_(false)
{
}

ObAllVirtualMemoryInfo::~ObAllVirtualMemoryInfo()
{
  reset();
}

void ObAllVirtualMemoryInfo::reset()
{
  ObVirtualTableScannerIterator::reset();
  col_count_ = 0;
  has_start_ = false;
}

int ObAllVirtualMemoryInfo::add_row_(const char *mod_name, const int64_t hold,
                                     const int64_t used,
                                     const int64_t mem_limit,
                                     const bool has_mem_limit)
{
  int ret = OB_SUCCESS;
  ObObj *cells = cur_row_.cells_;
  if (OB_ISNULL(mod_name) || OB_ISNULL(cells)) {
    ret = OB_INVALID_ARGUMENT;
    SERVER_LOG(WARN, "invalid memory info row", K(ret), KP(mod_name),
               KP(cells));
  } else {
    for (int64_t i = 0; OB_SUCC(ret) && i < col_count_; ++i) {
      const uint64_t col_id = output_column_ids_.at(i);
      switch (col_id) {
      case MOD_NAME:
        cells[i].set_varchar(mod_name);
        cells[i].set_collation_type(
            ObCharset::get_default_collation(ObCharset::get_default_charset()));
        break;
      case HOLD:
        cells[i].set_int(hold);
        break;
      case USED:
        cells[i].set_int(used);
        break;
      case MEM_LIMIT:
        if (has_mem_limit) {
          cells[i].set_int(mem_limit);
        } else {
          cells[i].set_null();
        }
        break;
      default:
        ret = OB_ERR_UNEXPECTED;
        SERVER_LOG(WARN, "unexpected column id", K(ret), K(col_id), K(i));
        break;
      }
    }
    if (OB_SUCC(ret) && OB_FAIL(scanner_.add_row(cur_row_))) {
      SERVER_LOG(WARN, "fail to add memory info row", K(ret), K(cur_row_));
    }
  }
  return ret;
}

int ObAllVirtualMemoryInfo::fill_scanner_()
{
  int ret = OB_SUCCESS;
  int64_t hold = 0;
  int64_t used = 0;
  int64_t mem_limit = 0;
  SERVER_MODULE_SCOPE {
    if (OB_FAIL(collect_kv_cache_memory(hold, used, mem_limit))) {
    } else if (OB_FAIL(add_row_("KV_CACHE", hold, used, mem_limit))) {
    } else if (OB_FAIL(collect_plan_cache_memory(hold, used, mem_limit))) {
    } else if (OB_FAIL(add_row_("PLAN_CACHE", hold, used, mem_limit))) {
    } else if (OB_FAIL(collect_ps_cache_memory(hold, used, mem_limit))) {
    } else if (OB_FAIL(add_row_("PS_CACHE", hold, used, mem_limit))) {
    } else if (OB_FAIL(collect_memstore_memory(hold, used, mem_limit))) {
    } else if (OB_FAIL(add_row_("MEMSTORE", hold, used, mem_limit))) {
    } else if (OB_FAIL(collect_tx_data_memory(hold, used, mem_limit))) {
    } else if (OB_FAIL(add_row_("TX_DATA", hold, used, mem_limit))) {
    } else if (OB_FAIL(collect_mds_memory(hold, used, mem_limit))) {
    } else if (OB_FAIL(add_row_("MDS", hold, used, mem_limit))) {
    } else if (OB_FAIL(collect_storage_meta_memory(hold, used))) {
    } else if (OB_FAIL(add_row_("STORAGE_META", hold, used, 0, false))) {
    } else if (OB_FAIL(collect_schema_memory(hold, used))) {
    } else if (OB_FAIL(add_row_("SCHEMA", hold, used, 0, false))) {
    } else if (OB_FAIL(collect_vector_memory(hold, used, mem_limit))) {
    } else if (OB_FAIL(add_row_("VECTOR", hold, used, mem_limit))) {
    } else if (OB_FAIL(collect_sql_workarea_memory(hold, used, mem_limit))) {
    } else if (OB_FAIL(add_row_("SQL_WORKAREA", hold, used, mem_limit))) {
    } else if (OB_FAIL(collect_dtl_memory(hold, used))) {
    } else if (OB_FAIL(add_row_("DTL", hold, used, 0, false))) {
    } else if (OB_FAIL(collect_tmp_file_memory(hold, used, mem_limit))) {
    } else if (OB_FAIL(add_row_("TMP_FILE", hold, used, mem_limit))) {
    }
  }
  return ret;
}

int ObAllVirtualMemoryInfo::inner_get_next_row(ObNewRow *&row)
{
  int ret = OB_SUCCESS;
  if (!has_start_) {
    col_count_ = output_column_ids_.count();
    if (OB_ISNULL(cur_row_.cells_)) {
      ret = OB_ERR_UNEXPECTED;
      SERVER_LOG(ERROR, "cur row cell is null", K(ret));
    } else if (OB_FAIL(fill_scanner_())) {
      SERVER_LOG(WARN, "fail to fill memory info scanner", K(ret));
    } else {
      scanner_it_ = scanner_.begin();
      has_start_ = true;
    }
  }

  if (OB_SUCC(ret)) {
    if (OB_FAIL(scanner_it_.get_next_row(cur_row_))) {
      if (OB_ITER_END != ret) {
        SERVER_LOG(WARN, "fail to get memory info row", K(ret));
      }
    } else {
      row = &cur_row_;
    }
  }
  return ret;
}

} // namespace observer
} // namespace oceanbase
