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

#define USING_LOG_PREFIX SQL_OPT
#include "ob_route_policy.h"
#include "sql/optimizer/ob_replica_compare.h"
#include "sql/optimizer/ob_log_plan.h"
#include "storage/ob_locality_manager.h"
using namespace oceanbase::common;
using namespace oceanbase::share;
using namespace oceanbase::storage;
namespace oceanbase
{
namespace sql
{


int ObRoutePolicy::weak_sort_replicas(ObIArray<CandidateReplica>& candi_replicas, ObRoutePolicyCtx &ctx)
{
  int ret = OB_SUCCESS;
  if (candi_replicas.count() > 1) {
    auto first = &candi_replicas.at(0);
    ObRoutePolicyType policy_type = get_calc_route_policy_type(ctx);
    ObReplicaCompare replica_cmp(policy_type);
    lib::ob_sort(first, first + candi_replicas.count(), replica_cmp);
    if (OB_FAIL(replica_cmp.get_sort_ret())) {
      LOG_WARN("fail sort", K(candi_replicas), K(ret));
    }
  }
  return ret;
}


int ObRoutePolicy::filter_replica(const ObAddr &local_server,
                                  const ObLSID &ls_id,
                                  ObIArray<CandidateReplica>& candi_replicas,
                                  ObRoutePolicyCtx &ctx)
{
  int ret = OB_SUCCESS;
  ObRoutePolicyType policy_type = get_calc_route_policy_type(ctx);
  bool need_break = false;
  for (int64_t i = 0; !need_break && OB_SUCC(ret) && i < candi_replicas.count(); ++i) {
    CandidateReplica &cur_replica = candi_replicas.at(i);
    bool can_read = true;
    bool is_local = cur_replica.get_server() == local_server;

    if (is_local && OB_FAIL(ObSqlTransControl::check_ls_readable(ctx.tenant_id_,
                                                     ls_id,
                                                     cur_replica.get_server(),
                                                     ctx.max_read_stale_time_,
                                                     can_read))) {
      LOG_WARN("fail to check ls readable", K(ctx), K(cur_replica), K(ret));
    } else {
      LOG_TRACE("check ls readable", K(ctx), K(ls_id), K(cur_replica.get_server()), K(can_read));
      if ((policy_type == ONLY_READONLY_ZONE && cur_replica.attr_.zone_type_ == ZONE_TYPE_READWRITE)
          || (policy_type == COLUMN_STORE_ONLY && !ObReplicaTypeCheck::is_columnstore_replica(cur_replica.get_replica_type()))
          || (policy_type != COLUMN_STORE_ONLY && ObReplicaTypeCheck::is_columnstore_replica(cur_replica.get_replica_type()))
          || (policy_type == FORCE_READONLY_ZONE && cur_replica.get_replica_type() != REPLICA_TYPE_READONLY)
          || cur_replica.attr_.zone_status_ == ObZoneStatus::INACTIVE
          || cur_replica.attr_.server_status_ != ObServerStatus::OB_SERVER_ACTIVE
          || cur_replica.attr_.start_service_time_ == 0
          || cur_replica.attr_.server_stop_time_ != 0
          || (0 == cur_replica.get_property().get_memstore_percent()
              && is_follower(cur_replica.get_role()))// As a Follower, the D role cannot be selected
          || !can_read) {
        cur_replica.is_filter_ = true;
      }

      // if is local replica and can read, filter all replicas and only select this replica.
      if (is_local && !cur_replica.is_filter_) {
        for (int64_t j = 0; j < candi_replicas.count(); ++j) {
          candi_replicas.at(i).is_filter_ = true;
        }
        cur_replica.is_filter_ = false;
        need_break = true;
      }
    }
  }
  if (OB_SUCC(ret)) {
    for (int64_t i = candi_replicas.count()-1; OB_SUCC(ret) && i >= 0; --i) {
      CandidateReplica &cur_replica = candi_replicas.at(i);
      if (cur_replica.is_filter_ && 
          ((policy_type == COLUMN_STORE_ONLY && !ObReplicaTypeCheck::is_columnstore_replica(cur_replica.get_replica_type()))
          || (policy_type != COLUMN_STORE_ONLY && ObReplicaTypeCheck::is_columnstore_replica(cur_replica.get_replica_type()))) &&
          OB_FAIL(candi_replicas.remove(i))) {
        LOG_WARN("failed to remove filted replica", K(ret));
      }
    }
    if (OB_SUCC(ret) && candi_replicas.count() == 0) {
      ret = OB_NO_REPLICA_VALID;
      LOG_USER_ERROR(OB_NO_REPLICA_VALID);
    }
  }
  if (OB_SUCC(ret) && policy_type == FORCE_READONLY_ZONE) {
    for (int64_t i = candi_replicas.count() - 1; OB_SUCC(ret) && i >= 0; --i) {
      CandidateReplica &cur_replica = candi_replicas.at(i);
      if (cur_replica.is_filter_ && OB_FAIL(candi_replicas.remove(i))) {
        LOG_WARN("failed to remove filtered replica", K(ret));
      }
    }
    if (OB_SUCC(ret) && candi_replicas.count() == 0) {
      ret = OB_NO_READABLE_REPLICA;
      LOG_WARN("all replicas are filted", K(ret), K(policy_type));
    }
  }
  return ret;
}

int ObRoutePolicy::calculate_replica_priority(const ObAddr &local_server,
                                              const ObLSID &ls_id,
                                              ObIArray<CandidateReplica>& candi_replicas,
                                              ObRoutePolicyCtx &ctx,
                                              bool is_inner_table)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(!is_inited_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", K(ret));
  } else if (candi_replicas.count() <= 1) {
    ObRoutePolicyType policy_type = get_calc_route_policy_type(ctx);
    if (1 == candi_replicas.count() &&
        policy_type == COLUMN_STORE_ONLY && 
        !is_inner_table &&
        !ObReplicaTypeCheck::is_columnstore_replica(candi_replicas.at(0).get_replica_type())) {
      ret = OB_NO_REPLICA_VALID;
      LOG_USER_ERROR(OB_NO_REPLICA_VALID);
    }
  } else if (WEAK == ctx.consistency_level_) {
    if (OB_FAIL(filter_replica(local_server, ls_id, candi_replicas, ctx))) {
      LOG_WARN("fail to filter replicas", K(candi_replicas), K(ctx), K(ret));
    } else if (OB_FAIL(weak_sort_replicas(candi_replicas, ctx))) {
      LOG_WARN("fail to sort replicas", K(candi_replicas), K(ctx), K(ret));
    }
  } else if (STRONG == ctx.consistency_level_) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("strong consistency cant't be here", K(ctx), K(ret));
  } else {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("unexpected consistency level_", K(ctx));
  }
  return ret;
}

