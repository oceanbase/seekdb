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

#ifndef OCEANBASE_STORAGE_TABLELOCK_OB_TABLE_LOCK_SERVICE_H_
#define OCEANBASE_STORAGE_TABLELOCK_OB_TABLE_LOCK_SERVICE_H_

#include <stdint.h>

#include "common/ob_tablet_id.h"
#include "lib/task/ob_timer.h"
#include "storage/tablelock/ob_table_lock_common.h"
#include "storage/tablelock/ob_table_lock_rpc_struct.h"
#include "storage/tablelock/ob_table_lock_local_executor.h"
#include "storage/tablelock/ob_named_lock_manager.h"

namespace oceanbase
{
namespace common
{
class ObMySQLProxy;
}
namespace share
{
namespace schema
{
class ObMultiVersionSchemaService;
}
}
namespace query
{
class ObIDeadlockSessionService;
}

namespace transaction
{

namespace tablelock
{

class ObTableLockService final
{
private:
  static const int64_t OB_DEFAULT_LOCK_ID_COUNT = 10;
  typedef common::ObSEArray<ObLockID, OB_DEFAULT_LOCK_ID_COUNT> ObLockIDArray;
  class ObLockSet
  {
  public:
    ObLockSet() : lock_ids_() {}
    ~ObLockSet() {}
    int reuse() { lock_ids_.reuse(); return common::OB_SUCCESS; }
    bool empty() const { return lock_ids_.empty(); }
    int64_t size() const { return lock_ids_.count(); }
    int64_t count() const { return lock_ids_.count(); }
    int push_back(const ObLockID &lock_id) { return lock_ids_.push_back(lock_id); }
    int assign(const common::ObIArray<ObLockID> &lock_ids) { return lock_ids_.assign(lock_ids); }
    const ObLockIDArray &get_lock_ids() const { return lock_ids_; }
    ObLockIDArray &get_lock_ids() { return lock_ids_; }
    TO_STRING_KV(K_(lock_ids));
  private:
    ObLockIDArray lock_ids_;
    DISALLOW_COPY_AND_ASSIGN(ObLockSet);
  };
  class ObTableLockCtx
  {
  public:
    ObTableLockCtx();
    ~ObTableLockCtx() {}
    int set_by_lock_req(const ObLockRequest &arg, const bool is_replace_task = false);
    int set_by_lock_req_common_part(const ObLockRequest &arg);
    int set_tablet_id(const common::ObIArray<common::ObTabletID> &tablet_ids);
    int set_tablet_id(const common::ObTabletID &tablet_id);
    int set_lock_id(const common::ObIArray<ObLockID> &lock_ids);
    bool is_try_lock() const { return 0 == timeout_us_; }
    bool is_deadlock_avoid_enabled() const;
    bool is_timeout() const;
    int64_t remain_timeoutus() const;
    int64_t get_tablet_cnt() const;
    const common::ObTabletID &get_tablet_id(const int64_t index) const;
    void mark_need_rollback();
    void clear_need_rollback();
    bool need_rollback() const { return need_rollback_; }
    bool is_savepoint_valid() { return current_savepoint_.is_valid(); }
    void reset_savepoint() { current_savepoint_.reset(); }

    bool is_stmt_savepoint_valid() { return stmt_savepoint_.is_valid(); }
    void reset_stmt_savepoint() { stmt_savepoint_.reset(); }
    ObTableLockOpType get_lock_op_type() const { return lock_op_type_; }
    bool is_unlock_task() const { return tablelock::is_unlock_task(task_type_); }
    bool is_replace_task() const { return tablelock::is_replace_lock_task(task_type_); }
    bool is_tablet_lock_task() const { return tablelock::is_tablet_lock_task(task_type_); }
    bool is_obj_lock_task() const { return tablelock::is_obj_lock_task(task_type_); }
    bool is_alone_tablet_lock_task() const
    {
      return LOCK_ALONE_TABLET == task_type_ || UNLOCK_ALONE_TABLET == task_type_ || REPLACE_LOCK_ALONE_TABLET == task_type_;
    }
    bool can_execute_push_lock_task() const {
      return is_enable_lock_priority_ && !is_unlock_task();
    }
  public:
    ObTableLockTaskType task_type_; // current lock request type
    bool is_in_trans_;
    union {
      // used for table/partition
      struct {
        uint64_t table_id_;
        uint64_t partition_id_;          // set when lock or unlock specified partition
      };
    };

