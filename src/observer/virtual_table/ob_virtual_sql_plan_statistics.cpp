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

#include "observer/virtual_table/ob_virtual_sql_plan_statistics.h"
#include "share/rc/ob_module_provider.h"
#include "observer/ob_server_utils.h"
#include "sql/plan_cache/ob_ps_cache.h"

using namespace oceanbase;
using namespace sql;
using namespace observer;
using namespace common;
namespace oceanbase
{
namespace observer
{
struct ObGetAllOperatorStatOp
{
  explicit ObGetAllOperatorStatOp(common::ObIArray<ObOperatorStat> *key_array)
    : key_array_(key_array)
  {
  }
  ObGetAllOperatorStatOp()
    : key_array_(NULL)
  {
  }
  int operator()(common::hash::HashMapPair<ObCacheObjID, ObILibCacheObject *> &entry)
  {
    int ret = common::OB_SUCCESS;
    if (NULL == key_array_) {
      ret = common::OB_NOT_INIT;
      SERVER_LOG(WARN, "invalid argument", K(ret));
    } else {
      ObOperatorStat stat;
      ObPhysicalPlan *plan = NULL;
      if (ObLibCacheNameSpace::NS_CRSR == entry.second->get_ns()) {
        if (OB_ISNULL(plan = dynamic_cast<ObPhysicalPlan *>(entry.second))) {
          ret = OB_ERR_UNEXPECTED;
          SERVER_LOG(WARN, "unexpected null plan", K(ret), K(plan));
        }
        for (int64_t i = 0; i < plan->op_stats_.count() && OB_SUCC(ret); i++) {
          if (OB_FAIL(plan->op_stats_.get_op_stat_accumulation(plan,
                                                               i, stat))) {
          } else if (OB_FAIL(key_array_->push_back(stat))) {
          }
        } // for end
      }
    }
    return ret;
  }

  common::ObIArray<ObOperatorStat> *key_array_;
};

ObVirtualSqlPlanStatistics::ObVirtualSqlPlanStatistics() :
    operator_stat_array_(),
    iter_end_(false),
    operator_stat_array_idx_(OB_INVALID_ID)
{
}

ObVirtualSqlPlanStatistics::~ObVirtualSqlPlanStatistics()
{
  reset();
}

void ObVirtualSqlPlanStatistics::reset()
{
  operator_stat_array_.reset();
  iter_end_ = false;
}

int ObVirtualSqlPlanStatistics::inner_open()
{
  int ret = OB_SUCCESS;
  return ret;
}



int ObVirtualSqlPlanStatistics::get_row_from_specified_tenant(bool &is_end)
{
  int ret = OB_SUCCESS;
  // !!! Must add ObReqTimeGuard before referencing plan cache resources
  ObReqTimeGuard req_timeinfo_guard;
  is_end = false;
  sql::ObPlanCache *plan_cache = NULL;
  if (OB_INVALID_ID == static_cast<uint64_t>(operator_stat_array_idx_)) {
    plan_cache = share::g_mp->plan_cache();
    ObGetAllOperatorStatOp operator_stat_op(&operator_stat_array_);
    if (OB_FAIL(plan_cache->foreach_cache_obj(operator_stat_op))) {
    } else {
      operator_stat_array_idx_ = 0;
    }
  }
  if (OB_SUCC(ret)) {
    if (operator_stat_array_idx_ < 0) {
      ret = OB_ERR_UNEXPECTED;
    } else if (operator_stat_array_idx_ >= operator_stat_array_.count()) {
      is_end = true;
      operator_stat_array_idx_ = OB_INVALID_ID;
      operator_stat_array_.reset();
    } else {
      is_end = false;
      ObOperatorStat &opstat = operator_stat_array_.at(operator_stat_array_idx_);
      ++operator_stat_array_idx_;
      if (OB_FAIL(fill_cells(opstat))) {
      }
    }
  }
  SERVER_LOG(DEBUG,
             "add plan from a tenant",
             K(ret));
  return ret;
}

int ObVirtualSqlPlanStatistics::fill_cells(const ObOperatorStat &pstat)
{
  int ret = OB_SUCCESS;
  const int64_t col_count = output_column_ids_.count();
  ObObj *cells = cur_row_.cells_;
    for (int64_t i =  0; OB_SUCC(ret) && i < col_count; ++i) {
    uint64_t col_id = output_column_ids_.at(i);
    switch(col_id) {
      case PLAN_ID: {
        cells[i].set_int(pstat.plan_id_);
        break;
      }
      case OPERATION_ID: {
        cells[i].set_int(pstat.operation_id_);
        break;
      }
      case EXECUTIONS: {
        cells[i].set_int(pstat.execute_times_);
        break;
      }
      case OUTPUT_ROWS: {
        cells[i].set_int(pstat.output_rows_);
        break;
      }

      case INPUT_ROWS: {
        cells[i].set_int(pstat.input_rows_);
        break;
      }

      case RESCAN_TIMES: {
        cells[i].set_int(pstat.rescan_times_);
        break;
      }
      case BUFFER_GETS: {
        cells[i].set_int(0);
        break;
      }
      case DISK_READS: {
        cells[i].set_int(0);
        break;
      }
      case DISK_WRITES: {
        cells[i].set_int(0);
        break;
      }
      case ELAPSED_TIME: {
        cells[i].set_int(0);
        break;
      }
      case EXTEND_INFO1: {
        cells[i].set_null();
        break;
      }
      case EXTEND_INFO2: {
        cells[i].set_null();
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
  } // end for
  return ret;
}

int ObVirtualSqlPlanStatistics::inner_get_next_row(common::ObNewRow *&row)
{
  int ret = OB_SUCCESS;
  bool is_sub_end = false;
  // At most one MOD_SCOPE pass
  if (iter_end_) {
    ret = OB_ITER_END;
  } else {
    MOD_SCOPE {
      if (OB_FAIL(get_row_from_specified_tenant(is_sub_end))) {
      } else if (is_sub_end) {
        iter_end_ = true;
        ret = OB_ITER_END;
      }
    }
  }
  if (OB_SUCC(ret)) {
    row = &cur_row_;
  }
  return ret;
}
} //end namespace observer
} //end namespace oceanbase

