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

#ifndef OCEANBASE_SQL_OPTIMIZER_OB_PHY_TABLE_LOCATION_INFO_
#define OCEANBASE_SQL_OPTIMIZER_OB_PHY_TABLE_LOCATION_INFO_

#include "sql/ob_phy_table_location.h"
#include "sql/ob_sql_define.h"
#include "share/location_cache/ob_location_struct.h"
namespace oceanbase
{
namespace sql
{
class DASRelatedTabletMap;
class ObOptTabletLoc
{
  OB_UNIS_VERSION(1);
public:
  ObOptTabletLoc();
  virtual ~ObOptTabletLoc();

  void reset();
  int assign(const ObOptTabletLoc &partition_location);
  int assign_local_replica(const ObObjectID &partition_id,
                           const ObObjectID &first_level_part_id,
                           const common::ObTabletID &tablet_id,
                           const share::ObLSReplicaLocation &replica);

  bool is_valid() const;
  bool operator==(const ObOptTabletLoc &other) const;

  // return OB_LS_LOCATION_LEADER_NOT_EXIST for leader not exist.
  int get_strong_leader(share::ObLSReplicaLocation &replica_location, int64_t &replica_idx) const;
  int get_strong_leader(share::ObLSReplicaLocation &replica_location) const;

  void set_tablet_info(common::ObTabletID tablet_id,
                       common::ObPartID part_id,
                       common::ObPartID first_level_part_id)
  {
    tablet_id_ = tablet_id;
    partition_id_ = part_id;
    first_level_part_id_ = first_level_part_id;
  }
  inline int64_t get_partition_id() const { return partition_id_; }

  inline int64_t get_first_level_part_id() const { return first_level_part_id_; }

  inline common::ObTabletID get_tablet_id() const { return tablet_id_; }

  inline const share::ObLSReplicaLocation &get_local_replica() const { return local_replica_; }
  inline share::ObLSReplicaLocation &get_local_replica() { return local_replica_; }

  TO_STRING_KV(K_(partition_id),
               K_(tablet_id),
               K_(local_replica));

private:
  int64_t partition_id_;
  // first level part id, only valid for subpartitioned table
  int64_t first_level_part_id_;
  common::ObTabletID tablet_id_;
  share::ObLSReplicaLocation local_replica_;
};

class ObCandiTabletLoc
{
public:
  ObCandiTabletLoc();
  ~ObCandiTabletLoc();

  int assign(const ObCandiTabletLoc &other);

  int get_selected_replica(share::ObLSReplicaLocation &replica_loc) const;
  int set_local_tablet_loc(const ObObjectID &partition_id,
                           const ObObjectID &first_level_part_id,
                           const common::ObTabletID &tablet_id,
                           const share::ObLSReplicaLocation &replica);
  const ObOptTabletLoc &get_partition_location() const { return opt_tablet_loc_; }
  ObOptTabletLoc &get_partition_location() { return opt_tablet_loc_; }
  bool is_local_server(const common::ObAddr &server) const;
  TO_STRING_KV(K_(opt_tablet_loc));

private:
  ObOptTabletLoc opt_tablet_loc_;
private:
  DISALLOW_COPY_AND_ASSIGN(ObCandiTabletLoc);
};

typedef common::ObIArray<ObCandiTabletLoc> ObCandiTabletLocIArray;
typedef common::ObSEArray<ObCandiTabletLoc, 2, common::ModulePageAllocator, true> ObCandiTabletLocSEArray;

class ObCandiTableLoc
{
public:
  ObCandiTableLoc();
  virtual ~ObCandiTableLoc();
public:
  int assign(const ObCandiTableLoc &other);

  void set_table_location_key(uint64_t table_location_key, uint64_t ref_table_id);
  int replace_local_index_loc(DASRelatedTabletMap &map, common::ObTableID ref_table_id);
  inline uint64_t get_table_location_key() const { return table_location_key_; }
  inline uint64_t get_ref_table_id() const { return ref_table_id_; }

  inline const ObCandiTabletLocIArray &get_phy_part_loc_info_list() const
  {
    return candi_tablet_locs_;
  }
  inline ObCandiTabletLocIArray &get_phy_part_loc_info_list_for_update()
  {
    return candi_tablet_locs_;
  }
  int64_t get_partition_cnt() const { return candi_tablet_locs_.count(); }

  int all_select_leader(bool &is_on_same_server,
                        common::ObAddr &same_server);
  int get_all_servers(common::ObIArray<common::ObAddr> &servers) const;
  TO_STRING_KV(K_(table_location_key), K_(ref_table_id), K_(candi_tablet_locs));

private:
  /* Used for addressing location by table ID (possibly generated alias id) */
  uint64_t table_location_key_;
  /* Used to get the actual physical table ID */
  uint64_t ref_table_id_;
  /* locations */
  ObCandiTabletLocSEArray candi_tablet_locs_;
private:
  /* functions */
  /* variables */
  DISALLOW_COPY_AND_ASSIGN(ObCandiTableLoc);
};
}
}
#endif /* OCEANBASE_SQL_OPTIMIZER_OB_PHY_TABLE_LOCATION_INFO_ */
