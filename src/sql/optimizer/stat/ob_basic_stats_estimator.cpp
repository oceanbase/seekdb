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
#include "ob_basic_stats_estimator.h"
#include "sql/optimizer/stat/ob_dbms_stats_utils.h"
#include "sql/optimizer/ob_storage_estimator.h"
#include "sql/optimizer/stat/ob_topk_hist_estimator.h"
#include "common/mysqlclient/ob_isql_connection.h"
namespace oceanbase
{
namespace common
{

ObBasicStatsEstimator::ObBasicStatsEstimator(ObExecContext &ctx, ObIAllocator &allocator)
  : ObStatsEstimator(ctx, allocator)
{}

int ObBasicStatsEstimator::estimate(const ObOptStatGatherParam &param,
                                    ObIArray<ObOptStat> &dst_opt_stats)
{
  int ret = OB_SUCCESS;
  const ObIArray<ObColumnStatParam> &column_params = param.column_params_;
  ObString calc_part_id_str;
  ObOptTableStat tab_stat;
  ObOptStat src_opt_stat;
  src_opt_stat.table_stat_ = &tab_stat;
  ObOptTableStat *src_tab_stat = src_opt_stat.table_stat_;
  ObIArray<ObOptColumnStat*> &src_col_stats = src_opt_stat.column_stats_;
  ObArenaAllocator allocator("ObBasicStatsEst", OB_MALLOC_NORMAL_BLOCK_SIZE);
  ObSqlString raw_sql;
  int64_t duration_time = -1;
  bool use_plan_cache = dst_opt_stats.count() == 1 && !param.partition_infos_.empty() && 
                        !param.sample_info_.is_specify_sample();
  // Note that there are dependences between different kinds of statistics
  //            1. RowCount should be added at the first
  //            2. NumDistinct should be estimated before TopKHist
  //            3. AvgRowLen should be added at the last
  if (OB_UNLIKELY(dst_opt_stats.empty()) || OB_ISNULL(param.allocator_)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("get unexpected empty", K(ret), K(dst_opt_stats.empty()), K(param.allocator_));
  } else if (OB_FAIL(ObDbmsStatsUtils::init_col_stats(allocator,
                                                      column_params.count(),
                                                      src_col_stats))) {
  } else if (OB_FAIL(fill_hints(allocator, param.tab_name_, param.gather_vectorize_,
                                use_plan_cache))) {
  } else if (OB_FAIL(add_from_table(allocator, param.db_name_, param.tab_name_))) {
  } else if (OB_FAIL(fill_parallel_info(allocator, param.degree_))) {
  } else if (OB_FAIL(ObDbmsStatsUtils::get_valid_duration_time(param.gather_start_time_,
                                                               param.max_duration_time_,
                                                               duration_time))) {
  } else if (OB_FAIL(fill_query_timeout_info(allocator, duration_time))) {
  } else if (OB_FAIL(fill_sample_info(allocator, param.sample_info_))) {
  } else if (OB_FAIL(fill_specify_scn_info(allocator, param.sepcify_scn_))) {
  } else if (OB_FAIL(add_stat_item(ObStatRowCount(src_tab_stat)))) {
  } else if (!param.is_split_gather_) {
    if (dst_opt_stats.count() > 1) {
      if (OB_FAIL(fill_group_by_info(allocator, param, calc_part_id_str))) {
      } else if (OB_FAIL(add_stat_item(ObPartitionId(src_tab_stat, calc_part_id_str, -1)))) {
      } else if (param.is_specify_partition_ &&
                 OB_FAIL(ObStatsEstimator::fill_partition_info(allocator, param.partition_infos_))) {
        LOG_WARN("failed to add partition info", K(ret));
      }
    } else if (OB_UNLIKELY(param.partition_infos_.count() > 1) ||
               OB_ISNULL(dst_opt_stats.at(0).table_stat_)) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("get unexpected error", K(ret), K(param.partition_infos_));
    } else if (!param.partition_infos_.empty() &&
               OB_FAIL(fill_partition_info(allocator, param, param.partition_infos_.at(0)))) {
      LOG_WARN("failed to add partition info", K(ret));
    } else {
      src_tab_stat->set_partition_id(dst_opt_stats.at(0).table_stat_->get_partition_id());
    }
  } else {//table has been split gather because the system resource limit
    if (dst_opt_stats.count() > 1) {
      if (OB_FAIL(fill_group_by_info(allocator, param, calc_part_id_str))) {
      } else if (OB_FAIL(add_stat_item(ObPartitionId(src_tab_stat, calc_part_id_str, -1)))) {
      } else if (OB_FAIL(ObStatsEstimator::fill_partition_info(allocator, param.partition_infos_))) {
      }
    } else if (OB_UNLIKELY(param.partition_infos_.count() > 1) ||
               OB_ISNULL(dst_opt_stats.at(0).table_stat_)) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("get unexpected error", K(ret), K(param.partition_infos_));
    } else if (!param.partition_infos_.empty() &&
               OB_FAIL(fill_partition_info(allocator, param, param.partition_infos_.at(0)))) {
      LOG_WARN("failed to add partition info", K(ret));
    } else {
      src_tab_stat->set_partition_id(dst_opt_stats.at(0).table_stat_->get_partition_id());
    }
  }
  for (int64_t i = 0; OB_SUCC(ret) && i < column_params.count(); ++i) {
    const ObColumnStatParam *col_param = &column_params.at(i);
    if (OB_FAIL(add_stat_item(ObStatMaxValue(col_param, src_col_stats.at(i)))) ||
        OB_FAIL(add_stat_item(ObStatMinValue(col_param, src_col_stats.at(i)))) ||
        OB_FAIL(add_stat_item(ObStatNumNull(col_param, src_tab_stat, src_col_stats.at(i)))) ||
        OB_FAIL(add_stat_item(ObStatNumDistinct(col_param, src_col_stats.at(i), param.need_approx_ndv_))) ||
        OB_FAIL(add_stat_item(ObStatAvgLen(col_param, src_col_stats.at(i)))) ||
        OB_FAIL(add_stat_item(ObStatLlcBitmap(col_param, src_col_stats.at(i))))) {
      LOG_WARN("failed to add statistic item", K(ret));
    } else {/*do nothing*/}
  }
  if (OB_SUCC(ret)) {
    if (OB_FAIL(add_stat_item(ObStatAvgRowLen(src_tab_stat, src_col_stats)))) {
    } else if (OB_FAIL(pack(raw_sql))) {
    } else if (OB_FAIL(do_estimate(param, raw_sql.string(), true, src_opt_stat, dst_opt_stats))) {
    } else if (OB_FAIL(refine_basic_stats(param, dst_opt_stats))) {
    } else {
      LOG_TRACE("basic stats is collected", K(dst_opt_stats.count()));
    }
  }
  return ret;
}

