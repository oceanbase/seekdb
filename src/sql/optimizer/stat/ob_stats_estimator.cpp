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

#define USING_LOG_PREFIX SQL_ENG
#include "ob_stats_estimator.h"
#include "query/session/ob_inner_sql_connection_access.h"
#include "sql/session/ob_inner_sql_connection.h"
#include "sql/optimizer/ob_opt_selectivity.h"

namespace oceanbase
{
namespace common
{

ObStatsEstimator::ObStatsEstimator(ObExecContext &ctx, ObIAllocator &allocator) :
  ctx_(ctx),
  allocator_(allocator),
  db_name_(),
  from_table_(),
  partition_hint_(),
  select_fields_(),
  sample_hint_(),
  other_hints_(),
  partition_string_(),
  group_by_string_(),
  where_string_(),
  stat_items_(),
  results_(),
  sample_value_(100.0)
{}

int ObStatsEstimator::gen_select_filed()
{
  int ret = OB_SUCCESS;
  const int64_t buf_size = 8000;
  char buf[buf_size];
  for (int64_t i = 0; OB_SUCC(ret) && i < stat_items_.count(); ++i) {
    int64_t pos = 0;
    if (i != 0 && OB_FAIL(select_fields_.append(", "))) {
      LOG_WARN("failed to append delimiter", K(ret));
    } else if (OB_FAIL(stat_items_.at(i)->gen_expr(buf, buf_size, pos))) {
    } else if (OB_FAIL(select_fields_.append(buf, pos))) {
    }
  }
  return ret;
}

int ObStatsEstimator::add_from_table(common::ObIAllocator &allocator,
                                     const ObString &db_name,
                                     const ObString &table_name)
{
  int ret = OB_SUCCESS;
  ObString new_db_name;
  ObString new_tbl_name;
  if (OB_FAIL(sql::ObSQLUtils::generate_new_name_with_escape_character(
              allocator,
              db_name,
              new_db_name))) {
  } else if (OB_FAIL(sql::ObSQLUtils::generate_new_name_with_escape_character(
                    allocator,
                    table_name,
                    new_tbl_name))) {
  } else {
    db_name_ = new_db_name;
    from_table_ = new_tbl_name;
  }
  return ret;
}

int ObStatsEstimator::pack(ObSqlString &raw_sql_str)
{
  int ret = OB_SUCCESS;
  // SELECT <hint> <fields> FROM <table> <partition hint>
  if (OB_FAIL(gen_select_filed())) {
  } else if (OB_FAIL(raw_sql_str.append_fmt("SELECT %.*s %.*s FROM `%.*s`.`%.*s` %.*s %.*s %.*s %.*s %.*s" ,
                                            other_hints_.length(),
                                            other_hints_.ptr(),
                                            static_cast<int32_t>(select_fields_.length()),
                                            select_fields_.ptr(),
                                            db_name_.length(),
                                            db_name_.ptr(),
                                            from_table_.length(),
                                            from_table_.ptr(),
                                            partition_string_.length(),
                                            partition_string_.ptr(),
                                            sample_hint_.length(),
                                            sample_hint_.ptr(),
                                            current_scn_string_.length(),
                                            current_scn_string_.ptr(),
                                            where_string_.length(),
                                            where_string_.ptr(),
                                            group_by_string_.length(),
                                            group_by_string_.ptr()))) {
  } else {
  }
  return ret;
}

int ObStatsEstimator::fill_sample_info(common::ObIAllocator &alloc,
                                       double est_percent,
                                       bool block_sample)
{
  int ret = OB_SUCCESS;
  if (est_percent>= 0.000001 && est_percent < 100.0) {
    char *buf = NULL;
    int32_t buf_len = 50;//double type generally occupies 15~16 bits, plus the character length of about 16, therefore an array length of 50 is sufficient
    int64_t real_len = -1;
    if (OB_ISNULL(buf = static_cast<char *>(alloc.alloc(buf_len)))) {
      ret = OB_ALLOCATE_MEMORY_FAILED;
      LOG_WARN("failed to alloc memory", K(ret), K(buf), K(buf_len));
    } else if (!block_sample) {
      real_len = sprintf(buf, "SAMPLE (%lf)", est_percent);
    } else {
      real_len = sprintf(buf, "SAMPLE BLOCK (%lf)", est_percent);
    }
    if (OB_SUCC(ret)) {
      if (OB_UNLIKELY(real_len < 0)) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("get unexpected error", K(ret), K(real_len));
      } else {
        sample_hint_.assign_ptr(buf, real_len);
      }
    }
  }
  return ret;
}

int ObStatsEstimator::fill_sample_info(common::ObIAllocator &alloc,
                                       const ObAnalyzeSampleInfo &sample_info)
{
  int ret = OB_SUCCESS;
  if (!sample_info.is_sample_ || sample_info.sample_type_ == SampleType::RowSample) {
  } else if (OB_UNLIKELY(sample_info.sample_value_ < 0.000001 || sample_info.sample_value_ > 100.0)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("get unexpected error", K(ret), K(sample_info));
  } else if (sample_info.sample_value_ == 100.0) {
    //do nothing
  } else if (OB_FAIL(fill_sample_info(alloc,
                                      sample_info.sample_value_,
                                      sample_info.is_block_sample_))) {
  } else {
    sample_value_ = sample_info.sample_value_;
  }
  return ret;
}

int ObStatsEstimator::fill_specify_scn_info(common::ObIAllocator &alloc,
                                            uint64_t sepcify_scn)
{
  int ret = OB_SUCCESS;
  if (sepcify_scn > 0) {
    const char* fmt_str = "as of snapshot %lu";
    char *buf = NULL;
    int64_t buf_len = strlen(fmt_str) + 30;//uint64_t:0 ~ 18446744073709551615,length no more than 20
    int64_t real_len = -1;
    if (OB_ISNULL(buf = static_cast<char *>(alloc.alloc(buf_len)))) {
      ret = OB_ALLOCATE_MEMORY_FAILED;
      LOG_WARN("failed to alloc memory", K(ret), K(buf), K(buf_len));
    } else {
      real_len = sprintf(buf, fmt_str, sepcify_scn);
      if (OB_UNLIKELY(real_len <= 0)) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("get unexpected error", K(ret), K(real_len));
      } else {
        current_scn_string_.assign_ptr(buf, real_len);
      }
    }
  }
  return ret;
}

