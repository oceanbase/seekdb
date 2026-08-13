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
#include "sql/plan_cache/ob_plan_match_helper.h"
#include "sql/plan_cache/ob_plan_set.h"
#include "sql/optimizer/ob_log_plan.h"

using namespace oceanbase::share;

namespace oceanbase {
namespace sql {

int ObPlanMatchHelper::match_plan(const ObPlanCacheCtx &pc_ctx,
                                  const ObPhysicalPlan *plan,
                                  bool &is_matched,
                                  ObIArray<ObCandiTableLoc> &phy_tbl_infos,
                                  ObIArray<ObTableLocation> &out_tbl_locations) const
{
  int ret = OB_SUCCESS;
  is_matched = true;
  const ObIArray<LocationConstraint>& base_cons = plan->get_base_constraints();
  const ObIArray<ObPlanPwjConstraint>& strict_cons = plan->get_strict_constraints();
  const ObIArray<ObPlanPwjConstraint>& non_strict_cons = plan->get_non_strict_constraints();
  const ObIArray<ObTableLocation> &plan_tbl_locs = plan->get_table_locations();
  PWJTabletIdMap pwj_map;
  bool use_pwj_map = false;

  if (OB_ISNULL(GET_MY_SESSION(pc_ctx.exec_ctx_))) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("fail to get session", KR(ret));
  } else if (0 == base_cons.count()) {
    // match all
    is_matched = true;
  } else {
    if (OB_SUCC(ret)) {
      // check base table constraints
      if (OB_FAIL(calc_table_locations(base_cons, plan_tbl_locs, pc_ctx,
                                      out_tbl_locations, phy_tbl_infos))) {
      } else if (OB_FAIL(cmp_table_types(base_cons, out_tbl_locations,
                                        phy_tbl_infos, is_matched))) {
      } else if (!is_matched) {
      } else if (OB_FAIL(check_partition_constraint(pc_ctx, base_cons, phy_tbl_infos, is_matched))) {
      } else if (!is_matched) {
      } else if (strict_cons.count() <= 0 && non_strict_cons.count() <= 0) {
        // do nothing
      } else if (OB_FAIL(pwj_map.create(8, ObModIds::OB_PLAN_EXECUTE))) {
      } else if (OB_FAIL(check_inner_constraints(strict_cons, non_strict_cons, phy_tbl_infos,
                                                pc_ctx, pwj_map, is_matched))) {
      } else {
        use_pwj_map = true;
      }

      if (OB_SUCC(ret) && is_matched && use_pwj_map) {
        GroupPWJTabletIdMap *exec_group_pwj_map = nullptr;
        if (OB_FAIL(pc_ctx.exec_ctx_.get_group_pwj_map(exec_group_pwj_map))) {
        } else if (OB_FAIL(exec_group_pwj_map->reuse())) {
        }
        GroupPWJTabletIdInfo group_pwj_tablet_id_info;
        TabletIdArray &tablet_id_array = group_pwj_tablet_id_info.tablet_id_array_;
        for (int64_t group_id = 0; OB_SUCC(ret) && group_id < strict_cons.count(); ++group_id) {
          group_pwj_tablet_id_info.group_id_ = group_id;
          const ObPlanPwjConstraint &pwj_cons = strict_cons.at(group_id);
          for (int64_t i = 0; OB_SUCC(ret) && i < pwj_cons.count(); ++i) {
            const int64_t table_idx = pwj_cons.at(i);
            uint64_t table_id = base_cons.at(table_idx).key_.table_id_;
            tablet_id_array.reset();
            if (!base_cons.at(table_idx).is_multi_part_insert()) {
              if (OB_FAIL(pwj_map.get_refactored(table_idx, tablet_id_array))) {
                if (OB_HASH_NOT_EXIST == ret) {
                  // means this is not a partition wise join table
                  ret = OB_SUCCESS;
                } else {
                  LOG_WARN("failed to get refactored", K(ret));
                }
              } else if (OB_FAIL(exec_group_pwj_map->set_refactored(table_id, group_pwj_tablet_id_info))) {
              }
            }
          }
        }
      }
    }
  }
  if (OB_FAIL(ret)) {
    is_matched = false;
  }
  if (pwj_map.created()) {
    int tmp_ret = OB_SUCCESS;
    if (OB_UNLIKELY(OB_SUCCESS != (tmp_ret = pwj_map.destroy()))) {
    }
  }
  return ret;
}

int ObPlanMatchHelper::get_tbl_loc_with_key(const TableLocationKey key,
                                            const ObIArray<ObTableLocation> &table_locations,
                                            const ObTableLocation *&ret_loc_ptr) const
{
  int ret = OB_SUCCESS;
  ret_loc_ptr = NULL;
  const ObTableLocation *tmp_loc_ptr;
  for (int i = 0; i < table_locations.count(); i++) {
    tmp_loc_ptr = &table_locations.at(i);
    if (tmp_loc_ptr->get_table_id() == key.table_id_ &&
        tmp_loc_ptr->get_ref_table_id() == key.ref_table_id_) {
      ret_loc_ptr = tmp_loc_ptr;
      break;
    }
  }
  if (OB_ISNULL(ret_loc_ptr)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("got an unexpected null", K(ret), K(key), K(table_locations));
  }
  return ret;
}

int ObPlanMatchHelper::calc_table_locations(
    const ObIArray<LocationConstraint> &loc_cons,
    const ObIArray<ObTableLocation> &in_tbl_locations,
    const ObPlanCacheCtx &pc_ctx,
    common::ObIArray<ObTableLocation> &out_tbl_locations,
    common::ObIArray<ObCandiTableLoc> &phy_tbl_infos) const
{
  int ret = OB_SUCCESS;
  if (loc_cons.count() <= 0 || in_tbl_locations.count() <= 0) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", K(ret), K(loc_cons.count()), K(in_tbl_locations.count()));
  } else {
    for (int64_t i = 0; OB_SUCC(ret) && i < loc_cons.count(); i++) {
      const ObTableLocation *tmp_tbl_loc_ptr;
      if (OB_FAIL(get_tbl_loc_with_key(loc_cons.at(i).key_,
                                       in_tbl_locations,
                                       tmp_tbl_loc_ptr))) {
      } else if (OB_ISNULL(tmp_tbl_loc_ptr)) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("got an unexpected null tbl_loc_ptr", K(ret), K(tmp_tbl_loc_ptr));
      } else if (OB_FAIL(out_tbl_locations.push_back(*tmp_tbl_loc_ptr))) {
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_FAIL(ObPhyLocationGetter::get_phy_locations(out_tbl_locations,
                                                         pc_ctx,
                                                         phy_tbl_infos))) {
      } else {
      }
    }
  }

  if (OB_FAIL(ret)) {
    out_tbl_locations.reset();
    phy_tbl_infos.reset();
  }
  return ret;
}

