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

#ifndef OCEANBASE_SQL_OPTIMIZER_OB_SHARDING_INFO_H
#define OCEANBASE_SQL_OPTIMIZER_OB_SHARDING_INFO_H 1
#include "lib/container/ob_array.h"
#include "lib/container/ob_se_array.h"
#include "lib/container/ob_fixed_array.h"
#include "lib/allocator/ob_allocator.h"
#include "lib/allocator/page_arena.h"
#include "sql/optimizer/ob_table_partition_info.h"
#include "sql/resolver/expr/ob_raw_expr.h"
#include "sql/optimizer/table_access_targets.h"

namespace oceanbase
{
namespace sql
{
class ObRawExpr;
class ObOptimizerContext;
class ObDMLStmt;
class ObLogPlan;
typedef common::ObFixedArray<ObRawExpr*, common::ObIAllocator> ObRawExprSet;
typedef common::ObSEArray<ObRawExprSet*, 8, common::ModulePageAllocator, true> ObRawExprSets;
typedef ObRawExprSets EqualSets;

class ObShardingInfo
{
public:
  ObShardingInfo()
    : part_level_(share::schema::PARTITION_LEVEL_MAX),
      part_func_type_(share::schema::PARTITION_FUNC_TYPE_MAX),
      subpart_func_type_(share::schema::PARTITION_FUNC_TYPE_MAX),
      access_shape_(TABLE_ACCESS_UNINITIALIZED),
      part_num_(0),
      partition_keys_(),
      sub_partition_keys_(),
      table_access_targets_(NULL),
      partition_array_ (NULL),
      all_tablet_ids_(),
      all_partition_indexes_(),
      all_subpartition_indexes_(),
      is_partition_single_(false),
      is_subpartition_sinlge_(false)
  {}
  ObShardingInfo(TableAccessShape type)
  : part_level_(share::schema::PARTITION_LEVEL_MAX),
    part_func_type_(share::schema::PARTITION_FUNC_TYPE_MAX),
    subpart_func_type_(share::schema::PARTITION_FUNC_TYPE_MAX),
    access_shape_(type),
    part_num_(0),
    partition_keys_(),
    sub_partition_keys_(),
    table_access_targets_(NULL),
    partition_array_ (NULL),
    all_tablet_ids_(),
    all_partition_indexes_(),
    all_subpartition_indexes_(),
    is_partition_single_(false),
    is_subpartition_sinlge_(false)
  {}

  virtual ~ObShardingInfo()
  {}
  /**
   *  Set the partition key and num based on table schema
   */
  int init_partition_info(ObOptimizerContext &ctx,
                          const ObDMLStmt &stmt,
                          const uint64_t table_id,
                          const uint64_t ref_table_id,
                          const TableAccessTargets &table_access_targets);
  const common::ObIArray<ObRawExpr*> &get_partition_keys() const { return partition_keys_; }
  common::ObIArray<ObRawExpr*> &get_partition_keys() { return partition_keys_; }
  const common::ObIArray<ObRawExpr*> &get_sub_partition_keys() const { return sub_partition_keys_; }
  common::ObIArray<ObRawExpr*> &get_sub_partition_keys() { return sub_partition_keys_; }
  const common::ObIArray<ObRawExpr*> &get_partition_func() const { return part_func_exprs_; }
  common::ObIArray<ObRawExpr*> &get_partition_func() { return part_func_exprs_; }
  inline TableAccessTargets *get_access_targets()
  {
    return const_cast<TableAccessTargets *>
      (static_cast<const ObShardingInfo&>(*this).get_access_targets());
  }
  inline const TableAccessTargets *get_access_targets() const
  { return table_access_targets_; }

  inline void set_access_targets(TableAccessTargets *access_targets)
  { table_access_targets_ = access_targets; }

