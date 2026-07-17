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

#ifndef OCEANBASE_STORAGE_DDL_OB_DDL_DAG_MONITOR_H_
#define OCEANBASE_STORAGE_DDL_OB_DDL_DAG_MONITOR_H_

// Task4 Op9：DDL DAG 阶段监控定义，用于定位 FTS 构建流水线瓶颈。

#include "lib/container/ob_array.h"
#include "lib/lock/ob_mutex.h"
#include "lib/ob_define.h"

namespace oceanbase
{
namespace storage
{

class ObDDLIndependentDag;
struct ObDDLTaskParam;

// Task4 Op9：稳定的阶段名称便于在诊断信息中直接检索。
enum ObDDLDagMonitorStage
{
  TASK4_OP9_DAG_TOTAL = 0,
  TASK4_OP9_FTS_SCAN,
  TASK4_OP9_WRITE_PIPELINE,
  TASK4_OP9_GROUP_WRITE,
  TASK4_OP9_MERGE_PREPARE,
  TASK4_OP9_MERGE_SLICE,
  TASK4_OP9_MERGE_ASSEMBLE,
  TASK4_OP9_STAGE_MAX
};

const char *get_ddl_dag_monitor_stage_name(const ObDDLDagMonitorStage stage);

struct ObDDLDagMonitorInfo
{
  ObDDLDagMonitorInfo();
  TO_STRING_KV(K_(tenant_id), K_(ddl_task_id), K_(target_table_id), K_(dag_id),
               K_(stage), K_(create_time_us), K_(finish_time_us), K_(schedule_count),
               K_(running_count), K_(execution_time_us), K_(ret_code), K_(is_closed));
  uint64_t tenant_id_;
  int64_t ddl_task_id_;
  uint64_t target_table_id_;
  uintptr_t dag_id_;
  ObDDLDagMonitorStage stage_;
  int64_t create_time_us_;
  int64_t finish_time_us_;
  int64_t schedule_count_;
  int64_t running_count_;
  int64_t execution_time_us_;
  int ret_code_;
  bool is_closed_;
};

class ObDDLDagMonitorNode
{
public:
  ObDDLDagMonitorNode(const uint64_t tenant_id,
                      const ObDDLTaskParam &task_param,
                      const void *dag,
                      const ObDDLDagMonitorStage stage);
  void start(const int64_t now_us);
  void finish(const int64_t start_time_us, const int64_t now_us, const int ret_code);
  void close(const int64_t now_us, const int ret_code);
  void snapshot(ObDDLDagMonitorInfo &info) const;
  bool matches(const uint64_t tenant_id,
               const int64_t ddl_task_id,
               const void *dag,
               const ObDDLDagMonitorStage stage) const;
  bool matches_dag(const uint64_t tenant_id, const int64_t ddl_task_id, const void *dag) const;
  bool is_expired(const int64_t now_us, const int64_t retention_us) const;
  TO_STRING_KV(K_(tenant_id), K_(ddl_task_id), K_(target_table_id), K_(dag_id),
               K_(stage), K_(create_time_us), K_(finish_time_us), K_(schedule_count),
               K_(running_count), K_(execution_time_us), K_(ret_code), K_(is_closed));

private:
  uint64_t tenant_id_;
  int64_t ddl_task_id_;
  uint64_t target_table_id_;
  uintptr_t dag_id_;
  ObDDLDagMonitorStage stage_;
  int64_t create_time_us_;
  int64_t finish_time_us_;
  int64_t schedule_count_;
  int64_t running_count_;
  int64_t execution_time_us_;
  int32_t ret_code_;
  int32_t is_closed_;
};

class ObDDLDagMonitorMgr
{
public:
  static ObDDLDagMonitorMgr &get_instance();
  ObDDLDagMonitorNode *get_or_create_node(const uint64_t tenant_id,
                                          const ObDDLTaskParam &task_param,
                                          const void *dag,
                                          const ObDDLDagMonitorStage stage);
  void close_dag(const uint64_t tenant_id,
                 const ObDDLTaskParam &task_param,
                 const void *dag,
                 const int ret_code);
  int get_snapshot(common::ObIArray<ObDDLDagMonitorInfo> &infos);

private:
  ObDDLDagMonitorMgr();
  ~ObDDLDagMonitorMgr();
  void cleanup_expired_nodes(const int64_t now_us);
  void destroy_node(ObDDLDagMonitorNode *node);
  DISALLOW_COPY_AND_ASSIGN(ObDDLDagMonitorMgr);

private:
  static const int64_t MAX_NODE_COUNT = 100000;
  static const int64_t MAX_ALLOCATOR_BYTES = 16L * 1024L * 1024L;
  static const int64_t RETENTION_US = 24L * 60L * 60L * 1000L * 1000L;
  static const int64_t CLEANUP_INTERVAL_US = 60L * 1000L * 1000L;
  lib::ObMutex mutex_;
  common::ObArray<ObDDLDagMonitorNode *> nodes_;
  int64_t last_cleanup_time_us_;
  int64_t allocated_bytes_;
};

class ObDDLDagStageGuard
{
public:
  ObDDLDagStageGuard(ObDDLIndependentDag *dag, const ObDDLDagMonitorStage stage);
  ObDDLDagStageGuard(const ObDDLTaskParam &task_param,
                     const void *dag,
                     const ObDDLDagMonitorStage stage);
  ~ObDDLDagStageGuard();
  void set_ret_code(const int ret_code) { ret_code_ = ret_code; }

private:
  ObDDLDagMonitorNode *node_;
  int64_t start_time_us_;
  int ret_code_;
  DISALLOW_COPY_AND_ASSIGN(ObDDLDagStageGuard);
};

} // namespace storage
} // namespace oceanbase

#endif // OCEANBASE_STORAGE_DDL_OB_DDL_DAG_MONITOR_H_