int ObStatsEstimator::fill_parallel_info(common::ObIAllocator &alloc,
                                         int64_t degree)
{
  int ret = OB_SUCCESS;
  if (degree > 1) {
    char *buf = NULL;
    int32_t buf_len = 50;//The length of int64 will not exceed 20 digits, plus the length of ordinary characters 10, so an array length of 50 is sufficient
    int64_t real_len = -1;
    if (OB_ISNULL(buf = static_cast<char *>(alloc.alloc(buf_len)))) {
      ret = OB_ALLOCATE_MEMORY_FAILED;
      LOG_WARN("failed to alloc memory", K(ret), K(buf), K(buf_len));
    } else {
      real_len = sprintf(buf, "USE_PX, PARALLEL(%ld)", degree);
      if (OB_UNLIKELY(real_len < 0)) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("get unexpected error", K(ret), K(real_len));
      } else {
        ObString degree_str;
        degree_str.assign_ptr(buf, real_len);
        if (OB_FAIL(add_hint(degree_str, alloc))) {
        } else {
        }
      }
    }
  }
  return ret;
}

int ObStatsEstimator::fill_query_timeout_info(common::ObIAllocator &alloc,
                                              const int64_t query_timeout)
{
  int ret = OB_SUCCESS;
  if (query_timeout > 0) {
    char *buf = NULL;
    int32_t buf_len = 50;//The length of int64 will not exceed 20 digits, plus the length of ordinary characters 10, so an array length of 50 is sufficient
    int64_t real_len = -1;
    if (OB_ISNULL(buf = static_cast<char *>(alloc.alloc(buf_len)))) {
      ret = OB_ALLOCATE_MEMORY_FAILED;
      LOG_WARN("failed to alloc memory", K(ret), K(buf), K(buf_len));
    } else {
      real_len = sprintf(buf, "QUERY_TIMEOUT(%ld)", query_timeout);
      if (OB_UNLIKELY(real_len < 0)) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("get unexpected error", K(ret), K(real_len));
      } else {
        ObString query_timeout_str;
        query_timeout_str.assign_ptr(buf, real_len);
        if (OB_FAIL(add_hint(query_timeout_str, alloc))) {
        } else {
        }
      }
    }
  }
  return ret;
}