int ObRoutePolicy::init_candidate_replica(CandidateReplica &candi_replica)
{
  int ret = OB_SUCCESS;
  ObServerLocality candi_locality;
  candi_replica.attr_.zone_status_ = ObZoneStatus::ACTIVE;
  candi_replica.attr_.merge_status_ = NOMERGING;
  candi_replica.attr_.pos_type_ = SAME_SERVER;
  candi_replica.attr_.zone_type_ = ObZoneType::ZONE_TYPE_READWRITE;
  candi_replica.attr_.start_service_time_ = GCTX.start_service_time_;
  candi_replica.attr_.server_stop_time_ = 0;
  candi_replica.attr_.server_status_ = ObServerStatus::OB_SERVER_ACTIVE;
  return ret;
}

int ObRoutePolicy::init_candidate_replicas(common::ObIArray<CandidateReplica> &candi_replicas)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(!is_inited_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", K(ret));
  }
  for (int64_t i = 0 ; OB_SUCC(ret) && i < candi_replicas.count(); ++i) {
    if (OB_FAIL(init_candidate_replica(candi_replicas.at(i)))) {
      LOG_WARN("fail to candidate replica", K(i), K(candi_replicas), K(ret));
    }
  }
  return ret;
}

int ObRoutePolicy::select_replica_with_priority(const ObRoutePolicyCtx &route_policy_ctx,
                                                const ObIArray<ObRoutePolicy::CandidateReplica> &replica_array,
                                                ObCandiTabletLoc &phy_part_loc_info)
{
  int ret = OB_SUCCESS;
  bool has_found = false;
  bool same_priority = true;
  ReplicaAttribute priority_attr;
  for (int64_t i = 0; OB_SUCC(ret) && same_priority && i < replica_array.count(); ++i) {
    if (replica_array.at(i).is_usable()/*+meet max_read_stale_time transaction delay*/) {
      if (has_found) {
        if (priority_attr == replica_array.at(i).attr_) {
          if (OB_FAIL(phy_part_loc_info.add_priority_replica_idx(i))) {
            LOG_WARN("fail to select replica", K(i), K(priority_attr), K(ret));
          }
        } else {
          same_priority = false;
        }
      } else {
        if (OB_FAIL(phy_part_loc_info.add_priority_replica_idx(i))) {
          LOG_WARN("fail to select replica", K(i), K(ret));
        } else {
          has_found = true;
          priority_attr = replica_array.at(i).attr_;
        }
      }
    }
  }
  // In extreme cases, if all contents in replica_array become unreadable, then randomly select one
  if (OB_UNLIKELY(false == has_found)) {
    int64_t select_idx = rand() % replica_array.count();
    if (OB_FAIL(phy_part_loc_info.add_priority_replica_idx(select_idx))) {
      LOG_WARN("fail to select replica", K(select_idx), K(ret));
    }
    if (REACH_TIME_INTERVAL(10 * 1000 * 1000)) {// Print once every 10 seconds}
      LOG_WARN("all replica is not usable currently", K(replica_array), K(route_policy_ctx), K(select_idx));
    }
  }
  return ret;
}