int ObBasicStatsEstimator::estimate_block_count(ObExecContext &ctx,
                                                const ObTableStatParam &param,
                                                PartitionIdBlockMap &id_block_map)
{
  int ret = OB_SUCCESS;
  ObGlobalTableStat global_tab_stat;
  ObSEArray<ObGlobalTableStat, 4> first_part_tab_stats;
  ObSEArray<ObTabletID, 4> tablet_ids;
  ObSEArray<ObObjectID, 4> partition_ids;
  ObSEArray<EstimateBlockRes, 4> estimate_result;
  hash::ObHashMap<int64_t, int64_t> first_part_idx_map;
  uint64_t table_id = param.table_id_;
  if (is_virtual_table(table_id)) {  // virtual table no need estimate block count
    // do nothing
  } else if (OB_FAIL(get_all_tablet_id_and_object_id(param, tablet_ids, partition_ids))) {
  } else if (param.part_level_ == share::schema::PARTITION_LEVEL_TWO &&
             OB_FAIL(first_part_tab_stats.prepare_allocate(param.all_part_infos_.count()))) {
    LOG_WARN("failed to prepare allocate", K(ret));
  } else if (param.part_level_ == share::schema::PARTITION_LEVEL_TWO &&
             OB_FAIL(generate_first_part_idx_map(param.all_part_infos_, first_part_idx_map))) {
    LOG_WARN("failed to generate first part idx map", K(ret));
  } else if (OB_FAIL(do_estimate_block_count(
                 ctx, table_id, tablet_ids, partition_ids, estimate_result))) {
  } else {
    for (int64_t i = 0; OB_SUCC(ret) && i < estimate_result.count(); ++i) {
      BlockNumStat *block_num_stat = NULL;
      void *buf = NULL;
      if (OB_ISNULL(param.allocator_)) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("get unexpected null", K(ret), K(param.allocator_));
      } else if (OB_ISNULL(buf = param.allocator_->alloc(sizeof(BlockNumStat)))) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_WARN("failed to alloc memory", K(ret), K(buf));
      } else {
        block_num_stat = new (buf) BlockNumStat();
        block_num_stat->tab_macro_cnt_ = estimate_result.at(i).macro_block_count_;
        block_num_stat->tab_micro_cnt_ = estimate_result.at(i).micro_block_count_;
        block_num_stat->sstable_row_cnt_ = estimate_result.at(i).sstable_row_count_;
        block_num_stat->memtable_row_cnt_ = estimate_result.at(i).memtable_row_count_;
        int64_t partition_id = static_cast<int64_t>(estimate_result.at(i).part_id_);
        if (OB_FAIL(id_block_map.set_refactored(partition_id, block_num_stat))) {
        } else if (param.part_level_ == share::schema::PARTITION_LEVEL_ONE) {
          if (OB_FAIL(global_tab_stat.add(1,
                                          0,
                                          0,
                                          block_num_stat->tab_macro_cnt_,
                                          block_num_stat->tab_micro_cnt_,
                                          block_num_stat->sstable_row_cnt_,
                                          block_num_stat->memtable_row_cnt_))) {
          }
        } else if (param.part_level_ == share::schema::PARTITION_LEVEL_TWO) {
          int64_t cur_part_id = -1;
          if (OB_UNLIKELY(!ObDbmsStatsUtils::is_subpart_id(param.all_subpart_infos_, partition_id, cur_part_id))) {
            ret = OB_ERR_UNEXPECTED;
            LOG_WARN("get unexpected error", K(ret), K(partition_id), K(cur_part_id), K(param));
          } else {
            if (OB_FAIL(global_tab_stat.add(1,
                                            0,
                                            0,
                                            block_num_stat->tab_macro_cnt_,
                                            block_num_stat->tab_micro_cnt_,
                                            block_num_stat->sstable_row_cnt_,
                                            block_num_stat->memtable_row_cnt_))) {
            } else {
              int64_t idx = 0;
              if (OB_FAIL(first_part_idx_map.get_refactored(cur_part_id, idx))) {
              } else if (OB_UNLIKELY(idx < 0 || idx >= first_part_tab_stats.count())) {
                ret = OB_ERR_UNEXPECTED;
                LOG_WARN("get invalid part id",
                         K(ret),
                         K(idx),
                         K(partition_id),
                         K(cur_part_id),
                         K(first_part_tab_stats.count()));
              } else if (OB_FAIL(first_part_tab_stats.at(idx).add(1,
                                                                  0,
                                                                  0,
                                                                  block_num_stat->tab_macro_cnt_,
                                                                  block_num_stat->tab_micro_cnt_,
                                                                  block_num_stat->sstable_row_cnt_,
                                                                  block_num_stat->memtable_row_cnt_))) {
              }
            }
          }
        }
      }
    }
    if (OB_SUCC(ret)) {
      if (param.part_level_ == share::schema::PARTITION_LEVEL_ONE ||
          param.part_level_ == share::schema::PARTITION_LEVEL_TWO) {
        BlockNumStat *block_num_stat = NULL;
        void *buf = NULL;
        if (OB_ISNULL(param.allocator_)) {
          ret = OB_ERR_UNEXPECTED;
          LOG_WARN("get unexpected null", K(ret), K(param.allocator_));
        } else if (OB_ISNULL(buf = param.allocator_->alloc(sizeof(BlockNumStat)))) {
          ret = OB_ALLOCATE_MEMORY_FAILED;
          LOG_WARN("failed to alloc memory", K(ret), K(buf));
        } else {
          block_num_stat = new (buf) BlockNumStat();
          block_num_stat->tab_macro_cnt_ = global_tab_stat.get_macro_block_count();
          block_num_stat->tab_micro_cnt_ = global_tab_stat.get_micro_block_count();
          block_num_stat->sstable_row_cnt_ = global_tab_stat.get_sstable_row_cnt();
          block_num_stat->memtable_row_cnt_ = global_tab_stat.get_memtable_row_cnt();
          if (OB_FAIL(id_block_map.set_refactored(-1, block_num_stat))) {
          } else if (param.part_level_ == share::schema::PARTITION_LEVEL_TWO &&
                     OB_UNLIKELY(first_part_tab_stats.count() != param.all_part_infos_.count())) {
            ret = OB_ERR_UNEXPECTED;
            LOG_WARN("get unexpected error", K(ret), K(first_part_tab_stats), K(param.all_part_infos_));
          } else {
            for (int64_t i = 0; OB_SUCC(ret) && i < first_part_tab_stats.count(); ++i) {
              if (OB_ISNULL(buf = param.allocator_->alloc(sizeof(BlockNumStat)))) {
                ret = OB_ALLOCATE_MEMORY_FAILED;
                LOG_WARN("failed to alloc memory", K(ret), K(buf));
              } else {
                block_num_stat = new (buf) BlockNumStat();
                block_num_stat->tab_macro_cnt_ = first_part_tab_stats.at(i).get_macro_block_count();
                block_num_stat->tab_micro_cnt_ = first_part_tab_stats.at(i).get_micro_block_count();
                block_num_stat->sstable_row_cnt_ = first_part_tab_stats.at(i).get_sstable_row_cnt();
                block_num_stat->memtable_row_cnt_ = first_part_tab_stats.at(i).get_memtable_row_cnt();
                if (OB_FAIL(id_block_map.set_refactored(param.all_part_infos_.at(i).part_id_, block_num_stat))) {
                } else { /*do nothing*/
                }
              }
            }
          }
        }
      }
    }
  }
  return ret;
}

