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
#include "share/rc/ob_module_provider.h"

#include "src/sql/plan_cache/ob_pcv_set.h"

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
#define SET_REF_HANDLE_COL(handle)                      \
  int64_t ref_idx = handle;                             \
  int64_t ref_cnt = ref_handle_mgr.get_ref_cnt(handle); \
  cells[i].set_int(ref_cnt);

  int ret = OB_SUCCESS;
  const int64_t col_count = output_column_ids_.count();
  ObObj *cells = cur_row_.cells_;
  const ObPlanCacheStat &pc_stat = plan_cache.get_plan_cache_stat();
    const ObCacheRefHandleMgr &ref_handle_mgr = plan_cache.get_ref_handle_mgr();
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
        SERVER_LOG(DEBUG, "rate:", "hit_count", pc_stat.hit_count_, "access_count", pc_stat.access_count_);
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
      //stmtkey_num, not used
    case STMTKEY_NUM: {
      cells[i].set_int(0);
      break;
    }
    case PC_REF_PLAN_LOCAL: {
      SET_REF_HANDLE_COL(PC_REF_PLAN_LOCAL_HANDLE);
      break;
    }
    case PC_REF_PLAN_REMOTE: {
      SET_REF_HANDLE_COL(PC_REF_PLAN_REMOTE_HANDLE);
      break;
    }
    case PC_REF_PLAN_DIST: {
      SET_REF_HANDLE_COL(PC_REF_PLAN_DIST_HANDLE);
      break;
    }
    case PC_REF_PLAN_ARR: {
      SET_REF_HANDLE_COL(PC_REF_PLAN_ARR_HANDLE);
      break;
    }
    case PC_REF_PLAN_STAT: {
      SET_REF_HANDLE_COL(PC_REF_PLAN_STAT_HANDLE);
      break;
    }
    case PC_REF_PL: {
      SET_REF_HANDLE_COL(PC_REF_PL_HANDLE);
      break;
    }
    case PC_REF_PL_STAT: {
      SET_REF_HANDLE_COL(PC_REF_PL_STAT_HANDLE);
      break;
    }
    case PLAN_GEN: {
      SET_REF_HANDLE_COL(PLAN_GEN_HANDLE);
      break;
    }
    case CLI_QUERY: {
      SET_REF_HANDLE_COL(CLI_QUERY_HANDLE);
      break;
    }
    case OUTLINE_EXEC: {
      SET_REF_HANDLE_COL(OUTLINE_EXEC_HANDLE);
      break;
    }
    case PLAN_EXPLAIN: {
      SET_REF_HANDLE_COL(PLAN_EXPLAIN_HANDLE);
      break;
    }
    case PS_EXEC: {
      SET_REF_HANDLE_COL(PS_EXEC_HANDLE);
      break;
    }
    case GV_SQL: {
      SET_REF_HANDLE_COL(PC_DIAG_HANDLE);
      break;
    }
    case PL_ANON: {
      SET_REF_HANDLE_COL(PL_ANON_HANDLE);
      break;
    }
    case PL_ROUTINE: {
      SET_REF_HANDLE_COL(PL_ROUTINE_HANDLE);
      break;
    }
    case PACKAGE_VAR: {
      SET_REF_HANDLE_COL(PACKAGE_VAR_HANDLE);
      break;
    }
    case PACKAGE_TYPE: {
      SET_REF_HANDLE_COL(PACKAGE_TYPE_HANDLE);
      break;
    }
    case PACKAGE_SPEC: {
      SET_REF_HANDLE_COL(PACKAGE_SPEC_HANDLE);
      break;
    }
    case PACKAGE_BODY: {
      SET_REF_HANDLE_COL(PACKAGE_BODY_HANDLE);
      break;
    }
    case PACKAGE_RESV: {
      SET_REF_HANDLE_COL(PACKAGE_RESV_HANDLE);
      break;
    }
    case GET_PKG: {
      SET_REF_HANDLE_COL(GET_PKG_HANDLE);
      break;
    }
    case INDEX_BUILDER: {
      SET_REF_HANDLE_COL(INDEX_BUILDER_HANDLE);
      break;
    }
    case PCV_SET: {
      SET_REF_HANDLE_COL(PCV_SET_HANDLE);
      break;
    }
    case PCV_RD: {
      SET_REF_HANDLE_COL(PCV_RD_HANDLE);
      break;
    }
    case PCV_WR: {
      SET_REF_HANDLE_COL(PCV_WR_HANDLE);
      break;
    }
    case PCV_GET_PLAN_KEY: {
      SET_REF_HANDLE_COL(PCV_GET_PLAN_KEY_HANDLE);
      break;
    }
    case PCV_GET_PL_KEY: {
      SET_REF_HANDLE_COL(PCV_GET_PL_KEY_HANDLE);
      break;
    }
    case PCV_EXPIRE_BY_USED: {
      SET_REF_HANDLE_COL(PCV_EXPIRE_BY_USED_HANDLE);
      break;
    }
    case PCV_EXPIRE_BY_MEM: {
      SET_REF_HANDLE_COL(PCV_EXPIRE_BY_MEM_HANDLE);
      break;
    }
    case LC_REF_CACHE_NODE: {
      SET_REF_HANDLE_COL(LC_REF_CACHE_NODE_HANDLE);
      break;
    }
    case LC_NODE: {
      SET_REF_HANDLE_COL(LC_NODE_HANDLE);
      break;
    }
    case LC_NODE_RD: {
      SET_REF_HANDLE_COL(LC_NODE_RD_HANDLE);
      break;
    }
    case LC_NODE_WR: {
      SET_REF_HANDLE_COL(LC_NODE_WR_HANDLE);
      break;
    }
    case LC_REF_CACHE_OBJ_STAT: {
      SET_REF_HANDLE_COL(LC_REF_CACHE_OBJ_STAT_HANDLE);
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
#undef SET_REF_HANDLE_COL
}

