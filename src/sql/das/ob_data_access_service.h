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

#ifndef OBDEV_SRC_SQL_DAS_OB_DATA_ACCESS_SERVICE_H_
#define OBDEV_SRC_SQL_DAS_OB_DATA_ACCESS_SERVICE_H_
#include "share/ob_define.h"
#include "sql/das/ob_das_id_cache.h"
#include "sql/das/ob_das_task_result.h"
#include "sql/das/ob_das_ref.h"
namespace oceanbase
{
namespace sql
{
class ObDASRef;
class ObDASTaskArg;
class ObDASTaskResp;
class ObPhyTableLocation;
class ObDASExtraData;
class ObDataAccessService
{
public:
  ObDataAccessService();
  ~ObDataAccessService() = default;
  static int mtl_init(ObDataAccessService *&das);
  static void mtl_destroy(ObDataAccessService *&das);
  int init(rpc::frame::ObReqTransport *transport,
           const common::ObAddr &self_addr);
  // Enable DAS Task partition related transaction control, and execute the op corresponding to the task
  int execute_das_task(ObDASRef &das_ref,
      ObDasAggregatedTask &task_ops, bool async = true);
  // Close the execution flow of DAS Task, release the resources held by the task, and end the related transaction control
  int end_das_task(ObDASRef &das_ref, ObIDASTaskOp &task_op);
  int get_das_task_id(int64_t &das_id);
  int rescan_das_task(ObDASRef &das_ref, ObDASScanOp &scan_op);
  ObDASTaskResultMgr &get_task_res_mgr() { return task_result_mgr_; }
  const common::ObAddr &get_ctrl_addr() const { return ctrl_addr_; };
  void set_max_concurrency(int32_t cpu_count);
  int32_t get_das_concurrency_limit() const { return das_concurrency_limit_; };
  int retry_das_task(ObDASRef &das_ref, ObIDASTaskOp &task_op);
  int setup_extra_result(ObDASRef &das_ref,
                        const ObDASTaskResp &task_resp,
                        const ObIDASTaskOp *task_op,
                        ObDASExtraData *&extra_result);
  int process_task_resp(ObDASRef &das_ref, const ObDASTaskResp &task_resp, const common::ObSEArray<ObIDASTaskOp*, 2> &task_ops);
  int parallel_execute_das_task(common::ObIArray<ObIDASTaskOp *> &task_list);
  int parallel_submit_das_task(ObDASRef &das_ref, ObDasAggregatedTask &agg_task);
  int push_parallel_task(ObDASRef &das_ref, ObDasAggregatedTask &agg_task, int32_t group_id);
  int collect_das_task_info(ObIArray<ObIDASTaskOp*> &task_list, ObDASRemoteInfo &remote_info);
private:
  int execute_dist_das_task(ObDASRef &das_ref,
      ObDasAggregatedTask &task_ops, bool async = true);
  int clear_task_exec_env(ObDASRef &das_ref, ObIDASTaskOp &task_op);
  int refresh_task_location_info(ObDASRef &das_ref, ObIDASTaskOp &task_op);
  int do_local_das_task(ObIArray<ObIDASTaskOp*> &task_list);
  int collect_das_task_attach_info(ObDASRemoteInfo &remote_info,
                                   ObDASBaseRtDef *attach_rtdef);
private:
  common::ObAddr ctrl_addr_;
  ObDASIDCache id_cache_;
  ObDASTaskResultMgr task_result_mgr_;
  int32_t das_concurrency_limit_;
};
}  // namespace sql
}  // namespace oceanbase

#endif /* OBDEV_SRC_SQL_DAS_OB_DATA_ACCESS_SERVICE_H_ */
