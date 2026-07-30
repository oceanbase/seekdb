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

#ifndef OCEANBASE_ROOTSERVER_OB_DDL_SCHEDULER_H_
#define OCEANBASE_ROOTSERVER_OB_DDL_SCHEDULER_H_

#include "share/ob_ddl_task_executor.h"
#include "share/scn.h" //for SCN
#include "rootserver/ddl_task/ob_ddl_task.h"
#include "rootserver/ddl_task/ob_column_redefinition_task.h"
#include "rootserver/ddl_task/ob_constraint_task.h"
#include "rootserver/ddl_task/ob_ddl_redefinition_task.h"
#include "rootserver/ddl_task/ob_ddl_retry_task.h"
#include "rootserver/ddl_task/ob_drop_index_task.h"
#include "rootserver/ddl_task/ob_drop_primary_key_task.h"
#include "rootserver/ddl_task/ob_index_build_task.h"
#include "rootserver/ddl_task/ob_modify_autoinc_task.h"
#include "rootserver/ddl_task/ob_table_redefinition_task.h"
#include "rootserver/ob_server_thread_helper.h"
#include "rootserver/ob_thread_idling.h"
#include "lib/container/ob_se_array.h"
#include "lib/hash/ob_hashmap.h"
#include "lib/lock/ob_spin_lock.h"
#include "lib/profile/ob_trace_id.h"
#include "lib/task/ob_timer.h"

namespace oceanbase
{
using namespace share;
namespace share
{
class SCN;
namespace schema
{
class ObTableSchema;
}
}
namespace common
{
class ObMySQLTransaction;
namespace sqlclient
{
class ObMySQLResult;
}
}
namespace rootserver
{
class ObDDLTaskQueue
{
public:
  ObDDLTaskQueue();
  virtual ~ObDDLTaskQueue();
  int init(const int64_t bucket_num);
  bool has_set_stop() const { return ATOMIC_LOAD(&stop_); }
  void set_stop(bool stop) { ATOMIC_STORE(&stop_, stop); }
  int push_task(ObDDLTask *task);
  int get_next_task(ObDDLTask *&task);
  int remove_task(ObDDLTask *task);
  int add_task_to_last(ObDDLTask *task);
  template<typename F>
  int modify_task(const ObDDLTaskKey &task_key, F &&op);
  template<typename F>
  int modify_task(const ObDDLTaskID &task_id, F &&op);
  template<typename F>
  int get_task(const ObDDLTaskKey &task_key, F &&op);
  int update_task_copy_deps_setting(const ObDDLTaskID &task_id,
                                    const bool is_copy_constraints,
                                    const bool is_copy_indexes,
                                    const bool is_copy_triggers,
                                    const bool is_copy_foreign_keys,
                                    const bool is_ignore_errors);
  int update_task_process_schedulable(const ObDDLTaskID &task_id);
  int update_task_ret_code(const ObDDLTaskID &task_id, const int ret_code);
  int abort_task(const ObDDLTaskID &task_id);
  int64_t get_task_cnt() const { return task_list_.get_size(); }
  void destroy();
private:
  typedef common::ObDList<ObDDLTask> TaskList;
  typedef common::hash::ObHashMap<ObDDLTaskKey, ObDDLTask *,
          common::hash::NoPthreadDefendMode> TaskKeyMap;
  typedef common::hash::ObHashMap<ObDDLTaskID, ObDDLTask *,
          common::hash::NoPthreadDefendMode> TaskIdMap;
  TaskList task_list_;
  TaskKeyMap task_map_;
  TaskIdMap task_id_map_;
  common::ObSpinLock lock_;
  bool stop_;
  bool is_inited_;
};

class ObDDLTaskHeartBeatMananger final
{
public:
  ObDDLTaskHeartBeatMananger();
  ~ObDDLTaskHeartBeatMananger();
  int init();
  int update_task_active_time(const ObDDLTaskID &task_id);
  int remove_task(const ObDDLTaskID &task_id);
  int get_inactive_ddl_task_ids(ObArray<ObDDLTaskID>& remove_task_ids);
private:
  struct TaskActiveTime final
  {
    TaskActiveTime() : task_id_(), active_time_(0) {}
    TaskActiveTime(const ObDDLTaskID &task_id, const int64_t active_time)
      : task_id_(task_id), active_time_(active_time) {}
    TO_STRING_KV(K_(task_id), K_(active_time));
    ObDDLTaskID task_id_;
    int64_t active_time_;
  };
  common::ObSEArray<TaskActiveTime, 4> register_task_times_;
  bool is_inited_;
  common::ObSpinLock lock_;
};
struct ObPrepareAlterTableArgParam final
{
public:
  ObPrepareAlterTableArgParam() :
    session_id_(common::OB_INVALID_ID),
    sql_mode_(0),
    tz_info_wrap_(),
    allocator_(lib::ObLabel("PrepAlterTblArg")),
    foreign_key_checks_(true)
  {}
  ~ObPrepareAlterTableArgParam() = default;
  int init(const uint64_t session_id,
          const ObSQLMode &sql_mode,
          const ObString &ddl_stmt_str,
          const ObString &orig_table_name,
          const ObString &orig_database_name,
          const ObString &target_database_name,
          const ObTimeZoneInfoWrap &tz_info_wrap,
          const bool foreign_key_checks);
  bool is_valid() const
  {
    return !orig_table_name_.empty() &&
            !orig_database_name_.empty() &&
            !target_database_name_.empty();
  }
  TO_STRING_KV(K_(session_id),
                K_(sql_mode),
                K_(ddl_stmt_str),
                K_(orig_table_name),
                K_(orig_database_name),
                K_(target_database_name),
                K_(tz_info_wrap),
                K_(foreign_key_checks));
public:
  uint64_t session_id_;
  ObSQLMode sql_mode_;
  common::ObString ddl_stmt_str_;
  common::ObString orig_table_name_;
  common::ObString orig_database_name_;
  common::ObString target_database_name_;
  common::ObTimeZoneInfoWrap tz_info_wrap_;
  common::ObArenaAllocator allocator_;
  bool foreign_key_checks_;
};

class ObRedefCallback
{
public:
  ObRedefCallback() : infos_(nullptr) {}
  virtual ~ObRedefCallback() = default;

