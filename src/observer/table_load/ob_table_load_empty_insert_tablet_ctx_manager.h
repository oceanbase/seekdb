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

#ifndef _OB_TABLE_LOAD_EMPTY_INSERT_TABLET_CTX_MANAGER_H_
#define _OB_TABLE_LOAD_EMPTY_INSERT_TABLET_CTX_MANAGER_H_

#include "lib/container/ob_iarray.h"
#include "observer/table_load/ob_table_load_partition_location.h"
#include "observer/table_load/ob_table_load_table_ctx.h"

namespace oceanbase
{
namespace table
{
struct ObTableLoadPartitionId;
} // namespace table
namespace observer
{

class ObTableLoadEmptyInsertTabletCtxManager
{
  using LeaderInfo = ObTableLoadPartitionLocation::LeaderInfo;
  static const int64_t TABLET_COUNT_PER_TASK = 20;
public:
  ObTableLoadEmptyInsertTabletCtxManager();
  ~ObTableLoadEmptyInsertTabletCtxManager();
  int init(
      const common::ObIArray<table::ObTableLoadPartitionId> &partition_ids,
      const common::ObIArray<table::ObTableLoadPartitionId> &target_partition_ids);
  int get_next_task(ObAddr &addr,
                    ObIArray<table::ObTableLoadLSIdAndPartitionId> &partition_ids,
                    ObIArray<table::ObTableLoadLSIdAndPartitionId> &target_partition_ids);
  int set_thread_count(const int64_t thread_count);
  int handle_thread_finish(bool &is_finish);
  static int execute(const uint64_t &table_id,
                     const ObTableLoadDDLParam &ddl_param,
                     const ObIArray<table::ObTableLoadLSIdAndPartitionId> &ls_part_ids,
                     const ObIArray<table::ObTableLoadLSIdAndPartitionId> &target_ls_part_ids);
  static int execute_for_dag(const uint64_t &table_id,
                             const ObTableLoadDDLParam &ddl_param,
                             const ObIArray<table::ObTableLoadLSIdAndPartitionId> &ls_part_ids,
                             const ObIArray<table::ObTableLoadLSIdAndPartitionId> &target_ls_part_ids);
private:
  ObTableLoadPartitionLocation partition_location_;
  ObTableLoadPartitionLocation target_partition_location_;
  table::ObTableLoadArray<LeaderInfo> all_leader_info_array_;
  table::ObTableLoadArray<LeaderInfo> target_all_leader_info_array_;
  int64_t thread_count_ CACHE_ALIGNED;
  lib::ObMutex op_lock_;
  int64_t idx_;
  int64_t start_;
  bool is_inited_;
};

} // namespace observer
} // namespace oceanbase
#endif // _OB_TABLE_LOAD_EMPTY_INSERT_TABLET_CTX_MANAGER_H_