int ObRoutePolicy::calc_intersect_repllica(const common::ObIArray<ObCandiTableLoc*> &phy_tbl_loc_info_list,
                                           ObList<ObRoutePolicy::CandidateReplica, ObArenaAllocator> &intersect_server_list)
{
  int ret = OB_SUCCESS;
  ObRoutePolicy::CandidateReplica tmp_replica;
  bool can_select_one_server = true;
  for (int64_t i = 0; OB_SUCC(ret) && can_select_one_server && i < phy_tbl_loc_info_list.count(); ++i) {
    const ObCandiTableLoc *phy_tbl_loc_info = phy_tbl_loc_info_list.at(i);
    if (OB_ISNULL(phy_tbl_loc_info)) {
      ret = OB_ERR_UNEXPECTED;
      LOG_ERROR("phy_tbl_loc_info is NULL", K(ret), K(i), K(phy_tbl_loc_info_list.count()));
    } else {
      const ObCandiTabletLocIArray &phy_part_loc_info_list = phy_tbl_loc_info->get_phy_part_loc_info_list();
      for (int64_t j = 0; OB_SUCC(ret) && can_select_one_server && j < phy_part_loc_info_list.count(); ++j) {
        const ObCandiTabletLoc &phy_part_loc_info = phy_part_loc_info_list.at(j);
        const ObIArray<int64_t> &priority_replica_idxs = phy_part_loc_info.get_priority_replica_idxs();
        if (0 == i && 0 == j) { // first partition
          if (phy_part_loc_info.has_selected_replica()) { // replica has already been selected
            tmp_replica.reset();
            if (OB_FAIL(phy_part_loc_info.get_selected_replica(tmp_replica))) {
              LOG_WARN("fail to get selected replica", K(ret), K(phy_part_loc_info));
            } else if (OB_FAIL(intersect_server_list.push_back(tmp_replica))) {
              LOG_WARN("fail to push back candidate server", K(ret), K(tmp_replica));
            }
          } else { // No replica has been selected yet
            for (int64_t k = 0; OB_SUCC(ret) && k < priority_replica_idxs.count(); ++k) {
              tmp_replica.reset();
              int64_t replica_idx = priority_replica_idxs.at(k);
              if (OB_FAIL(phy_part_loc_info.get_priority_replica(replica_idx, tmp_replica))) {
                LOG_WARN("fail to get priority replica", K(k), K(priority_replica_idxs), K(ret));
              } else if (OB_FAIL(intersect_server_list.push_back(tmp_replica))) {
                LOG_WARN("fail to push back server ", K(k), K(priority_replica_idxs), K(ret));
              }
            }
          }
        } else {// not the first partition
          ObList<ObRoutePolicy::CandidateReplica, ObArenaAllocator>::iterator intersect_server_list_iter = intersect_server_list.begin();
          for (; OB_SUCC(ret) && intersect_server_list_iter != intersect_server_list.end(); intersect_server_list_iter++) {
            const ObAddr &candidate_server = intersect_server_list_iter->get_server();
            bool has_replica = false;
            if (phy_part_loc_info.has_selected_replica()) { // replica has already been selected
              tmp_replica.reset();
              if (OB_FAIL(phy_part_loc_info.get_selected_replica(tmp_replica))) {
                LOG_WARN("fail to get selected replica", K(ret), K(phy_part_loc_info));
              } else if (tmp_replica.get_server() == candidate_server) {
                has_replica = true;
              }
            } else { // Copy not yet selected
              for (int64_t k = 0; OB_SUCC(ret) && !has_replica && k < priority_replica_idxs.count(); ++k) {
                tmp_replica.reset();
                int64_t replica_idx = priority_replica_idxs.at(k);
                if (OB_FAIL(phy_part_loc_info.get_priority_replica(replica_idx, tmp_replica))) {
                  LOG_WARN("fail to get priority replica", K(k), K(priority_replica_idxs), K(ret));
                } else if (candidate_server == tmp_replica.get_server()) {
                  has_replica = true;
                }
              }
            }
            if (OB_SUCC(ret) && !has_replica) {
              if (OB_FAIL(intersect_server_list.erase(intersect_server_list_iter))) {
                LOG_WARN("fail to erase from list", K(ret), K(candidate_server));
              }
            }
          }
          if (OB_SUCC(ret) && intersect_server_list.empty()) {
            can_select_one_server = false;
          }
        }
      }
    }
  }
  return ret;
}

