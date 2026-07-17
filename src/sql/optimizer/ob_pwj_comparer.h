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

#ifndef OCEANBASE_SQL_OPTIMIZER_OB_PWJ_COMPARER_H
#define OCEANBASE_SQL_OPTIMIZER_OB_PWJ_COMPARER_H 1
#include "lib/container/ob_array.h"
#include "share/partition_table/ob_partition_location.h"
#include "sql/optimizer/ob_table_partition_info.h"
#include "sql/resolver/expr/ob_raw_expr.h"
#include "sql/optimizer/ob_sharding_info.h"

namespace oceanbase
{
namespace sql
{
class ObRawExpr;
class ObOptimizerContext;
class ObDMLStmt;
class ObLogPlan;

//Do not allocate a PwjTable on the heap. If it is necessary to allocate a PwjTable on the heap, manually free it after its lifecycle ends.
struct PwjTable {
  PwjTable()
    : phy_table_loc_info_(NULL),
      server_list_(),
      part_level_(share::schema::PARTITION_LEVEL_ZERO),
      part_type_(share::schema::PARTITION_FUNC_TYPE_MAX),
      subpart_type_(share::schema::PARTITION_FUNC_TYPE_MAX),
      is_partition_single_(false),
      is_subpartition_single_(false),
      partition_array_(NULL),
      part_number_(-1)
  {}

  virtual ~PwjTable() {}

  int assign(const PwjTable &other);

  int init(const ObShardingInfo &info);
  int init(const share::schema::ObTableSchema &table_schema,
           const ObCandiTableLoc &phy_tbl_info);
  int init(const ObIArray<ObAddr> &server_list);

  TO_STRING_KV(K_(part_level), K_(part_type), K_(subpart_type),
               K_(part_number),
               K_(is_partition_single), K_(is_subpartition_single),
               K_(all_partition_indexes), K_(all_subpartition_indexes),
               K_(server_list));

  const ObCandiTableLoc *phy_table_loc_info_;
  ObSEArray<common::ObAddr, 8> server_list_;
  // Partition level
  share::schema::ObPartitionLevel part_level_;
  // First-level partition type
  share::schema::ObPartitionFuncType part_type_;
  // Secondary partition type
  share::schema::ObPartitionFuncType subpart_type_;
  // Whether only one primary partition is involved in the phy_table_location_info_ of the secondary partition table
  bool is_partition_single_;
  // In phy_table_location_info_ of the secondary partition table, does each primary partition involve only one secondary partition
  bool is_subpartition_single_;
  // first-level partition array
  share::schema::ObPartition **partition_array_;
  // First-level partition count
  int64_t part_number_;
  // All partition_id (physical partition id) in phy_table_location_info_
  // ObPwjComparer generates the _id mapping relationship according to the order of partition_id in this array on the left table
  common::ObSEArray<uint64_t, 8> ordered_tablet_ids_;
  // part_id(logical partition id level 1) in part_array offset
  common::ObSEArray<int64_t, 8> all_partition_indexes_;
  // subpart_id(subpartition id) in subpart_array offset
  common::ObSEArray<int64_t, 8> all_subpartition_indexes_;
};

typedef common::ObSEArray<uint64_t, 8> TabletIdArray;

struct GroupPWJTabletIdInfo {
  OB_UNIS_VERSION(1);
public:
  TO_STRING_KV(K_(group_id), K_(tablet_id_array));
  /* 
    for union all non strict partition wise join, there may be several partition wise join groups,
    for example:
                          union all
                    |                     |
                  join                  join
                |       |             |       |
          t1(p0-p15)  t2(p0-p15)    t3(p0-p8)  t4(p0-p8)

        t1 and t2 are in group 0
        t3 and t4 are in group 1
  */
  int64_t group_id_{0};
  TabletIdArray tablet_id_array_;
};
// TODO yibo use a pointer to PartitionIdArray as value, otherwise each get will copy the array once
typedef common::hash::ObHashMap<uint64_t, TabletIdArray, common::hash::NoPthreadDefendMode> PWJTabletIdMap;
typedef common::hash::ObHashMap<uint64_t, GroupPWJTabletIdInfo, common::hash::NoPthreadDefendMode> GroupPWJTabletIdMap;
typedef common::hash::ObHashMap<uint64_t, uint64_t, common::hash::NoPthreadDefendMode,
                                common::hash::hash_func<uint64_t>,
                                common::hash::equal_to<uint64_t>,
                                common::hash::SimpleAllocer<typename common::hash::HashMapTypes<uint64_t, uint64_t>::AllocType>,
                                common::hash::NormalPointer,
                                oceanbase::common::ObMalloc,
                                2> TabletIdIdMap;
typedef common::hash::ObHashMap<uint64_t, const ObCandiTabletLoc *,
                                common::hash::NoPthreadDefendMode> TabletIdLocationMap;

//Do not allocate a ObPwjComparer on the heap. If it is necessary to allocate a ObPwjComparer on the heap, manually free it after its lifecycle ends.
class ObPwjComparer
{
public:
  ObPwjComparer(bool is_strict)
    : is_strict_(is_strict), pwj_tables_() {};
  virtual ~ObPwjComparer() {};
  virtual void reset();

