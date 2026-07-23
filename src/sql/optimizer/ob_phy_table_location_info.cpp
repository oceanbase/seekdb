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

#include "ob_phy_table_location_info.h"
#include "sql/das/ob_das_location_router.h"
using namespace oceanbase::common;
using namespace oceanbase::share;
using namespace oceanbase::transaction;
namespace oceanbase
{
namespace sql
{

ObOptTabletLoc::ObOptTabletLoc()
    : partition_id_(OB_INVALID_INDEX),
      first_level_part_id_(OB_INVALID_INDEX),
      tablet_id_(),
      local_replica_()
{
}

ObOptTabletLoc::~ObOptTabletLoc()
{
}

void ObOptTabletLoc::reset()
{
  partition_id_ = OB_INVALID_INDEX;
  first_level_part_id_ = OB_INVALID_INDEX;
  tablet_id_.reset();
  local_replica_.reset();
}

int ObOptTabletLoc::assign(const ObOptTabletLoc &other)
{
  int ret = OB_SUCCESS;
  tablet_id_ = other.tablet_id_;
  partition_id_ = other.partition_id_;
  first_level_part_id_ = other.first_level_part_id_;
  local_replica_ = other.local_replica_;
  return ret;
}

int ObOptTabletLoc::assign_local_replica(const ObObjectID &partition_id,
                                         const ObObjectID &first_level_part_id,
                                         const common::ObTabletID &tablet_id,
                                         const ObLSReplicaLocation &replica)
{
  int ret = OB_SUCCESS;
  reset();
  partition_id_ = partition_id;
  first_level_part_id_ = first_level_part_id;
  tablet_id_ = tablet_id;
  local_replica_ = replica;
  return ret;
}

bool ObOptTabletLoc::is_valid() const
{
  return OB_INVALID_INDEX != partition_id_
      && tablet_id_.is_valid()
      && local_replica_.is_valid();
}

int ObOptTabletLoc::get_strong_leader(ObLSReplicaLocation &replica_location, int64_t &replica_idx) const
{
  int ret = OB_SUCCESS;
  replica_location = local_replica_;
  replica_idx = 0;
  return ret;
}

int ObOptTabletLoc::get_strong_leader(ObLSReplicaLocation &replica_location) const
{
  int64_t replica_idx = OB_INVALID_INDEX;
  return get_strong_leader(replica_location, replica_idx);
}

ObCandiTabletLoc::ObCandiTabletLoc()
  : opt_tablet_loc_()
{
}

ObCandiTabletLoc::~ObCandiTabletLoc()
{
}


int ObCandiTabletLoc::assign(const ObCandiTabletLoc &other)
{
  int ret = OB_SUCCESS;
  if (OB_FAIL(opt_tablet_loc_.assign(other.opt_tablet_loc_))) {
    LOG_WARN("fail to assign other opt_tablet_loc_", K(ret), K(other.opt_tablet_loc_));
  }
  return ret;
}

bool ObCandiTabletLoc::is_local_server(const ObAddr &server) const
{
  return opt_tablet_loc_.get_local_replica().get_server() == server;
}

int ObCandiTabletLoc::get_selected_replica(share::ObLSReplicaLocation &replica_loc) const
{
  replica_loc = opt_tablet_loc_.get_local_replica();
  return OB_SUCCESS;
}

int ObCandiTabletLoc::set_local_tablet_loc(const ObObjectID &partition_id,
                                           const ObObjectID &first_level_part_id,
                                           const common::ObTabletID &tablet_id,
                                           const ObLSReplicaLocation &replica)
{
  return opt_tablet_loc_.assign_local_replica(partition_id,
                                              first_level_part_id,
                                              tablet_id,
                                              replica);
}

ObCandiTableLoc::ObCandiTableLoc()
  : table_location_key_(OB_INVALID_ID),
    ref_table_id_(OB_INVALID_ID),
    candi_tablet_locs_()
{
}

ObCandiTableLoc::~ObCandiTableLoc()
{
}


int ObCandiTableLoc::assign(const ObCandiTableLoc &other)
{
  int ret = OB_SUCCESS;
  table_location_key_ = other.table_location_key_;
  ref_table_id_ = other.ref_table_id_;
  if (OB_FAIL(candi_tablet_locs_.assign(other.candi_tablet_locs_))) {
    LOG_WARN("Failed to assign phy_part_loc_info_list", K(ret));
  }
  return ret;
}
int ObCandiTableLoc::all_select_leader(bool &is_on_same_server,
                                              ObAddr &same_server)
{
  int ret = OB_SUCCESS;
  is_on_same_server = true;
  ObAddr first_server;
  for (int64_t i = 0; OB_SUCC(ret) && i < candi_tablet_locs_.count(); ++i) {
    const ObAddr &replica_addr =
        candi_tablet_locs_.at(i).get_partition_location().get_local_replica().get_server();
    if (0 == i) {
      first_server = replica_addr;
    } else if (first_server != replica_addr) {
      is_on_same_server = false;
    }
  }
  if (OB_SUCC(ret) && is_on_same_server) {
    same_server = first_server;
  }
  return ret;
}
int ObCandiTableLoc::get_all_servers(common::ObIArray<common::ObAddr> &servers) const
{
  int ret = OB_SUCCESS;
  const ObCandiTabletLocIArray &phy_part_loc_info_list = get_phy_part_loc_info_list();
  FOREACH_CNT_X(it, phy_part_loc_info_list, OB_SUCC(ret)) {
    share::ObLSReplicaLocation replica_location;
    if (OB_FAIL((*it).get_selected_replica(replica_location))) {
      LOG_WARN("fail to get selected replica", K(*it));
    } else if (!replica_location.is_valid()) {
      ret = OB_ERR_UNEXPECTED;
      LOG_ERROR("replica location is invalid", K(ret), K(replica_location));
    } else if (OB_FAIL(add_var_to_array_no_dup(servers, replica_location.get_server()))) {
      LOG_WARN("failed to push back server", K(ret));
    }
  }
  return ret;
}
void ObCandiTableLoc::set_table_location_key(uint64_t table_location_key, uint64_t ref_table_id)
{
  table_location_key_ = table_location_key;
  ref_table_id_ = ref_table_id;
}

int ObCandiTableLoc::replace_local_index_loc(DASRelatedTabletMap &map, ObTableID ref_table_id)
{
  int ret = OB_SUCCESS;
  ref_table_id_ = ref_table_id;
  for (int64_t i = 0; i < candi_tablet_locs_.count(); ++i) {
    ObOptTabletLoc &tablet_loc = candi_tablet_locs_.at(i).get_partition_location();
    const DASRelatedTabletMap::Value *rv = nullptr;
    if (OB_ISNULL(rv = map.get_related_tablet_id(tablet_loc.get_tablet_id(), ref_table_id))) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("related tablet info is invalid", K(ret),
               K(tablet_loc.get_tablet_id()), K(ref_table_id), K(map));
    } else {
      tablet_loc.set_tablet_info(rv->tablet_id_, rv->part_id_, rv->first_level_part_id_);
    }
  }
  return ret;
}
}/* ns sql*/
}/* ns oceanbase */