int ObBasicStatsEstimator::do_estimate_block_count(ObExecContext &ctx,
                                                   const uint64_t table_id,
                                                   const ObIArray<ObTabletID> &tablet_ids,
                                                   const ObIArray<ObObjectID> &partition_ids,
                                                   ObIArray<EstimateBlockRes> &estimate_res)
{
  int ret = OB_SUCCESS;
  int64_t retry_cnt = 0;
  const int64_t MAX_RETRY_CNT = 10;
  do {
    if (OB_FAIL(THIS_WORKER.check_status())) {
      LOG_WARN("failed to check status", K(ret));
      retry_cnt = MAX_RETRY_CNT;
    } else if (OB_FAIL(do_estimate_block_count_and_row_count(ctx, table_id,
                                                             tablet_ids,
                                                             partition_ids, estimate_res))) {
      LOG_WARN("failed to do estimate block count and row count", K(ret));
      if (DAS_CTX(ctx).is_refresh_location_error(ret)) {
        DAS_CTX(ctx).refresh_location_cache_by_errno(true, ret);
        ++ retry_cnt;
        ob_usleep(1000L * 1000L); // retry interval 1s
      } else {
        retry_cnt = MAX_RETRY_CNT;
      }
    }
  } while (OB_FAIL(ret) && retry_cnt < MAX_RETRY_CNT);
  return ret;
}

int ObBasicStatsEstimator::do_estimate_block_count_and_row_count(ObExecContext &ctx,
                                                                 const uint64_t table_id,
                                                                 const ObIArray<ObTabletID> &tablet_ids,
                                                                 const ObIArray<ObObjectID> &partition_ids,
                                                                 ObIArray<EstimateBlockRes> &estimate_res)
{
  int ret = OB_SUCCESS;
  typedef common::ObSEArray<ObCandiTabletLoc, 4> ObCandiTabletLocArray;
  SMART_VAR(ObCandiTabletLocArray, candi_tablet_locs) {
    if (OB_FAIL(get_tablet_locations(ctx, table_id, tablet_ids, partition_ids, candi_tablet_locs))) {
    } else if (OB_UNLIKELY(candi_tablet_locs.count() != tablet_ids.count()
        || candi_tablet_locs.count() != partition_ids.count())) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("tablet location count does not match", K(ret), K(candi_tablet_locs.count()),
          K(tablet_ids.count()), K(partition_ids.count()));
    } else if (OB_FAIL(estimate_res.prepare_allocate(partition_ids.count()))) {
    } else {
      obcall::ObEstBlockArg arg;
      obcall::ObEstBlockRes result;
      for (int64_t i = 0; OB_SUCC(ret) && i < candi_tablet_locs.count(); ++i) {
        const ObCandiTabletLoc &tablet_loc = candi_tablet_locs.at(i);
        const ObOptTabletLoc &opt_tablet_loc = tablet_loc.get_partition_location();
        if (OB_UNLIKELY(tablet_ids.at(i) != opt_tablet_loc.get_tablet_id())) {
          ret = OB_ERR_UNEXPECTED;
          LOG_WARN("tablet location does not match", K(ret), K(tablet_ids), K(i),
                   K(opt_tablet_loc.get_tablet_id()));
        } else {
          obcall::ObEstBlockArgElement arg_element;
          arg_element.tablet_id_ = opt_tablet_loc.get_tablet_id();
          if (OB_FAIL(arg.tablet_params_arg_.push_back(arg_element))) {
          }
        }
      }
      if (OB_FAIL(ret)) {
      } else if (OB_FAIL(ObStorageEstimator::estimate_block_count_and_row_count(arg, result))) {
      } else if (OB_UNLIKELY(result.tablet_params_res_.count() != estimate_res.count())) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("unexpected local storage estimation result count", K(ret), K(result), K(estimate_res));
      } else {
        for (int64_t i = 0; i < estimate_res.count(); ++i) {
          estimate_res.at(i).part_id_ = partition_ids.at(i);
          estimate_res.at(i).macro_block_count_ = result.tablet_params_res_.at(i).macro_block_count_;
          estimate_res.at(i).micro_block_count_ = result.tablet_params_res_.at(i).micro_block_count_;
          estimate_res.at(i).sstable_row_count_ = result.tablet_params_res_.at(i).sstable_row_count_;
          estimate_res.at(i).memtable_row_count_ = result.tablet_params_res_.at(i).memtable_row_count_;
        }
      }
    }
  }
  return ret;
}
int ObBasicStatsEstimator::get_tablet_locations(ObExecContext &ctx,
                                                const uint64_t ref_table_id,
                                                const ObIArray<ObTabletID> &tablet_ids,
                                                const ObIArray<ObObjectID> &partition_ids,
                                                ObCandiTabletLocIArray &candi_tablet_locs)
{
  int ret = OB_SUCCESS;
  ObSQLSessionInfo *session = ctx.get_my_session();
  if (OB_ISNULL(session) || OB_UNLIKELY(tablet_ids.count() != partition_ids.count())) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("get unexpected null", K(ret), K(session), K(tablet_ids.count()), K(partition_ids.count()));
  } else {
    candi_tablet_locs.reset();
    if (OB_FAIL(candi_tablet_locs.prepare_allocate(tablet_ids.count()))) {
    } else {
      share::ObLSLocation local_location;
      if (OB_FAIL(local_location.init(share::SYS_LS, GCTX.self_addr(), 1))) {
      }
      for (int64_t i = 0; OB_SUCC(ret) && i < tablet_ids.count(); ++i) {
        if (OB_FAIL(candi_tablet_locs.at(i).set_local_location(
                       partition_ids.at(i),
                       OB_INVALID_ID,
                       tablet_ids.at(i),
                       local_location,
                       GCTX.self_addr()))) {
        }
      }
    }
  }
  return ret;
}

int ObBasicStatsEstimator::estimate_modified_count(ObExecContext &ctx,
                                                   const uint64_t table_id,
                                                   int64_t &result,
                                                   const bool need_inc_modified_count/*default true*/)
{
  int ret = OB_SUCCESS;
  ObSqlString select_sql;
  const int64_t obj_pos = 0;
  ObObj result_obj;
  bool is_valid = true;
  if (OB_FAIL(ObDbmsStatsUtils::check_table_read_write_valid( is_valid))) {
  } else if (!is_valid) {
    // do nothing
  } else if (need_inc_modified_count &&
             OB_FAIL(select_sql.append_fmt(
        "select cast(sum(inserts + updates + deletes) - sum(last_inserts + last_updates + " \
        "last_deletes) as signed) as inc_mod_count " \
        "from %s where table_id = %lu;",
        share::OB_ALL_MONITOR_MODIFIED_TNAME,
        share::schema::ObSchemaUtils::get_extract_schema_id(table_id)))) {
    LOG_WARN("failed to append fmt", K(ret));
  } else if (!need_inc_modified_count &&
             OB_FAIL(select_sql.append_fmt(
        "select cast(sum(inserts + updates + deletes) as signed) as modified_count " \
        "from %s where table_id = %lu;",
        share::OB_ALL_MONITOR_MODIFIED_TNAME,
        share::schema::ObSchemaUtils::get_extract_schema_id(table_id)))) {
    LOG_WARN("failed to append fmt", K(ret));
  } else {
    ObCommonSqlProxy *sql_proxy = ctx.get_sql_proxy();
    SMART_VAR(ObMySQLProxy::MySQLResult, proxy_result) {
      sqlclient::ObMySQLResult *client_result = NULL;
      auto &sql_client_retry_weak = *sql_proxy;
      if (OB_FAIL(sql_client_retry_weak.read(proxy_result, select_sql.ptr()))) {
      } else if (OB_ISNULL(client_result = proxy_result.get_result())) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("failed to execute sql", K(ret));
      } else if (OB_FAIL(client_result->next())) {
      } else if (OB_FAIL(client_result->get_obj(obj_pos, result_obj))) {
      } else if (result_obj.is_null()) {
        result = 0;
      } else if (OB_UNLIKELY(!result_obj.is_integer_type())) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("get unexpected obj type", K(ret), K(result_obj.get_type()));
      } else {
        result = result_obj.get_int();
      }
      int tmp_ret = OB_SUCCESS;
      if (NULL != client_result) {
        if (OB_SUCCESS != (tmp_ret = client_result->close())) {
          LOG_WARN("close result set failed", K(ret), K(tmp_ret));
          ret = COVER_SUCC(tmp_ret);
        }
      }
    }
  }
  return ret;
}