  virtual int modify_info(ObTableRedefinitionTask &redef_task,
                          ObDDLTaskQueue &task_queue,
                          ObISQLClient &trans);
  virtual int update_redef_task_info(ObTableRedefinitionTask& redef_task) = 0;
  virtual int update_task_info_in_queue(ObTableRedefinitionTask& redef_task,
                                      ObDDLTaskQueue &ddl_task_queue) = 0;
protected:
  common::hash::ObHashMap<ObString, bool> *infos_;
};

class ObAbortRedefCallback : public ObRedefCallback
{
public:
  ObAbortRedefCallback() = default;
  virtual ~ObAbortRedefCallback() = default;
  virtual int update_redef_task_info(ObTableRedefinitionTask& redef_task) override;
  virtual int update_task_info_in_queue(ObTableRedefinitionTask& redef_task,
                                      ObDDLTaskQueue &ddl_task_queue) override;
};

class ObCopyTableDepCallback : public ObRedefCallback
{
public:
  ObCopyTableDepCallback() = default;
  virtual ~ObCopyTableDepCallback() = default;
  virtual int update_redef_task_info(ObTableRedefinitionTask& redef_task) override;
  virtual int update_task_info_in_queue(ObTableRedefinitionTask& redef_task,
                                      ObDDLTaskQueue &ddl_task_queue) override;
  int set_infos(common::hash::ObHashMap<ObString, bool> *infos);
};

class ObFinishRedefCallback : public ObRedefCallback
{
public:
  ObFinishRedefCallback() = default;
  virtual ~ObFinishRedefCallback() = default;
  virtual int update_redef_task_info(ObTableRedefinitionTask& redef_task) override;
  virtual int update_task_info_in_queue(ObTableRedefinitionTask& redef_task,
                                      ObDDLTaskQueue &ddl_task_queue) override;
};

class ObUpdateSSTableCompleteStatusCallback : public ObRedefCallback
{
public:
  ObUpdateSSTableCompleteStatusCallback()
    : ret_code_(common::OB_SUCCESS)
  {}
  ~ObUpdateSSTableCompleteStatusCallback() = default;
  void set_ret_code (const int ret_code) { ret_code_ = ret_code; }
  int get_ret_code() const { return ret_code_; }
  virtual int update_redef_task_info(ObTableRedefinitionTask& redef_task) override;
  virtual int update_task_info_in_queue(ObTableRedefinitionTask& redef_task,
                                      ObDDLTaskQueue &ddl_task_queue) override;
private:
  int ret_code_;
};

/*
 * the only scheduler for all ddl tasks executed in local DDL service
 *
 * each category of ddl request has an unique task type.
 * every ddl task has its record in an inner table(__all_ddl_task_status),
 * which will be used to recover or cleanup the task when the root server has switched
 */
class ObDDLScheduler : public rootserver::ObServerThreadHelper,
                       public logservice::ObICheckpointSubHandler,
                       public logservice::ObIReplaySubHandler
{
public:
#ifdef ERRSIM
    static const int64_t DDL_TASK_SCAN_PERIOD = 1000L * 1000L; // 1s
#else
    static const int64_t DDL_TASK_SCAN_PERIOD = 60 * 1000L * 1000L; // 60s
#endif
public:
  ObDDLScheduler();
  virtual ~ObDDLScheduler();

  int init();
  void stop();
  void destroy();
  inline bool is_stoped() const { return is_stop_; }

  virtual void do_work() override;
  virtual share::SCN get_rec_scn() override { return share::SCN::max_scn(); }
  virtual int flush(SCN &rec_scn) override { return OB_SUCCESS; }
  int replay(const void *buffer, const int64_t nbytes, const palf::LSN &lsn, const share::SCN &scn) override
  {
    UNUSED(buffer);
    UNUSED(nbytes);
    UNUSED(lsn);
    UNUSED(scn);
    return OB_SUCCESS;
  }
  // for role change
  void deactivate() override;
  int activate() override;

  // server_module_functions
  static int server_module_init(ObDDLScheduler *&ddl_scheduler);
  static void server_module_stop(ObDDLScheduler *&ddl_scheduler);
  static void server_module_wait(ObDDLScheduler *&ddl_scheduler);

  int create_ddl_task(
      const ObCreateDDLTaskParam &param,
      common::ObISQLClient &proxy,
      ObDDLTaskRecord &task_record);

  int schedule_ddl_task(
      const ObDDLTaskRecord &task_record);
  int recover_task();
  int remove_inactive_ddl_task();

  int destroy_task();

  int on_column_checksum_calc_reply(
      const common::ObTabletID &tablet_id,
      const ObDDLTaskKey &task_key,
      const int ret_code);

  int on_sstable_complement_job_reply(
      const common::ObTabletID &tablet_id,
      const ObDDLTaskKey &task_key,
      const int64_t snapshot_version,
      const int64_t execution_id,
      const int ret_code,
      const ObDDLTaskInfo &addition_info);

  int on_ddl_task_finish(
      const ObDDLTaskID &parent_task_id,
      const ObDDLTaskKey &task_key,
      const int ret_code,
      const ObCurTraceId::TraceId &parent_task_trace_id);

  int notify_update_autoinc_end(
      const ObDDLTaskKey &task_key,
      const uint64_t autoinc_val,
      const int ret_code);
  int get_task_record(const ObDDLTaskID &task_id, 
                      ObISQLClient &trans,
                      ObDDLTaskRecord &task_record,
                      common::ObIAllocator &allocator);
  int modify_redef_task(const ObDDLTaskID &task_id, ObRedefCallback &cb);
  int abort_redef_table(const ObDDLTaskID &task_id);

  int copy_table_dependents(const ObDDLTaskID &task_id,
                            const bool is_copy_constraints,
                            const bool is_copy_indexes,
                            const bool is_copy_triggers,
                            const bool is_copy_foreign_keys,
                            const bool is_ignore_errors);
  int finish_redef_table(const ObDDLTaskID &task_id);
  int start_redef_table(const obcall::ObStartRedefTableArg &arg, obcall::ObStartRedefTableRes &res);
  int update_ddl_task_active_time(const ObDDLTaskID &task_id);
  int prepare_alter_table_arg(const ObPrepareAlterTableArgParam &param,
                              const ObTableSchema *target_table_schema,
                              obcall::ObAlterTableArg &alter_table_arg);
  inline share::ObDDLLocalBuilder &get_ddl_builder() { return ddl_builder_; }
private:
  class DDLIdling : public ObThreadIdling
  {
  public:
    explicit DDLIdling(volatile bool &stop): ObThreadIdling(stop) {}
    virtual ~DDLIdling() {}
    virtual int64_t get_idle_interval_us() override { return 30L * 1000L * 1000L; }
  };
  class DDLScanTask : public common::ObTimerTask
  {
  public:
    explicit DDLScanTask(ObDDLScheduler &ddl_scheduler): ddl_scheduler_(ddl_scheduler), timer_() {}
    virtual ~DDLScanTask() {};
    int init();
    int schedule();
    void server_module_thread_wait();
    void server_module_thread_stop();
    void destroy();
    bool task_exist() { return timer_.task_exist(*this); }
    int cancel() { return timer_.inited() ? timer_.cancel(*this) : OB_SUCCESS; }
  private:
    void runTimerTask() override;
  private:
    ObDDLScheduler &ddl_scheduler_;
    common::ObTimer timer_;
  };

  class HeartBeatCheckTask : public common::ObTimerTask
  {
  public:
    explicit HeartBeatCheckTask(ObDDLScheduler &ddl_scheduler): ddl_scheduler_(ddl_scheduler), timer_() {}
    virtual ~HeartBeatCheckTask() {};
    int init();
    int schedule();
    void server_module_thread_wait();
    void server_module_thread_stop();
    void destroy();
    bool task_exist() { return timer_.task_exist(*this); }
    int cancel() { return timer_.inited() ? timer_.cancel(*this) : OB_SUCCESS; }
  private:
    void runTimerTask() override;
  private:
#ifdef ERRSIM
    static const int64_t DDL_TASK_CHECK_PERIOD = 1000L * 1000L; // 1s
#else
    static const int64_t DDL_TASK_CHECK_PERIOD = 30 * 1000L * 1000L; // 30s
#endif
    ObDDLScheduler &ddl_scheduler_;
    common::ObTimer timer_;
  };
private:
  int insert_task_record(
      common::ObISQLClient &proxy,
      ObDDLTask &ddl_task,
      ObIAllocator &allocator,
      ObDDLTaskRecord &task_record);
  template<typename T>
  int alloc_ddl_task(T *&ddl_task);
  void free_ddl_task(ObDDLTask *ddl_task);
  void destroy_all_tasks();
  int inner_schedule_ddl_task(ObDDLTask *ddl_task,
                              const ObDDLTaskRecord &task_record);
  int create_build_index_task(
      common::ObISQLClient &proxy,
      const share::ObDDLType &ddl_type,
      const share::schema::ObTableSchema *data_table_schema,
      const share::schema::ObTableSchema *index_schema,
      const int64_t parallelism,
      const int64_t parent_task_id,
      const int32_t sub_task_trace_id,
      const obcall::ObCreateIndexArg *create_index_arg,
      const share::ObDDLType task_type,
      const uint64_t data_format_version,
      ObIAllocator &allocator,
      ObDDLTaskRecord &task_record,
      const int64_t snapshot_version = 0,
      const bool ddl_need_retry_at_executor = false);
  int create_build_fts_index_task(
      common::ObISQLClient &proxy,
      const share::schema::ObTableSchema *data_table_schema,
      const share::schema::ObTableSchema *index_schema,
      const int64_t parallelism,
      const int64_t parent_task_id,
      const uint64_t data_format_version,
      const obcall::ObCreateIndexArg *create_index_arg,
      ObIAllocator &allocator,
      ObDDLTaskRecord &task_record,
      int64_t snapshot_version = 0,
      const bool ddl_need_retry_at_executor = false);
  int create_build_vec_ivf_index_task(
      common::ObISQLClient &proxy,
      const share::schema::ObTableSchema *data_table_schema,
      const share::schema::ObTableSchema *index_schema,
      const int64_t parallelism,
      const int64_t parent_task_id,
      const share::ObDDLType task_type,
      const obcall::ObCreateIndexArg *create_index_arg,
      const uint64_t data_format_version,
      ObIAllocator &allocator,
      ObDDLTaskRecord &task_record);
  int create_build_vec_index_task(
      common::ObISQLClient &proxy,
      const share::schema::ObTableSchema *data_table_schema,
      const share::schema::ObTableSchema *index_schema,
      const int64_t parallelism,
      const int64_t parent_task_id,
      const obcall::ObCreateIndexArg *create_index_arg,
      const uint64_t data_format_version,
      ObIAllocator &allocator,
      ObDDLTaskRecord &task_record,
      const int64_t snapshot_version,
      const bool ddl_need_retry_at_executor = false);
  int create_constraint_task(
      common::ObISQLClient &proxy,
      const share::schema::ObTableSchema *table_schema,
      const int64_t constraint_id,
      const share::ObDDLType ddl_type,
      const int64_t schema_version,
      const obcall::ObAlterTableArg *arg,
      const int64_t parent_task_id,
      const int32_t sub_task_trace_id,
      ObIAllocator &allocator,
      ObDDLTaskRecord &task_record);
  int create_table_redefinition_task(
      common::ObISQLClient &proxy,
      const share::ObDDLType &type,
      const share::schema::ObTableSchema *src_schema,
      const share::schema::ObTableSchema *dest_schema,
      const int64_t parallelism,
      const int64_t parent_task_id,
      const int64_t task_id,
      const int32_t sub_task_trace_id,
      const obcall::ObAlterTableArg *alter_table_arg,
      const uint64_t data_format_version,
      const bool ddl_need_retry_at_executor,
      ObIAllocator &allocator,
      ObDDLTaskRecord &task_record);

  int create_drop_primary_key_task(
      common::ObISQLClient &proxy,
      const share::ObDDLType &type,
      const ObTableSchema *src_schema,
      const ObTableSchema *dest_schema,
      const int64_t parallelism,
      const int64_t task_id,
      const int32_t sub_task_trace_id,
      const obcall::ObAlterTableArg *alter_table_arg,
      const uint64_t data_format_version,
      ObIAllocator &allocator,
      ObDDLTaskRecord &task_record);

  int create_column_redefinition_task(
      common::ObISQLClient &proxy,
      const share::ObDDLType &type,
      const share::schema::ObTableSchema *src_schema,
      const share::schema::ObTableSchema *dest_schema,
      const int64_t parallelism,
      const int64_t task_id,
      const int32_t sub_task_trace_id,
      const obcall::ObAlterTableArg *alter_table_arg,
      const uint64_t data_format_version,
      ObIAllocator &allocator,
      ObDDLTaskRecord &task_record);

  int create_modify_autoinc_task(
      common::ObISQLClient &proxy,
      const int64_t table_id,
      const int64_t schema_version,
      const int64_t task_id,
      const int32_t sub_task_trace_id,
      const obcall::ObAlterTableArg *alter_table_arg,
      ObIAllocator &allocator,
      ObDDLTaskRecord &task_record);
  
  int create_rebuild_index_task(
      common::ObISQLClient &proxy,
      const share::ObDDLType &ddl_type,
      const ObTableSchema *index_schema,
      const int64_t parallelism,
      const int64_t parent_task_id,
      const int32_t sub_task_trace_id,
      const obcall::ObRebuildIndexArg *rebuild_index_arg,
      const uint64_t data_format_version,
      ObIAllocator &allocator,
      ObDDLTaskRecord &task_record);

  int create_drop_index_task(
      common::ObISQLClient &proxy,
      const share::ObDDLType &ddl_type,
      const share::schema::ObTableSchema *index_schema,
      const int64_t parent_task_id,
      const int32_t sub_task_trace_id,
      const obcall::ObDropIndexArg *drop_index_arg,
      ObIAllocator &allocator,
      ObDDLTaskRecord &task_record);

  int create_drop_fts_index_task(
      common::ObISQLClient &proxy,
      const share::schema::ObTableSchema *index_schema,
      const int64_t schema_version,
      const share::schema::ObTableSchema *rowkey_doc_schema,
      const share::schema::ObTableSchema *doc_rowkey_schema,
      const share::schema::ObTableSchema *domain_index_schema,
      const share::schema::ObTableSchema *doc_word_schema,
      const obcall::ObDropIndexArg *drop_index_arg,
      ObIAllocator &allocator,
      ObDDLTaskRecord &task_record);
  
  int create_drop_vec_ivf_index_task(
      common::ObISQLClient &proxy,
      const share::schema::ObTableSchema *index_schema,
      const int64_t schema_version,
      const share::ObDDLType task_type,
      const share::schema::ObTableSchema *centroid_schema_,
      const share::schema::ObTableSchema *cid_vector_schema_,
      const share::schema::ObTableSchema *rowkey_cid_schema,
      const share::schema::ObTableSchema *sq_meta_schema_,
      const share::schema::ObTableSchema *pq_centroid_schema_,
      const share::schema::ObTableSchema *pq_code_schema_,
      const uint64_t data_format_version,
      const obcall::ObDropIndexArg *drop_index_arg,
      ObIAllocator &allocator,
      ObDDLTaskRecord &task_record);

  int create_drop_vec_index_task(
      common::ObISQLClient &proxy,
      const share::schema::ObTableSchema *index_schema,
      const int64_t schema_version,
      const share::schema::ObTableSchema *vid_rowkey_schema_,
      const share::schema::ObTableSchema *rowkey_vid_schema_,
      const share::schema::ObTableSchema *domain_index_schema,
      const share::schema::ObTableSchema *delta_buffer_schema_,
      const share::schema::ObTableSchema *index_snapshot_data_schema_,
      const share::schema::ObTableSchema *embedded_vec_schema_,
      const uint64_t data_format_version,
      const obcall::ObDropIndexArg *drop_index_arg,
      ObIAllocator &allocator,
      ObDDLTaskRecord &task_record);

  int create_drop_lob_task(
      common::ObISQLClient &proxy,
      const ObCreateDDLTaskParam &param,
      const uint64_t aux_lob_meta_table_id,
      ObDDLTaskRecord &task_record);


  int create_fork_table_task(
      common::ObISQLClient &proxy,
      const share::schema::ObTableSchema *src_table_schema,
      const share::schema::ObTableSchema *dst_table_schema,
      const int64_t schema_version,
      const int64_t snapshot_version,
      const int64_t parent_task_id,
      const obcall::ObForkTableArg *fork_table_arg,
      ObIAllocator &allocator,
      ObDDLTaskRecord &task_record);

  int schedule_build_fts_index_task(
    const ObDDLTaskRecord &task_record);
  int schedule_build_vec_ivf_index_task(
      const ObDDLTaskRecord &task_record);
  int schedule_build_vec_index_task(
      const ObDDLTaskRecord &task_record);
  int schedule_build_index_task(
      const ObDDLTaskRecord &task_record);
  int schedule_drop_primary_key_task(const ObDDLTaskRecord &task_record);
  int schedule_table_redefinition_task(const ObDDLTaskRecord &task_record);
  int schedule_constraint_task(const ObDDLTaskRecord &task_record);
  int schedule_column_redefinition_task(const ObDDLTaskRecord &task_record);
  int schedule_modify_autoinc_task(const ObDDLTaskRecord &task_record);
  int schedule_drop_index_task(const ObDDLTaskRecord &task_record);
  int schedule_drop_vec_ivf_index_task(const ObDDLTaskRecord &task_record);
  int schedule_drop_vec_index_task(const ObDDLTaskRecord &task_record);
  int schedule_rebuild_index_task(const ObDDLTaskRecord &task_record);
  int schedule_drop_fts_index_task(const ObDDLTaskRecord &task_record);
  int schedule_drop_lob_task(const ObDDLTaskRecord &task_record);
  int schedule_ddl_retry_task(const ObDDLTaskRecord &task_record);
  int schedule_fork_table_task(const ObDDLTaskRecord &task_record);
  int add_sys_task(ObDDLTask *task);
  int remove_sys_task(ObDDLTask *task);
  int add_task_to_longops_mgr(ObDDLTask *ddl_task);
  int remove_task_from_longops_mgr(ObDDLTask *ddl_task);
  int remove_ddl_task(ObDDLTask *ddl_task);
  void add_event_info(const ObDDLTaskRecord &ddl_record, const ObString &ddl_event_stmt);
private:
  static const int64_t TOTAL_LIMIT = 1024L * 1024L * 1024L;
  static const int64_t HOLD_LIMIT = 8 * 1024L * 1024L;
  static const int64_t ALLOC_PAGE_SIZE = common::OB_MALLOC_NORMAL_BLOCK_SIZE;
  bool is_inited_;
  bool is_stop_;
  DDLIdling idler_;
  common::ObConcurrentFIFOAllocator allocator_;
  ObDDLTaskQueue task_queue_;
  ObDDLTaskHeartBeatMananger manager_reg_heart_beat_task_;
  DDLScanTask scan_task_;
  HeartBeatCheckTask heart_beat_check_task_;
  share::ObDDLLocalBuilder ddl_builder_;
};

template<typename T>
int ObDDLScheduler::alloc_ddl_task(T *&ddl_task)
{
  int ret = OB_SUCCESS;
  ddl_task = nullptr;
  void *tmp_buf = nullptr;
  if (OB_ISNULL(tmp_buf = allocator_.alloc(sizeof(T)))) {
    ret = common::OB_ALLOCATE_MEMORY_FAILED;
    RS_LOG(WARN, "alloc ddl task failed", K(ret));
  } else {
    ddl_task = new (tmp_buf) T;
  }
  return ret;
}


} // end namespace rootserver
} // end namespace oceanbase


#endif /* OCEANBASE_ROOTSERVER_OB_DDL_SCHEDULER_H_ */