  inline bool get_is_strict() { return is_strict_; }

  inline common::ObIArray<PwjTable> &get_pwj_tables() { return pwj_tables_; }
  
  /**
   * Add a PwjTable to ObPwjComparer, it will compare subsequent added PwjTables with the first added PwjTable as the baseline.
   * Strict comparison will generate a mapping of tablet_id.
   */
  virtual int add_table(PwjTable &table, bool &is_match_pwj);

  /**
   * Extract the following partition-related information from phy_table_location_info
   * @param all_partition_ids:
   *    all partition_id (physical partition id) in phy_table_location_info
   * @param all_partition_indexes:
   *    the offset of part_id (first-level logical partition id) of each partition_id (physical partition id)
   *    in part_array in phy_table_location_info
   * @param all_subpartition_indexes:
   *    the offset of subpart_id (second-level logical partition id) of each partition_id (physical partition id)
   *    in subpart_array in phy_table_location_info
   * @param is_partition_single:
   *    whether only one first-level partition is involved in phy_table_location_info_ of a secondary partition table
   * @param is_subpartition_single:
   *    whether each first-level partition involves only one second-level partition in phy_table_location_info_ of a secondary partition table
   */
  static int extract_all_partition_indexes(const ObCandiTableLoc &phy_table_location_info,
                                           const share::schema::ObTableSchema &table_schema,
                                           ObIArray<uint64_t> &all_tablet_ids,
                                           ObIArray<int64_t> &all_partition_indexes,
                                           ObIArray<int64_t> &all_subpartition_indexes,
                                           bool &is_partition_single,
                                           bool &is_subpartition_single);

  /**
   * Check if the definitions of l_partition and r_partition are equal
   */
  static int is_partition_equal(const share::schema::ObPartition *l_partition,
                                const share::schema::ObPartition *r_partition,
                                const bool is_range_partition,
                                bool &is_equal);

  /**
   * Check if the definitions of l_subpartition and r_subpartition are equal
   */
  static int is_subpartition_equal(const share::schema::ObSubPartition *l_subpartition,
                                   const share::schema::ObSubPartition *r_subpartition,
                                   const bool is_range_partition,
                                   bool &is_equal);

  static int is_row_equal(const ObRowkey &first_row,
                          const ObRowkey &second_row,
                          bool &is_equal);

  static int is_list_partition_equal(const share::schema::ObBasePartition *first_partition,
                                     const share::schema::ObBasePartition *second_partition,
                                     bool &is_equal);

  static int is_row_equal(const common::ObNewRow &first_row,
                          const common::ObNewRow &second_row,
                          bool &is_equal);

  static int is_obj_equal(const common::ObObj &first_obj,
                          const common::ObObj &second_obj,
                          bool &is_equal);

  TO_STRING_KV(K_(is_strict), K_(pwj_tables));

protected:
  // Whether to check partition wise join in strict mode
  // Strict mode requires that the partition logic and physical structure of the two base tables are equal
  // Non-strict mode requires that the data distribution nodes of the two base tables are the same
  bool is_strict_;
  // Save a set of pwj constraint related base table information
  common::ObSEArray<PwjTable, 4> pwj_tables_;
  static const int64_t MIN_ID_LOCATION_BUCKET_NUMBER;
  static const int64_t DEFAULT_ID_ID_BUCKET_NUMBER;
  DISALLOW_COPY_AND_ASSIGN(ObPwjComparer);
};

class ObStrictPwjComparer : public ObPwjComparer
{
public:
  ObStrictPwjComparer();
  virtual ~ObStrictPwjComparer();
  virtual void reset() override;
  inline common::ObIArray<TabletIdArray> &get_tablet_id_group() { return tablet_id_group_;}