int ObStatsEstimator::add_hint(const ObString &hint_str,
                               common::ObIAllocator &alloc)
{
  int ret = OB_SUCCESS;
  if (other_hints_.empty()) {
    const char *str = "/*+*/";
    char *buf = NULL;
    if (OB_ISNULL(buf = static_cast<char *>(alloc.alloc(strlen(str))))) {
      ret = OB_ALLOCATE_MEMORY_FAILED;
      LOG_WARN("failed to alloc memory", K(ret), K(buf), K(strlen(str)));
    } else {
      MEMCPY(buf, str, strlen(str));
      other_hints_.assign_ptr(buf, static_cast<ObString::obstr_size_t>(strlen(str)));
    }
  }
  if (OB_SUCC(ret) && hint_str.length() > 0) {
    char *buf = NULL;
    int32_t len = 0;
    if (OB_ISNULL(buf = static_cast<char *>(alloc.alloc(other_hints_.length() + hint_str.length() + 1)))) {
      ret = OB_ALLOCATE_MEMORY_FAILED;
      LOG_WARN("failed to alloc memory", K(ret), K(buf), K(other_hints_.length()), K(hint_str.length()));
    } else {
      MEMCPY(buf, other_hints_.ptr(), other_hints_.length() - 2);
      len += other_hints_.length() - 2;
      MEMCPY(buf + len, " ", 1);
      len += 1;
      MEMCPY(buf + len, hint_str.ptr(), hint_str.length());
      len += hint_str.length();
      MEMCPY(buf + len, other_hints_.ptr() + other_hints_.length() - 2, 2);
      len += 2;
      other_hints_.assign_ptr(buf, len);
    }
  }
  return ret;
}

int ObStatsEstimator::fill_partition_info(ObIAllocator &allocator,
                                          const ObIArray<PartInfo> &partition_infos)
{
  int ret = OB_SUCCESS;
  ObSqlString tmp_part_str;
  if (OB_UNLIKELY(partition_infos.count() <= 1)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("get unexpected error", K(ret), K(partition_infos));
  } else {
    for (int64_t i = 0; OB_SUCC(ret) && i < partition_infos.count(); ++i) {
      if (OB_UNLIKELY(partition_infos.at(i).part_name_.empty())) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("partition name is empty", K(ret), K(partition_infos), K(i));
      } else if (OB_FAIL(tmp_part_str.append_fmt("%s`%.*s`%s",
                                                i == 0 ? "PARTITION(" : " ",
                                                partition_infos.at(i).part_name_.length(),
                                                partition_infos.at(i).part_name_.ptr(),
                                                i == partition_infos.count() - 1 ? ")" : ", "))) {
      } else {/*do nothing*/}
    }
    if (OB_SUCC(ret)) {
      char *buf = NULL;
      int64_t buf_len = tmp_part_str.length();
      if (OB_ISNULL(buf = static_cast<char *>(allocator.alloc(tmp_part_str.length())))) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_WARN("failed to alloc memory", K(ret), K(tmp_part_str.length()));
      } else {
        MEMCPY(buf, tmp_part_str.ptr(), tmp_part_str.length());
        partition_string_.assign(buf, tmp_part_str.length());
      }
    }
  }
  return ret;
}