int ObBasicStatsEstimator::estimate_stale_partition(ObExecContext &ctx,
                                                    const uint64_t table_id,
                                                    const int64_t global_part_id,
                                                    const ObIArray<PartInfo> &partition_infos,
                                                    const double stale_percent_threshold,
                                                    ObIArray<ObPartitionStatInfo> &partition_stat_infos)
{
  int ret = OB_SUCCESS;
  ObSqlString select_sql;
  bool is_valid = true;
  ObSEArray<int64_t, 4> monitor_modified_part_ids;
  bool is_check_global = false;
  int64_t table_inc_modified = 0;
  bool has_part_invalid_inc = false;
  if (OB_FAIL(ObDbmsStatsUtils::check_table_read_write_valid( is_valid))) {
  } else if (!is_valid) {
    // do nothing
  } else if (OB_FAIL(select_sql.append_fmt(
          "select tablet_id, (inserts + updates + deletes - last_inserts - " \
          "last_updates - last_deletes) as inc_mod_count "\
          "from %s where table_id = %lu order by 1;",
        share::OB_ALL_MONITOR_MODIFIED_TNAME,
        share::schema::ObSchemaUtils::get_extract_schema_id(table_id)))) {
  } else {
    ObCommonSqlProxy *sql_proxy = ctx.get_sql_proxy();
    SMART_VAR(ObMySQLProxy::MySQLResult, proxy_result) {
      sqlclient::ObMySQLResult *client_result = NULL;
      auto &sql_client_retry_weak = *sql_proxy;
      if (OB_FAIL(sql_client_retry_weak.read(proxy_result, select_sql.ptr()))) {
      } else if (OB_ISNULL(client_result = proxy_result.get_result())) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("failed to execute sql", K(ret));
      } else {
        int64_t cur_part_id = -1; //current partition for first part
        int64_t cur_inc_mod_count = 0;//current inc_mod_count for first part
        bool has_subpart_invalid_inc = false;
        while (OB_SUCC(ret) && OB_SUCC(client_result->next())) {
          int64_t idx1 = 0;
          int64_t idx2 = 1;
          ObObj tablet_id_obj;
          ObObj inc_mod_count_obj;
          int64_t dst_tablet_id = 0;
          int64_t dst_partition = -1;
          int64_t inc_mod_count = 0;
          int64_t dst_part_id = -1;
          if (OB_FAIL(client_result->get_obj(idx1, tablet_id_obj))) {
          } else if (OB_FAIL(tablet_id_obj.get_int(dst_tablet_id))) {
          } else if (OB_FAIL(client_result->get_obj(idx2, inc_mod_count_obj))) {
          } else if (!inc_mod_count_obj.is_null() &&
                     OB_FAIL(inc_mod_count_obj.get_int(inc_mod_count))) {
            LOG_WARN("failed to get int", K(ret), K(inc_mod_count_obj));
          } else if (OB_FAIL(ObDbmsStatsUtils::get_dst_partition_by_tablet_id(ctx,
                                                                              dst_tablet_id,
                                                                              partition_infos,
                                                                              dst_partition))) {
          } else if (OB_FAIL(check_partition_stat_state(dst_partition,
                                                        inc_mod_count,
                                                        stale_percent_threshold,
                                                        partition_stat_infos))) {
          } else if (OB_FAIL(monitor_modified_part_ids.push_back(dst_partition))) {
          } else if (OB_FAIL(add_var_to_array_no_dup(monitor_modified_part_ids, cur_part_id))) {
          } else if (ObDbmsStatsUtils::is_subpart_id(partition_infos, dst_partition, dst_part_id)) {
            has_subpart_invalid_inc |= inc_mod_count < 0;
            if (cur_part_id == dst_part_id) {
              cur_inc_mod_count += inc_mod_count;
            } else if (cur_part_id == -1) {
              cur_part_id = dst_part_id;
              cur_inc_mod_count = inc_mod_count;
            } else if (OB_FAIL(check_partition_stat_state(cur_part_id,
                                                          has_subpart_invalid_inc ? -1 : cur_inc_mod_count,
                                                          stale_percent_threshold,
                                                          partition_stat_infos))) {
            } else {
              cur_part_id = dst_part_id;
              cur_inc_mod_count = inc_mod_count;
              has_subpart_invalid_inc = false;
            }
          }

          has_part_invalid_inc |= inc_mod_count < 0;
          is_check_global = true;
          table_inc_modified += inc_mod_count;
        }

        // cacl global part
        if (OB_FAIL(ret)) {
          if (OB_ITER_END != ret) {
            LOG_WARN("failed to get result", K(ret));
          } else {
            ret = OB_SUCCESS;
            if (cur_part_id != -1 &&
                OB_FAIL(check_partition_stat_state(cur_part_id,
                                                   has_subpart_invalid_inc ? -1 : cur_inc_mod_count,
                                                   stale_percent_threshold,
                                                   partition_stat_infos))) {
              LOG_WARN("failed to check partition stat state", K(ret));
            } else if (OB_FAIL(add_var_to_array_no_dup(monitor_modified_part_ids, cur_part_id))) {
            } else if (is_check_global &&
                       OB_FAIL(check_partition_stat_state(global_part_id,
                                                          has_part_invalid_inc ? -1 : table_inc_modified,
                                                          stale_percent_threshold,
                                                          partition_stat_infos))) {
              LOG_WARN("failed to check partition stat state", K(ret));
            } else {/*do nothing*/}
          }
        }
      }

      int tmp_ret = OB_SUCCESS;
      if (NULL != client_result) {
        if (OB_SUCCESS != (tmp_ret = client_result->close())) {
          LOG_WARN("close result set failed", K(ret), K(tmp_ret));
          ret = COVER_SUCC(tmp_ret);
        }
      }
    }

    ObSEArray<int64_t, 4> record_first_part_ids;
    for (int64_t i = 0; OB_SUCC(ret) && i < partition_stat_infos.count(); ++i) {
      int64_t partition_id = partition_stat_infos.at(i).partition_id_;
      int64_t first_part_id = OB_INVALID_ID;
      // Partitions who not have dml infos are no need to regather stats
      if (!is_contain(monitor_modified_part_ids, partition_id)) {
        if (OB_FAIL(set_partition_stat_no_regather(partition_id, partition_stat_infos))) {
        }
      }
      if (OB_SUCC(ret) && ObDbmsStatsUtils::is_subpart_id(partition_infos, partition_id, first_part_id)) {
        if (first_part_id != OB_INVALID_ID && !is_contain(monitor_modified_part_ids, first_part_id) &&
            !is_contain(record_first_part_ids, first_part_id)) {
          if (OB_FAIL(set_partition_stat_no_regather(first_part_id, partition_stat_infos))) {
          } else if (OB_FAIL(record_first_part_ids.push_back(first_part_id))) {
          }
        }
      }
    }
  }
  return ret;
}

