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

#ifndef OCEANBASE_SQL_EXECUTOR_OB_TASK_
#define OCEANBASE_SQL_EXECUTOR_OB_TASK_

#include "share/ob_define.h"
#include "sql/executor/ob_job.h"
#include "lib/ob_name_id_def.h"
#include "share/rpc/ob_batch_proxy.h"

namespace oceanbase
{
namespace sql
{
class ObExecContext;
class ObPhysicalPlan;
class ObJob;
class ObTaskInfo;
class ObOpSpec;
/*
 * Serialize a task to be executed remotely, need to specify the following:
 * 1. Operator Tree
 * 2. Operator Input Array
 * 3. Query ID, Job ID, Task ID
 */
class ObTask
{
  OB_UNIS_VERSION_V(1);
public:
  ObTask();
  virtual ~ObTask();
  void set_ob_task_id(const ObTaskID &ob_task_id) { ob_task_id_ = ob_task_id; }
  const ObTaskID &get_ob_task_id() const { return ob_task_id_; }
  void set_location_idx(int64_t location_idx) { location_idx_ = location_idx; }
  int64_t get_location_idx() const { return location_idx_; }
  void set_runner_server(const common::ObAddr &svr) { runner_svr_ = svr; }
  const common::ObAddr &get_runner_server() const { return runner_svr_; }
  void set_ctrl_server(const common::ObAddr &svr) { ctrl_svr_ = svr; }
  const common::ObAddr &get_ctrl_server() const { return ctrl_svr_; }
  void set_job(ObJob &job) { job_ = &job; }
  void set_serialize_param(ObExecContext *exec_ctx,
                           ObOpSpec *op_root,
                           const ObPhysicalPlan *ser_phy_plan);
  void set_deserialize_param(ObExecContext &exec_ctx, ObPhysicalPlan &des_phy_plan);
  ObOpSpec *get_root_spec() const { return root_spec_; }
  int load_code();
  const ObPhysicalPlan& get_des_phy_plan() const { return *des_phy_plan_; }
  const ObPhysicalPlan& get_ser_phy_plan() const { return *ser_phy_plan_; }
  ObExecContext *get_exec_context() const { return exec_ctx_; }
  inline void set_exec_context(ObExecContext *exec_ctx) { exec_ctx_ = exec_ctx; }
  inline void set_ser_phy_plan(const ObPhysicalPlan *ser_phy_plan) { ser_phy_plan_ = ser_phy_plan; }


  int add_range(const common::ObNewRange &range)
  {
    return ranges_.push_back(range);
  }

  void set_max_sql_no(int64_t max_sql_no) { max_sql_no_ = max_sql_no; }
  const common::ObIArray<ObNewRange> &get_ranges() const { return ranges_; }
  int assign_ranges(const ObIArray<ObNewRange> &ranges);
  const ObString get_sql_string() const { return ObString(sql_string_); }
  void set_sql_string(const ObString &sql_string)
  {
    if (0 == sql_string.length()) {
      sql_string_[0] = '\0';
    } else {
      int64_t str_size = min(sql_string.length(), common::OB_TINY_SQL_LENGTH);
      STRNCPY(sql_string_, sql_string.ptr(), str_size);
      sql_string_[str_size] = '\0';
    }
  }

  TO_STRING_KV(N_OB_TASK_ID, ob_task_id_,
               K_(runner_svr),
               K_(ctrl_svr),
               K_(ranges),
               K_(location_idx));
  DECLARE_TO_YSON_KV;
protected:
  //TODO: Xiao Chu
  // ObTask sent to the remote end, the remote end needs the following information:
  //1. task location info
  //2. transaction control
  //3. scan range
  //4. ?
  ObExecContext *exec_ctx_;
  const ObPhysicalPlan *ser_phy_plan_;
  ObPhysicalPlan *des_phy_plan_;
  ObOpSpec *root_spec_;
  ObJob *job_;
  // ObTask target machine to send to
  common::ObAddr runner_svr_;
  // Master address, used to report status to the master after remote execution
  common::ObAddr ctrl_svr_;
  // The addressing information of this Task
  ObTaskID ob_task_id_;
  int64_t location_idx_;
  // This Task involves the scan range, default involving one table (one or more partitions)
  common::ObSEArray<ObNewRange, 32> ranges_;
  int64_t max_sql_no_;
  char sql_string_[common::OB_TINY_SQL_LENGTH + 1];
  //DISALLOW_COPY_AND_ASSIGN(ObTask);
};

inline void ObTask::set_serialize_param(ObExecContext *exec_ctx,
                                        ObOpSpec *root_spec,
                                        const ObPhysicalPlan *ser_phy_plan)
{
  exec_ctx_ = exec_ctx;
  root_spec_ = root_spec;
  ser_phy_plan_ = ser_phy_plan;
}

inline void ObTask::set_deserialize_param(ObExecContext &exec_ctx, ObPhysicalPlan &des_phy_plan)
{
  exec_ctx_ = &exec_ctx;
  des_phy_plan_ = &des_phy_plan;
}

class ObMiniTask : public ObTask
{
  OB_UNIS_VERSION_V(1);
public:
  ObMiniTask()
    : ObTask(),
      extend_root_spec_(NULL)
  {}
  ~ObMiniTask() {}

  inline void set_extend_root_spec(ObOpSpec *spec) { extend_root_spec_ = spec; }
  inline const ObOpSpec *get_extend_root_spec() const { return extend_root_spec_; }

