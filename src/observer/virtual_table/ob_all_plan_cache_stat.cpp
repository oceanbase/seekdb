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

#define USING_LOG_PREFIX SERVER

#include "observer/virtual_table/ob_all_plan_cache_stat.h"
#include "share/rc/ob_server_runtime.h"
#include "sql/plan_cache/ob_plan_cache.h"

#include "observer/ob_server_utils.h"

using namespace oceanbase::common;
using namespace oceanbase::sql;
namespace oceanbase
{
namespace observer
{
ObAllPlanCacheBase::ObAllPlanCacheBase()
    : ObVirtualTableIterator(),
      iter_end_(false)
{

}

ObAllPlanCacheBase::~ObAllPlanCacheBase()
{
  reset();
}

void ObAllPlanCacheBase::reset()
{
  iter_end_ = false;
}

int ObAllPlanCacheBase::inner_get_next_row(common::ObNewRow *&row)
{
  int ret = OB_SUCCESS;
  if (OB_FAIL(inner_get_next_row())) {
    if (OB_ITER_END != ret) {
      SERVER_LOG(WARN, "fail to get next row", K(ret));
    }
  } else {
    row = &cur_row_;
  }
  return ret;
}

int ObAllPlanCacheStat::fill_cells(ObPlanCache &plan_cache)
{
  int ret = OB_SUCCESS;
  const int64_t col_count = output_column_ids_.count();
  ObObj *cells = cur_row_.cells_;
  const ObPlanCacheStat &pc_stat = plan_cache.get_plan_cache_stat();
  for (int64_t i =  0; OB_SUCC(ret) && i < col_count; ++i) {
    uint64_t col_id = output_column_ids_.at(i);
    switch(col_id) {
      //sql_num
    case SQL_NUM: {
      cells[i].set_int(plan_cache.get_cache_obj_size());
      break;
    }
      //mem_used
    case MEM_USED: {
      cells[i].set_int(plan_cache.get_mem_used());
      break;
    }
    case MEM_HOLD: {
      cells[i].set_int(plan_cache.get_mem_hold());
      break;
    }
    case ACCESS_COUNT: {
      cells[i].set_int(pc_stat.access_count_);
      break;
    }
    case HIT_COUNT: {
      cells[i].set_int(pc_stat.hit_count_);
      break;
    }
    //hit_rate
    case HIT_RATE: {
      if (pc_stat.access_count_ !=0) {
        cells[i].set_int(pc_stat.hit_count_*100/pc_stat.access_count_);
      } else {
        cells[i].set_int(0);
      }
      break;
    }
    //plan_num
    case PLAN_NUM: {//id->plan_stat map size;
      cells[i].set_int(plan_cache.get_cache_obj_size());
      break;
    }
      //mem_limit
    case MEM_LIMIT: {
      cells[i].set_int(plan_cache.get_mem_limit());
      break;
    }
      //hash_bucket
    case HASH_BUCKET: {
      cells[i].set_int(plan_cache.get_bucket_num());
      break;
    }
    default: {
      ret = OB_ERR_UNEXPECTED;
      SERVER_LOG(WARN,
                 "invalid column id",
                 K(ret),
                 K(i),
                 K(output_column_ids_),
                 K(col_id));
      break;
    }
    }
  }
  return ret;
}

int ObAllPlanCacheStat::get_row()
{
  int ret = OB_SUCCESS;
  if (iter_end_) {
    ret = OB_ITER_END;
  } else {
    SERVER_MODULE_SCOPE {
      ObPlanCache *plan_cache =
          ::oceanbase::share::server_service<::oceanbase::sql::ObPlanCache>();
      if (OB_FAIL(fill_cells(*plan_cache))) {
      } else {
      }
      iter_end_ = true;
    }
  }
  return ret;
}

} // end of namespace observer
} // end of namespace oceanbase
