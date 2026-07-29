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

#ifndef OCEANBASE_SQL_TASK_EXECUTOR_CTX_
#define OCEANBASE_SQL_TASK_EXECUTOR_CTX_

#include "share/ob_autoincrement_service.h"
#include "sql/executor/ob_execute_result.h"
#include "sql/ob_sql_context.h"
#include "lib/worker.h"
#include "lib/list/ob_list.h"
#include "sql/das/ob_das_ref.h"
namespace oceanbase
{
namespace common
{
class ObITabletScan;
}

namespace obcall
{
}

namespace sql
{

typedef common::ObIArray<ObPhyTableLocation> ObPhyTableLocationIArray;
typedef common::ObIArray<ObCandiTableLoc> ObCandiTableLocIArray;
typedef common::ObSEArray<ObPhyTableLocation, 2> ObPhyTableLocationFixedArray;

class ObExecContext;
class ObTaskExecutorCtx
{
  OB_UNIS_VERSION(1);
public:
  class CalcVirtualPartitionIdParams
  {
  public:
    CalcVirtualPartitionIdParams() : inited_(false), ref_table_id_(common::OB_INVALID_ID) {}
    ~CalcVirtualPartitionIdParams() {}

    int init(uint64_t ref_table_id);
    inline void reset() { inited_ = false; ref_table_id_ = common::OB_INVALID_ID; }
    inline bool is_inited() const { return inited_; }
    inline uint64_t get_ref_table_id() const { return ref_table_id_; }

    TO_STRING_KV(K_(inited), K_(ref_table_id));
  private:
    bool inited_;
    uint64_t ref_table_id_;
  };

  explicit ObTaskExecutorCtx(ObExecContext &exec_context);
  virtual ~ObTaskExecutorCtx();

  inline ObExecuteResult &get_execute_result()
  {
    return execute_result_;
  }
  inline common::ObITabletScan *get_vt_partition_service()
  {
    return GCTX.vt_par_ser_;
  }
  inline void set_query_begin_schema_version(const int64_t schema_version)
  {
    query_begin_schema_version_ = schema_version;
  }
  inline int64_t get_query_begin_schema_version() const
  {
    return query_begin_schema_version_;
  }
  // init_calc_virtual_part_id_params and reset_calc_virtual_part_id_params should be used in pairs,
  // Otherwise the calc_virtual_partition_id function is prone to errors;
  // Involving the calc function when addr_to_part_id function runs or calc_virtual_partition_id function runs
  // Only need to use init_calc_virtual_part_id_params and reset_calc_virtual_part_id_params
  inline int init_calc_virtual_part_id_params(uint64_t ref_table_id)
  {
    return calc_params_.init(ref_table_id);
  }
  inline void reset_calc_virtual_part_id_params()
  {
    calc_params_.reset();
  }
  inline const CalcVirtualPartitionIdParams &get_calc_virtual_part_id_params() const
  {
    return calc_params_;
  }
  inline void set_retry_times(int64_t retry_times)
  {
    retry_times_ = retry_times;
  }
  inline int64_t get_retry_times() const
  {
    return retry_times_;
  }
  void set_sys_job_id(const int64_t id) { sys_job_id_ = id; }
  int64_t get_sys_job_id() const { return sys_job_id_; }

  ObExecContext *get_exec_context() const { return exec_ctx_; }

  void set_expected_worker_cnt(int64_t cnt) { expected_worker_cnt_ = cnt; }
  int64_t get_expected_worker_cnt() const { return expected_worker_cnt_; }
  void set_minimal_worker_cnt(int64_t cnt) { minimal_worker_cnt_ = cnt; }
  int64_t get_minimal_worker_cnt() const { return minimal_worker_cnt_; }
  void set_admited_worker_cnt(int64_t cnt) { admited_worker_cnt_ = cnt; } // alias
  int64_t get_admited_worker_cnt() const { return admited_worker_cnt_; } // alias
  // try to trigger a location update task and clear location in cache,
  // if it is limited by the limiter and not be done, is_limited will be set to true

private:
  // BEGIN local local variable
  // Used to encapsulate the Op Tree of the top-level Job of executor, outputting data externally
  ObExecuteResult execute_result_;
  // Used for temporarily passing parameters when calculating the partition id of a virtual table, it's best to reset this member variable after calculation
  CalcVirtualPartitionIdParams calc_params_;
  //
  ObExecContext *exec_ctx_;
  // PX records the expected number of threads required for the entire Query, as well as the actual number of threads allocated
  int64_t expected_worker_cnt_; // query expected worker count computed by optimizer
  int64_t minimal_worker_cnt_;  // minimal worker count to support execute this query
  int64_t admited_worker_cnt_; // query final used worker count admitted by admission
  // END local local variable
  // The number of retries
  int64_t retry_times_;
  int64_t sys_job_id_;
public:
  // BEGIN global singleton variable
  //
  int64_t query_begin_schema_version_; // Latest global database schema version at query start
  share::schema::ObMultiVersionSchemaService *schema_service_;
  //
  // END global singleton variable


  DISALLOW_COPY_AND_ASSIGN(ObTaskExecutorCtx);
  TO_STRING_KV(K(retry_times_), K(expected_worker_cnt_),
      K(admited_worker_cnt_), K(query_begin_schema_version_),
      K(minimal_worker_cnt_));
};

class ObTaskExecutorCtxUtil
{
public:
  template<typename DEST_TYPE, typename SRC_TYPE>
  static int merge_task_result_meta(DEST_TYPE &dest, const SRC_TYPE &task_meta);
}; /* class ObTaskExecutorCtxUtil */

template<typename DEST_TYPE, typename SRC_TYPE>
int ObTaskExecutorCtxUtil::merge_task_result_meta(DEST_TYPE &dest, const SRC_TYPE &task_meta)
{
  int ret  = common::OB_SUCCESS;
  dest.set_affected_rows(dest.get_affected_rows() + task_meta.get_affected_rows());
  dest.set_found_rows(dest.get_found_rows() + task_meta.get_found_rows());
  dest.set_row_matched_count(dest.get_row_matched_count() + task_meta.get_row_matched_count());
  dest.set_row_duplicated_count(dest.get_row_duplicated_count() + task_meta.get_row_duplicated_count());
  dest.set_last_insert_id_session(task_meta.get_last_insert_id_session());
  dest.set_last_insert_id_changed(task_meta.get_last_insert_id_changed());
  if (!task_meta.is_result_accurate()) {
    dest.set_is_result_accurate(task_meta.is_result_accurate());
  }
  return ret;
}
}
}
#endif /* OCEANBASE_SQL_TASK_EXECUTOR_CTX_ */
//// end of header file