  TO_STRING_KV(K_(ob_task_id),
               K_(runner_svr),
               K_(ctrl_svr),
               K_(ranges),
               K_(location_idx),
               KP_(extend_root_spec));
private:
  // For mini task, allow executing an extension plan along with the main plan,
  // This mainly used when conflict checking, to get the unique index conflict row primary key when,
  // You can also bring back the other column information of the conflicting rowkey for the primary key and local unique index, optimizing the number of RPC calls
  ObOpSpec *extend_root_spec_;
};

class ObPingSqlTask
{
  OB_UNIS_VERSION_V(1);
public:
  ObPingSqlTask();
  virtual ~ObPingSqlTask();
  TO_STRING_KV(K(trans_id_),
               K(sql_no_),
               K(task_id_),
               K(exec_svr_),
               K(cur_status_));
public:
  transaction::ObTransID trans_id_;
  uint64_t sql_no_;
  ObTaskID task_id_;
  common::ObAddr exec_svr_;
  int64_t cur_status_;
};

class ObPingSqlTaskResult
{
  OB_UNIS_VERSION_V(1);
public:
  ObPingSqlTaskResult();
  virtual ~ObPingSqlTaskResult();
  TO_STRING_KV(K(err_code_),
               K(ret_status_));
public:
  int err_code_;
  int64_t ret_status_;
};

class ObDesExecContext;
class ObRemoteTask : public obrpc::ObIFill
{
  OB_UNIS_VERSION(1);
public:
  ObRemoteTask() :
    tenant_schema_version_(-1),
    sys_schema_version_(-1),
    runner_svr_(),
    ctrl_svr_(),
    task_id_(),
    remote_sql_info_(nullptr),
    session_info_(NULL),
    exec_ctx_(NULL),
    inner_alloc_("RemoteTask"),
    dependency_tables_(&inner_alloc_),
    snapshot_(),
    ls_list_()
  {
  }
  ~ObRemoteTask() = default;

  void set_query_schema_version(int64_t tenant_schema_version, int64_t sys_schema_version)
  {
    tenant_schema_version_ = tenant_schema_version;
    sys_schema_version_ = sys_schema_version;
  }
  int64_t get_tenant_schema_version() const { return tenant_schema_version_; }
  int64_t get_sys_schema_version() const { return sys_schema_version_; }
  int assign_ls_list(const share::ObLSArray ls_ids);
  const share::ObLSArray &get_all_ls() const { return ls_list_; }
  bool check_ls_list(share::ObLSArray &ls_ids) const;
  void set_session(ObSQLSessionInfo *session_info) { session_info_ = session_info; }
  void set_runner_svr(const common::ObAddr &runner_svr) { runner_svr_ = runner_svr; }
  const common::ObAddr &get_runner_svr() const { return runner_svr_; }
  void set_ctrl_server(const common::ObAddr &ctrl_svr) { ctrl_svr_ = ctrl_svr; }
  const common::ObAddr &get_ctrl_server() const { return ctrl_svr_; }
  void set_task_id(const ObTaskID &task_id) { task_id_ = task_id; }
  const ObTaskID &get_task_id() const { return task_id_; }
  void set_remote_sql_info(ObRemoteSqlInfo *remote_sql_info) { remote_sql_info_ = remote_sql_info; }
  const ObRemoteSqlInfo *get_remote_sql_info() const { return remote_sql_info_; }
  void set_exec_ctx(ObDesExecContext *exec_ctx) { exec_ctx_ = exec_ctx; }
  int assign_dependency_tables(const DependenyTableStore &dependency_tables);
  const DependenyTableStore &get_dependency_tables() const {
    return dependency_tables_;
  }
  int set_snapshot(const transaction::ObTxReadSnapshot &snapshot) { return snapshot_.assign(snapshot); }
  const transaction::ObTxReadSnapshot &get_snapshot() const { return snapshot_; }
  int fill_buffer(char* buf, int64_t size, int64_t &filled_size) const
  {
    filled_size = 0;
    return serialize(buf, size, filled_size);
  }
  int64_t get_req_size() const { return get_serialize_size(); }
  TO_STRING_KV(K_(tenant_schema_version),
               K_(sys_schema_version),
               K_(runner_svr),
               K_(ctrl_svr),
               K_(task_id),
               KPC_(remote_sql_info),
               K_(snapshot),
               K_(ls_list));
  DECLARE_TO_YSON_KV;
private:
  int64_t tenant_schema_version_;
  int64_t sys_schema_version_;
  // ObTask target machine to send to
  common::ObAddr runner_svr_;
  // Master address, used to report status to the master after remote execution
  common::ObAddr ctrl_svr_;
  // The addressing information of this Task
  ObTaskID task_id_;
  // Remote execution of the corresponding templated SQL text
  ObRemoteSqlInfo *remote_sql_info_;
  // Execute involved session info, mainly transaction status information in the session
  ObSQLSessionInfo *session_info_;
  ObDesExecContext *exec_ctx_;
  common::ModulePageAllocator inner_alloc_;
  DependenyTableStore dependency_tables_;
  transaction::ObTxReadSnapshot snapshot_;
  // remote execution pre-save ls
  share::ObLSArray ls_list_;
};
}
}
#endif /* OCEANBASE_SQL_EXECUTOR_OB_TASK_ */
//// end of header file