int ObRoutePolicy::select_intersect_replica(ObRoutePolicyCtx &route_policy_ctx,
                                            common::ObIArray<ObCandiTableLoc*> &phy_tbl_loc_info_list,
                                            ObList<ObRoutePolicy::CandidateReplica, ObArenaAllocator> &intersect_server_list,
                                            bool &is_proxy_hit)
{
  UNUSED(route_policy_ctx);
  int ret = OB_SUCCESS;
  if (OB_FAIL(calc_intersect_repllica(phy_tbl_loc_info_list, intersect_server_list))) {
    LOG_WARN("fail to calc intersect replica", K(phy_tbl_loc_info_list), K(ret));
  } else if (intersect_server_list.empty()) {//No intersection case, each partition selects replica separately
    is_proxy_hit = true;
    for (int64_t i = 0; OB_SUCC(ret) && i < phy_tbl_loc_info_list.count(); ++i) {
      ObCandiTableLoc *phy_tbl_loc_info = phy_tbl_loc_info_list.at(i);
      if (OB_ISNULL(phy_tbl_loc_info)) {
        ret = OB_ERR_UNEXPECTED;
        LOG_ERROR("phy_tbl_loc_info is NULL", K(ret), K(i), K(phy_tbl_loc_info_list.count()));
      } else {
        ObCandiTabletLocIArray &phy_part_loc_info_list = phy_tbl_loc_info->get_phy_part_loc_info_list_for_update();
        for (int64_t j = 0; OB_SUCC(ret) && j < phy_part_loc_info_list.count(); ++j) {
          ObCandiTabletLoc &phy_part_loc_info = phy_part_loc_info_list.at(j);
          if (phy_part_loc_info.has_selected_replica()) {
            // do nothing
          } else if (OB_FAIL(phy_part_loc_info.set_selected_replica_idx_with_priority())) {
            LOG_WARN("fail to set selected replica idx", K(ret));
          }
        }
      }
    }
  } else {
    CandidateReplica selected_replica;
    ObList<ObRoutePolicy::CandidateReplica, ObArenaAllocator>::iterator replica_iter = intersect_server_list.begin();
    ObSEArray<ObAddr, 16> same_priority_servers;
    selected_replica.attr_.pos_type_ = POSITION_TYPE_MAX;
    bool is_first = true;
    for (; OB_SUCC(ret) && replica_iter != intersect_server_list.end(); replica_iter++) {
      if (is_first) {
        selected_replica = *replica_iter;
        is_first = false;
        if (OB_FAIL(same_priority_servers.push_back(replica_iter->get_server()))) {
          LOG_WARN("fail to replica iterator", K(replica_iter), K(ret));
        }
      } else if (replica_iter->attr_.pos_type_ == selected_replica.attr_.pos_type_) {
        // Aggregate same priority servers for random selection later
        if (OB_FAIL(same_priority_servers.push_back(replica_iter->get_server()))) {
          LOG_WARN("fail to replica iterator", K(replica_iter), K(ret));
        }
      } else if (replica_iter->attr_.pos_type_ < selected_replica.attr_.pos_type_) {
        selected_replica = *replica_iter;
        same_priority_servers.reset();
        if (OB_FAIL(same_priority_servers.push_back(replica_iter->get_server()))) {
          LOG_WARN("fail to replica iterator", K(replica_iter), K(ret));
        }
      } else {
        /* replica_iter->attr_.pos_type_ > selected_replica.attr_.pos_type_ do nothing */
      }
    }

    if (OB_SUCC(ret)) {//select server for all partitions of the query
      int64_t selected_idx = rand() % same_priority_servers.count();
      const ObAddr &selected_server = same_priority_servers.at(selected_idx);
      if (OB_FAIL(ObLogPlan::select_one_server(selected_server, phy_tbl_loc_info_list))) {
        LOG_WARN("fail to select one server", K(selected_idx), K(selected_server), K(ret));
      } else {
        is_proxy_hit = (local_addr_ == selected_server);
      }
    }
  }
  return ret;
}

