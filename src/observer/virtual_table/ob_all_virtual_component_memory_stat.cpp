/*
 * Copyright (c) 2026 OceanBase.
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

#define USING_LOG_PREFIX SERVER

#include "observer/virtual_table/ob_all_virtual_component_memory_stat.h"

#include "share/cache/ob_kv_storecache.h"
#include "share/rc/ob_server_runtime.h"
#include "sql/engine/ob_sql_memory_manager.h"
#include "storage/allocator/ob_shared_memory_allocator_mgr.h"
#include "storage/meta_mem/ob_storage_meta_mem_mgr.h"

namespace oceanbase
{
namespace observer
{

using common::MemoryQuotaSample;

ComponentMemoryStat::ComponentMemoryStat()
    : ObVirtualTableScannerIterator(), component_index_(0), ip_buf_()
{}

void ComponentMemoryStat::reset()
{
  ObVirtualTableScannerIterator::reset();
  component_index_ = 0;
  MEMSET(ip_buf_, 0, sizeof(ip_buf_));
}

const char *ComponentMemoryStat::component_name(const int64_t component_index)
{
  static const char *const COMPONENT_NAMES[COMPONENT_COUNT] = {
      "KV_CACHE", "SQL_WORKAREA", "VECTOR", "META_OBJECT"};
  return component_index >= 0 && component_index < COMPONENT_COUNT
      ? COMPONENT_NAMES[component_index] : nullptr;
}

int ComponentMemoryStat::get_component_sample(
    const int64_t component_index,
    const char *&component_name,
    MemoryQuotaSample &sample) const
{
  int ret = OB_SUCCESS;
  component_name = ComponentMemoryStat::component_name(component_index);
  sample = MemoryQuotaSample();
  switch (component_index) {
    case 0:
      sample = ObKVGlobalCache::get_instance().get_memory_quota_sample();
      break;
    case 1: {
      sql::ObSqlMemoryManager *manager =
          share::server_service<sql::ObSqlMemoryManager>();
      if (OB_NOT_NULL(manager)) {
        sample = manager->get_memory_quota_sample();
      }
      break;
    }
    case 2: {
      share::ObSharedMemAllocMgr *manager =
          share::server_service<share::ObSharedMemAllocMgr>();
      if (OB_NOT_NULL(manager)) {
        sample = manager->vector_allocator().get_memory_quota_sample();
      }
      break;
    }
    case 3: {
      storage::ObStorageMetaMemMgr *manager =
          share::server_service<storage::ObStorageMetaMemMgr>();
      if (OB_NOT_NULL(manager)) {
        sample = manager->get_memory_quota_sample();
      }
      break;
    }
    default:
      ret = OB_INVALID_ARGUMENT;
      break;
  }
  return ret;
}

int ComponentMemoryStat::inner_get_next_row(common::ObNewRow *&row)
{
  int ret = OB_SUCCESS;
  row = nullptr;
  if (OB_ISNULL(allocator_) || OB_ISNULL(cur_row_.cells_)) {
    ret = OB_NOT_INIT;
    SERVER_LOG(WARN, "component memory virtual table is not initialized", K(ret));
  } else if (component_index_ >= COMPONENT_COUNT) {
    ret = OB_ITER_END;
  } else if (!GCTX.self_addr().ip_to_string(ip_buf_, sizeof(ip_buf_))) {
    ret = OB_ERR_UNEXPECTED;
    SERVER_LOG(WARN, "failed to stringify server address", K(ret), K(GCTX.self_addr()));
  } else {
    const char *component_name = nullptr;
    MemoryQuotaSample sample;
    if (OB_FAIL(get_component_sample(component_index_, component_name, sample))) {
      SERVER_LOG(WARN, "failed to sample component memory", K(ret), K(component_index_));
    } else {
      for (int64_t i = 0; OB_SUCC(ret) && i < output_column_ids_.count(); ++i) {
        common::ObObj &cell = cur_row_.cells_[i];
        switch (output_column_ids_.at(i)) {
          case SVR_IP:
            cell.set_varchar(ip_buf_);
            cell.set_collation_type(common::ObCharset::get_default_collation(
                common::ObCharset::get_default_charset()));
            break;
          case SVR_PORT:
            cell.set_int(GCTX.self_addr().get_port());
            break;
          case COMPONENT_NAME:
            cell.set_varchar(component_name);
            cell.set_collation_type(common::ObCharset::get_default_collation(
                common::ObCharset::get_default_charset()));
            break;
          case LIMIT_BYTES:
            cell.set_int(sample.limit_bytes_);
            break;
          case COMMITTED_BYTES:
            cell.set_int(sample.committed_bytes_);
            break;
          case RESERVED_BYTES:
            cell.set_int(sample.reserved_bytes_);
            break;
          case REJECT_COUNT:
            cell.set_int(sample.reject_count_);
            break;
          case RECLAIM_COUNT:
            cell.set_int(sample.reclaim_count_);
            break;
          default:
            ret = OB_ERR_UNEXPECTED;
            SERVER_LOG(WARN, "unexpected component memory column", K(ret),
                K(output_column_ids_.at(i)));
            break;
        }
      }
      if (OB_SUCC(ret)) {
        ++component_index_;
        row = &cur_row_;
      }
    }
  }
  return ret;
}

} // namespace observer
} // namespace oceanbase