    ObTableLockOpType lock_op_type_;  // specify the lock op type

    int64_t origin_timeout_us_;  // the origin timeout us specified by user.
    int64_t timeout_us_;         // the timeout us for every retry times.
    int64_t abs_timeout_ts_;     // the abstract timeout us.
    // This anonymous transaction belongs to the table-lock implementation.
    // Keep only the state that implementation needs instead of depending on
    // SQL's statement/transaction orchestration state machine.
    bool tx_started_;
    transaction::ObTxDesc *tx_desc_;
    ObTxParam tx_param_;                      // the tx param for current tx
    transaction::ObTxSEQ current_savepoint_;  // used to rollback current sub tx.
    bool need_rollback_;                     // lock state changed after the savepoint was created
    common::ObTabletIDArray tablet_list_;     // all the tablets need to be locked/unlocked
    ObLockIDArray obj_list_;

    ObTableLockMode lock_mode_;
    ObTableLockOwnerID lock_owner_;

	    int64_t schema_version_;             // the schema version of the table to be locked
    bool tx_is_killed_;                  // used to kill a trans.
    bool is_from_sql_;
    int ret_code_before_end_stmt_or_tx_;  // used to mark this lock is still conflict while lock request exiting
    bool is_enable_lock_priority_;
    ObTableLockPriority lock_priority_;

    // use to kill the whole lock table stmt.
    transaction::ObTxSEQ stmt_savepoint_;
    bool is_for_replace_;

    TO_STRING_KV(K(task_type_), K(is_in_trans_), K(table_id_), K(partition_id_),
                 K(tablet_list_), K(obj_list_), K(lock_op_type_),
                 K(origin_timeout_us_), K(timeout_us_),
                 K(abs_timeout_ts_), KPC(tx_desc_), K(tx_param_),
	                 K(current_savepoint_), K(need_rollback_),
                 K(lock_mode_), K(lock_owner_),
                 K(schema_version_), K(tx_is_killed_),
                 K(is_from_sql_), K(ret_code_before_end_stmt_or_tx_), K(stmt_savepoint_),
                 K(is_enable_lock_priority_), K(lock_priority_), K_(is_for_replace));
  };

  class ObReplaceTableLockCtx : public ObTableLockCtx
  {
  public:
    ObReplaceTableLockCtx() : ObTableLockCtx(), new_lock_mode_(NO_LOCK), new_lock_owner_() {}
    ~ObReplaceTableLockCtx() {}
    int get_lock_param(const ObLockID &lock_id, ObReplaceLockParam &lock_param) const;
    INHERIT_TO_STRING_KV("ObTableLockCtx", ObTableLockCtx, K_(new_lock_mode), K_(new_lock_owner));

  public:
    ObTableLockMode new_lock_mode_;
    ObTableLockOwnerID new_lock_owner_;
  };

	  class ObRetryCtx
	  {
	  public:
	    ObRetryCtx() : need_retry_(false),
	                   task_executed_(false),
	                   task_prepared_(false),
	                   retry_lock_ids_()
	    {}
	    ~ObRetryCtx()
	    { reuse(); }
	    void reuse();
	  public:
	    TO_STRING_KV(K_(need_retry), K_(task_executed), K_(task_prepared),
	                 K_(retry_lock_ids));
	    bool need_retry_;
	    bool task_executed_;
	    bool task_prepared_;
	    ObLockIDArray retry_lock_ids_;           // the lock id need to be retry.
	  };
public:
  class ObOBJLockGarbageCollector
  {
  public:
    ObOBJLockGarbageCollector();
    ~ObOBJLockGarbageCollector();
  public:
    int init(common::ObMySQLProxy &sql_proxy);
    int start();
    void stop();
    void wait();
    void destroy();
    int garbage_collect_right_now();