int ObPlanMatchHelper::cmp_table_types(
    const ObIArray<LocationConstraint> &loc_cons,
    const common::ObIArray<ObTableLocation> &tbl_locs,
    const common::ObIArray<ObCandiTableLoc> &phy_tbl_infos,
    bool &is_same) const
{
  int ret = OB_SUCCESS;
  is_same = true;
  if (loc_cons.count() != phy_tbl_infos.count() ||
      tbl_locs.count() != phy_tbl_infos.count()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", K(loc_cons.count()), K(phy_tbl_infos.count()));
  } else {
    for (int64_t i = 0; OB_SUCC(ret) && is_same && i < loc_cons.count(); i++) {
      const ObCandiTabletLocIArray &phy_part_loc_info_list =
        phy_tbl_infos.at(i).get_phy_part_loc_info_list();
      const ObTableLocation &tbl_loc = tbl_locs.at(i);
      const ObTableLocationType loc_type = tbl_loc.get_location_type(phy_part_loc_info_list);
      is_same = (loc_type == loc_cons.at(i).phy_loc_type_);
    }
  }
  if (OB_FAIL(ret)) {
    is_same = false;
  }
  return ret;
}

int ObPlanMatchHelper::check_partition_constraint(
    const ObPlanCacheCtx &pc_ctx,
    const ObIArray<LocationConstraint> &loc_cons,
    const common::ObIArray<ObCandiTableLoc> &phy_tbl_infos,
    bool &is_match) const
{
  int ret = OB_SUCCESS;
  is_match = true;
  share::schema::ObSchemaGetterGuard *schema_guard = pc_ctx.sql_ctx_.schema_guard_;
  const share::schema::ObTableSchema *table_schema = NULL;
  if (loc_cons.count() != phy_tbl_infos.count()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", K(loc_cons.count()), K(phy_tbl_infos.count()));
  } else if (OB_ISNULL(schema_guard)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("get unexpected null", K(ret));
  } else if (OB_ISNULL(GET_MY_SESSION(pc_ctx.exec_ctx_))) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("fail to get session", KR(ret));
  } else {
    
    for (int64_t i = 0; OB_SUCC(ret) && is_match && i < loc_cons.count(); i++) {
      const ObCandiTabletLocIArray &phy_part_loc_info_list =
        phy_tbl_infos.at(i).get_phy_part_loc_info_list();
      if (!loc_cons.at(i).is_partition_single() && !loc_cons.at(i).is_subpartition_single()) {
        // do nothing
      } else if (OB_FAIL(schema_guard->get_table_schema( phy_tbl_infos.at(i).get_ref_table_id(), table_schema))) {
      } else if (OB_ISNULL(table_schema)) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("get null table schema", K(ret), K(phy_tbl_infos.at(i).get_ref_table_id()));
      } else if (loc_cons.at(i).is_partition_single()) {
        // is_partition_single requires that the current secondary partition
        // table only involves one primary partition
        int64_t first_part_id = OB_INVALID_PARTITION_ID;
        for (int64_t j = 0; OB_SUCC(ret) && is_match && j < phy_part_loc_info_list.count(); ++j) {
          ObTabletID cur_tablet_id =
              phy_part_loc_info_list.at(j).get_partition_location().get_tablet_id();
          int64_t cur_part_id = OB_INVALID_ID;
          int64_t cur_subpart_id = OB_INVALID_ID;
          if (OB_FAIL(table_schema->get_part_id_by_tablet(cur_tablet_id, cur_part_id, cur_subpart_id))) {
          } else if (OB_INVALID_PARTITION_ID == first_part_id) {
            first_part_id = cur_part_id;
          } else if (cur_part_id != first_part_id) {
            is_match = false;
          }
        }
      } else if (loc_cons.at(i).is_subpartition_single()) {
        // is_subpartition_single requires that each primary partition of the current
        // secondary partition table involves only one secondary partition
        ObSEArray<int64_t, 4> part_ids;
        for (int64_t j = 0; OB_SUCC(ret) && is_match && j < phy_part_loc_info_list.count(); ++j) {
          ObTabletID cur_tablet_id =
              phy_part_loc_info_list.at(j).get_partition_location().get_tablet_id();
          int64_t cur_part_id = OB_INVALID_ID;
          int64_t cur_subpart_id = OB_INVALID_ID;
          if (OB_FAIL(table_schema->get_part_id_by_tablet(cur_tablet_id, cur_part_id, cur_subpart_id))) {
          } else {
            for (int64_t k = 0; OB_SUCC(ret) && is_match && k < part_ids.count(); ++k) {
              if (part_ids.at(k) == cur_part_id) {
                is_match = false;
              }
            }

            if (OB_FAIL(ret)) {
              // do nothing
            } else if (!is_match) {
              // do nothing
            } else if (OB_FAIL(part_ids.push_back(cur_part_id))) {
            } else {
              // do nothing
            }
          }
        }
      }
    }
  }
  if (OB_FAIL(ret)) {
    is_match = false;
  }
  return ret;
}