  /**
   * Add a PwjTable to ObPwjComparer, it will strictly compare with subsequent added PwjTables based on the first added PwjTable
   * and generate a mapping of tablet_id.
   */
  virtual int add_table(PwjTable &table, bool &is_match_pwj) override;

  /**
   * Check if the partitions of l_table and r_table are logically equal, and calculate the mapping of logically equal partition_id (physical partition id)
   */
  int check_logical_equal_and_calc_match_map(const PwjTable &l_table,
                                             const PwjTable &r_table,
                                             bool &is_match);

  /**
   * Check if the first-level partitions of l_table and r_table are logically equal
   */
  int is_first_partition_logically_equal(const PwjTable &l_table,
                                         const PwjTable &r_table,
                                         bool &is_equal);

  /**
   * Retrieve the used (sub)part_index in ascending order from all (sub)part_index
   * For example all_partition_indexes = [0,0,3,3,1] can get used_partition_indexes = [0,1,3]
   */
  int get_used_partition_indexes(const int64_t part_count,
                                 const ObIArray<int64_t> &all_partition_indexes,
                                 ObIArray<int64_t> &used_partition_indexes);

  /**
   * Check if the secondary partitions of l_table and r_table are logically equal
   */
  int is_sub_partition_logically_equal(const PwjTable &l_table,
                                       const PwjTable &r_table,
                                       bool &is_equal);

  /**
   * Get all used subpart_index under the specified part_index and sort them in ascending order
   */
  int get_subpartition_indexes_by_part_index(const PwjTable &table,
                                             const int64_t part_index,
                                             ObIArray<int64_t> &used_subpart_indexes);

  /**
   * Check if the first-level hash/key partition is logically equal, requirements:
   * 1. The number of partitions on both left and right tables are consistent
   * 2. Every part_index on the left table has an equal part_index on the right table
   */
  int check_hash_partition_equal(const PwjTable &l_table,
                                 const PwjTable &r_table,
                                 const ObIArray<int64_t> &l_indexes,
                                 const ObIArray<int64_t> &r_indexes,
                                 ObIArray<std::pair<uint64_t,uint64_t> > &part_tablet_id_map,
                                 ObIArray<std::pair<int64_t,int64_t> > &part_index_map,
                                 bool &is_equal);

  /**
   * Check if the secondary hash/key partition is logically equal, requirements:
   * 1. The number of partitions on the left and right tables are consistent
   * 2. Every subpart_index on the left table has an equal subpart_index on the right table
   */
  int check_hash_subpartition_equal(share::schema::ObSubPartition **l_subpartition_array,
                                    share::schema::ObSubPartition **r_subpartition_array,
                                    const ObIArray<int64_t> &l_indexes,
                                    const ObIArray<int64_t> &r_indexes,
                                    ObIArray<std::pair<uint64_t,uint64_t> > &subpart_tablet_id_map,
                                    bool &is_equal);

  bool is_same_part_type(const ObPartitionFuncType part_type1,
                         const ObPartitionFuncType part_type2);

  /**
   * Check if the first-level range partitions are logically equal, requirements:
   * 1. The upper bounds of corresponding partitions in the left and right tables are the same
   */
  int check_range_partition_equal(share::schema::ObPartition **left_partition_array,
                                  share::schema::ObPartition **right_partition_array,
                                  const ObIArray<int64_t> &left_indexes,
                                  const ObIArray<int64_t> &right_indexes,
                                  ObIArray<std::pair<uint64_t,uint64_t> > &part_tablet_id_map,
                                  ObIArray<std::pair<int64_t,int64_t> > &part_index_map,
                                  bool &is_equal);

  /**
   * Check if the secondary range partitions are logically equal, requirements:
   * 1. The upper bounds of corresponding partitions in the left and right tables are the same
   */
  int check_range_subpartition_equal(share::schema::ObSubPartition **left_subpartition_array,
                                     share::schema::ObSubPartition **right_subpartition_array,
                                     const ObIArray<int64_t> &left_indexes,
                                     const ObIArray<int64_t> &right_indexes,
                                     ObIArray<std::pair<uint64_t,uint64_t> > &subpart_tablet_id_map,
                                     bool &is_equal);

