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
#include "sql/das/ob_das_tablet_mapper.h"
using namespace oceanbase::common;
using namespace oceanbase::share;
namespace oceanbase
{
namespace sql
{

ObOptTabletLoc::ObOptTabletLoc()
    : partition_id_(OB_INVALID_INDEX),
      first_level_part_id_(OB_INVALID_INDEX)
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
  ls_id_.reset();
  server_.reset();
}

int ObOptTabletLoc::assign(const ObOptTabletLoc &other)
{
  int ret = OB_SUCCESS;
  tablet_id_ = other.tablet_id_;
  partition_id_ = other.partition_id_;
  first_level_part_id_ = other.first_level_part_id_;
  ls_id_ = other.ls_id_;
  server_ = other.server_;
  return ret;
}

int ObOptTabletLoc::assign_local_location(const ObObjectID &partition_id,
                                          const ObObjectID &first_level_part_id,
                                          const common::ObTabletID &tablet_id,
                                          const ObLSLocation &ls_location,
                                          const ObAddr &local_server)
{
  int ret = OB_SUCCESS;
  reset();
  partition_id_ = partition_id;
  first_level_part_id_ = first_level_part_id;
  tablet_id_ = tablet_id;
  ls_id_ = ls_location.get_ls_id();
  if (!ls_location.is_valid() || ls_location.get_server() != local_server) {
    ret = OB_NO_READABLE_REPLICA;
    LOG_WARN("local LS location is not readable", K(ret), K(local_server), K(ls_location));
  } else {
    server_ = ls_location.get_server();
  }
  return ret;
}

bool ObOptTabletLoc::is_valid() const
{
  return OB_INVALID_INDEX != partition_id_
      && tablet_id_.is_valid()
      && ls_id_.is_valid()
      && server_.is_valid();
}

bool ObOptTabletLoc::operator==(const ObOptTabletLoc &other) const
{
  return partition_id_ == other.partition_id_
      && first_level_part_id_ == other.first_level_part_id_
      && tablet_id_ == other.tablet_id_
      && ls_id_ == other.ls_id_
      && server_ == other.server_;
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
  }
  return ret;
}

int ObCandiTabletLoc::set_local_location(const ObObjectID &partition_id,
                                         const ObObjectID &first_level_part_id,
                                         const common::ObTabletID &tablet_id,
                                         const ObLSLocation &ls_location,
                                         const ObAddr &local_server)
{
  int ret = OB_SUCCESS;
  if (OB_FAIL(opt_tablet_loc_.assign_local_location(partition_id,
                                                    first_level_part_id,
                                                    tablet_id,
                                                    ls_location,
                                                    local_server))) {
  }
  return ret;
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
  }
  return ret;
}
int ObCandiTableLoc::get_all_servers(common::ObIArray<common::ObAddr> &servers) const
{
  int ret = OB_SUCCESS;
  const ObCandiTabletLocIArray &phy_part_loc_info_list = get_phy_part_loc_info_list();
  FOREACH_CNT_X(it, phy_part_loc_info_list, OB_SUCC(ret)) {
    const ObAddr &server = (*it).get_partition_location().get_server();
    if (!server.is_valid()) {
      ret = OB_ERR_UNEXPECTED;
      LOG_ERROR("local server is invalid", K(ret), K(server));
    } else if (OB_FAIL(add_var_to_array_no_dup(servers, server))) {
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
