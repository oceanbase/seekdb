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

#include "observer/virtual_table/ob_all_virtual_ps_stat.h"
#include "share/rc/ob_server_runtime.h"

#include "observer/ob_server_utils.h"
#include "sql/plan_cache/ob_ps_cache.h"

using namespace oceanbase;
using namespace sql;
using namespace observer;
using namespace common;

int ObAllVirtualPsStat::fill_cells(ObPsCache &ps_cache)
{
  int ret = OB_SUCCESS;
  ObObj *cells = cur_row_.cells_;
  int64_t col_count = output_column_ids_.count();
  for (int64_t i = 0; OB_SUCC(ret) && i < col_count; ++i) {
    uint64_t col_id = output_column_ids_.at(i);
    switch (col_id) {
      case share::ALL_VIRTUAL_PS_STAT_CDE::STMT_COUNT: {
        // @shaoge use the size of stmt_id_map as the size of ps_cache
        cells[i].set_int(ps_cache.get_stmt_id_map_size());
        break;
      }
      case share::ALL_VIRTUAL_PS_STAT_CDE::HIT_COUNT: {
        uint64_t hit_count = ps_cache.get_hit_count();
        cells[i].set_int(hit_count);
        break;
      }
      case share::ALL_VIRTUAL_PS_STAT_CDE::ACCESS_COUNT: {
        uint64_t access_count = ps_cache.get_access_count();
        cells[i].set_int(access_count);
        break;
      }
      case share::ALL_VIRTUAL_PS_STAT_CDE::MEM_HOLD: {
        int64_t mem_total = 0;
        if (OB_FAIL(ps_cache.mem_total(mem_total))) {
        } else {
          cells[i].set_int(mem_total);
        }
        break;
      }
      default: {
        ret = OB_ERR_UNEXPECTED;
        SERVER_LOG(WARN, "invalid column id", K(ret), K(i), K(output_column_ids_), K(col_id));
        break;
      }
    }
  }
  return ret;
}

int ObAllVirtualPsStat::inner_get_next_row()
{
  int ret = OB_SUCCESS;

  if (iter_end_) {
    ret = OB_ITER_END;
  } else {
    iter_end_ = true;
    SERVER_MODULE_SCOPE {
      ObPsCache *ps_cache = ::oceanbase::share::server_service<::oceanbase::sql::ObPsCache>();
      if (OB_ISNULL(ps_cache)) {
      } else if (false == ps_cache->is_inited()) {
      } else if (OB_FAIL(fill_cells(*ps_cache))) {
      } else {
      }
    }
    // ignore error
    if (ret != OB_SUCCESS) {
      ret = OB_SUCCESS;
    }
  }
  return ret;
}
