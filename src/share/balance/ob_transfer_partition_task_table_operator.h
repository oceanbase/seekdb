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

#ifndef OCEANBASE_SHARE_OB_TRANSFER_PARTITION_TASK_OPERATOR_H_
#define OCEANBASE_SHARE_OB_TRANSFER_PARTITION_TASK_OPERATOR_H_

#include "share/ob_ls_id.h"//share::ObLSID
#include "share/ob_common_id.h"// ObCommonID
#include "share/ob_balance_define.h"  // ObTransferPartitionTaskID
#include "share/transfer/ob_transfer_info.h"//ObTransferPartInfo
#include "lib/string/ob_sql_string.h"//ObSqlString

namespace oceanbase
{

namespace common
{
class ObMySQLTransaction;
class ObString;
namespace sqltrans
{
class ObMySQLResult;
}
}

namespace share
{
class ObDMLSqlSplicer;
class ObTransferPartitionTaskStatus
{
public:
  static const int64_t TRP_TASK_STATUS_INVALID = -1;
  static const int64_t TRP_TASK_STATUS_WAITING = 0;
  static const int64_t TRP_TASK_STATUS_INIT = 1;
  static const int64_t TRP_TASK_STATUS_DOING = 2;
  static const int64_t TRP_TASK_STATUS_COMPLETED = 3;
  static const int64_t TRP_TASK_STATUS_FAILED = 4;
  static const int64_t TRP_TASK_STATUS_CANCELED = 5;
  static const int64_t TRP_TASK_STATUS_MAX = 6;
  ObTransferPartitionTaskStatus(const int64_t value = TRP_TASK_STATUS_INVALID) : val_(value) {}
  ObTransferPartitionTaskStatus(const ObString &str);
  ~ObTransferPartitionTaskStatus() {reset(); }

public:
  void reset() { val_ = TRP_TASK_STATUS_INVALID; }
  bool is_valid() const { return val_ > TRP_TASK_STATUS_INVALID
                                 && val_ < TRP_TASK_STATUS_MAX; }
  const char* to_str() const;

  // assignment
  ObTransferPartitionTaskStatus &operator=(const int64_t value) { val_ = value; return *this; }

  // compare operator
  bool operator == (const ObTransferPartitionTaskStatus &other) const { return val_ == other.val_; }
  bool operator != (const ObTransferPartitionTaskStatus &other) const { return val_ != other.val_; }
#define IS_TRANSFER_PARTITION_TASK(TRANSFER_PARTITION_TASK, TRANSFER_PARTITION)\
  bool is_##TRANSFER_PARTITION()const { return TRANSFER_PARTITION_TASK == val_;}
  IS_TRANSFER_PARTITION_TASK(TRP_TASK_STATUS_INVALID, invalid)
  IS_TRANSFER_PARTITION_TASK(TRP_TASK_STATUS_WAITING, waiting)
  IS_TRANSFER_PARTITION_TASK(TRP_TASK_STATUS_INIT, init)
  IS_TRANSFER_PARTITION_TASK(TRP_TASK_STATUS_DOING, doing)
  IS_TRANSFER_PARTITION_TASK(TRP_TASK_STATUS_COMPLETED, completed)
  IS_TRANSFER_PARTITION_TASK(TRP_TASK_STATUS_FAILED, failed)
  IS_TRANSFER_PARTITION_TASK(TRP_TASK_STATUS_CANCELED, canceled)
#undef IS_TRANSFER_PARTITION_TASK
  bool is_finish_status() const
  {
    return is_completed() || is_failed() || is_canceled();
  }
  TO_STRING_KV(K_(val), "job_status", to_str());
private:
  int64_t val_;
};

struct ObTransferPartitionTask
{
public:
  ObTransferPartitionTask() { reset(); }
  ~ObTransferPartitionTask() {}
  void reset();
  //set status_ to waiting, reset balance_job_id_, reset transfer_task_id_
  int simple_init(const uint64_t tenant_id,
           const ObTransferPartInfo &part_info,
           const ObLSID &dest_ls,
           const ObTransferPartitionTaskID &task_id);
  int init(const uint64_t tenant_id,
           const ObTransferPartInfo &part_info,
           const ObLSID &dest_ls,
           const ObTransferPartitionTaskID &task_id,
           const ObBalanceJobID &balance_job_id,
           const ObTransferTaskID &transfer_task_id,
           const ObTransferPartitionTaskStatus &task_status,
           const ObString &comment);
  bool is_valid() const;
  int assign(const ObTransferPartitionTask &other);
  TO_STRING_KV(K_(tenant_id), K_(part_info), K_(dest_ls), K_(task_id),
               K_(balance_job_id), K_(transfer_task_id), K_(task_status), K_(comment));

#define Property_declare_var(variable_type, variable_name) \
 private:                                                  \
  variable_type variable_name##_;                          \
                                                           \
 public:                                                   \
  variable_type get_##variable_name() const { return variable_name##_; }

