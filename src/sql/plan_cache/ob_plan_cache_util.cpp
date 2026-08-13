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

#define USING_LOG_PREFIX SQL_PC

#include "ob_plan_cache_util.h"
#include "sql/optimizer/ob_log_plan.h"
using namespace oceanbase::share;
using namespace oceanbase::share::schema;
using namespace oceanbase::omt;

namespace oceanbase
{
namespace sql
{



int ObGetAllCacheIdOp::operator()(common::hash::HashMapPair<ObCacheObjID, ObILibCacheObject *> &entry)
{
  int ret = common::OB_SUCCESS;
  if (NULL == key_array_ || OB_ISNULL(entry.second)) {
    ret = common::OB_NOT_INIT;
    SQL_PC_LOG(WARN, "invalid argument", K(ret));
  } else if ((entry.second->get_ns() >= ObLibCacheNameSpace::NS_CRSR
            && entry.second->get_ns() <= ObLibCacheNameSpace::NS_PKG)
            ||entry.second->get_ns() == ObLibCacheNameSpace::NS_CALLSTMT) {
    if (OB_ISNULL(entry.second)) {
      // do nothing
    } else if (!entry.second->added_lc()) {
      // do nothing
    } else if (OB_FAIL(key_array_->push_back(entry.first))) {
    }
  }
  return ret;
}

int ObPhyLocationGetter::get_phy_locations(const common::ObIArray<ObTablePartitionInfo *> &partition_infos,
                                           ObIArray<ObCandiTableLoc> &candi_table_locs)
{
  int ret = OB_SUCCESS;
  //ObDASTableLoc table_loc;
  int64_t N = partition_infos.count();
  if (OB_FAIL(candi_table_locs.reserve(N))) {
  }
  for (int64_t i = 0; OB_SUCC(ret) && i < N; i++) {
    if (OB_ISNULL(partition_infos.at(i))) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("invalid partition info", K(ret));
    } else if (OB_FAIL(candi_table_locs.push_back(
                   partition_infos.at(i)->get_phy_tbl_location_info()))) {
    } else { /* do nothing */ }
  }
  return ret;
}
int ObPhyLocationGetter::get_phy_locations(const ObIArray<ObTableLocation> &table_locations,
                                           const ObPlanCacheCtx &pc_ctx,
                                           ObIArray<ObCandiTableLoc> &candi_table_locs)
{
  int ret = OB_SUCCESS;
  ObExecContext &exec_ctx = pc_ctx.exec_ctx_;
  const ObDataTypeCastParams dtc_params = ObBasicSessionInfo::create_dtc_params(pc_ctx.sql_ctx_.session_info_);
  ObPhysicalPlanCtx *plan_ctx = exec_ctx.get_physical_plan_ctx();
  int64_t N = table_locations.count();
  if (OB_ISNULL(plan_ctx)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("invalid executor ctx!", K(ret), K(plan_ctx));
  } else {
    ObSEArray<const ObTableLocation *, 2> table_location_ptrs;
    ObSEArray<ObCandiTableLoc *, 2> phy_location_info_ptrs;
    const ParamStore &params = plan_ctx->get_param_store();
    if (OB_FAIL(candi_table_locs.prepare_allocate(N))) {
    } else {
      for (int64_t i = 0; OB_SUCC(ret) && i < N; i++) {
        const ObTableLocation &table_location = table_locations.at(i);
        ObCandiTableLoc &candi_table_loc = candi_table_locs.at(i);
        NG_TRACE(calc_partition_location_begin);
        if (OB_FAIL(table_location.calculate_candi_tablet_locations(exec_ctx,
                                                                    params,
                                                                    candi_table_loc.get_phy_part_loc_info_list_for_update(),
                                                                    dtc_params))) {
        } else {
          NG_TRACE(calc_partition_location_end);
          candi_table_loc.set_table_location_key(
              table_location.get_table_id(), table_location.get_ref_table_id());
        }
        if (OB_SUCC(ret)) {
          if (OB_FAIL(table_location_ptrs.push_back(&table_location))) {
          } else if (OB_FAIL(phy_location_info_ptrs.push_back(&candi_table_loc))) {
          }
        }
      } // for end
    }

    if (OB_SUCC(ret) && N!=0 ) {
      if (OB_FAIL(ObLogPlan::validate_local_tablets(exec_ctx,
                                                    table_location_ptrs,
                                                    phy_location_info_ptrs))) {
      }
    }
  }

  return ret;
}

int ObPhyLocationGetter::build_table_locs(ObDASCtx &das_ctx,
                                          const ObIArray<ObTableLocation> &table_locations,
                                          const ObIArray<ObCandiTableLoc> &candi_table_locs)
{
  int ret = OB_SUCCESS;
  CK(table_locations.count() == candi_table_locs.count());
  for (int64_t i = 0; OB_SUCC(ret) && i < table_locations.count(); i++) {
    if (OB_FAIL(das_ctx.add_candi_table_loc(table_locations.at(i).get_loc_meta(), candi_table_locs.at(i)))) {
    }
  }
  if (OB_FAIL(ret)) {
    das_ctx.clear_all_location_info();
  }

  return ret;
}


//this function will rewrite the related tablet map info in DASCtx
int ObPhyLocationGetter::build_related_tablet_info(const ObTableLocation &table_location,
                                                   ObExecContext &exec_ctx,
                                                   DASRelatedTabletMap *&related_map)
{
  int ret = OB_SUCCESS;
  ObDataTypeCastParams dtc_params = ObBasicSessionInfo::create_dtc_params(exec_ctx.get_my_session());
  ObPhysicalPlanCtx *plan_ctx = exec_ctx.get_physical_plan_ctx();
  ObArray<ObObjectID> partition_ids;
  ObArray<ObObjectID> first_level_part_ids;
  ObArray<ObTabletID> tablet_ids;

  if (OB_FAIL(table_location.calculate_tablet_ids(exec_ctx,
                                                  plan_ctx->get_param_store(),
                                                  tablet_ids,
                                                  partition_ids,
                                                  first_level_part_ids,
                                                  dtc_params))) {
  } else {
    related_map = &exec_ctx.get_das_ctx().get_related_tablet_map();
    LOG_DEBUG("build_related tablet info", K(tablet_ids), K(partition_ids),
             K(table_location.get_loc_meta()), KPC(related_map));
  }
  return ret;
}

OB_SERIALIZE_MEMBER(ObTableRowCount, op_id_, row_count_);

int ObConfigInfoInPC::load_influence_plan_config()
{
  int ret = OB_SUCCESS;
  // Add runtime configuration dependencies here when needed.

  // For Cluster configs
  // here to add value of configs that can influence execution plan.
  enable_px_ordered_coord_ = GCONF._enable_px_ordered_coord;
  enable_newsort_ = GCONF._enable_newsort;
  is_strict_defensive_check_ = GCONF.enable_strict_defensive_check();
  bloom_filter_ratio_ = GCONF._bloom_filter_ratio;
  realistic_runtime_bloom_filter_size_ = !GCONF._preset_runtime_bloom_filter_size;
  ndv_runtime_bloom_filter_size_ = GCONF._ndv_runtime_bloom_filter_size;

  // Runtime configuration dependencies.
  // Use the runtime configuration to read the current settings.

  pushdown_storage_level_ = GCONF._pushdown_storage_level;
  rowsets_enabled_ = GCONF._rowsets_enabled;
  enable_px_batch_rescan_ = GCONF._enable_px_batch_rescan;
  bloom_filter_enabled_ = GCONF._bloom_filter_enabled;
  px_join_skew_handling_ = GCONF._px_join_skew_handling;
  px_join_skew_minfreq_ = static_cast<int8_t>(GCONF._px_join_skew_minfreq);
  enable_spf_batch_rescan_ = GCONF._enable_spf_batch_rescan;
  enable_var_assign_use_das_ = GCONF._enable_var_assign_use_das;
  enable_das_keep_order_ = GCONF._enable_das_keep_order;
  enable_index_merge_ = GCONF._enable_index_merge;
  enable_parallel_das_dml_ = GCONF._enable_parallel_das_dml;
  hash_rollup_policy_ = GCONF._use_hash_rollup.case_compare("auto") == 0 ?
                          0 :
                          (GCONF._use_hash_rollup.case_compare("forced") == 0 ? 1 : 2);
  enable_distributed_das_scan_ = GCONF._enable_distributed_das_scan;
  enable_das_batch_rescan_flag_ = GCONF._enable_das_batch_rescan_flag;
  enable_topn_runtime_filter_ = GCONF._enable_topn_runtime_filter;
  min_const_integer_precision_ = static_cast<int8_t>(GCONF._min_const_integer_precision);
  enable_px_task_rebalance_ = GCONF._enable_px_task_rebalance;


  return ret;
}

// reading values and generate strings
int ObConfigInfoInPC::serialize_configs(char *buf, int buf_len, int64_t &pos)
{
  int ret = OB_SUCCESS;
  pos = 0;

  // gen config str
  if (OB_FAIL(databuff_printf(buf, buf_len, pos,
                              "%d,", pushdown_storage_level_))) {
  } else if (OB_FAIL(databuff_printf(buf, buf_len, pos,
                              "%d,", rowsets_enabled_))) {
  } else if (OB_FAIL(databuff_printf(buf, buf_len, pos,
                              "%d,", enable_px_batch_rescan_))) {
  } else if (OB_FAIL(databuff_printf(buf, buf_len, pos,
                              "%d,", enable_px_ordered_coord_))) {
  } else if (OB_FAIL(databuff_printf(buf, buf_len, pos,
                              "%d,", bloom_filter_enabled_))) {
  } else if (OB_FAIL(databuff_printf(buf, buf_len, pos,
                              "%d,", enable_newsort_))) {
  } else if (OB_FAIL(databuff_printf(buf, buf_len, pos,
                              "%d,", px_join_skew_handling_))) {
  } else if (OB_FAIL(databuff_printf(buf, buf_len, pos,
                              "%d,", is_strict_defensive_check_))) {
  } else if (OB_FAIL(databuff_printf(buf, buf_len, pos,
                              "%d,", px_join_skew_minfreq_))) {
  } else if (OB_FAIL(databuff_printf(buf, buf_len, pos,
                               "%d,", enable_spf_batch_rescan_))) {
  } else if (OB_FAIL(databuff_printf(buf, buf_len, pos,
                               "%d,", enable_var_assign_use_das_))) {
  } else if (OB_FAIL(databuff_printf(buf, buf_len, pos,
                               "%d,", enable_das_keep_order_))) {
  } else if (OB_FAIL(databuff_printf(buf, buf_len, pos,
                               "%d,", bloom_filter_ratio_))) {
  } else if (OB_FAIL(databuff_printf(buf, buf_len, pos,
                               "%d,", realistic_runtime_bloom_filter_size_))) {
  } else if (OB_FAIL(databuff_printf(buf, buf_len, pos,
                               "%d,", enable_parallel_das_dml_))) {
  } else if (OB_FAIL(databuff_printf(buf, buf_len, pos, "%d,", hash_rollup_policy_))) {
  } else if (OB_FAIL(databuff_printf(buf, buf_len, pos,
                               "%d,", ndv_runtime_bloom_filter_size_))) {
  } else if (OB_FAIL(databuff_printf(buf, buf_len, pos,
                               "%d,", enable_index_merge_))) {
  } else if (OB_FAIL(databuff_printf(buf, buf_len, pos,
                              "%d,", enable_distributed_das_scan_))) {
  } else if (OB_FAIL(databuff_printf(buf, buf_len, pos,
                              "%ld,", enable_das_batch_rescan_flag_))) {
  } else if (OB_FAIL(databuff_printf(buf, buf_len, pos,
                              "%d,", enable_topn_runtime_filter_))) {
  } else if (OB_FAIL(databuff_printf(buf, buf_len, pos, "%d,", min_const_integer_precision_))) {
  } else if (OB_FAIL(databuff_printf(buf, buf_len, pos, "%d,", enable_px_task_rebalance_))) {
  } else {
    // do nothing
  }
  // trim last comma
  pos--;
  return ret;
}

}
}
