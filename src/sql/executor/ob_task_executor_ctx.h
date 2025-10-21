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
#include "share/ob_common_rpc_proxy.h"
#include "sql/executor/ob_executor_rpc_impl.h"
#include "sql/executor/ob_execute_result.h"
#include "sql/ob_phy_table_location.h"
#include "sql/optimizer/ob_table_partition_info.h"
#include "sql/ob_sql_context.h"
#include "lib/worker.h"
#include "lib/list/ob_list.h"
#include "sql/das/ob_das_ref.h"
namespace oceanbase
{
namespace common
{
class ObAddr;
class ObITabletScan;
}

namespace obrpc
{
class ObSrvRpcProxy;
class ObCommonRpcProxy;
}

namespace sql
{

typedef common::ObIArray<ObPhyTableLocation> ObPhyTableLocationIArray;
typedef common::ObIArray<ObCandiTableLoc> ObCandiTableLocIArray;
typedef common::ObSEArray<ObPhyTableLocation, 2> ObPhyTableLocationFixedArray;

class ObJobControl;
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

  int set_table_locations(const ObTablePartitionInfoArray &table_partition_infos);
  int append_table_location(const ObCandiTableLoc &phy_location_info);

  const ObTablePartitionInfoArray &get_partition_infos() const;
  inline RemoteExecuteStreamHandle* get_stream_handler()
  {
    return task_resp_handler_;
  }
  int reset_and_init_stream_handler();
  ObExecutorRpcImpl *get_task_executor_rpc()
  {
    return GCTX.executor_rpc_;
  }
  // @nijia.nj FIXme: this func should be replaced by
  // int get_common_rpc(obrpc::ObCommonRpcProxy *&)
  obrpc::ObCommonRpcProxy *get_common_rpc();

  int get_common_rpc(obrpc::ObCommonRpcProxy *&common_rpc_proxy);
  inline ObExecuteResult &get_execute_result()
  {
    return execute_result_;
  }
  inline common::ObITabletScan *get_vt_partition_service()
  {
    return GCTX.vt_par_ser_;
  }
  inline obrpc::ObSrvRpcProxy *get_srv_rpc()
  {
    return GCTX.srv_rpc_proxy_;
  }
  inline void set_query_tenant_begin_schema_version(const int64_t schema_version)
  {
    query_tenant_begin_schema_version_ = schema_version;
  }
  const common::ObAddr get_self_addr() const;
  inline int64_t get_query_tenant_begin_schema_version() const
  {
    return query_tenant_begin_schema_version_;
  }
  inline void set_query_sys_begin_schema_version(const int64_t schema_version)
  {
    query_sys_begin_schema_version_ = schema_version;
  }
  inline int64_t get_query_sys_begin_schema_version() const
  {
    return query_sys_begin_schema_version_;
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
  //FIXME qianfu compatibility code, remove this function after 1.4.0
  // Equals INVALID_CLUSTER_VERSION means it is serialized from an old observer on a remote node
  inline bool min_cluster_version_is_valid() const
  {
    return ObExecutorRpcCtx::INVALID_CLUSTER_VERSION != min_cluster_version_;
  }
  inline void set_min_cluster_version(uint64_t min_cluster_version)
  {
    min_cluster_version_ = min_cluster_version;
  }
  inline uint64_t get_min_cluster_version() const
  {
    return min_cluster_version_;
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
  //
  // RPC provided streaming processing interface, LocalReceiveOp reads remote data from this interface
  // The reason this variable is placed in ObTaskExecutorCtx is: task_resp_handler_
  // Must be initialized in Scheduler, used in LocalReceiveOp, so it utilizes
  // ObTaskExecutorCtx pass variables
  RemoteExecuteStreamHandle *task_resp_handler_;
  // Used to encapsulate the Op Tree of the top-level Job of executor, outputting data externally
  ObExecuteResult execute_result_;
  // Used to record the correspondence between virtual table's partition_id and machine's (ip, port),
  // The index of this array corresponds to the partition_id of the server
  common::ObList<common::ObAddr, common::ObIAllocator> virtual_part_servers_;
  // Used for temporarily passing parameters when calculating the partition id of a virtual table, it's best to reset this member variable after calculation
  CalcVirtualPartitionIdParams calc_params_;
  //
  ObExecContext *exec_ctx_;
  // PX records the expected number of threads required for the entire Query, as well as the actual number of threads allocated
  int64_t expected_worker_cnt_; // query expected worker count computed by optimizer
  int64_t minimal_worker_cnt_;  // minimal worker count to support execute this query
  int64_t admited_worker_cnt_; // query final used worker count admitted by admission
  // END local local variable
  // BEGIN variables that need to be serialized
  // This information is serialized to all machines that participated in the query.
  // NOTE:The intermediate calculation machine needs to be judged, do not as Participant
  ObPhyTableLocationFixedArray table_locations_;
  // The number of retries
  int64_t retry_times_;
  //
  // Global observer minimum version number
  uint64_t min_cluster_version_;
  //
  //  END variables that need to be serialized

  int64_t sys_job_id_;
public:
  // BEGIN global singleton variable
  //
  obrpc::ObCommonRpcProxy *rs_rpc_proxy_;
  int64_t query_tenant_begin_schema_version_; // Query start time to get the latest global tenant schema version
  int64_t query_sys_begin_schema_version_; // Query start time to get the latest global sys schema version
  share::schema::ObMultiVersionSchemaService *schema_service_;
  //
  // END global singleton variable


  DISALLOW_COPY_AND_ASSIGN(ObTaskExecutorCtx);
  TO_STRING_KV(K(table_locations_), K(retry_times_), K(min_cluster_version_), K(expected_worker_cnt_),
      K(admited_worker_cnt_), K(query_tenant_begin_schema_version_), K(query_sys_begin_schema_version_),
      K(minimal_worker_cnt_));
};

class ObExecutorRpcImpl;
class ObTaskExecutorCtxUtil
{
public:
  // trigger a location update task and clear location in cache
  static int get_stream_handler(ObExecContext &ctx, RemoteExecuteStreamHandle *&handler);
  static int get_task_executor_rpc(ObExecContext &ctx, ObExecutorRpcImpl *&rpc);

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
