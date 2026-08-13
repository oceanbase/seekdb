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

# define USING_LOG_PREFIX SERVER
#include "ob_all_virtual_schema_memory.h"

namespace oceanbase
{
namespace observer
{
int ObAllVirtualSchemaMemory::get_next_mem_info(ObSchemaMemory &schema_mem) {
  int ret = OB_SUCCESS;
  if (!loaded_) {
    schema_mem_infos_.reset();
    if (OB_FAIL(schema_service_.get_runtime_mem_info(1UL, schema_mem_infos_))) {
    } else {
      loaded_ = true;
      mem_idx_ = 0;
    }
  } else if (mem_idx_ >= schema_mem_infos_.count()) {
    ret = OB_ITER_END;
  }
  if (OB_SUCC(ret) && mem_idx_ >= schema_mem_infos_.count()) {
    ret = OB_ITER_END;
  }
  if (OB_SUCC(ret)) {
    if (mem_idx_ >= schema_mem_infos_.count()) {
      ret = OB_ITER_END;
    } else {
      schema_mem = schema_mem_infos_[mem_idx_++];
    }
  }
  return ret;
}

int ObAllVirtualSchemaMemory::inner_get_next_row(common::ObNewRow *&row)
{
  int ret = OB_SUCCESS;
  ObSchemaMemory schema_mem;

  if (OB_FAIL(get_next_mem_info(schema_mem))) {
    if (OB_ITER_END != ret) {
      LOG_WARN("fail to get next schema memory info", KR(ret));
    }
  }
  if (OB_SUCC(ret)) {
    const int64_t pos = schema_mem.get_pos();
    
    const int64_t used_schema_mgr_cnt = schema_mem.get_used_schema_mgr_cnt();
    const int64_t free_schema_mgr_cnt = schema_mem.get_free_schema_mgr_cnt();
    const int64_t mem_used = schema_mem.get_mem_used();
    const int64_t mem_total = schema_mem.get_mem_total();
    const int64_t allocator_idx = schema_mem.get_allocator_idx();

    const int64_t col_count = output_column_ids_.count();
    for (int64_t i = 0; OB_SUCC(ret) && i < col_count; ++i) {
      uint64_t col_id = output_column_ids_.at(i);
      switch (col_id) {
        case ALLOCATOR_TYPE: {
          cur_row_.cells_[i].set_varchar( 0 == pos ? "current" : "another");
          cur_row_.cells_[i].set_collation_type(ObCharset::get_default_collation(ObCharset::get_default_charset()));
          break;
        }
        case USED_SCHEMA_MGR_CNT: {
          cur_row_.cells_[i].set_int(used_schema_mgr_cnt);
          break;
        }
        case FREE_SCHEMA_MGR_CNT: {
          cur_row_.cells_[i].set_int(free_schema_mgr_cnt);
          break;
        }
        case MEM_USED: {
          cur_row_.cells_[i].set_int(mem_used);
          break;
        }
        case MEM_TOTAL: {
          cur_row_.cells_[i].set_int(mem_total);
          break;
        }
        case ALLOCATOR_IDX: {
          cur_row_.cells_[i].set_int(allocator_idx);
          break;
        }
        default : {
          ret = OB_ERR_UNEXPECTED;
          LOG_WARN("invalid col_id", KR(ret), K(col_id));
        }
      }
    }

    if (OB_SUCC(ret)) {
      row = &cur_row_;
    }
  }
  return ret;
}
} /* namespace observer */
} /* namespace oceanbase */