  Property_declare_var(uint64_t, tenant_id)
  Property_declare_var(ObLSID, dest_ls)
  Property_declare_var(ObTransferPartitionTaskID, task_id)
  Property_declare_var(ObBalanceJobID, balance_job_id)
  Property_declare_var(ObTransferTaskID, transfer_task_id)
  Property_declare_var(ObTransferPartitionTaskStatus, task_status)

#undef Property_declare_var 
public:
  const ObSqlString& get_comment() const
  {
    return comment_;
  }
  const ObTransferPartInfo& get_part_info() const
  {
    return part_info_;
  }
private:
  ObTransferPartInfo part_info_;
  ObSqlString comment_;
};

class ObTransferPartitionTaskTableOperator
{
public:
  /**
   * for add task by sql
   * @description: insert new task to __all_transfer_partition_task
   * @param[in] task : a valid transfer partition task include tenant_id
   * @param[in] trans: trans 
   * @return OB_SUCCESS if success, otherwise failed
   */
  static int insert_new_task(
      const uint64_t tenant_id,
      const ObTransferPartInfo &part_info,
      const ObLSID &dest_ls,
      ObMySQLTransaction &trans);
  /* When generating tasks, it is necessary to obtain all transfer partition tasks, without caring about any newly inserted concurrent tasks.
   * @description: get all transfer partition task from __all_transfer_partition_task
                   and smaller than max_task_id
   * @param[in] tenant_id : user_tenant_id
   * @param[in] max_task_id : max_task_id
   * @param[in] for_update  
   * @param[in] trans: trans
   * @param[out] task_array: transfer_partition_task 
   * */
  static int load_all_wait_task_in_part_info_order(const uint64_t tenant_id,
      const bool for_update,
      const ObTransferPartitionTaskID &max_task_id,
      ObIArray<ObTransferPartitionTask> &task_array,
      ObISQLClient &sql_client);
  /*
   * @description: get all transfer partition task from __all_transfer_partition_task
   * @param[in] tenant_id : user_tenant_id
   * @param[in] trans: sql trans or trans
   * @param[out] task_array: transfer_partition_task 
   * */
  static int load_all_task(const uint64_t tenant_id,
      ObIArray<ObTransferPartitionTask> &task_array,
      ObISQLClient &sql_client);
  /*
   * @description: get all tasks of a certain balance_job
   * @param[in] tenant_id : user_tenant_id
   * @param[in] job_id : the corelative balance job id
   * @param[in] trans: sql trans or trans
   * @param[out] task_array: transfer_partition_task 
   * */
  static int load_all_balance_job_task(const uint64_t tenant_id,
      const share::ObBalanceJobID &job_id,
      ObIArray<ObTransferPartitionTask> &task_array,
      ObISQLClient &sql_client);

  /*
   * When constructing tasks, it is necessary to ensure the partial order relationship between tasks. Tasks with smaller task_id added later may merge multiple tasks into one balance_job.
   * Therefore, they cannot be modified one by one but in batches, utilizing the partial order relationship.
   * If there is a requirement in the future, it can be done in batches, for example, 1024 tasks per batch.
   * @description: set all task smaller than max_task_id from waiting to schedule and set balance job id
   * @param[in] tenant_id : user_tenant_id
   * @param[in] max_task_id : max_task_id
   * @param[in] job_id : the corelative balance job id
   * @param[in] task_count: for double check the task smaller than max_task_id and affected_rows
   * @param[in] trans: trans
   * @return OB_SUCCESS if success, otherwise failed
   * */
  static int set_all_tasks_schedule(const uint64_t tenant_id,
      const ObTransferPartitionTaskID &max_task_id,
      const ObBalanceJobID &job_id,
      const int64_t &task_count,
      ObMySQLTransaction &trans);
  /*
   * When enable_transfer is disabled, the current balance_job needs to be canceled, and the transfer partition tasks associated with balance_job need to return to the waiting state
   * @description: balance job may be canceled, disassociate balance_job and transfer_partition_task,
   *               rollback task from doing to waiting, and clean balance_job and transfer_task_id
   *  @param[in] tenant_id : user_tenant_id
   *  @param[in] job_id : the correlated balance job id
   *  @param[in] trans: must be in trans
   * */
  static int rollback_all_to_waitting(const uint64_t tenant_id,
                           const ObBalanceJobID &job_id,
                           ObMySQLTransaction &trans);
  /*
   * When a transfer task starts, it is necessary to mark this batch of transfer tasks
   * @description: set task corelative transfer task ID
   * @param[in] tenant_id : user_tenant_id
   * @param[in] job_id : the corelative balance job id
   * @param[in] part_list : part_info start transfer
   * @param[in] transfer_task_id : the corelative transfer task id 
   * @param[in] trans: must in trans
   * */
  static int start_transfer_task(const uint64_t tenant_id,
                         const ObBalanceJobID &job_id,
                         const ObTransferPartList &part_list,
                         const ObTransferTaskID &transfer_task_id,
                         ObMySQLTransaction &trans); 