int ObPlanMatchHelper::check_inner_constraints(
    const ObIArray<ObPlanPwjConstraint> &strict_cons,
    const ObIArray<ObPlanPwjConstraint> &non_strict_cons,
    const common::ObIArray<ObCandiTableLoc> &phy_tbl_infos,
    const ObPlanCacheCtx &pc_ctx,
    PWJTabletIdMap &pwj_map,
    bool &is_same) const
{
  int ret = OB_SUCCESS;
  is_same = true;
  if (strict_cons.count() >0 || non_strict_cons.count() > 0) {
    SMART_VAR(ObStrictPwjComparer, strict_pwj_comparer) {
      for (int64_t i = 0; OB_SUCC(ret) && is_same && i < strict_cons.count(); ++i) {
        const ObPlanPwjConstraint &pwj_cons = strict_cons.at(i);
        if (OB_UNLIKELY(pwj_cons.count() <= 1)) {
          ret = OB_ERR_UNEXPECTED;
          LOG_WARN("get unexpected pwj constraint", K(ret), K(pwj_cons));
        } else if (OB_FAIL(check_strict_pwj_cons(pc_ctx, pwj_cons, phy_tbl_infos,
                                                 strict_pwj_comparer, pwj_map, is_same))) {
        } else {
        }
      }

      for (int64_t i = 0; OB_SUCC(ret) && is_same && i < non_strict_cons.count(); ++i) {
        const ObPlanPwjConstraint &pwj_cons = non_strict_cons.at(i);
        if (OB_UNLIKELY(pwj_cons.count() <= 1)) {
          ret = OB_ERR_UNEXPECTED;
          LOG_WARN("get unexpected pwj constraint", K(ret), K(pwj_cons));
        } else {
          // Every valid tablet is local in seekdb. A non-strict PWJ constraint
          // therefore only needs each referenced table to have a local tablet.
          for (int64_t j = 0; OB_SUCC(ret) && is_same && j < pwj_cons.count(); ++j) {
            const int64_t table_idx = pwj_cons.at(j);
            if (OB_UNLIKELY(table_idx < 0 || table_idx >= phy_tbl_infos.count())) {
              ret = OB_ERR_UNEXPECTED;
              LOG_WARN("invalid table index in pwj constraint", K(ret), K(table_idx), K(pwj_cons));
            } else if (phy_tbl_infos.at(table_idx).get_partition_cnt() <= 0) {
              is_same = false;
            }
          }
        }
      }
    }
  }
  if (OB_FAIL(ret)) {
    is_same = false;
  }
  return ret;
}