int ObRoutePolicy::init()
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(is_inited_)) {
    ret = OB_INIT_TWICE;
    LOG_WARN("init twice", K(ret));
  } else {
    is_inited_ = true;
  }
  return ret;
}

bool ObRoutePolicy::is_same_idc(const share::ObServerLocality &locality1, const share::ObServerLocality &locality2)
{
  bool ret_bool = false;
  if (locality1.get_region().is_empty() || locality2.get_region().is_empty()) {
    // If the REGION is not set for the cluster, it is impossible to determine if they are in the same REGION
    ret_bool = false;
    LOG_WARN_RET(OB_ERR_UNEXPECTED, "cluster region is not set", K(locality1), K(locality2));
  } else if (locality1.get_idc().is_empty() || locality2.get_idc().is_empty()) {
    // If the IDC is not set for the zone, it is impossible to determine if they are in the same IDC
    ret_bool = false;
    LOG_TRACE("zone idc is not set", K(locality1), K(locality2));
  } else if (locality1.get_region() == locality2.get_region()) {
    // First determine if the region is the same, to avoid having the same name idc in different regions
    if (locality1.get_idc() == locality2.get_idc()) {
      ret_bool = true;
    }
  }
  return ret_bool;
}

bool ObRoutePolicy::is_same_region(const share::ObServerLocality &locality1, const share::ObServerLocality &locality2)
{
  bool ret_bool = false;
  if (locality1.get_region().is_empty() || locality2.get_region().is_empty()) {
    // If the REGION is not set for the cluster, it is impossible to determine if they are in the same REGION
    ret_bool = false;
    LOG_WARN_RET(OB_ERR_UNEXPECTED, "cluster region is not set", K(locality1), K(locality2));
  } else if (locality1.get_region() == locality2.get_region()) {
    ret_bool = true;
  }
  return ret_bool;
}

}//sql
}//oceanbase