int ObBasicStatsEstimator::update_last_modified_count(ObExecContext &ctx,
                                                      const ObTableStatParam &param)
{
  int ret = OB_SUCCESS;
  ObMySQLTransaction trans;
  if (OB_FAIL(trans.start(ctx.get_sql_proxy()))) {
  } else if (OB_FAIL(update_last_modified_count(trans.get_connection(), param))) {
  }
  //end gather trans
  if (OB_SUCC(ret)) {
    if (OB_FAIL(trans.end(true))) {
    }
  } else {
    int tmp_ret = OB_SUCCESS;
    if (OB_SUCCESS != (tmp_ret = trans.end(false))) {
    }
  }
  return ret;
}

int ObBasicStatsEstimator::update_last_modified_count(sqlclient::ObISQLConnection *conn,
                                                      const ObTableStatParam &param)
{
  int ret = OB_SUCCESS;
  ObSqlString udpate_sql;
  ObSqlString tablet_list;
  int64_t affected_rows = 0;
  bool is_valid = true;
  bool is_all_update = false;


  uint64_t table_id = param.table_id_;
  if (OB_ISNULL(conn)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("get unexpected null", K(ret), K(conn));
  } else if (OB_FAIL(ObDbmsStatsUtils::check_table_read_write_valid( is_valid))) {
  } else if (!is_valid) {
    // do nothing
  } else if (OB_FAIL(gen_tablet_list(param, tablet_list, is_all_update))) {
  } else if (tablet_list.empty() && !is_all_update) {
    /*do nothing*/
  } else if (OB_FAIL(udpate_sql.append_fmt(
        "update %s set last_inserts = inserts, last_updates = updates, last_deletes = deletes " \
        "where table_id = %lu %s %s;",
        share::OB_ALL_MONITOR_MODIFIED_TNAME,
        share::schema::ObSchemaUtils::get_extract_schema_id(table_id),
        !tablet_list.empty() ? "and tablet_id in" : " ",
        !tablet_list.empty() ? tablet_list.ptr() : " "))) {
  } else if (OB_FAIL(conn->execute_write(udpate_sql.ptr(), affected_rows))) {
  } else {
  }

  return ret;
}

int ObBasicStatsEstimator::check_table_statistics_state(ObExecContext &ctx,
                                                        const uint64_t table_id,
                                                        const int64_t global_part_id,
                                                        bool &is_locked,
                                                        ObIArray<ObPartitionStatInfo> &partition_stat_infos)
{
  int ret = OB_SUCCESS;
  ObSqlString select_sql;
  bool is_valid = true;
  is_locked = false;
  if (OB_FAIL(ObDbmsStatsUtils::check_table_read_write_valid( is_valid))) {
  } else if (!is_valid) {
    // do nothing
  } else if (OB_FAIL(select_sql.append_fmt(
                 "select partition_id, stattype_locked, row_cnt, spare2 from %s where "
                 "table_id = %lu and (last_analyzed > 0 or spare2 >= 5) order by 1;",
                 share::OB_ALL_TABLE_STAT_TNAME,
                 share::schema::ObSchemaUtils::get_extract_schema_id(table_id)))) {
  } else {
    ObCommonSqlProxy *sql_proxy = ctx.get_sql_proxy();
    SMART_VAR(ObMySQLProxy::MySQLResult, proxy_result) {
      sqlclient::ObMySQLResult *client_result = NULL;
      auto &sql_client_retry_weak = *sql_proxy;
      if (OB_FAIL(sql_client_retry_weak.read(proxy_result, select_sql.ptr()))) {
      } else if (OB_ISNULL(client_result = proxy_result.get_result())) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("failed to execute sql", K(ret));
      } else {
        while (OB_SUCC(ret) && !is_locked && OB_SUCC(client_result->next())) {
          ObObj tmp;
          int64_t part_val = -1;
          int64_t lock_val = -1;
          int64_t row_cnt = 0;
          int64_t consecutive_failed_count = 0;
          int64_t idx1 = 0;
          int64_t idx2 = 1;
          int64_t idx3 = 2;
          int64_t idx4 = 3;
          if (OB_FAIL(client_result->get_obj(idx1, tmp))) {
          } else if (OB_FAIL(tmp.get_int(part_val))) {
          } else if (OB_FAIL(client_result->get_obj(idx2, tmp))) {
          } else if (OB_FAIL(tmp.get_int(lock_val))) {
          } else if (OB_FAIL(client_result->get_obj(idx3, tmp))) {
          } else if (!tmp.is_null() && OB_FAIL(tmp.get_int(row_cnt))) {
            LOG_WARN("failed to get int", K(ret), K(tmp));
          } else if (OB_FAIL(client_result->get_obj(idx4, tmp))) {
          } else if (!tmp.is_null() && OB_FAIL(tmp.get_int(consecutive_failed_count))) {
            LOG_WARN("failed to get int", K(ret), K(tmp));
          } else if (global_part_id == part_val && lock_val > 0) {
            is_locked = true;
          } else {
            ObPartitionStatInfo partition_stat_info(part_val, row_cnt, lock_val > 0, false, consecutive_failed_count >= 5);
            if (OB_FAIL(partition_stat_infos.push_back(partition_stat_info))) {
            } else {/*do nothing*/}
          }
        }
        if (OB_FAIL(ret)) {
          if (OB_ITER_END != ret) {
            LOG_WARN("failed to get result", K(ret));
          } else {
           ret = OB_SUCCESS;
          }
        }
      }
      int tmp_ret = OB_SUCCESS;
      if (NULL != client_result) {
        if (OB_SUCCESS != (tmp_ret = client_result->close())) {
          LOG_WARN("close result set failed", K(ret), K(tmp_ret));
          ret = COVER_SUCC(tmp_ret);
        }
      }
    }
  }
  return ret;
}

int ObBasicStatsEstimator::check_partition_stat_state(const int64_t partition_id,
                                                      const int64_t inc_mod_count,
                                                      const double stale_percent_threshold,
                                                      ObIArray<ObPartitionStatInfo> &partition_stat_infos)
{
  int ret = OB_SUCCESS;
  bool find_it = false;
  for (int64_t i = 0; !find_it && i < partition_stat_infos.count(); ++i) {
    if (partition_stat_infos.at(i).partition_id_ == partition_id) {
      //locked partition id or no arrived stale percent threshold no need regather stats.
      double stale_percent = 0.0;
      if (inc_mod_count < 0 || partition_stat_infos.at(i).row_cnt_ <= 0) {
        stale_percent = inc_mod_count == 0 ? 0.0 : 1.0;
      } else {
        stale_percent = 1.0 * inc_mod_count / partition_stat_infos.at(i).row_cnt_;
      }
      partition_stat_infos.at(i).is_no_stale_ = stale_percent <= stale_percent_threshold;
      find_it = true;
    }
  }
  if (!find_it) {
    ObPartitionStatInfo partition_stat_info(partition_id, 0, false, false, false);
    partition_stat_info.is_no_stale_ = false;
    ret = partition_stat_infos.push_back(partition_stat_info);
  }
  return ret;
}