  inline int64_t get_part_cnt() const
  {
    return NULL == table_access_targets_ ? 0 : table_access_targets_->get_tablet_count();
  }
  // Partition-related getter
  inline share::schema::ObPartitionLevel get_part_level() const { return part_level_;}
  inline share::schema::ObPartitionFuncType get_part_func_type() const { return part_func_type_; }
  inline share::schema::ObPartitionFuncType get_sub_part_func_type() const
  { return subpart_func_type_; }
  inline int64_t get_part_number() const { return part_num_; }
  inline share::schema::ObPartition ** get_partition_array() const
  { return partition_array_; }
  const common::ObIArray<uint64_t> &get_all_tablet_ids() const { return all_tablet_ids_; }
  const common::ObIArray<int64_t> &get_all_partition_indexes() const { return all_partition_indexes_; }
  const common::ObIArray<int64_t> &get_all_subpartition_indexes() const { return all_subpartition_indexes_; }
  inline bool is_partition_single() const { return is_partition_single_; }
  inline bool is_subpartition_single() const { return is_subpartition_sinlge_; }
  // end partition-related getter

  // for set-op and insert-op to check partition wise
  static int check_if_match_partition_wise(const EqualSets &equal_sets,
                                           const common::ObIArray<ObRawExpr*> &left_keys,
                                           const common::ObIArray<ObRawExpr*> &right_keys,
                                           ObShardingInfo *left_strong_sharding,
                                           ObShardingInfo *right_strong_sharding,
                                           bool &is_partition_wise);

  // for join and subplan filter op to check partition wise
  static int check_if_match_partition_wise(const EqualSets &equal_sets,
                                           const common::ObIArray<ObRawExpr*> &left_keys,
                                           const common::ObIArray<ObRawExpr*> &right_keys,
                                           const common::ObIArray<bool> &null_safe_info,
                                           ObShardingInfo *left_strong_sharding,
                                           const common::ObIArray<ObShardingInfo *> &left_weak_sharding,
                                           ObShardingInfo *right_strong_sharding,
                                           const common::ObIArray<ObShardingInfo *> &right_weak_sharding,
                                           bool &is_partition_wise);

  static int check_if_match_partition_wise(const EqualSets &equal_sets,
                                           const common::ObIArray<ObRawExpr*> &left_keys,
                                           const common::ObIArray<ObRawExpr*> &right_keys,
                                           const common::ObIArray<ObShardingInfo *> &left_sharding,
                                           const common::ObIArray<ObShardingInfo *> &right_sharding,
                                           bool &is_partition_wise);

  static int check_if_match_extended_partition_wise(const EqualSets &equal_sets,
                                                    const common::ObIArray<ObRawExpr*> &left_keys,
                                                    const common::ObIArray<ObRawExpr*> &right_keys,
                                                    const common::ObIArray<ObShardingInfo *> &left_sharding,
                                                    const common::ObIArray<ObShardingInfo *> &right_sharding,
                                                    bool &is_ext_partition_wise);

  static int check_if_match_extended_partition_wise(const EqualSets &equal_sets,
                                                    const common::ObIArray<ObRawExpr*> &left_keys,
                                                    const common::ObIArray<ObRawExpr*> &right_keys,
                                                    const common::ObIArray<bool> &null_safe_info,
                                                    ObShardingInfo *left_strong_sharding,
                                                    const common::ObIArray<ObShardingInfo *> &left_weak_sharding,
                                                    ObShardingInfo *right_strong_sharding,
                                                    const common::ObIArray<ObShardingInfo *> &right_weak_sharding,
                                                    bool &is_ext_partition_wise);

  static int check_if_match_extended_partition_wise(const EqualSets &equal_sets,
                                                    const common::ObIArray<ObRawExpr*> &left_keys,
                                                    const common::ObIArray<ObRawExpr*> &right_keys,
                                                    ObShardingInfo *left_strong_sharding,
                                                    ObShardingInfo *right_strong_sharding,
                                                    bool &is_ext_partition_wise);

  static int check_if_match_repart_or_rehash(const EqualSets &equal_sets,
                                             const common::ObIArray<ObRawExpr *> &src_join_keys,
                                             const common::ObIArray<ObRawExpr *> &target_join_keys,
                                             const common::ObIArray<ObRawExpr *> &target_part_keys,
                                             bool &is_match_join_keys);

