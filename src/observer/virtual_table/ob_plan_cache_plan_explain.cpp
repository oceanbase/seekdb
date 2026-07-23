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

#include "ob_plan_cache_plan_explain.h"
#include "share/rc/ob_module_provider.h"
#include "observer/ob_server_utils.h"
#include "sql/ob_sql.h"
#include "sql/engine/table/ob_table_scan_op.h"
using namespace oceanbase;
using namespace oceanbase::observer;
using namespace oceanbase::sql;
using namespace oceanbase::common;
using namespace oceanbase::share::schema;

namespace oceanbase
{
namespace observer
{
template<class Op>
int ObExpVisitor::add_row(const Op &cur_op)
{
  int ret = OB_SUCCESS;
  ObObj *cells = NULL;
  const int64_t col_count = output_column_ids_.count();
  if (OB_ISNULL(cells = cur_row_.cells_)) {
    ret = OB_ERR_UNEXPECTED;
    SERVER_LOG(WARN, "cur row cell is NULL", K(ret));
  } else {
    ObQueryFlag scan_flag;
    for (int64_t i =  0; OB_SUCC(ret) && i < col_count; ++i) {
      uint64_t col_id = output_column_ids_.at(i);
      switch(col_id) {
      case ObPlanCachePlanExplain::PLAN_ID_COL: {
        cells[i].set_int(plan_id_);
        break;
      }
      case ObPlanCachePlanExplain::OP_NAME_COL: {
        char *buf = NULL;
        int64_t buf_len = cur_op.get_plan_depth() + strlen(cur_op.get_name()) + 1;
        int64_t pos = 0;
        if (OB_ISNULL(buf = static_cast<char *> (allocator_.alloc(buf_len)))) {
          ret = OB_ALLOCATE_MEMORY_FAILED;
        } else {
          int64_t j = cur_op.get_plan_depth();
          while (j > 0 && pos < buf_len) {
            BUF_PRINTF(" ");
            --j;
          }
          if (OB_UNLIKELY(0 > snprintf(buf + pos, buf_len - pos, "%s", cur_op.get_name()))) {
            ret = OB_ERR_UNEXPECTED;
            SERVER_LOG(WARN, "fail to gen operator name");
          } else {
            cells[i].set_varchar(buf);
            cells[i].set_collation_type(ObCharset::get_default_collation(
                                        ObCharset::get_default_charset()));
          }
        }
        break;
      }
      case ObPlanCachePlanExplain::TBL_NAME_COL: {
        ObString tbl_name;
        ret = get_table_name(cur_op, tbl_name);
        if (OB_SUCC(ret)) {
          cells[i].set_varchar(tbl_name);
          cells[i].set_collation_type(ObCharset::get_default_collation(
                                      ObCharset::get_default_charset()));
        }
        break;
      }
      case ObPlanCachePlanExplain::ROWS_COL: {
        cells[i].set_int(cur_op.get_rows());
        break;
      }
      case ObPlanCachePlanExplain::COST_COL: {
        cells[i].set_int(cur_op.get_cost());
        break;
      }
      case ObPlanCachePlanExplain::PROPERTY_COL: {
        ObString property;
        ret = get_property(cur_op, property);
        if (OB_SUCC(ret)) {
          cells[i].set_varchar(property);
          cells[i].set_collation_type(ObCharset::get_default_collation(
                                      ObCharset::get_default_charset()));
        }
        break;
      }
      case ObPlanCachePlanExplain::PLAN_DEPTH_COL: {
        cells[i].set_int(cur_op.get_plan_depth());
        break;
      }
      case ObPlanCachePlanExplain::PLAN_LINE_ID_COL: {
        cells[i].set_int(cur_op.get_id());
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
    if (OB_SUCC(ret)) {
      // deep copy row
      if (OB_FAIL(scanner_.add_row(cur_row_))) {
        SERVER_LOG(WARN, "fail to add row", K(ret), K(cur_row_));
      } else {
        // free memory
        allocator_.reuse();
      }
    }
  }
  return ret;
}

int ObOpSpecExpVisitor::add_row(const sql::ObOpSpec &op)
{
  return ObExpVisitor::add_row(op);
}

template<>
int ObExpVisitor::get_table_name<ObOpSpec>(const ObOpSpec &cur_op, ObString &table_name)
{
  int ret = OB_SUCCESS;
  char *buffer = NULL;
  ObString index_name;
  ObString tmp_table_name;
  if (OB_ISNULL(buffer = static_cast<char *>(allocator_.alloc(OB_MAX_PLAN_EXPLAIN_NAME_LENGTH)))) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
    SERVER_LOG(WARN, "failed to allocate memory", K(ret));
  } else if (PHY_TABLE_SCAN == cur_op.get_type()) {
    const ObTableScanSpec &tsc_spec = static_cast<const ObTableScanSpec &>(cur_op);
    ObQueryFlag scan_flag;
    scan_flag.flag_ = tsc_spec.flags_;
    tmp_table_name = tsc_spec.table_name_;
    index_name = tsc_spec.index_name_;
    if (OB_FAIL(get_table_access_desc(tsc_spec.should_scan_index(), scan_flag,
                                      tmp_table_name, index_name, table_name))) {
        SERVER_LOG(WARN, "failed to get table name", K(ret));
    }
  } else {
    table_name = ObString::make_string("NULL");
  }
  return ret;
}

int ObExpVisitor::get_table_access_desc(bool is_idx_access, const ObQueryFlag &scan_flag, ObString &tab_name,
                                        const ObString &index_name, ObString &ret_name)
{
  int ret = OB_SUCCESS;
  char *buffer = NULL;
  int64_t tmp_pos = 0;

  if (OB_ISNULL(buffer = static_cast<char *>(allocator_.alloc(OB_MAX_PLAN_EXPLAIN_NAME_LENGTH)))) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
    SERVER_LOG(WARN, "failed to allocate memory", K(ret));
  } else {
    tmp_pos = tab_name.to_string(buffer, OB_MAX_PLAN_EXPLAIN_NAME_LENGTH);
    if (is_idx_access) {
        IGNORE_RETURN snprintf(buffer+tmp_pos,
                              OB_MAX_PLAN_EXPLAIN_NAME_LENGTH-tmp_pos,
                              LEFT_BRACKET);
        tmp_pos += strlen(LEFT_BRACKET);
        tmp_pos += index_name.to_string(buffer+tmp_pos,
                                        OB_MAX_PLAN_EXPLAIN_NAME_LENGTH - tmp_pos);
        if (scan_flag.is_reverse_scan()) {
          IGNORE_RETURN snprintf(buffer + tmp_pos,
                                 OB_MAX_PLAN_EXPLAIN_NAME_LENGTH - tmp_pos,
                                 COMMA_REVERSE);
          tmp_pos += strlen(COMMA_REVERSE);
        } else {
          // do nothing
        }
        IGNORE_RETURN snprintf(buffer + tmp_pos,
                               OB_MAX_PLAN_EXPLAIN_NAME_LENGTH - tmp_pos,
                               RIGHT_BRACKET);
        tmp_pos += strlen(RIGHT_BRACKET);
    } else {
      if (scan_flag.is_reverse_scan()) {
        IGNORE_RETURN snprintf(buffer + tmp_pos,
                               OB_MAX_PLAN_EXPLAIN_NAME_LENGTH - tmp_pos,
                               BRACKET_REVERSE);
        tmp_pos += strlen(BRACKET_REVERSE);
      } else { /* Do nothing */ }
    }

   if (OB_SUCC(ret)) {
      ret_name.assign_ptr(buffer, tmp_pos);
    }
  }
  return ret;
}

template<>
int ObExpVisitor::get_property<ObOpSpec>(const ObOpSpec &cur_op,
                                         common::ObString &property)
{
  int ret = OB_SUCCESS;
  char *buf = NULL;
  int64_t pos = 0;
  if (OB_ISNULL(buf = static_cast<char *> (allocator_.alloc(OB_MAX_OPERATOR_PROPERTY_LENGTH)))) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
    SERVER_LOG(WARN, "failed to allocate memory", K(ret));
  } else {
    switch (cur_op.get_type()) {
    case PHY_TABLE_SCAN: {
      if (OB_FAIL(static_cast<const ObTableScanSpec &>(cur_op).explain_index_selection_info(
                  buf, OB_MAX_OPERATOR_PROPERTY_LENGTH, pos))) {
        if (ret == OB_SIZE_OVERFLOW) {
          ret = OB_SUCCESS;
          SERVER_LOG(INFO,
                     "The properties of ObTableScanSpec exceed "
                     "OB_MAX_OPERATOR_PROPERTY_LENGTH and have been truncated.",
                     K(ret), K(OB_MAX_OPERATOR_PROPERTY_LENGTH));
        } else {
          SERVER_LOG(WARN, "fail to gen property", K(ret));
        }
      } else {
        property.assign_ptr(buf, pos);
      }
      break;
    }
    default: {
      property = ObString::make_string("NULL");
      break;
    }
    }
  }
  return ret;
}

int ObCacheObjIterator::next(ObSessionPlanCacheEntry &entry)
{
  int ret = OB_SUCCESS;
  if (!initialized_) {
    if (OB_FAIL(collect_session_plan_cache_entries(GCTX.session_mgr_,
                                                   entries_))) {
      SERVER_LOG(WARN, "failed to collect session sql plans", K(ret));
    } else {
      initialized_ = true;
      entry_idx_ = 0;
    }
  }
  if (OB_SUCC(ret)) {
    if (entry_idx_ >= entries_.count()) {
      ret = OB_ITER_END;
    } else {
      entry = entries_.at(entry_idx_++);
    }
  }
  return ret;
}

int ObPlanCachePlanExplain::set_tenant_plan_id(const common::ObIArray<common::ObNewRange> &ranges)
{
  int ret = OB_SUCCESS;
  // display only one plan
  // In single-node mode, rowkey only has plan_id (index 0)
  if (ranges.count() == 1 && ranges.at(0).is_single_rowkey()) {
    ObRowkey start_key = ranges.at(0).start_key_;
    const ObObj *start_key_obj_ptr = start_key.get_obj_ptr();
    scan_all_plan_ = false;
    if (OB_ISNULL(start_key_obj_ptr)
        || start_key.get_obj_cnt() < 1) {
      ret = OB_ERR_UNEXPECTED;
      SERVER_LOG(WARN,
                 "fail to init plan visitor",
                 K(ret),
                 K(start_key_obj_ptr),
                 "count", start_key.get_obj_cnt());
    } else {
      
      plan_id_ = start_key_obj_ptr[0].get_int();  // plan_id is at index 0
    }
  } else {
    scan_all_plan_ = true;
  }
  return ret;
}

int ObPlanCachePlanExplain::inner_open()
{
  int ret = OB_SUCCESS;
  static_engine_exp_visitor_.set_row_mem_attr();
  if (OB_FAIL(set_tenant_plan_id(key_ranges_))) {
    LOG_WARN("set tenant id and plan id failed", K(ret));
  } else if (!scan_all_plan_) {
    ObSEArray<ObSessionPlanCacheEntry, 16> entries;
    if (OB_FAIL(collect_session_plan_cache_entries(GCTX.session_mgr_,
                                                   entries))) {
      SERVER_LOG(WARN, "failed to collect session sql plans", K(ret));
    } else {
      for (int64_t i = 0; OB_SUCC(ret) && i < entries.count(); ++i) {
        if (plan_id_ == static_cast<int64_t>(entries.at(i).object_id_)) {
          if (OB_FAIL(add_plan_to_scanner(entries.at(i)))) {
            SERVER_LOG(WARN, "failed to explain session sql plan",
                       K(ret), K_(plan_id));
          }
          break;
        }
      }
    }
  } else {
    cache_obj_iterator_.reset();
  }

  if (OB_SUCC(ret)) {
    scanner_it_ = scanner_.begin();
  }

  return ret;
}

int ObPlanCachePlanExplain::add_plan_to_scanner(
    const ObSessionPlanCacheEntry &entry)
{
  int ret = OB_SUCCESS;
  int tmp_ret = OB_SUCCESS;
  ObReqTimeGuard req_timeinfo_guard;
  if (OB_ISNULL(GCTX.session_mgr_)) {
    ret = OB_ERR_UNEXPECTED;
    SERVER_LOG(WARN, "sql session manager is null", K(ret));
  } else {
    ObSessionGetterGuard session_guard(*GCTX.session_mgr_, entry.session_id_);
    ObSQLSessionInfo *session = NULL;
    tmp_ret = session_guard.get_session(session);
    if (OB_ENTRY_NOT_EXIST == tmp_ret) {
      // The session ended after the plan id snapshot was collected.
    } else if (OB_SUCCESS != tmp_ret) {
      ret = tmp_ret;
      SERVER_LOG(WARN, "failed to get plan owner session",
                 K(ret), K(entry.session_id_), K(entry.object_id_));
    } else if (OB_ISNULL(session)) {
      ret = OB_ERR_UNEXPECTED;
      SERVER_LOG(WARN, "unexpected null plan owner session", K(ret));
    } else {
      ObSessionPlanCacheLockGuard lock_guard(*session);
      if (OB_SUCCESS != lock_guard.get_lock_ret()) {
        ret = lock_guard.get_lock_ret();
        SERVER_LOG(WARN, "failed to lock plan owner session cache",
                   K(ret), K(entry.session_id_), K(entry.object_id_));
      } else {
        ObPlanCache *plan_cache = session->peek_sql_plan_cache();
        ObCacheObjGuard guard(PLAN_EXPLAIN_HANDLE);
        if (OB_ISNULL(plan_cache)) {
          // RESET CONNECTION may have cleared the cache.
        } else if (OB_SUCCESS !=
                   (tmp_ret = plan_cache->ref_plan(entry.object_id_, guard))) {
          if (OB_HASH_NOT_EXIST != tmp_ret) {
            ret = tmp_ret;
            SERVER_LOG(WARN, "failed to reference session sql plan",
                       K(ret), K(entry.session_id_), K(entry.object_id_));
          }
        } else {
          ObPhysicalPlan *plan =
              static_cast<ObPhysicalPlan *>(guard.get_cache_obj());
          if (OB_ISNULL(plan)) {
            ret = OB_ERR_UNEXPECTED;
            SERVER_LOG(WARN, "unexpected null physical plan", K(ret));
          } else if (OB_NOT_NULL(plan->get_root_op_spec())) {
            if (OB_FAIL(static_engine_exp_visitor_.init(entry.object_id_))) {
              SERVER_LOG(WARN, "failed to init visitor", K(ret));
            } else if (OB_FAIL(plan->get_root_op_spec()->accept(
                               static_engine_exp_visitor_))) {
              SERVER_LOG(WARN, "fail to traverse physical plan", K(ret));
            }
          }
        }
      }
    }
  }
  return ret;
}

int ObPlanCachePlanExplain::inner_get_next_row(common::ObNewRow *&row)
{
  int ret = OB_SUCCESS;
  if (iter_end_) {
    ret = OB_ITER_END;
  } else {
    do {
      if (OB_FAIL(scanner_it_.get_next_row(cur_row_))) {
        if (OB_ITER_END != ret) {
          SERVER_LOG(WARN, "fail to get next row", K(ret));
        } else {
          if (scan_all_plan_) {
            ret = OB_SUCCESS;
            ObReqTimeGuard req_timeinfo_guard;
            ObSessionPlanCacheEntry entry;
            if (OB_FAIL(cache_obj_iterator_.next(entry))) {
              if (OB_ITER_END == ret) {
                iter_end_ = true;
              } else {
                SERVER_LOG(WARN, "fail to get next physical plan", K(ret));
              }
            } else if (OB_FAIL(add_plan_to_scanner(entry))) {
              SERVER_LOG(WARN, "failed to explain session sql plan",
                         K(ret), K(entry.session_id_), K(entry.object_id_));
            }
          } else {
            iter_end_ = true;
          }
        }
      } else {
        row = &cur_row_;
        break;
      }
    } while (OB_SUCC(ret));
  }
  return ret;
}

ObPlanCachePlanExplain::~ObPlanCachePlanExplain()
{
}

} // end namespace observr
} // end namespace oceanbase