int ObBasicStatsEstimator::gen_tablet_list(const ObTableStatParam &param,
                                           ObSqlString &tablet_list,
                                           bool &is_all_update)
{
  int ret = OB_SUCCESS;
  ObSEArray<uint64_t, 4> tablet_ids;
  is_all_update = false;
  if (param.global_stat_param_.need_modify_) {
    if (param.part_level_ == share::schema::ObPartitionLevel::PARTITION_LEVEL_ZERO ||
        !param.global_stat_param_.gather_approx_) {
      is_all_update = true;
    }
  }
  if (OB_SUCC(ret) && !is_all_update && param.part_stat_param_.need_modify_) {
    if (param.part_level_ == share::schema::ObPartitionLevel::PARTITION_LEVEL_ONE) {
      for (int64_t i = 0; OB_SUCC(ret) && i < param.part_infos_.count(); ++i) {
        if (OB_FAIL(tablet_ids.push_back(param.part_infos_.at(i).tablet_id_.id()))) {
        }
      }
    } else if (param.part_level_ == share::schema::ObPartitionLevel::PARTITION_LEVEL_TWO) {
      for (int64_t i = 0; OB_SUCC(ret) && i < param.part_infos_.count(); ++i) {
        for (int64_t j = 0; OB_SUCC(ret) && j < param.subpart_infos_.count(); ++j) {
          if (param.part_infos_.at(i).part_id_ == param.subpart_infos_.at(j).first_part_id_) {
            if (OB_FAIL(tablet_ids.push_back(param.subpart_infos_.at(j).tablet_id_.id()))) {
            }
          }
        }
      }
    }
  }
  if (OB_SUCC(ret) && !is_all_update && param.subpart_stat_param_.need_modify_) {
    for (int64_t i = 0; OB_SUCC(ret) && i < param.subpart_infos_.count(); ++i) {
      if (OB_FAIL(tablet_ids.push_back(param.subpart_infos_.at(i).tablet_id_.id()))) {
      }
    }
  }
  if (OB_SUCC(ret)) {
    for (int64_t i = 0; OB_SUCC(ret) && i < tablet_ids.count(); i++) {
      char prefix = (i == 0 ? '(' : ' ');
      char suffix = (i == tablet_ids.count() - 1 ? ')' : ',');
      if (OB_FAIL(tablet_list.append_fmt("%c%lu%c", prefix, tablet_ids.at(i), suffix))) {
      } else {/*do nothing*/}
    }
  }
  return ret;
}

int ObBasicStatsEstimator::get_all_tablet_id_and_object_id(const ObTableStatParam &param,
                                                           ObIArray<ObTabletID> &tablet_ids,
                                                           ObIArray<ObObjectID> &partition_ids)
{
  int ret = OB_SUCCESS;
  if (param.part_level_ == share::schema::PARTITION_LEVEL_ZERO) {
    ObTabletID global_tablet_id(param.global_tablet_id_);
    if (OB_FAIL(tablet_ids.push_back(global_tablet_id))) {
    } else if (OB_FAIL(partition_ids.push_back(static_cast<ObObjectID>(param.global_part_id_)))) {
    }
  } else if (param.part_level_ == share::schema::PARTITION_LEVEL_ONE) {
    for (int64_t i = 0; OB_SUCC(ret) && i < param.all_part_infos_.count(); ++i) {
      if (OB_FAIL(tablet_ids.push_back(param.all_part_infos_.at(i).tablet_id_))) {
      } else if (OB_FAIL(partition_ids.push_back(static_cast<ObObjectID>(param.all_part_infos_.at(i).part_id_)))) {
      }
    }
  } else if (param.part_level_ == share::schema::PARTITION_LEVEL_TWO) {
    for (int64_t i = 0; OB_SUCC(ret) && i < param.all_subpart_infos_.count(); ++i) {
      if (OB_FAIL(tablet_ids.push_back(param.all_subpart_infos_.at(i).tablet_id_))) {
      } else if (OB_FAIL(partition_ids.push_back(static_cast<ObObjectID>(param.all_subpart_infos_.at(i).part_id_)))) {
      }
    }
  } else {/*do nothing*/}
  return ret;
}

int ObBasicStatsEstimator::get_need_stats_tables(ObExecContext &ctx,
                                                 const int64_t last_table_id,
                                                 const int64_t slice_cnt,
                                                 ObIArray<int64_t> &table_ids)
{
  int ret = OB_SUCCESS;
  ObSqlString gather_table_type_list;
  ObSqlString select_sql;
  if (OB_FAIL(get_gather_table_type_list(gather_table_type_list))) {
  } else if (OB_FAIL(select_sql.append_fmt("SELECT /*+no_rewrite*/table_id "\
                                           " FROM   %s t "\
                                           " WHERE  table_id > %ld"
                                           "  AND  table_type IN %s"\
                                           " AND table_id  not in (select distinct table_id from %s "\
                                           " WHERE table_id > %ld AND spare2 >= 5) "\
                                           " ORDER  BY table_id "\
                                           " LIMIT  %ld;",
                                           share::OB_ALL_TABLE_TNAME,
                                           last_table_id,
                                           gather_table_type_list.ptr(),
                                           share::OB_ALL_TABLE_STAT_TNAME,
                                           last_table_id,
                                           slice_cnt))) {
  } else {
    ObCommonSqlProxy *sql_proxy = ctx.get_sql_proxy();
    SMART_VAR(ObMySQLProxy::MySQLResult, proxy_result) {
      sqlclient::ObMySQLResult *client_result = NULL;
      auto &sql_client_retry_weak = *sql_proxy;
      if (OB_FAIL(sql_client_retry_weak.read(proxy_result, select_sql.ptr()))) {
      } else if (OB_ISNULL(client_result = proxy_result.get_result())) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("failed to execute sql", K(ret));
      } else {
        while (OB_SUCC(ret) && OB_SUCC(client_result->next())) {
          int64_t idx = 0;
          ObObj obj;
          int64_t table_id = -1;
          if (OB_FAIL(client_result->get_obj(idx, obj))) {
          } else if (OB_FAIL(obj.get_int(table_id))) {
          } else if (OB_FAIL(table_ids.push_back(table_id))) {
          }
        }
        ret = OB_ITER_END == ret ? OB_SUCCESS : ret;
      }
      int tmp_ret = OB_SUCCESS;
      if (NULL != client_result) {
        if (OB_SUCCESS != (tmp_ret = client_result->close())) {
          LOG_WARN("close result set failed", K(ret), K(tmp_ret));
          ret = COVER_SUCC(tmp_ret);
        }
      }
    }
    LOG_TRACE("succeed to get table ids that need gathering table stats",
                K(select_sql), K(last_table_id), K(slice_cnt), K(table_ids.count()), K(table_ids));
  }
  return ret;
}