  static int is_sharding_equal(const ObShardingInfo *left_strong_sharding,
                               const ObIArray<ObShardingInfo*> &left_weak_shardings,
                               const ObShardingInfo *right_strong_sharding,
                               const ObIArray<ObShardingInfo*> &right_weak_shardings,
                               const EqualSets &equal_sets,
                               bool &is_equal);

  static int is_sharding_equal(const ObIArray<ObShardingInfo*> &left_sharding,
                               const ObIArray<ObShardingInfo*> &right_sharding,
                               const EqualSets &equal_sets,
                               bool &is_equal);

  static int is_subset_sharding(const ObIArray<ObShardingInfo*> &subset_sharding,
                                const ObIArray<ObShardingInfo*> &target_sharding,
                                const EqualSets &equal_sets,
                                bool &is_subset);

  static int is_sharding_equal(const ObShardingInfo *left_sharding,
                               const ObShardingInfo *right_sharding,
                               const EqualSets &equal_sets,
                               bool &is_equal);

  static int extract_partition_key(const common::ObIArray<ObShardingInfo *> &input_shardings,
                                   ObIArray<ObSEArray<ObRawExpr*, 8>> &partition_key_list);

  inline void set_access_shape(TableAccessShape access_shape)
  {
    access_shape_ = access_shape;
  }
  inline TableAccessShape get_access_shape() const
  {
    return access_shape_;
  }
  inline bool is_sharding() const
  {
    return TABLE_ACCESS_MULTI_TABLET == access_shape_;
  }
  inline bool is_distributed() const
  {
    return TABLE_ACCESS_MULTI_TABLET == access_shape_;
  }
  inline void set_distributed()
  {
    access_shape_ = TABLE_ACCESS_MULTI_TABLET;
  }
  inline bool is_match_all() const
  {
    return (TABLE_ACCESS_ANY == access_shape_);
  }
  inline void set_match_all()
  {
    access_shape_ = TABLE_ACCESS_ANY;
  }
  bool is_local() const
  {
    return TABLE_ACCESS_SINGLE_TABLET == access_shape_;
  }
  inline void set_local()
  {
    access_shape_ = TABLE_ACCESS_SINGLE_TABLET;
  }
  bool is_single() const
  {
    return (TABLE_ACCESS_SINGLE_TABLET == access_shape_ ||
            TABLE_ACCESS_ANY == access_shape_);
  }
  bool is_uninitial() const {
    return TABLE_ACCESS_UNINITIALIZED == access_shape_;
  }
  inline bool is_multi_tablet_without_targets() const {
    return is_distributed() && NULL == table_access_targets_;
  }
  inline bool is_multi_tablet_with_targets() const {
    return is_distributed() && NULL != table_access_targets_;
  }
  inline bool is_distributed_without_partitioning() const {
    return is_distributed() && partition_keys_.empty();
  }
  inline bool is_distributed_with_partitioning() const {
    return is_distributed() && !partition_keys_.empty();
  }
  inline bool is_partitioned_multi_tablet_with_targets()
  {
    return is_distributed() && NULL != table_access_targets_ && !partition_keys_.empty();
  }
  inline bool is_partitioned_multi_tablet_without_targets()
  {
    return is_distributed() && NULL == table_access_targets_ && !partition_keys_.empty();
  }
  /**
   * Get all partition keys
   * @param ignore_single_partition: Whether to ignore partition keys on partition registrations that involve only one partition
   */
  int get_all_partition_keys(common::ObIArray<ObRawExpr*> &out_part_keys,
                             bool ignore_single_partition = false) const;