int ObStatsEstimator::fill_partition_info(ObIAllocator &allocator,
                                          const ObString &part_name)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(part_name.empty())) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("partition name is empty", K(ret), K(part_name));
  } else {
    const char *fmt_str = "PARTITION (`%.*s`)";
    char *buf = NULL;
    const int64_t len = strlen(fmt_str) + part_name.length();
    int32_t real_len = -1;
    if (OB_ISNULL(buf = static_cast<char *>(allocator.alloc(len)))) {
      ret = OB_ALLOCATE_MEMORY_FAILED;
      LOG_WARN("failed to alloc memory", K(ret), K(len));
    } else {
      real_len = sprintf(buf, fmt_str, part_name.length(), part_name.ptr());
      if (OB_UNLIKELY(real_len < 0 || real_len >= len)) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("failed to print partition hint", K(ret), K(real_len), K(len));
      } else {
        partition_string_.assign(buf, real_len);
      }
    }
  }
  return ret;
}

//Specify the partition name or non-partition table don't use group by.
int ObStatsEstimator::fill_group_by_info(ObIAllocator &allocator,
                                         const ObOptStatGatherParam &param,
                                         ObString &calc_part_id_str)
{
  int ret = OB_SUCCESS;
  const char *fmt_str = "GROUP BY CALC_PARTITION_ID(`%.*s`, %.*s)";
  char *buf = NULL;
  ObString type_str;
  if (param.stat_level_ == PARTITION_LEVEL) {
    type_str = ObString(4, "PART");
  } else if (param.stat_level_ == SUBPARTITION_LEVEL) {
    type_str = ObString(7, "SUBPART");
  } else {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("get unexpected type", K(param.stat_level_), K(ret));
  }
  if (OB_SUCC(ret)) {
    const int64_t len = strlen(fmt_str) + param.tab_name_.length() + type_str.length();
    int32_t real_len = -1;
    if (OB_ISNULL(buf = static_cast<char *>(allocator.alloc(len)))) {
      ret = OB_ALLOCATE_MEMORY_FAILED;
      LOG_WARN("failed to alloc memory", K(ret), K(len));
    } else {
      real_len = sprintf(buf, fmt_str, param.tab_name_.length(), param.tab_name_.ptr(), type_str.length(), type_str.ptr());
      if (OB_UNLIKELY(real_len < 0 || real_len >= len)) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("failed to print partition hint", K(ret), K(real_len), K(len));
      } else {
        group_by_string_.assign(buf, real_len);
        //"GROUP BY CALC_PARTITION_ID(xxxxx)"
        const int64_t len_group_by = strlen("GROUP BY ");
        calc_part_id_str.assign_ptr(group_by_string_.ptr() + len_group_by,
                                    group_by_string_.length() - len_group_by);
      }
    }
  }
  return ret;
}