int ObBasicStatsEstimator::generate_first_part_idx_map(const ObIArray<PartInfo> &all_part_infos,
                                                       hash::ObHashMap<int64_t, int64_t> &first_part_idx_map)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(all_part_infos.empty())) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("get unexpected empty", K(ret), K(all_part_infos.empty()));
  } else if (OB_FAIL(first_part_idx_map.create(all_part_infos.count(), "ObStatsEst"))) {
  } else {
    for (int64_t i = 0; OB_SUCC(ret) && i < all_part_infos.count(); ++i) {
      if (OB_FAIL(first_part_idx_map.set_refactored(all_part_infos.at(i).part_id_, i))) {
      } else {/*do nothing*/}
    }
  }
  return ret;
}

/**
 * @brief ObBasicStatsEstimator::refine_basic_stats
 *   when the user specify estimate_percent is too small, the sample data isn't enough to describe the
 * overall data distribution, So we need consider refine it, and reset the appropriate estimate_percent
 * to regather basic stats.
 */
int ObBasicStatsEstimator::refine_basic_stats(const ObOptStatGatherParam &param,
                                              ObIArray<ObOptStat> &dst_opt_stats)
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(param.allocator_)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("get unexpected null", K(ret));
  } else if (sample_value_ >= 0.000001 && sample_value_ < 100.0) {
    for (int64_t i = 0; OB_SUCC(ret) && i < dst_opt_stats.count(); ++i) {
      bool need_re_estimate = false;
      ObOptStatGatherParam new_param;
      ObSEArray<ObOptStat, 1> tmp_opt_stats;
      ObBasicStatsEstimator basic_re_est(ctx_, *param.allocator_);
      if (OB_FAIL(check_stat_need_re_estimate(param, dst_opt_stats.at(i), need_re_estimate, new_param))) {
      } else if (!need_re_estimate) {
        //do nothing
      } else if (OB_FAIL(tmp_opt_stats.push_back(dst_opt_stats.at(i)))) {
      } else if (OB_FAIL(basic_re_est.estimate(new_param, tmp_opt_stats))) {
      } else {
      }
    }
  }
  return ret;
}

int ObBasicStatsEstimator::check_stat_need_re_estimate(const ObOptStatGatherParam &origin_param,
                                                       ObOptStat &opt_stat,
                                                       bool &need_re_estimate,
                                                       ObOptStatGatherParam &new_param)
{
  int ret = OB_SUCCESS;
  need_re_estimate = false;
  if (OB_ISNULL(opt_stat.table_stat_)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("get unexpected error", K(ret), K(opt_stat.table_stat_));
  } else if (opt_stat.table_stat_->get_row_count() * sample_value_ / 100 >= MAGIC_MIN_SAMPLE_SIZE) {
    //do nothing
  } else if (OB_FAIL(new_param.assign(origin_param))) {
  } else {
    need_re_estimate = true;
    int64_t total_row_count = opt_stat.table_stat_->get_row_count();
    //1.set sample ratio
    if (total_row_count <= MAGIC_SAMPLE_SIZE) {
      new_param.sample_info_.is_sample_ = false;
      new_param.sample_info_.sample_value_ = 0.0;
      new_param.sample_info_.is_block_sample_ = false;
    } else {
      new_param.sample_info_.is_sample_ = true;
      new_param.sample_info_.is_block_sample_ = false;
      new_param.sample_info_.sample_value_ = (MAGIC_SAMPLE_SIZE * 100.0) / total_row_count;
      new_param.sample_info_.sample_type_ = PercentSample;
    }
    //2.set partition info
    if (new_param.stat_level_ != TABLE_LEVEL) {
      if (OB_FAIL(ObDbmsStatsUtils::remove_stat_gather_param_partition_info(opt_stat.table_stat_->get_partition_id(),
                                                                            new_param))) {
      }
    }
    //3.reset opt stat
    if (OB_SUCC(ret)) {
      opt_stat.table_stat_->set_row_count(0);
      opt_stat.table_stat_->set_avg_row_size(0);
      for (int64_t i = 0; OB_SUCC(ret) && i < opt_stat.column_stats_.count(); ++i) {
        if (OB_ISNULL(opt_stat.column_stats_.at(i))) {
          ret = OB_ERR_UNEXPECTED;
          LOG_WARN("get unexpected error", K(ret));
        } else {
          ObObj null_val;
          null_val.set_null();
          opt_stat.column_stats_.at(i)->set_max_value(null_val);
          opt_stat.column_stats_.at(i)->set_min_value(null_val);
          opt_stat.column_stats_.at(i)->set_num_not_null(0);
          opt_stat.column_stats_.at(i)->set_num_null(0);
          opt_stat.column_stats_.at(i)->set_num_distinct(0);
          opt_stat.column_stats_.at(i)->set_avg_len(0);
          opt_stat.column_stats_.at(i)->set_llc_bitmap_size(ObOptColumnStat::NUM_LLC_BUCKET);
          MEMSET(opt_stat.column_stats_.at(i)->get_llc_bitmap(), 0, ObOptColumnStat::NUM_LLC_BUCKET);
          opt_stat.column_stats_.at(i)->get_histogram().reset();
        }
      }
    }
  }
  return ret;
}

int ObBasicStatsEstimator::fill_hints(common::ObIAllocator &alloc,
                                      const ObString &table_name,
                                      int64_t gather_vectorize,
                                      bool use_plan_cache)
{
  int ret = OB_SUCCESS;
  ObSqlString default_hints;
  const char *use_full_table_hint = " FULL(`%.*s`) ";
  const char *no_use_plan_cache_hint = " USE_PLAN_CACHE(NONE)";
  if (OB_UNLIKELY(table_name.empty() || gather_vectorize < 0)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("get unexpected null", K(table_name), K(gather_vectorize));
  } else if (OB_FAIL(default_hints.append_fmt("NO_REWRITE DBMS_STATS OPT_PARAM('ROWSETS_MAX_ROWS', %ld)",
                                               gather_vectorize))) {
  } else if (OB_FAIL(default_hints.append_fmt(use_full_table_hint,
                                              table_name.length(),
                                              table_name.ptr()))) {
  } else if (!use_plan_cache && OB_FAIL(default_hints.append(no_use_plan_cache_hint))) {
    LOG_WARN("failed to append fmt", K(ret));
  } else if (OB_FAIL(add_hint(default_hints.string(), alloc))) {
  } else {
  }
  return ret;
}

int ObBasicStatsEstimator::get_gather_table_type_list(ObSqlString &gather_table_type_list)
{
  int ret = OB_SUCCESS;
  int64_t table_type_arr[] = {share::schema::ObTableType::SYSTEM_TABLE,
                              share::schema::ObTableType::VIRTUAL_TABLE,
                              share::schema::ObTableType::USER_TABLE};
  int64_t table_type_cnt = sizeof(table_type_arr)/sizeof(table_type_arr[0]);
  for (int64_t i = 0; OB_SUCC(ret) && i < table_type_cnt; ++i) {
    char prefix = (i == 0 ? '(' : ' ');
    char suffix = (i == table_type_cnt - 1 ? ')' : ',');
    if (OB_FAIL(gather_table_type_list.append_fmt("%c%lu%c", prefix, table_type_arr[i], suffix))) {
    } else {/*do nothing*/}
  }
  return ret;
}