    TO_STRING_KV(KP(this),
                 K_(last_success_timestamp));
  private:
    class TimerTask : public common::ObTimerTask
    {
    public:
      explicit TimerTask(ObOBJLockGarbageCollector &collector) : collector_(collector) {}
      virtual ~TimerTask() = default;
      void runTimerTask() override { collector_.run_gc_once_(); }
    private:
      ObOBJLockGarbageCollector &collector_;
    };
  private:
    void run_gc_once_();
    int garbage_collect_();
    void check_and_report_timeout_();
  public:
    static int64_t GARBAGE_COLLECT_EXEC_INTERVAL;
    static int64_t GARBAGE_COLLECT_TIMEOUT;
  private:
    common::ObTimer timer_;
    TimerTask timer_task_;
    int64_t last_success_timestamp_;
    common::ObMySQLProxy *sql_proxy_;
  };

  ObTableLockService()
    : sql_proxy_(nullptr),
      session_service_(nullptr),
      obj_lock_garbage_collector_(),
      named_lock_manager_(),
      is_inited_(false) {}
  ~ObTableLockService() {}
  int init(query::ObIDeadlockSessionService &session_service);
  static int server_module_init(
      ObTableLockService* &lock_service,
      query::ObIDeadlockSessionService &session_service);
  int start();
  void stop();
  void wait();
  void destroy();
  query::ObIDeadlockSessionService &get_deadlock_session_service()
  {
    OB_ASSERT(nullptr != session_service_);
    return *session_service_;
  }

  // Generate an owner ID unique within the database runtime.
  // this owner id can be used to link OUT_TRANS_LOCK and OUT_TRANS_UNLOCK operation.
  // ---------------------------- interface for OUT_TRANS lock ------------------------------/
  // lock and unlock with anonymous trans.

  // lock the table level lock and all the tablet level lock within an anonymous trans.
  // @param [in] table_id, specified the table which will be locked.
  // @param [in] lock_mode, may be ROW_SHARE/ROW_EXCLUSIVE/SHARE/SHARE_ROW_EXCLUSIVE/EXCLUSIVE
  // @param [in] lock_owner, who will lock the table, and who will unlock the table later.
  // @param [in] timeout_us, 0 means it is try lock, if there is some lock conflict will return immediately.
  //                         otherwise retry until timeout if there is some lock conflict.
  // @return
  int lock_table(const uint64_t table_id,
                 const ObTableLockMode lock_mode,
                 const ObTableLockOwnerID lock_owner,
                 const int64_t timeout_us = 0);
  int unlock_table(const uint64_t table_id,
                   const ObTableLockMode lock_mode,
                   const ObTableLockOwnerID lock_owner,
                   const int64_t timeout_us = 0);
  // lock the tablet level lock and corresponding table level lock within an anonymous trans.
  // @param [in] table_id, specified the table whose tablet will be locked.
  // @param [in] tablet_id, specified which tablet will be locked.
  // @param [in] lock_mode, may be ROW_SHARE/ROW_EXCLUSIVE/SHARE/SHARE_ROW_EXCLUSIVE/EXCLUSIVE
  // @param [in] lock_owner, who will lock the table, and who will unlock the table later.
  // @param [in] timeout_us, 0 means it is try lock, if there is some lock conflict will return immediately.
  //                         otherwise retry until timeout if there is some lock conflict.
  // @return
  int lock_tablet(const uint64_t table_id,
                  const common::ObTabletID &tablet_id,
                  const ObTableLockMode lock_mode,
                  const ObTableLockOwnerID lock_owner,
                  const int64_t timeout_us = 0);
  int unlock_tablet(const uint64_t table_id,
                    const common::ObTabletID &tablet_id,
                    const ObTableLockMode lock_mode,
                    const ObTableLockOwnerID lock_owner,
                    const int64_t timeout_us = 0);