int ObStatsEstimator::do_estimate(const ObOptStatGatherParam &gather_param,
                                  const ObString &raw_sql,
                                  bool need_copy_basic_stat,
                                  ObOptStat &src_opt_stat,
                                  ObIArray<ObOptStat> &dst_opt_stats)
{
  int ret = OB_SUCCESS;
  ObCommonSqlProxy *sql_proxy = ctx_.get_sql_proxy();
  ObArenaAllocator tmp_alloc("OptStatGather", OB_MALLOC_NORMAL_BLOCK_SIZE);
  sql::ObSQLSessionInfo::StmtSavedValue *session_value = NULL;
  void *ptr = NULL;
  ObSQLSessionInfo *session = ctx_.get_my_session();
  if (OB_ISNULL(ptr = tmp_alloc.alloc(sizeof(sql::ObSQLSessionInfo::StmtSavedValue)))) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
    LOG_WARN("failed to alloc memory for saved session value", K(ret));
  } else {
    session_value = new(ptr)sql::ObSQLSessionInfo::StmtSavedValue();
    if (OB_ISNULL(sql_proxy) || OB_ISNULL(session) ||
        OB_UNLIKELY(dst_opt_stats.empty() || raw_sql.empty())) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("get unexpected empty", K(ret), K(sql_proxy), K(dst_opt_stats.empty()),
                                       K(session), K(raw_sql.empty()));
    } else if (OB_FAIL(session->save_session(*session_value))) {
    }
  }
  if (OB_SUCC(ret)) {
    sqlclient::ObISQLConnection *conn = NULL;
    sqlclient::ObISQLConnectionGuard conn_guard;
    session->set_inner_session();
    //
    session->set_autocommit(true);
    SMART_VAR(ObMySQLProxy::MySQLResult, proxy_result) {
      sqlclient::ObMySQLResult *client_result = NULL;
      if (OB_FAIL(
              query::ObInnerSQLConnectionAccess::
                  create_connection_with_external_session(
                      session, conn_guard))) {
      } else if (OB_ISNULL(conn = conn_guard.get_ptr())) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("conn is null", K(ret), K(conn));
      } else if (OB_FAIL(conn->execute_read(raw_sql.ptr(), proxy_result))) {
      } else if (OB_ISNULL(client_result = proxy_result.get_result())) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("failed to execute sql", K(ret));
      } else {
        while (OB_SUCC(ret) && OB_SUCC(client_result->next())) {
          for (int64_t i = 0; OB_SUCC(ret) && i < get_item_size(); ++i) {
            ObObj tmp;
            ObObj val;
            if (OB_FAIL(client_result->get_obj(i, tmp))) {
            } else if (OB_FAIL(ob_write_obj(allocator_, tmp, val))) {
            } else if (OB_FAIL(add_result(val))) {
            }
          }
          if (OB_SUCC(ret)) {
            if (OB_FAIL(decode(allocator_))) {
            } else if (need_copy_basic_stat &&
                       OB_FAIL(copy_basic_opt_stat(src_opt_stat, dst_opt_stats))) {
              LOG_WARN("failed to copy stat to target opt stat", K(ret));
            } else {
              results_.reset();
            }
          }
        }
        if (OB_ITER_END != ret) {
          LOG_WARN("failed to get result", K(ret));
        } else {
          ret = OB_SUCCESS;
        }
      }
    }
    int tmp_ret = OB_SUCCESS;
    if (session_value != NULL && OB_SUCCESS != (tmp_ret = session->restore_session(*session_value))) {
      LOG_ERROR("failed to restore session", K(tmp_ret));
      ret = COVER_SUCC(tmp_ret);
    }
    if (OB_NOT_NULL(session_value)) {
      session_value->reset();
    }
  }
  return ret;
}

int ObStatsEstimator::decode(ObIAllocator &allocator)
{
  int ret = OB_SUCCESS;
  const ObDatumAccessContext *datum_access_ctx = NULL;
  if (OB_UNLIKELY(stat_items_.count() != results_.count())) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("size does not match", K(ret), K(stat_items_.count()), K(results_.count()));
  } else if (OB_FAIL(ctx_.get_datum_access_ctx(datum_access_ctx))) {
  } else if (OB_ISNULL(datum_access_ctx)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("get unexpected null datum access context", K(ret));
  }
  for (int64_t i = 0; OB_SUCC(ret) && i < stat_items_.count(); ++i) {
    if (OB_FAIL(stat_items_.at(i)->decode(
            results_.at(i), allocator, datum_access_ctx))) {
    }
  }
  return ret;
}

int ObStatsEstimator::copy_basic_opt_stat(ObOptStat &src_opt_stat,
                                          ObIArray<ObOptStat> &dst_opt_stats)
{
  int ret = OB_SUCCESS;
  ObOptTableStat *tmp_tab_stat = src_opt_stat.table_stat_;
  ObIArray<ObOptColumnStat*> &tmp_col_stats = src_opt_stat.column_stats_;
  if (OB_ISNULL(tmp_tab_stat)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("get unexpected null", K(ret), K(tmp_tab_stat));
  } else {
    int64_t partition_id = tmp_tab_stat->get_partition_id();
    bool find_it = false;
    for (int64_t i = 0; OB_SUCC(ret) && !find_it && i < dst_opt_stats.count(); ++i) {
      if (OB_ISNULL(dst_opt_stats.at(i).table_stat_)) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("get unexpected null", K(ret), K(dst_opt_stats.at(i).table_stat_));
      } else if (dst_opt_stats.at(i).table_stat_->get_partition_id() == partition_id) {
        find_it = true;
        int64_t row_cnt = tmp_tab_stat->get_row_count();
        if (sample_value_ >= 0.000001 && sample_value_ < 100.0) {
          row_cnt = static_cast<int64_t>(row_cnt * 100 / sample_value_);
        }
        dst_opt_stats.at(i).table_stat_->set_row_count(row_cnt);
        dst_opt_stats.at(i).table_stat_->set_avg_row_size(tmp_tab_stat->get_avg_row_size());
        dst_opt_stats.at(i).table_stat_->set_sample_size(tmp_tab_stat->get_row_count());
        if (OB_FAIL(copy_basic_col_stats(tmp_tab_stat->get_row_count(), row_cnt, tmp_col_stats, dst_opt_stats.at(i).column_stats_))) {
        } else {/*do nothing*/}
      } else {/*do nothing*/}
    }
    if (OB_SUCC(ret) && !find_it) {
    }
  }
  return ret;
}