int ObAllPlanCacheStatI1::get_all_ids(ObIArray<uint64_t> &batch_ids)
{
  int ret = OB_SUCCESS;
  if (OB_FAIL(set_ids(key_ranges_, batch_ids))) {
    LOG_WARN("set tenant ids failed", K(ret));
  }
  return ret;
}

int ObAllPlanCacheStat::get_all_ids(ObIArray<uint64_t> &batch_ids)
{
  int ret = OB_SUCCESS;
  // single sys tenant
  if (OB_FAIL(batch_ids.push_back(1UL))) {
    SERVER_LOG(WARN, "failed to add tenant id", K(ret));
  }
  return ret;
}

int ObAllPlanCacheStat::inner_open()
{
  int ret = OB_SUCCESS;
  // Still drive I1 rowkey validation via get_all_tenants
  ObSEArray<uint64_t, 16> batch_ids;
  if (OB_FAIL(get_all_ids(batch_ids))) {
    SERVER_LOG(WARN, "fail get all tenant ids", K(ret));
  }
  return ret;
}
int ObAllPlanCacheStat::get_row_from_tenants()
{
  int ret = OB_SUCCESS;
  if (iter_end_) {
    ret = OB_ITER_END;
  } else {
    MOD_SCOPE {
      ObPlanCache *plan_cache = share::g_mp->plan_cache(); 
      if (OB_FAIL(fill_cells(*plan_cache))) {
        SERVER_LOG(WARN, "fail to fill cells", K(ret), K(cur_row_));
      } else {
        SERVER_LOG(DEBUG, "add plan cache");
      }
      iter_end_ = true;
    }
  }
  return ret;
}

int ObAllPlanCacheStatI1::set_ids(const common::ObIArray<common::ObNewRange> &ranges, common::ObIArray<uint64_t> &batch_ids)
{
  int ret = OB_SUCCESS;
  ObRowkey start_key;
  ObRowkey end_key;
  for (int64_t i = 0; OB_SUCC(ret) && i < ranges.count(); ++i) {
    
    start_key.reset();
    end_key.reset();
    start_key = ranges.at(i).start_key_;
    end_key = ranges.at(i).end_key_;
    if (!(start_key.get_obj_cnt() > 0)) {
      ret = OB_ERR_UNEXPECTED;
      SERVER_LOG(WARN, "assert start_key.get_obj_cnt() > 0", K(ret));
    } else if (!(start_key.get_obj_cnt() == end_key.get_obj_cnt())) {
      ret = OB_ERR_UNEXPECTED;
      SERVER_LOG(WARN, "assert start_key.get_obj_cnt() == end_key.get_obj_cnt()", K(ret));
    }
    const ObObj *start_key_obj_ptr = NULL;
    const ObObj *end_key_obj_ptr = NULL;
    if (OB_SUCC(ret)) {
      start_key_obj_ptr = start_key.get_obj_ptr();
      end_key_obj_ptr = end_key.get_obj_ptr();
      if (OB_ISNULL(start_key_obj_ptr) || OB_ISNULL(end_key_obj_ptr)) {
        ret = OB_INVALID_ARGUMENT;
        SERVER_LOG(WARN, "invalid args", KP(start_key_obj_ptr), KP(end_key_obj_ptr));
      } else if ((!start_key_obj_ptr[0].is_min_value() || !end_key_obj_ptr[0].is_max_value())
          && start_key_obj_ptr[0] != end_key_obj_ptr[0]) {
        ret = OB_NOT_IMPLEMENT;
        SERVER_LOG(WARN, "tenant id exact value", K(ret));
      } else if (start_key_obj_ptr[0] == end_key_obj_ptr[0]) {
        if (!(ObIntType == start_key_obj_ptr[0].get_type())) {
          ret = OB_ERR_UNEXPECTED;
          SERVER_LOG(WARN, "assert ObIntType == start_key_obj_ptr[0].get_type()", K(ret));
        } else if (!(start_key_obj_ptr[0].get_type() == end_key_obj_ptr[0].get_type())) {
          ret = OB_ERR_UNEXPECTED;
          SERVER_LOG(WARN,
                     "assert start_key_obj_ptr[0].get_type() == end_key_obj_ptr[0].get_type()",
                     K(ret));
        } else {
          (void)(start_key_obj_ptr[0].get_int());
          if (OB_FAIL(add_var_to_array_no_dup(batch_ids,
                                                     static_cast<uint64_t>(1)))) {
            SERVER_LOG(WARN, "Failed to add id to array no duplicate", K(ret));
          } else { }//do nothing
        }
      }
    }
  }
  return ret;
}

} // end of namespace observer
} // end of namespace oceanbase