  /*
   * The task is found to be in a partition that should cause it to fail during execution, the call points are in finish_task_from_init and the generation of balance_job processes
   * The transfer task needs to be terminated when it executes successfully, the call point is in finish_task
   * The CANCELED status will not be handled this time
   * @description: task maybe CANCELED, FAILED, COMPLETE 
   *               and insert into __all_transfer_partition_task_history
   * @param[in] tenant_id : user_tenant_id
   * @param[in] part_list : part_info need finish
   * @param[in] status : must be CANCELED, FAILED, COMPLETE, must be finish
   * @param[in] max_task_id: max_task_id
   * @param[in] comment : task comment
   * @param[in] trans: must in trans 
   * */
  static int finish_task(const uint64_t tenant_id,
                         const ObTransferPartList &part_list,
                         const ObTransferPartitionTaskID &max_task_id, 
                         const ObTransferPartitionTaskStatus &status,
                         const ObString &comment,
                         ObMySQLTransaction &trans); 
  /*
   * The transfer task may have a batch of not_exist partitions at the end, but this part of the partitions
   * may not be truly not_exist, it could be due to incorrect source log stream statistics, requiring a rollback
   * of this part of the task status to waiting status, and regenerating the task
   * @param[in] tenant_id : user_tenant_id
   * @param[in] job_id : the corelative balance job id
   * @param[in] part_list : part_info start transfer
   * @param[in] transfer_task_id : the corelative transfer task id 
   * @param[in] comment : reason of rollback
   * @param[in] trans: must in trans
   * */
 
  static int rollback_from_doing_to_waiting(const uint64_t tenant_id,
                         const ObBalanceJobID &job_id,
                         const ObTransferPartList &part_list,
                         const ObString &comment,
                         ObMySQLTransaction &trans); 

  /*
  * Get a batch of task information for part_list, which may not exist in the table.
  * @description: get dest_ls of part_list
   * @param[in] tenant_id : user_tenant_id
   * @param[in] part_list : part_info
   * @param[in] job_id : the corelative balance job id
   * @param[out] dest_ls : dest_ls of part_info
   * @param[in] trans: must in trans  
  */
  static int load_part_list_task(const uint64_t tenant_id,
                         const ObBalanceJobID &job_id,
                         const ObTransferPartList &part_list,
                         ObIArray<ObTransferPartitionTask> &task_array,
                         ObMySQLTransaction &trans);
  /*
   * Get the transfer partition task for the specified partition
   * @description: get transfer partition task of part
   * @param[in] tenant_id : user_tenant_id
   * @param[in] part_info : table_id and part_object_id
   * @param[out] task : transfer partition task
   * @param[in] sql_client: trans or sql_client
   * return OB_SUCCESS if success,
   *        OB_ENTRY_NOT_EXIST if task not exist
   *        otherwise failed
   * */
  static int get_transfer_partition_task(const uint64_t tenant_id,
      const ObTransferPartInfo &part_info,
      ObTransferPartitionTask &task,
      ObISQLClient &sql_client);
private:
  static int fill_dml_splicer_(share::ObDMLSqlSplicer &dml,
                              const ObTransferPartitionTask &task);
  static int fill_cell_(const uint64_t tenant_id,
                       sqlclient::ObMySQLResult *result,
                       ObTransferPartitionTask &task);
  static int append_sql_with_part_list_(const ObTransferPartList &part_list,
                                        ObSqlString &sql);
  static int get_tasks_(const uint64_t tenant_id,
      const ObSqlString &sql,
      ObIArray<ObTransferPartitionTask> &task_array,
      ObISQLClient &sql_client);
  static int fetch_new_task_id_(const uint64_t tenant_id,
      ObMySQLTransaction &trans,
      ObTransferPartitionTaskID &task_id);

};
}
}

#endif /* !OCEANBASE_SHARE_OB_TRANSFER_PARTITION_TASK_OPERATOR_H_ */
