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
 
#include "observer/virtual_table/rowkey_filter_value.h"
#include "observer/ob_server_utils.h"
#include "observer/omt/ob_server_runtime_controller.h"  // previously hidden behind the server_struct include chain, make the dependency explicit
#include "share/rc/ob_server_runtime.h"
#include "ob_all_virtual_sql_plan.h"
#include "sql/engine/ob_physical_plan.h"
#include "sql/monitor/ob_plan_info_manager.h"

using namespace oceanbase::common;
using namespace oceanbase::sql;
using namespace oceanbase::share;
namespace oceanbase {
namespace observer {

const int64_t ObAllVirtualSqlPlan::KEY_PLAN_ID_IDX;
const int64_t ObAllVirtualSqlPlan::ROWKEY_COUNT;

ObAllVirtualSqlPlan::PlanInfo::PlanInfo()
{
  reset();
}

ObAllVirtualSqlPlan::PlanInfo::~PlanInfo()
{

}

void ObAllVirtualSqlPlan::PlanInfo::reset()
{
  plan_id_ = OB_INVALID_ID;
  
}

ObAllVirtualSqlPlan::DumpAllPlan::DumpAllPlan()
{
  reset();
}

ObAllVirtualSqlPlan::DumpAllPlan::~DumpAllPlan()
{

}

void ObAllVirtualSqlPlan::DumpAllPlan::reset()
{
  plan_ids_ = NULL;
  
}

int ObAllVirtualSqlPlan::DumpAllPlan::operator()(
    common::hash::HashMapPair<ObCacheObjID, ObILibCacheObject *> &entry)
{
  int ret = common::OB_SUCCESS;
  ObPhysicalPlan *plan = NULL;
  if (OB_ISNULL(entry.second) || OB_ISNULL(plan_ids_)) {
    ret = common::OB_NOT_INIT;
    SERVER_LOG(WARN, "invalid argument", K(ret));
  } else if (ObLibCacheNameSpace::NS_CRSR != entry.second->get_ns()) {
    // not sql plan
    // do nothing
  } else if (OB_ISNULL(plan = dynamic_cast<ObPhysicalPlan *>(entry.second))) {
    //do nothing
  } else if (NULL != plan->get_logical_plan().logical_plan_) {
    PlanInfo info;
    
    info.plan_id_ = plan->get_plan_id();
    if (OB_FAIL(plan_ids_->push_back(info))) {
    }
  }
  return ret;
}

ObAllVirtualSqlPlan::ObAllVirtualSqlPlan() :
    ObVirtualTableScannerIterator(),
    plan_ids_(),
    plan_idx_(0),
    plan_items_(),
    plan_item_idx_(0),
    sql_id_(),
    db_id_(0),
    plan_hash_(0),
    gmt_create_(0),
    plan_id_(0)
{
}

ObAllVirtualSqlPlan::~ObAllVirtualSqlPlan() {
  reset();
}

void ObAllVirtualSqlPlan::reset()
{
  ObVirtualTableScannerIterator::reset();
  plan_idx_ = 0;
  plan_items_.reuse();
  plan_item_idx_ = 0;
}

int ObAllVirtualSqlPlan::inner_open()
{
  int ret = OB_SUCCESS;
  if (OB_FAIL(extract_plan_ids(key_ranges_))) {
  }
  return ret;
}

int ObAllVirtualSqlPlan::inner_get_next_row(common::ObNewRow *&row)
{
  int ret = OB_SUCCESS;
  while (OB_SUCC(ret) && plan_item_idx_ >= plan_items_.count()) {
    if (plan_idx_ >= plan_ids_.count()) {
      ret = OB_ITER_END;
    } else if (OB_FAIL(prepare_next_plan())) {
    }
  }
  if (OB_FAIL(ret)) {
  } else if (OB_FAIL(fill_cells(plan_items_.at(plan_item_idx_++)))) {
  } else {
    //finish fetch one row
    row = &cur_row_;
  }
  return ret;
}

int ObAllVirtualSqlPlan::fill_cells(ObSqlPlanItem *plan_item)
{
  int ret = OB_SUCCESS;
  const int64_t col_count = output_column_ids_.count();
  DatumValue *cells = cur_row_.cells_;
  #define REFINE_LENGTH(len) ((len) > MAX_LENGTH ? MAX_LENGTH : (len))
  if (OB_ISNULL(cells) || OB_ISNULL(plan_item)) {
    ret = OB_INVALID_ARGUMENT;
    SERVER_LOG(WARN, "invalid argument", K(cells));
  }
  for (int64_t cell_idx = 0; OB_SUCC(ret) && cell_idx < col_count; cell_idx++) {
    uint64_t col_id = output_column_ids_.at(cell_idx);
    switch(col_id) {
    case PLAN_ID: {
      (OB_SUCC(ret) ? ret = DatumValueBuffer::set_int(cells, cur_row_.count_, cell_idx, plan_id_) : ret);
      break;
    }
    case SQL_ID: {
      (OB_SUCC(ret) ? ret = DatumValueBuffer::set_varchar_view(cells, cur_row_.count_, cell_idx, sql_id_) : ret);
      cells[cell_idx].set_collation_type(ObCharset::get_default_collation(
                                         ObCharset::get_default_charset()));
      break;
    }
    case DB_ID: {
      (OB_SUCC(ret) ? ret = DatumValueBuffer::set_int(cells, cur_row_.count_, cell_idx, db_id_) : ret);
      break;
    }
    case PLAN_HASH: {
      (OB_SUCC(ret) ? ret = DatumValueBuffer::set_uint64(cells, cur_row_.count_, cell_idx, plan_hash_) : ret);
      break;
    }
    case GMT_CREATE: {
      (OB_SUCC(ret) ? ret = DatumValueBuffer::set_timestamp(cells, cur_row_.count_, cell_idx, gmt_create_) : ret);
      break;
    }
    case OPERATOR: {
      (OB_SUCC(ret) ? ret = DatumValueBuffer::set_varchar_view(cells, cur_row_.count_, cell_idx, ObString(plan_item->operation_len_, plan_item->operation_)) : ret);
      cells[cell_idx].set_collation_type(ObCharset::get_default_collation(
                                         ObCharset::get_default_charset()));
      break;
    }
    case OPTIONS: {
      (OB_SUCC(ret) ? ret = DatumValueBuffer::set_varchar_view(cells, cur_row_.count_, cell_idx, ObString(plan_item->options_len_, plan_item->options_)) : ret);
      cells[cell_idx].set_collation_type(ObCharset::get_default_collation(
                                         ObCharset::get_default_charset()));
      break;
    }
    case OBJECT_NODE: {
      (OB_SUCC(ret) ? ret = DatumValueBuffer::set_varchar_view(cells, cur_row_.count_, cell_idx, ObString(plan_item->object_node_len_, plan_item->object_node_)) : ret);
      cells[cell_idx].set_collation_type(ObCharset::get_default_collation(
                                         ObCharset::get_default_charset()));
      break;
    }
    case OBJECT_ID: {
      (OB_SUCC(ret) ? ret = DatumValueBuffer::set_int(cells, cur_row_.count_, cell_idx, plan_item->object_id_) : ret);
      break;
    }
    case OBJECT_OWNER: {
      (OB_SUCC(ret) ? ret = DatumValueBuffer::set_varchar_view(cells, cur_row_.count_, cell_idx, ObString(plan_item->object_owner_len_, plan_item->object_owner_)) : ret);
      cells[cell_idx].set_collation_type(ObCharset::get_default_collation(
                                         ObCharset::get_default_charset()));
      break;
    }
    case OBJECT_NAME: {
      (OB_SUCC(ret) ? ret = DatumValueBuffer::set_varchar_view(cells, cur_row_.count_, cell_idx, ObString(plan_item->object_name_len_, plan_item->object_name_)) : ret);
      cells[cell_idx].set_collation_type(ObCharset::get_default_collation(
                                         ObCharset::get_default_charset()));
      break;
    }
    case OBJECT_ALIAS: {
      (OB_SUCC(ret) ? ret = DatumValueBuffer::set_varchar_view(cells, cur_row_.count_, cell_idx, ObString(plan_item->object_alias_len_, plan_item->object_alias_)) : ret);
      cells[cell_idx].set_collation_type(ObCharset::get_default_collation(
                                         ObCharset::get_default_charset()));
      break;
    }
    case OBJECT_TYPE: {
      (OB_SUCC(ret) ? ret = DatumValueBuffer::set_varchar_view(cells, cur_row_.count_, cell_idx, ObString(plan_item->object_type_len_, plan_item->object_type_)) : ret);
      cells[cell_idx].set_collation_type(ObCharset::get_default_collation(
                                         ObCharset::get_default_charset()));
      break;
    }
    case OPTIMIZER: {
      (OB_SUCC(ret) ? ret = DatumValueBuffer::set_varchar_view(cells, cur_row_.count_, cell_idx, ObString(REFINE_LENGTH(plan_item->optimizer_len_), plan_item->optimizer_)) : ret);
      cells[cell_idx].set_collation_type(ObCharset::get_default_collation(
                                         ObCharset::get_default_charset()));
      break;
    }
    case ID: {
      (OB_SUCC(ret) ? ret = DatumValueBuffer::set_int(cells, cur_row_.count_, cell_idx, plan_item->id_) : ret);
      break;
    }
    case PARENT_ID: {
      (OB_SUCC(ret) ? ret = DatumValueBuffer::set_int(cells, cur_row_.count_, cell_idx, plan_item->parent_id_) : ret);
      break;
    }
    case DEPTH: {
      (OB_SUCC(ret) ? ret = DatumValueBuffer::set_int(cells, cur_row_.count_, cell_idx, plan_item->depth_) : ret);
      break;
    }
    case POSITION: {
      (OB_SUCC(ret) ? ret = DatumValueBuffer::set_int(cells, cur_row_.count_, cell_idx, plan_item->position_) : ret);
      break;
    }
    case SEARCH_COLUMNS: {
      (OB_SUCC(ret) ? ret = DatumValueBuffer::set_int(cells, cur_row_.count_, cell_idx, plan_item->search_columns_) : ret);
      break;
    }
    case IS_LAST_CHILD: {
      (OB_SUCC(ret) ? ret = DatumValueBuffer::set_int(cells, cur_row_.count_, cell_idx, (int)plan_item->is_last_child_) : ret);
      break;
    }
    case COST: {
      if (plan_item->cost_ < 0) {
        (OB_SUCC(ret) ? ret = DatumValueBuffer::set_int(cells, cur_row_.count_, cell_idx, INT64_MAX) : ret);
      } else {
        (OB_SUCC(ret) ? ret = DatumValueBuffer::set_int(cells, cur_row_.count_, cell_idx, plan_item->cost_) : ret);
      }
      break;
    }
    case REAL_COST: {
      (OB_SUCC(ret) ? ret = DatumValueBuffer::set_int(cells, cur_row_.count_, cell_idx, plan_item->real_cost_) : ret);
      break;
    }
    case CARDINALITY: {
      if (plan_item->cardinality_ < 0) {
        (OB_SUCC(ret) ? ret = DatumValueBuffer::set_int(cells, cur_row_.count_, cell_idx, INT64_MAX) : ret);
      } else {
        (OB_SUCC(ret) ? ret = DatumValueBuffer::set_int(cells, cur_row_.count_, cell_idx, plan_item->cardinality_) : ret);
      }
      break;
    }
    case REAL_CARDINALITY: {
      (OB_SUCC(ret) ? ret = DatumValueBuffer::set_int(cells, cur_row_.count_, cell_idx, plan_item->real_cardinality_) : ret);
      break;
    }
    case BYTES: {
      (OB_SUCC(ret) ? ret = DatumValueBuffer::set_int(cells, cur_row_.count_, cell_idx, plan_item->bytes_) : ret);
      break;
    }
    case ROWSET: {
      (OB_SUCC(ret) ? ret = DatumValueBuffer::set_int(cells, cur_row_.count_, cell_idx, plan_item->rowset_) : ret);
      break;
    }
    case OTHER_TAG: {
      (OB_SUCC(ret) ? ret = DatumValueBuffer::set_varchar_view(cells, cur_row_.count_, cell_idx, ObString(REFINE_LENGTH(plan_item->other_tag_len_), plan_item->other_tag_)) : ret);
      cells[cell_idx].set_collation_type(ObCharset::get_default_collation(
                                         ObCharset::get_default_charset()));
      break;
    }
    case PARTITION_START: {
      (OB_SUCC(ret) ? ret = DatumValueBuffer::set_varchar_view(cells, cur_row_.count_, cell_idx, ObString(REFINE_LENGTH(plan_item->partition_start_len_), plan_item->partition_start_)) : ret);
      cells[cell_idx].set_collation_type(ObCharset::get_default_collation(
                                         ObCharset::get_default_charset()));
      break;
    }
    case PARTITION_STOP: {
      (OB_SUCC(ret) ? ret = DatumValueBuffer::set_varchar_view(cells, cur_row_.count_, cell_idx, ObString(REFINE_LENGTH(plan_item->partition_stop_len_), plan_item->partition_stop_)) : ret);
      cells[cell_idx].set_collation_type(ObCharset::get_default_collation(
                                         ObCharset::get_default_charset()));
      break;
    }
    case PARTITION_ID: {
      (OB_SUCC(ret) ? ret = DatumValueBuffer::set_int(cells, cur_row_.count_, cell_idx, plan_item->partition_id_) : ret);
      break;
    }
    case OTHER: {
      (OB_SUCC(ret) ? ret = DatumValueBuffer::set_varchar_view(cells, cur_row_.count_, cell_idx, ObString(REFINE_LENGTH(plan_item->other_len_), plan_item->other_)) : ret);
      cells[cell_idx].set_collation_type(ObCharset::get_default_collation(
                                         ObCharset::get_default_charset()));
      break;
    }
    case DISTRIBUTION: {
      (OB_SUCC(ret) ? ret = DatumValueBuffer::set_varchar_view(cells, cur_row_.count_, cell_idx, ObString(plan_item->distribution_len_, plan_item->distribution_)) : ret);
      cells[cell_idx].set_collation_type(ObCharset::get_default_collation(
                                         ObCharset::get_default_charset()));
      break;
    }
    case CPU_COST: {
      (OB_SUCC(ret) ? ret = DatumValueBuffer::set_int(cells, cur_row_.count_, cell_idx, plan_item->cpu_cost_) : ret);
      break;
    }
    case IO_COST: {
      (OB_SUCC(ret) ? ret = DatumValueBuffer::set_int(cells, cur_row_.count_, cell_idx, plan_item->io_cost_) : ret);
      break;
    }
    case TEMP_SPACE: {
      (OB_SUCC(ret) ? ret = DatumValueBuffer::set_int(cells, cur_row_.count_, cell_idx, plan_item->temp_space_) : ret);
      break;
    }
    case ACCESS_PREDICATES: {
      (OB_SUCC(ret) ? ret = DatumValueBuffer::set_varchar_view(cells, cur_row_.count_, cell_idx, ObString(REFINE_LENGTH(plan_item->access_predicates_len_), plan_item->access_predicates_)) : ret);
      cells[cell_idx].set_collation_type(ObCharset::get_default_collation(
                                         ObCharset::get_default_charset()));
      break;
    }
    case FILTER_PREDICATES: {
      (OB_SUCC(ret) ? ret = DatumValueBuffer::set_varchar_view(cells, cur_row_.count_, cell_idx, ObString(REFINE_LENGTH(plan_item->filter_predicates_len_), plan_item->filter_predicates_)) : ret);
      cells[cell_idx].set_collation_type(ObCharset::get_default_collation(
                                         ObCharset::get_default_charset()));
      break;
    }
    case STARTUP_PREDICATES: {
      (OB_SUCC(ret) ? ret = DatumValueBuffer::set_varchar_view(cells, cur_row_.count_, cell_idx, ObString(REFINE_LENGTH(plan_item->startup_predicates_len_), plan_item->startup_predicates_)) : ret);
      cells[cell_idx].set_collation_type(ObCharset::get_default_collation(
                                         ObCharset::get_default_charset()));
      break;
    }
    case PROJECTION: {
      (OB_SUCC(ret) ? ret = DatumValueBuffer::set_varchar_view(cells, cur_row_.count_, cell_idx, ObString(REFINE_LENGTH(plan_item->projection_len_), plan_item->projection_)) : ret);
      cells[cell_idx].set_collation_type(ObCharset::get_default_collation(
                                         ObCharset::get_default_charset()));
      break;
    }
    case SPECIAL_PREDICATES: {
      (OB_SUCC(ret) ? ret = DatumValueBuffer::set_varchar_view(cells, cur_row_.count_, cell_idx, ObString(REFINE_LENGTH(plan_item->special_predicates_len_), plan_item->special_predicates_)) : ret);
      cells[cell_idx].set_collation_type(ObCharset::get_default_collation(
                                         ObCharset::get_default_charset()));
      break;
    }
    case TIME: {
      (OB_SUCC(ret) ? ret = DatumValueBuffer::set_int(cells, cur_row_.count_, cell_idx, plan_item->time_) : ret);
      break;
    }
    case QBLOCK_NAME: {
      (OB_SUCC(ret) ? ret = DatumValueBuffer::set_varchar_view(cells, cur_row_.count_, cell_idx, ObString(plan_item->qblock_name_len_, plan_item->qblock_name_)) : ret);
      cells[cell_idx].set_collation_type(ObCharset::get_default_collation(
                                         ObCharset::get_default_charset()));
      break;
    }
    case REMARKS: {
      (OB_SUCC(ret) ? ret = DatumValueBuffer::set_varchar_view(cells, cur_row_.count_, cell_idx, ObString(REFINE_LENGTH(plan_item->remarks_len_), plan_item->remarks_)) : ret);
      cells[cell_idx].set_collation_type(ObCharset::get_default_collation(
                                         ObCharset::get_default_charset()));
      break;
    }
    case OTHER_XML: {
      (OB_SUCC(ret) ? ret = DatumValueBuffer::set_varchar_view(cells, cur_row_.count_, cell_idx, ObString(REFINE_LENGTH(plan_item->other_xml_len_), plan_item->other_xml_)) : ret);
      cells[cell_idx].set_collation_type(ObCharset::get_default_collation(
                                         ObCharset::get_default_charset()));
      break;
    }
    default: {
      ret = OB_ERR_UNEXPECTED;
      SERVER_LOG(WARN, "invalid column id", K(ret), K(cell_idx), K(col_id));
      break;
    }
    }
  }
  return ret;
}

int ObAllVirtualSqlPlan::extract_plan_ids(const common::ObIArray<common::ObNewRange> &ranges)
{
  int ret = OB_SUCCESS;
  ObRowkey start_key, end_key;
  int64_t N = ranges.count();
  bool is_always_true = false;
  bool is_always_false = false;
  plan_ids_.reuse();
  
  for (int64_t i = 0; OB_SUCC(ret) && !is_always_true && !is_always_false && i < N; i++) {
    start_key.reset();
    end_key.reset();
    start_key = ranges.at(i).start_key_;
    end_key = ranges.at(i).end_key_;
    const DatumValue *start_key_obj_ptr = start_key.get_values();
    const DatumValue *end_key_obj_ptr = end_key.get_values();

    if (start_key.get_value_count() != end_key.get_value_count() ||
        start_key.get_value_count() != ROWKEY_COUNT) {
      ret = OB_ERR_UNEXPECTED;
      SERVER_LOG(WARN, "invalid range", K(ret), K(start_key.get_value_count()), K(ROWKEY_COUNT));
    } else if (OB_ISNULL(start_key_obj_ptr) || OB_ISNULL(end_key_obj_ptr)) {
      ret = OB_INVALID_ARGUMENT;
      SERVER_LOG(WARN, "invalid arguments", K(ret));
    } else if (start_key_obj_ptr[KEY_PLAN_ID_IDX].is_min_value() && 
               end_key_obj_ptr[KEY_PLAN_ID_IDX].is_max_value()) {
      is_always_true = true;
      if (OB_FAIL(dump_plans())) {
      }
    } else if (start_key_obj_ptr[KEY_PLAN_ID_IDX].is_max_value() &&
               end_key_obj_ptr[KEY_PLAN_ID_IDX].is_min_value()) {
      is_always_false = true;
    } else if (start_key_obj_ptr[KEY_PLAN_ID_IDX].is_min_value() ||
               end_key_obj_ptr[KEY_PLAN_ID_IDX].is_max_value() ||
               key_filter_not_equal(start_key_obj_ptr[KEY_PLAN_ID_IDX], end_key_obj_ptr[KEY_PLAN_ID_IDX])) {
      ret = OB_NOT_IMPLEMENT;
      SERVER_LOG(WARN, "plan id only supports exact value", K(ret));
    } else if (key_filter_equal(start_key_obj_ptr[KEY_PLAN_ID_IDX], end_key_obj_ptr[KEY_PLAN_ID_IDX])) {
      if (ObIntType != start_key_obj_ptr[KEY_PLAN_ID_IDX].get_type() || 
          (start_key_obj_ptr[KEY_PLAN_ID_IDX].get_type() != end_key_obj_ptr[KEY_PLAN_ID_IDX].get_type())) {
        ret = OB_ERR_UNEXPECTED;
        SERVER_LOG(WARN, "expect plan id type to be int",
                    K(start_key_obj_ptr[KEY_PLAN_ID_IDX].get_type()),
                    K(end_key_obj_ptr[KEY_PLAN_ID_IDX].get_type()));
      } else {
        int64_t plan_id = start_key_obj_ptr[KEY_PLAN_ID_IDX].get_int();
        PlanInfo info;
        
        info.plan_id_ = plan_id;
        if (OB_FAIL(plan_ids_.push_back(info))) {
        }
      }
    }
  } // for end
  return ret;
}

int ObAllVirtualSqlPlan::dump_plans()
{
  int ret = OB_SUCCESS;
  {
    DumpAllPlan dump_plan;
    
    dump_plan.plan_ids_ = &plan_ids_;
    // !!!Before referencing plan cache resources, ObReqTimeGuard must be added
    ObReqTimeGuard req_timeinfo_guard;
    ObPlanCache *plan_cache = NULL;
    SERVER_MODULE_SCOPE {
      plan_cache = ::oceanbase::share::server_service<::oceanbase::sql::ObPlanCache>();
      if (OB_ISNULL(plan_cache)) {
        ret = OB_ERR_UNEXPECTED;
        SERVER_LOG(WARN, "unexpect null plan cache", K(ret));
      } else if (OB_FAIL(plan_cache->foreach_alloc_cache_obj(dump_plan))) {
      }
    }
    if (OB_OP_NOT_ALLOW == ret) {
      ret = OB_SUCCESS;
    }
  }
  return ret;
}

int ObAllVirtualSqlPlan::prepare_next_plan()
{
  int ret = OB_SUCCESS;
  //init plan item array
  plan_items_.reuse();
  plan_item_idx_ = 0;
  if (plan_idx_ >= plan_ids_.count()) {
    ret = OB_ERR_UNEXPECTED;
    SERVER_LOG(WARN, "no more plan", K(ret));
  } else if (OB_INVALID_INDEX == plan_ids_.at(plan_idx_).plan_id_) {
    //next plan
    ++plan_idx_;
  } else {
    
    plan_id_ = plan_ids_.at(plan_idx_).plan_id_;
    //next plan
    ++plan_idx_;
    ObPhysicalPlan *plan = NULL;
    // !!!Before referencing plan cache resources, ObReqTimeGuard must be added
    ObReqTimeGuard req_timeinfo_guard;
    ObPlanCache *plan_cache = NULL;
    ObCacheObjGuard guard;
    int tmp_ret = OB_SUCCESS;
    SERVER_MODULE_SCOPE {
      plan_cache = ::oceanbase::share::server_service<::oceanbase::sql::ObPlanCache>();
      if (OB_SUCCESS != (tmp_ret = plan_cache->ref_alloc_plan(plan_id_, guard))) {
        // should not panic
      } else if (FALSE_IT(plan = static_cast<ObPhysicalPlan*>(guard.get_cache_obj()))) {
        // do nothing
      } else if (OB_ISNULL(plan)) {
        // maybe pl object, do nothing
      } else {
        //decompress logical plan
        ObLogicalPlanRawData &raw_plan = plan->get_logical_plan();
        if (OB_ISNULL(allocator_)) {
          ret = OB_ERR_UNEXPECTED;
          SERVER_LOG(WARN, "unexpect null allocator", K(ret));
        } else if (OB_FAIL(raw_plan.uncompress_logical_plan(*allocator_, plan_items_))) {
        } else {
          db_id_ = plan->stat_.db_id_;
          plan_hash_ = plan->get_plan_hash_value();
          gmt_create_ = plan->stat_.gen_time_;
          if (plan->stat_.sql_id_.length() <= OB_MAX_SQL_ID_LENGTH) {
            MEMCPY(sql_id_, plan->stat_.sql_id_.ptr(), plan->stat_.sql_id_.length());
          } else {
            MEMSET(sql_id_, 0, OB_MAX_SQL_ID_LENGTH);
          }
        }
      }
    }
  }
  return ret;
}

} //namespace observer
} //namespace oceanbase