  /**
   * Check if the first-level list partitions are logically equal, requirements:
   * 1. Each partition in the left table can find a partition with the same boundaries in the right table
   */
  int check_list_partition_equal(share::schema::ObPartition **left_partition_array,
                                 share::schema::ObPartition **right_partition_array,
                                 const ObIArray<int64_t> &left_indexes,
                                 const ObIArray<int64_t> &right_indexes,
                                 ObIArray<std::pair<uint64_t,uint64_t> > &part_tablet_id_map,
                                 ObIArray<std::pair<int64_t,int64_t> > &part_index_map,
                                 bool &is_equal);

  /**
   * Check if the secondary list partitions are logically equal, requirements:
   * 1. Each partition in the left table can find a partition with the same boundaries in the right table
   */
  int check_list_subpartition_equal(share::schema::ObSubPartition **left_subpartition_array,
                                    share::schema::ObSubPartition **right_subpartition_array,
                                    const ObIArray<int64_t> &left_indexes,
                                    const ObIArray<int64_t> &right_indexes,
                                    ObIArray<std::pair<uint64_t,uint64_t> > &subpart_tablet_id_map,
                                    bool &is_equal);

  /**
   * According to the mapping relationship of partition_id (physical partition id) in phy_part_map_, check if the corresponding partitions in the two tables are in the same physical location
   */
  int is_physically_equal_partitioned(const PwjTable &l_table,
                                      const PwjTable &r_table,
                                      bool &is_physical_equal);

  /**
   * Get the part_id (primary logical partition id) of the partition corresponding to part_index (offset of the primary logical partition in part_array)
   */
  int get_part_tablet_id_by_part_index(const PwjTable &table,
                                       const int64_t part_index,
                                       uint64_t &tablet_id);

  /**
   * Get the subpart_id (secondary logical partition id) of the secondary partition corresponding to a certain part_index in the secondary partition table
   */
  int get_sub_part_tablet_id(const PwjTable &table,
                             const int64_t &part_index,
                             uint64_t &sub_part_tablet_id);

private:
  // Save the mapping relationship of base table part_id (primary logical partition id)
  common::ObSEArray<std::pair<uint64_t, uint64_t>, 8, common::ModulePageAllocator, true> part_tablet_id_map_;
  // Save the mapping relationship of base table part_index (the offset of the first-level logical partition in part_array)
  common::ObSEArray<std::pair<int64_t, int64_t>, 8, common::ModulePageAllocator, true> part_index_map_;
  // Save the mapping relationship of base table subpart_id (secondary logical partition id)
  common::ObSEArray<std::pair<uint64_t, uint64_t>, 8, common::ModulePageAllocator, true> subpart_tablet_id_map_;
  // Save the mapping relationship of base table partition_id (physical partition id)
  TabletIdIdMap phy_part_map_;
  // Save the mapping relationship of tablet_id for the base table in a group of pwj constraints
  // For example, the pwj constraint includes [t1,t2,t3],
  //      t1,t2's tablet_id mapping relationship is [0,1,2] <-> [1,2,0]
  //      t2,t3's tablet_id mapping relationship is [0,1,2] <-> [2,1,0]
  // tablet_id_group_ = [[0,1,2], [1,2,0], [1,0,2]]
  common::ObSEArray<TabletIdArray, 4, common::ModulePageAllocator, true> tablet_id_group_;
  DISALLOW_COPY_AND_ASSIGN(ObStrictPwjComparer);
};

class ObNonStrictPwjComparer : public ObPwjComparer
{
public:
  ObNonStrictPwjComparer()
    : ObPwjComparer(false) {};
  virtual ~ObNonStrictPwjComparer() {};
  /**
   * Add a PwjTable to ObPwjComparer, it will perform a non-strict comparison with subsequent PwjTables using the first added PwjTable as the baseline
   */
  virtual int add_table(PwjTable &table, bool &is_match_nonstrict_pw) override;
  /**
   * check left table and right table have at least one partition on corresponding server
   */
  int is_match_non_strict_partition_wise(PwjTable &l_table,
                                         PwjTable &r_table,
                                         bool &is_match_nonstrict_pw);
private:  
  DISALLOW_COPY_AND_ASSIGN(ObNonStrictPwjComparer);
};
}
}
#endif
