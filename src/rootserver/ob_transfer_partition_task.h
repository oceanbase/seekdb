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

#ifndef OCEANBASE_ROOTSERVER_OB_TRANSFER_PARTITION_TASK_H
#define OCEANBASE_ROOTSERVER_OB_TRANSFER_PARTITION_TASK_H

#include "share/balance/ob_balance_task_table_operator.h"//ObBalanceTask
#include "share/balance/ob_balance_job_table_operator.h"//ObBalanceJob
#include "share/balance/ob_transfer_partition_task_table_operator.h"//ObTransferPartitionTask
#include "lib/container/ob_array.h"//ObArray
#include "lib/allocator/page_arena.h"//allocator
#include "rootserver/balance/ob_partition_balance_helper.h" // ObPartTransferJobGenerator

namespace oceanbase
{
namespace common
{
class ObMySQLProxy;
class ObMySQLTransaction;
}
namespace share
{
struct ObBalanceJob;
namespace schema{
class ObSimpleTableSchemaV2;
}
}
namespace rootserver
{
struct ObTransferPartitionInfo
{
public:
  ObTransferPartitionInfo() : task_(NULL), tablet_id_(), src_ls_(){}
  ~ObTransferPartitionInfo() {}
  int init(share::ObTransferPartitionTask &task,
         const ObTabletID &tablet_id);
  int set_src_ls(const share::ObLSID &ls_id);
  int assign(const ObTransferPartitionInfo &other);
  bool is_valid() const
  {
    return OB_NOT_NULL(task_) && task_->is_valid()
      && tablet_id_.is_valid() && src_ls_.is_valid();
  }
  const ObTabletID& get_tablet_id() const
  {
    return tablet_id_;
  }
  const share::ObLSID& get_ls_id() const
  {
    return src_ls_;
  }
  const share::ObTransferPartitionTask* get_task() const
  {
    return task_;
  } 
  TO_STRING_KV(KPC_(task), K_(tablet_id), K_(src_ls));
private:
  share::ObTransferPartitionTask* task_;
  ObTabletID tablet_id_;
  share::ObLSID src_ls_;
};

class ObTransferPartitionHelper
{
public:
  ObTransferPartitionHelper(const uint64_t tenant_id,
      common::ObMySQLProxy *sql_proxy) : 
    is_inited_(false), tenant_id_(tenant_id), sql_proxy_(sql_proxy),
    allocator_("TRANFER_PART", OB_MALLOC_NORMAL_BLOCK_SIZE, tenant_id),
    task_array_(OB_MALLOC_NORMAL_BLOCK_SIZE, ModulePageAllocator(allocator_, "TrPTaskArray")),
    part_info_(OB_MALLOC_NORMAL_BLOCK_SIZE, ModulePageAllocator(allocator_, "PartInfoArray")),
    max_task_id_(), job_generator_() {}
  ~ObTransferPartitionHelper()
  {
    destroy();
  }
  void destroy();

  share::ObBalanceJob& get_balance_job()
  {
    return job_generator_.get_balance_job();
  }
  ObArray<share::ObBalanceTask>& get_balance_tasks()
  {
    return job_generator_.get_balance_tasks();
  }
  //Construct the source log stream information required for each task
  int build(bool &has_job);
  //Lock construction logic task and physical task and write into the table
  int process_in_trans(const share::ObLSStatusInfoIArray &status_info_array,
      int64_t unit_num, int64_t primary_zone_num,
      ObMySQLTransaction &trans);
  /*
  * Find the tablet_id information of a specified partition from an array (table_schema_array) sorted in ascending order by table_id. This function can be repeatedly called to find the tablet_id information for a group of partitions.
  * Usage method:
  *     1. Ensure that table_schema_array is sorted in ascending order by table_id
  *     2. A group of partition information part_info, sorted in ascending order by <table_id, part_id>
  *     3. Initialize table_index = 0, and repeatedly call this function in ascending order with part_info to obtain the corresponding tablet_id information
  *
  * @param[in] table_schema_array: table_schema_array sorted in ascending order by table_id
  * @param[in] part_info: specified part_info, table_id must be greater than or equal to the table_id of the last specified part_info
  * @param[in/out] table_index: current traversal position, the caller only needs to initialize it to 0 for the first time, do not modify this variable value subsequently, otherwise it may lead to incorrect results
  * @param[out] tablet_id: tablet_id of part_info
  * @return OB_SUCCESS if success
  *         OB_TABLE_NOT_EXIST : table does not exist
  *         OB_PARTITION_NOT_EXIST : part_object_id does not exist
  * */
  static int get_tablet_in_order_array(
      const ObArray<share::schema::ObSimpleTableSchemaV2*> &table_schema_array,
      const share::ObTransferPartInfo &part_info,
      int64_t &table_index,
      ObTabletID &tablet_id);
private:
  //no need check is_inited_, after rebuild, is_inited_ = true
  int check_inner_stat_();
  int try_process_dest_not_exist_task_(
      const share::ObLSStatusInfoIArray &status_info_array,	
      int64_t& task_cnt);
  int try_process_object_not_exist_task_();
  int set_task_src_ls_();
  int try_finish_failed_task_(const share::ObTransferPartList &part_list,
      const ObString &comment);
  //By filtering the part_list according to tablet_id, object_id or the table_schema according to table_id
  int batch_get_table_schema_in_order_(
      common::ObArenaAllocator &allocator,
      ObArray<share::schema::ObSimpleTableSchemaV2*> &table_schema_array);
  int construct_logical_task_(const ObArray<share::ObTransferPartitionTask> &task_array);
private:
  bool is_inited_;
  uint64_t tenant_id_;
  common::ObMySQLProxy *sql_proxy_;
  common::ObArenaAllocator allocator_;
  ObArray<share::ObTransferPartitionTask> task_array_;
  //After initializing part_info_, task_array cannot change anymore
  ObArray<ObTransferPartitionInfo> part_info_;
  share::ObTransferPartitionTaskID max_task_id_;
  ObPartTransferJobGenerator job_generator_;
};
}
}

#endif /* !OB_TRANSFER_PARTITION_TASK_H */