  // ---------------------------- interface for IN_TRANS/OUT_TRANS lock ------------------------------/
  int lock_partition_or_subpartition(ObTxDesc &tx_desc,
                                     const ObTxParam &tx_param,
                                     ObLockPartitionRequest &arg);
  int lock(ObTxDesc &tx_desc,
           const ObTxParam &tx_param,
           const ObLockRequest &arg,
           const bool is_for_replace = false);
  int unlock(ObTxDesc &tx_desc,
             const ObTxParam &tx_param,
             const ObUnLockRequest &arg);
  // NOTICE: has the same restrictions as the lock interface mentioned above.
  int replace_lock(ObTxDesc &tx_desc,
                   const ObTxParam &tx_param,
                   const ObReplaceLockRequest &replace_req);
  int replace_lock(ObTxDesc &tx_desc,
                   const ObTxParam &tx_param,
                   const ObReplaceAllLocksRequest &replace_req);
  int garbage_collect_right_now();
  int get_obj_lock_garbage_collector(ObOBJLockGarbageCollector *&obj_lock_garbage_collector);
  NamedLockManager &get_named_lock_manager() { return named_lock_manager_; }

private:
  bool need_retry_trans_(const ObTableLockCtx &ctx,
                         const int64_t ret) const;
  bool need_retry_single_task_(const ObTableLockCtx &ctx,
                               const int64_t ret) const;
  bool need_retry_whole_task_(const int ret);
  bool need_retry_partial_task_(const int ret,
                                const ObTableLockTaskResult *result) const;
  int rewrite_return_code_(const int ret, const int ret_code_before_end_stmt_or_tx = OB_SUCCESS, const bool is_from_sql = false) const;
  bool is_lock_conflict_ret_code_(const int ret) const;
  bool is_timeout_ret_code_(const int ret) const;
  bool is_can_retry_err_(const int ret) const;
  int process_lock_task_(ObTableLockCtx &ctx);
  int process_obj_lock_task_(ObTableLockCtx &ctx);
  int process_table_lock_task_(ObTableLockCtx &ctx);
  int process_tablet_lock_task_(ObTableLockCtx &ctx,
                                const ObSimpleTableSchemaV2 *table_schema);
  int process_alone_tablet_lock_task_(ObTableLockCtx &ctx);
  int start_tx_(ObTableLockCtx &ctx);
  int end_tx_(ObTableLockCtx &ctx, const bool is_rollback);
  int start_sub_tx_(ObTableLockCtx &ctx);
  int end_sub_tx_(ObTableLockCtx &ctx, const bool is_rollback);
  int start_stmt_(ObTableLockCtx &ctx);
  int end_stmt_(ObTableLockCtx &ctx, const bool is_rollback);
  int check_op_allowed_(const uint64_t table_id,
                        const ObSimpleTableSchemaV2 *table_schema,
                        bool &is_allowed);
  int get_process_tablets_(const ObSimpleTableSchemaV2 *table_schema,
                           ObTableLockCtx &ctx);
  int get_tablet_lock_set_(const ObTableLockMode lock_mode, ObTableLockCtx &ctx, ObLockSet &tablet_lock_set);
  int get_lock_set_(ObTableLockCtx &ctx,
                       const common::ObTabletIDArray &tablets,
                       ObLockSet &lock_set);
  int get_lock_set_(ObTableLockCtx &ctx,
                       const ObLockID &lock_id,
                       ObLockSet &lock_set);
  int get_lock_set_(ObTableLockCtx &ctx,
                       const common::ObIArray<ObLockID> &lock_ids,
                       ObLockSet &lock_set);
  int fill_lock_set_(ObTableLockCtx &ctx,
                        const ObLockIDArray &lock_ids,
                        ObLockSet &lock_set);
  int fill_lock_set_(ObTableLockCtx &ctx,
                        const common::ObTabletIDArray &tablets,
                        ObLockSet &lock_set);
  int pack_batch_request_(ObTableLockCtx &ctx,
                          const ObTableLockTaskType task_type,
                          const ObLockIDArray &lock_ids,
                          ObLockTaskBatchRequest<ObLockParam> &request);
  int pack_batch_request_(ObTableLockCtx &ctx,
                          const ObTableLockTaskType task_type,
                          const ObLockIDArray &lock_ids,
                          ObLockTaskBatchRequest<ObReplaceLockParam> &request);
  template<class LocalExecutor>
  int execute_lock_set_(LocalExecutor &executor,
                        ObTableLockCtx &ctx,
                        const ObLockSet &lock_map);
  template<class LocalExecutor>
  int execute_lock_set_(LocalExecutor &executor,
                        ObTableLockCtx &ctx,
                        const ObLockSet &lock_set,
                        bool &can_retry,
                        ObLockSet &retry_lock_set);
  template<class LocalExecutor>
  int handle_task_result_(LocalExecutor &executor,
                          ObTableLockCtx &ctx,
                          const ObLockSet &lock_set,
                          bool &can_retry,
                          ObRetryCtx &retry_ctx);
  template<class LocalExecutor>
  int execute_lock_set_in_batches_(LocalExecutor &executor,
                                   ObTableLockCtx &ctx,
                                   const ObLockSet &lock_set);
  template<class LocalExecutor>
  int execute_lock_set_in_batches_(LocalExecutor &executor,
                                   ObTableLockCtx &ctx,
                                   const ObLockSet &lock_set,
                                   bool &can_retry,
                                   ObLockSet &retry_lock_set);
  template<class LocalExecutor>
  int execute_lock_set_once_(LocalExecutor &executor,
                             ObTableLockCtx &ctx,
                             const ObLockSet &lock_set,
                             ObRetryCtx &retry_ctx);
  template<class LocalExecutor>
  int execute_one_lock_task_(LocalExecutor &executor,
                             ObTableLockCtx &ctx,
                             const ObLockIDArray &lock_ids,
                             ObRetryCtx &retry_ctx);
  template<class LocalExecutor>
  int execute_lock_task_(LocalExecutor &executor,
                         ObTableLockCtx &ctx,
                         const ObLockIDArray &lock_ids,
                         ObRetryCtx &retry_ctx);
  template<class LocalExecutor>
  int pack_and_execute_task_(LocalExecutor &executor,
                             ObTableLockCtx &ctx,
                             const ObLockIDArray &lock_ids,
                             ObRetryCtx &retry_ctx);
  template<>
  int pack_and_execute_task_(ObLocalBatchLockExecutor<ObLockTaskBatchRequest<ObReplaceLockParam>> &executor,
                             ObTableLockCtx &ctx,
                             const ObLockIDArray &lock_ids,
                             ObRetryCtx &retry_ctx);
  int get_retry_lock_ids_(const ObLockIDArray &lock_ids,
                          const int64_t start_pos,
                          ObLockIDArray &retry_lock_ids);
	  int get_retry_lock_ids_(const ObLockSet &lock_set,
	                          const int64_t start_pos,
	                          ObLockIDArray &retry_lock_ids);
	  int collect_rollback_info_(ObTableLockCtx &ctx);
	  int collect_rollback_info_(const ObRetryCtx &retry_ctx,
	                             ObTableLockCtx &ctx);
  int inner_process_obj_lock_batch_(ObTableLockCtx &ctx,
                                    const ObLockSet &lock_set);
  int process_obj_lock_(ObTableLockCtx &ctx,
                        const ObLockSet &lock_set);
  int process_obj_lock_with_prio_(ObTableLockCtx &ctx,
                                  const ObLockSet &lock_set);
  static bool is_part_table_lock_(const ObTableLockTaskType task_type);
  int get_table_lock_mode_(const ObTableLockTaskType task_type,
                           const ObTableLockMode part_lock_mode,
                           ObTableLockMode &table_lock_mode);
  int process_table_tablet_lock_(ObTableLockCtx &ctx,
                                 const ObTableLockMode lock_mode,
                                 const ObTableLockMode table_lock_mode,
                                 const ObLockSet &lock_set);
  int process_table_tablet_lock_with_prio_(ObTableLockCtx &ctx,
                                           const ObTableLockMode lock_mode,
                                           const ObTableLockMode table_lock_mode,
                                           const ObLockSet &table_lock_set);
  // only useful in LOCK_TABLE/LOCK_PARTITION
  int pre_check_lock_(ObTableLockCtx &ctx,
                      const ObLockSet &lock_set);
  int batch_pre_check_lock_(ObTableLockCtx &ctx,
                            const ObLockSet &lock_set);
  // used by deadlock detector.
  int deal_with_deadlock_(ObTableLockCtx &ctx);
  int get_table_partition_level_(const ObTableID table_id, ObPartitionLevel &part_level);
  int get_table_schema_(const ObTableLockCtx &ctx,
                        common::ObIAllocator &allocator,
                        ObSimpleTableSchemaV2 *&table_schema);

  DISALLOW_COPY_AND_ASSIGN(ObTableLockService);
private:
  static const int64_t DEFAULT_TIMEOUT_US = 1500L * 1000L * 1000L; // 1500s

  common::ObMySQLProxy *sql_proxy_;
  query::ObIDeadlockSessionService *session_service_;
  ObOBJLockGarbageCollector obj_lock_garbage_collector_;
  NamedLockManager named_lock_manager_;
  bool is_inited_;
};
}
}
}

#endif /* OCEANBASE_STORAGE_TABLELOCK_OB_TABLE_LOCK_SERVICE_H_ */
