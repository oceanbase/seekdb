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

#ifndef OB_STATS_ESTIMATOR_H
#define OB_STATS_ESTIMATOR_H

#include "share/stat/ob_stat_define.h"
#include "sql/engine/ob_exec_context.h"
#include "share/stat/ob_stat_item.h"
#include "observer/ob_sql_client_decorator.h"
#include "share/stat/ob_opt_table_stat.h"
#include "share/stat/ob_opt_column_stat.h"

namespace oceanbase
{
using namespace sql;
namespace common
{

class ObStatsEstimator
{
public:
  explicit ObStatsEstimator(ObExecContext &ctx, ObIAllocator &allocator);

protected:

  int gen_select_filed();

  int64_t get_item_size() const { return stat_items_.count(); }

  int decode(ObIAllocator &allocator);

  int add_result(ObObj &obj)  { return results_.push_back(obj); }

  int do_estimate(const ObOptStatGatherParam &gather_param,
                  const ObString &raw_sql,
                  bool need_copy_basic_stat,
                  ObOptStat &src_opt_stat,
                  ObIArray<ObOptStat> &dst_opt_stats);

  int pack(ObSqlString &raw_sql_str);

  int add_from_table(common::ObIAllocator &allocator,
                     const ObString &db_name,
                     const ObString &table_name);

  int add_partition_hint(const ObString &partition);

  int fill_sample_info(common::ObIAllocator &alloc,
                       double est_percent,
                       bool block_sample);

  int fill_sample_info(common::ObIAllocator &alloc,
                       const ObAnalyzeSampleInfo &sample_info);

  int fill_parallel_info(common::ObIAllocator &alloc,
                         int64_t degree);

  int fill_query_timeout_info(common::ObIAllocator &alloc,
                              const int64_t duration_timeout);

  int fill_partition_info(ObIAllocator &allocator,
                          const ObString &part_nam);

  int fill_partition_info(ObIAllocator &allocator,
                          const ObIArray<PartInfo> &partition_infos);

  int add_hint(const ObString &hint_str,
               common::ObIAllocator &alloc);

  int fill_group_by_info(ObIAllocator &allocator,
                         const ObOptStatGatherParam &param,
                         ObString &calc_part_id_str);

  void reset_select_items() { stat_items_.reset(); select_fields_.reset(); }
  void reset_sample_hint() { sample_hint_.reset(); }
  void reset_other_hint() { other_hints_.reset(); }

  int fill_specify_scn_info(common::ObIAllocator &alloc, uint64_t sepcify_scn);

private:
  int copy_basic_opt_stat(ObOptStat &src_opt_stat,
                          ObIArray<ObOptStat> &dst_opt_stats);

  int copy_basic_col_stats(const int64_t cur_row_cnt,
                           const int64_t total_row_cnt,
                           ObIArray<ObOptColumnStat *> &src_col_stats,
                           ObIArray<ObOptColumnStat *> &dst_col_stats);

protected:

  ObExecContext &ctx_;
  ObIAllocator &allocator_;

  ObString db_name_;
  ObString from_table_;
  ObString partition_hint_;
  ObSqlString select_fields_;
  ObString sample_hint_;
  ObString other_hints_;
  ObString partition_string_;
  ObString group_by_string_;
  ObString where_string_;

  ObArray<ObStatItem *> stat_items_;
  ObArray<ObObj> results_;
  double sample_value_;
  ObString current_scn_string_;
};


}
}

#endif // OB_STATS_ESTIMATOR_H