  int copy_with_part_keys(const ObShardingInfo &other);
  int copy_without_part_keys(const ObShardingInfo &other);
  int get_total_part_cnt(int64_t &total_part_cnt) const;
  static int get_all_partition_key(ObOptimizerContext &ctx,
                                   const ObDMLStmt &stmt,
                                   const uint64_t table_id,
                                   const uint64_t ref_table_id,
                                   ObIArray<ObRawExpr*> &all_partition_keys);
  TO_STRING_KV(K_(part_level),
               K_(part_func_type),
               K_(subpart_func_type),
               K_(part_num),
               K_(access_shape),
               K_(table_access_targets));

private:
  static int get_partition_key(ObRawExpr *part_expr,
                               const share::schema::ObPartitionFuncType part_func_type,
                               common::ObIArray<ObRawExpr*> &partition_keys);
  // check whether all partition keys are of the same type
  static int is_compatible_partition_key(const ObShardingInfo *first_sharding,
                                         const ObIArray<ObSEArray<ObRawExpr*, 8>> &first_part_keys_list,
                                         const ObIArray<ObSEArray<ObRawExpr*, 8>> &second_part_keys_list,
                                         bool &is_compatible);
  static int is_compatible_partition_key(const ObShardingInfo &first_sharding,
                                         const ObShardingInfo &second_sharding,
                                         bool &is_compatible);

  // check whether all the partition keys are covered by join keys
  static int is_join_key_cover_partition_key(const EqualSets &equal_sets,
                                             const common::ObIArray<ObRawExpr *> &first_keys,
                                             const common::ObIArray<ObShardingInfo *> &first_shardings,
                                             const common::ObIArray<ObRawExpr *> &second_keys,
                                             const common::ObIArray<ObShardingInfo *> &second_shardins,
                                             bool &is_cover);

  static int is_expr_equivalent(const EqualSets &equal_sets,
                                const sql::ObShardingInfo &first_sharding,
                                const common::ObIArray<ObRawExpr*> &first_part_exprs,
                                const common::ObIArray<ObRawExpr*> &second_part_exprs,
                                ObRawExpr *first_key,
                                ObRawExpr *second_key,
                                bool &is_equal);

  static int remove_lossless_cast_for_sharding_key(ObRawExpr *&expr,
                                                   const ObShardingInfo &sharding_info);

  static bool is_part_func_scale_sensitive(const sql::ObShardingInfo &sharding_info,
                                           const common::ObObjType obj_type);

private:
  // Partition level
  share::schema::ObPartitionLevel part_level_;
  // First-level partition type
  share::schema::ObPartitionFuncType part_func_type_;
  // Secondary partition type
  share::schema::ObPartitionFuncType subpart_func_type_;
  TableAccessShape access_shape_;
  // First-level partition count
  int64_t part_num_;
  // First-level partition key
  common::ObSEArray<ObRawExpr *, 4, common::ModulePageAllocator, true> partition_keys_;
  // Secondary partition key
  common::ObSEArray<ObRawExpr *, 4, common::ModulePageAllocator, true> sub_partition_keys_;
  // Partition method expr
  common::ObSEArray<ObRawExpr *, 4, common::ModulePageAllocator, true> part_func_exprs_;
  const TableAccessTargets *table_access_targets_;
  // first-level partition array
  share::schema::ObPartition **partition_array_;
  // All partition_id (physical partition id) in table_access_targets_
  common::ObSEArray<uint64_t, 8, common::ModulePageAllocator, true> all_tablet_ids_;
  // part_id(logical partition id level 1) in part_array offset
  common::ObSEArray<int64_t, 8, common::ModulePageAllocator, true> all_partition_indexes_;
  // subpart_id(sub-secondary logical partition id) in subpart_array offset
  common::ObSEArray<int64_t, 8, common::ModulePageAllocator, true> all_subpartition_indexes_;
  // Whether only one primary partition is involved in the table_access_targets_ of the secondary partition table
  bool is_partition_single_;
  // In the table_access_targets_ of the secondary partition table, does each primary partition involve only one secondary partition
  bool is_subpartition_sinlge_;
  DISALLOW_COPY_AND_ASSIGN(ObShardingInfo);
};
}
}
#endif