int ObStatsEstimator::copy_basic_col_stats(const int64_t cur_row_cnt,
                                           const int64_t total_row_cnt,
                                           ObIArray<ObOptColumnStat *> &src_col_stats,
                                           ObIArray<ObOptColumnStat *> &dst_col_stats)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(src_col_stats.count() != dst_col_stats.count())) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("get unexpected error", K(src_col_stats.count()), K(dst_col_stats.count()), K(ret));
  } else {
    for (int64_t i = 0; OB_SUCC(ret) && i < dst_col_stats.count(); ++i) {
      if (OB_ISNULL(dst_col_stats.at(i)) || OB_ISNULL(src_col_stats.at(i))) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("get unexpected error", K(ret), K(dst_col_stats.at(i)));
      } else {
        int64_t num_not_null = src_col_stats.at(i)->get_num_not_null();
        int64_t num_null = src_col_stats.at(i)->get_num_null();
        int64_t num_distinct = src_col_stats.at(i)->get_num_distinct();
        if (sample_value_ >= 0.000001 && sample_value_ < 100.0) {
          num_distinct = ObOptSelectivity::scale_distinct(total_row_cnt, cur_row_cnt, num_distinct);
          num_not_null = static_cast<int64_t>(num_not_null * 100 / sample_value_);
          num_null = static_cast<int64_t>(num_null * 100 / sample_value_);
        }
        dst_col_stats.at(i)->set_max_value(src_col_stats.at(i)->get_max_value());
        dst_col_stats.at(i)->set_min_value(src_col_stats.at(i)->get_min_value());
        dst_col_stats.at(i)->set_num_not_null(num_not_null);
        dst_col_stats.at(i)->get_histogram().set_sample_size(src_col_stats.at(i)->get_num_not_null());
        dst_col_stats.at(i)->set_num_null(num_null);
        dst_col_stats.at(i)->set_num_distinct(num_distinct);
        dst_col_stats.at(i)->set_avg_len(src_col_stats.at(i)->get_avg_len());
        if (OB_ISNULL(dst_col_stats.at(i)->get_llc_bitmap()) ||
            OB_ISNULL(src_col_stats.at(i)->get_llc_bitmap()) ||
            OB_UNLIKELY(src_col_stats.at(i)->get_llc_bitmap_size() >
                                                      dst_col_stats.at(i)->get_llc_bitmap_size())) {
          ret = OB_ERR_UNEXPECTED;
          LOG_WARN("get unexpected error", K(dst_col_stats.at(i)->get_llc_bitmap()),
                                           K(src_col_stats.at(i)->get_llc_bitmap()),
                                           K(dst_col_stats.at(i)->get_llc_bitmap_size()),
                                           K(src_col_stats.at(i)->get_llc_bitmap_size()), K(ret));
        } else {
          MEMCPY(dst_col_stats.at(i)->get_llc_bitmap(),
                 src_col_stats.at(i)->get_llc_bitmap(),
                 src_col_stats.at(i)->get_llc_bitmap_size());
          dst_col_stats.at(i)->set_llc_bitmap_size(src_col_stats.at(i)->get_llc_bitmap_size());
          LOG_TRACE("Succeed to copy basic col stats", K(*dst_col_stats.at(i)), K(*src_col_stats.at(i)));
        }
      }
    }
  }
  return ret;
}

} // end of common
} // end of ocenabase