int ObPlanMatchHelper::check_strict_pwj_cons(
        const ObPlanCacheCtx &pc_ctx,
        const ObPlanPwjConstraint &pwj_cons,
        const ObIArray<ObCandiTableLoc> &phy_tbl_infos,
        ObStrictPwjComparer &pwj_comparer,
        PWJTabletIdMap &pwj_map,
        bool &is_same) const
{
  int ret = OB_SUCCESS;
  // check all table in same pwj constraint have same partition count
  const int64_t part_count = phy_tbl_infos.at(pwj_cons.at(0)).get_partition_cnt();
  for (int64_t i = 1; is_same && i < pwj_cons.count(); ++i) {
    if (part_count != phy_tbl_infos.at(pwj_cons.at(i)).get_partition_cnt()) {
      is_same = false;
    }
  }

  if (1 == part_count) {
    // All tables in this PWJ constraint are single-partition tables.
    for (int64_t i = 0; OB_SUCC(ret) && is_same && i < pwj_cons.count() - 1; ++i) {
      const ObCandiTableLoc &l_phy_tbl_info = phy_tbl_infos.at(pwj_cons.at(i));
      const ObCandiTableLoc &r_phy_tbl_info = phy_tbl_infos.at(pwj_cons.at(i+1));
      if (OB_FAIL(match_tbl_partition_locs(l_phy_tbl_info, r_phy_tbl_info, is_same))) {
      }
    }
  } else if (OB_ISNULL(GET_MY_SESSION(pc_ctx.exec_ctx_))) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("fail to get session", KR(ret));
  } else {
    // distribute partition wise join
    pwj_comparer.reset();
    
    for (int64_t i = 0; OB_SUCC(ret) && is_same && i < pwj_cons.count(); ++i) {
      const int64_t table_idx = pwj_cons.at(i);
      PwjTable pwj_table;
      const ObCandiTableLoc &phy_tbl_info = phy_tbl_infos.at(table_idx);
      share::schema::ObSchemaGetterGuard *schema_guard = pc_ctx.sql_ctx_.schema_guard_;
      const share::schema::ObTableSchema *table_schema = NULL;
      if (OB_ISNULL(schema_guard)) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("get unexpected null", K(ret));
      } else if (OB_FAIL(schema_guard->get_table_schema( phy_tbl_info.get_ref_table_id(), table_schema))) {
      } else if (OB_ISNULL(table_schema)) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("get unexpected null", K(ret));
      } else if (OB_FAIL(pwj_table.init(*table_schema, phy_tbl_info))) {
      } else if (OB_FAIL(pwj_comparer.add_table(pwj_table, is_same))) {
      } else if (is_same &&
                 OB_FAIL(pwj_map.set_refactored(table_idx, pwj_comparer.get_tablet_id_group().at(i)))) {
        LOG_WARN("failed to set refactored", K(ret));
      }
    }
  }
  return ret;
}

int ObPlanMatchHelper::match_tbl_partition_locs(const ObCandiTableLoc &left,
                                                const ObCandiTableLoc &right,
                                                bool &is_matched) const
{
  int ret = OB_SUCCESS;
  is_matched = true;
  if (left.get_partition_cnt() != right.get_partition_cnt()) {
    is_matched = false;
  } else if (left.get_partition_cnt() <= 0) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("there is no partition_location in phy_location", K(ret), K(left),
             K(right));
  } else {
    for (int64_t i = 0;
         OB_SUCC(ret) && is_matched && i < left.get_partition_cnt(); i++) {
      const ObCandiTabletLoc &left_phy_part_loc_info =
          left.get_phy_part_loc_info_list().at(i);
      const ObCandiTabletLoc &right_phy_part_loc_info =
          right.get_phy_part_loc_info_list().at(i);
      const ObAddr &left_server =
          left_phy_part_loc_info.get_partition_location().get_server();
      const ObAddr &right_server =
          right_phy_part_loc_info.get_partition_location().get_server();

      if (!left_server.is_valid() || !right_server.is_valid()) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("local server is invalid", K(ret), K(left_server), K(right_server));
      } else if (left_server != right_server) {
        is_matched = false;
      } else {
      }
    }
  }
  if (OB_FAIL(ret)) {
    is_matched = false;
  } else {
    /* do nothing */
  }

  return ret;
}
} // namespace sql
} // namespace oceanbase