int ObBasicStatsEstimator::get_async_gather_stats_tables(ObExecContext &ctx,
                                                         const int64_t max_table_cnt,
                                                         int64_t &last_table_id,
                                                         int64_t &last_tablet_id,
                                                         int64_t &total_part_cnt,
                                                         ObIArray<AsyncStatTable> &stat_tables)
{
  int ret = OB_SUCCESS;
  ObSqlString select_sql;
  if (OB_FAIL(select_sql.append_fmt(
          "SELECT table_id, tablet_id, avg(changed_ratio) over (partition by table_id)  ratio from "\
          " ( SELECT    m.table_id, m.tablet_id, (CASE WHEN (m.last_inserts-m.last_deletes) = 0 THEN 1 + cast(coalesce(up.valchar, gp.spare4) as double) "\
          "                            ELSE (m.inserts - m.last_inserts + m.updates - m.last_updates + m.deletes - m.last_deletes) * 1.0 / (m.last_inserts-m.last_deletes) END) as changed_ratio "\
          "  FROM    %s m "\
          "  LEFT JOIN %s up "\
          "  ON        m.table_id = up.table_id "\
          "  AND       up.pname = 'ASYNC_GATHER_STALE_RATIO' "\
          "  JOIN      %s gp "\
          "  ON        gp.sname = 'ASYNC_GATHER_STALE_RATIO' "\
          "  where "\
          " (CASE WHEN (m.last_inserts-m.last_deletes) = 0 THEN 1 + cast(coalesce(up.valchar, gp.spare4) as "\
          " double) "\
          "    ELSE (m.inserts - m.last_inserts + m.updates - m.last_updates + m.deletes - m.last_deletes) * 1.0 "\
          " / (m.last_inserts-m.last_deletes) END) > cast(coalesce(up.valchar, gp.spare4) as double) "\
          " AND m.table_id > %lu and m.tablet_id > %lu"\
          " )t "\
          " order by ratio desc,table_id, tablet_id  limit %lu ",
          share::OB_ALL_MONITOR_MODIFIED_TNAME,
          share::OB_ALL_OPTSTAT_USER_PREFS_TNAME,
          share::OB_ALL_OPTSTAT_GLOBAL_PREFS_TNAME,
          last_table_id,
          last_tablet_id,
          max_table_cnt
          ))) {
  } else {
    ObCommonSqlProxy *sql_proxy = ctx.get_sql_proxy();
    SMART_VAR(ObMySQLProxy::MySQLResult, proxy_result) {
      sqlclient::ObMySQLResult *client_result = NULL;
      auto &sql_client_retry_weak = *sql_proxy;
      if (OB_FAIL(sql_client_retry_weak.read(proxy_result, select_sql.ptr()))) {
      } else if (OB_ISNULL(client_result = proxy_result.get_result())) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("failed to execute sql", K(ret));
      } else {
        while (OB_SUCC(ret) && OB_SUCC(client_result->next())) {
          int64_t idx_col1 = 0;
          int64_t idx_col2 = 1;
          ObObj obj;
          int64_t table_id = 0;
          int64_t tablet_id = 0;
          if (OB_FAIL(client_result->get_obj(idx_col1, obj))) {
          } else if (OB_FAIL(obj.get_int(table_id))) {
          } else if (OB_FAIL(client_result->get_obj(idx_col2, obj))) {
          } else if (OB_FAIL(obj.get_int(tablet_id))) {
          } else if ((stat_tables.empty() || table_id != (stat_tables.at(stat_tables.count() - 1).table_id_)) &&
                     OB_FAIL(stat_tables.push_back(AsyncStatTable(table_id)))) {
            LOG_WARN("failed to push back", K(ret));
          } else if (OB_FAIL(stat_tables.at(stat_tables.count() - 1).tablet_ids_.push_back(static_cast<uint64_t>(tablet_id)))) {
          } else {
            last_table_id = table_id;
            last_tablet_id = tablet_id;
          }
          total_part_cnt++;
        }
        ret = OB_ITER_END == ret ? OB_SUCCESS : ret;
      }
      int tmp_ret = OB_SUCCESS;
      if (NULL != client_result) {
        if (OB_SUCCESS != (tmp_ret = client_result->close())) {
          LOG_WARN("close result set failed", K(ret), K(tmp_ret));
          ret = COVER_SUCC(tmp_ret);
        }
      }
    }
    
  }
  return ret;
}

int ObBasicStatsEstimator::set_partition_stat_no_regather(const int64_t partition_id,
                                                          ObIArray<ObPartitionStatInfo> &partition_stat_infos)
{
  int ret = OB_SUCCESS;
  bool find_it = false;
  for (int64_t i = 0; !find_it && i < partition_stat_infos.count(); ++i) {
    if (partition_stat_infos.at(i).partition_id_ == partition_id) {
      partition_stat_infos.at(i).is_no_dml_modified_ = true;
      find_it = true;
    }
  }
  if (!find_it) {
    ObPartitionStatInfo partition_stat_info(partition_id, 0, false, true, false);
    ret = partition_stat_infos.push_back(partition_stat_info);
  }
  return ret;
}

int ObBasicStatsEstimator::fill_partition_info(ObIAllocator &allocator,
                                               const ObOptStatGatherParam &param,
                                               const PartInfo &part_info)
{
  int ret = OB_SUCCESS;
  const char *fmt_str = "CALC_PARTITION_ID(`%.*s`, %.*s) = %d";
  ObSqlString raw_sql_str;
  const int64_t buf_len = 512;
  char buf[buf_len];
  
  if (param.stat_level_ == PARTITION_LEVEL) {
    if (OB_FAIL(raw_sql_str.append("WHERE "))) {
    } else if (OB_FAIL(raw_sql_str.append_fmt(fmt_str, param.tab_name_.length(), param.tab_name_.ptr(),
                                              4, "PART", part_info.part_id_))) {
    } 
  } else if (param.stat_level_ == SUBPARTITION_LEVEL) {
    if (OB_FAIL(raw_sql_str.append("WHERE "))) {
    } else if (OB_FAIL(raw_sql_str.append_fmt(fmt_str, param.tab_name_.length(), param.tab_name_.ptr(),
                                              4, "PART", part_info.first_part_id_))) {
    } else if (OB_FAIL(raw_sql_str.append(" AND "))) {
    } else if (OB_FAIL(raw_sql_str.append_fmt(fmt_str, param.tab_name_.length(), param.tab_name_.ptr(),
                                              7, "SUBPART", part_info.part_id_))) {
    }
  }

  if (OB_SUCC(ret)) {
    char *buf = NULL;
    int64_t buf_len = raw_sql_str.length();
    if (OB_ISNULL(buf = static_cast<char *>(allocator.alloc(raw_sql_str.length())))) {
      ret = OB_ALLOCATE_MEMORY_FAILED;
      LOG_WARN("failed to alloc memory", K(ret), K(raw_sql_str.length()));
    } else {
      MEMCPY(buf, raw_sql_str.ptr(), raw_sql_str.length());
      where_string_.assign(buf, raw_sql_str.length());
    }
  }
  return ret;
}

} // end of common
} // end of oceanbase
