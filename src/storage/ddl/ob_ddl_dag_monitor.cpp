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

#define USING_LOG_PREFIX STORAGE

// Task4 Op9：实现 DDL DAG 阶段统计、快照读取和自动关闭。

#include "storage/ddl/ob_ddl_dag_monitor.h"
#include "storage/ddl/ob_ddl_independent_dag.h"
#include "storage/ddl/ob_ddl_struct.h"
#include "lib/allocator/ob_malloc.h"
#include "lib/atomic/ob_atomic.h"
#include "lib/time/ob_time_utility.h"

namespace oceanbase
{
namespace storage
{

using namespace common;

const char *get_ddl_dag_monitor_stage_name(const ObDDLDagMonitorStage stage)
{
  const char *name = "TASK4_OP9_UNKNOWN";
  switch (stage) {
    case TASK4_OP9_DAG_TOTAL: name = "TASK4_OP9_DAG_TOTAL"; break;
    case TASK4_OP9_FTS_SCAN: name = "TASK4_OP9_FTS_SCAN"; break;
    case TASK4_OP9_WRITE_PIPELINE: name = "TASK4_OP9_WRITE_PIPELINE"; break;
    case TASK4_OP9_GROUP_WRITE: name = "TASK4_OP9_GROUP_WRITE"; break;
    case TASK4_OP9_MERGE_PREPARE: name = "TASK4_OP9_MERGE_PREPARE"; break;
    case TASK4_OP9_MERGE_SLICE: name = "TASK4_OP9_MERGE_SLICE"; break;
    case TASK4_OP9_MERGE_ASSEMBLE: name = "TASK4_OP9_MERGE_ASSEMBLE"; break;
    default: break;
  }
  return name;
}

ObDDLDagMonitorInfo::ObDDLDagMonitorInfo()
  : tenant_id_(OB_INVALID_TENANT_ID),
    ddl_task_id_(0),
    target_table_id_(OB_INVALID_ID),
    dag_id_(0),
    stage_(TASK4_OP9_STAGE_MAX),
    create_time_us_(0),
    finish_time_us_(0),
    schedule_count_(0),
    running_count_(0),
    execution_time_us_(0),
    ret_code_(OB_SUCCESS),
    is_closed_(false)
{
}

ObDDLDagMonitorNode::ObDDLDagMonitorNode(const uint64_t tenant_id,
                                         const ObDDLTaskParam &task_param,
                                         const void *dag,
                                         const ObDDLDagMonitorStage stage)
  : tenant_id_(tenant_id),
    ddl_task_id_(task_param.ddl_task_id_),
    target_table_id_(task_param.target_table_id_),
    dag_id_(reinterpret_cast<uintptr_t>(dag)),
    stage_(stage),
    create_time_us_(0),
    finish_time_us_(0),
    schedule_count_(0),
    running_count_(0),
    execution_time_us_(0),
    ret_code_(OB_SUCCESS),
    is_closed_(false)
{
}

void ObDDLDagMonitorNode::start(const int64_t now_us)
{
  ATOMIC_STORE(&is_closed_, 0);
  (void)ATOMIC_BCAS(&create_time_us_, 0, now_us);
  (void)ATOMIC_AAF(&schedule_count_, 1);
  (void)ATOMIC_AAF(&running_count_, 1);
}

void ObDDLDagMonitorNode::finish(const int64_t start_time_us,
                                 const int64_t now_us,
                                 const int ret_code)
{
  if (now_us > start_time_us) {
    (void)ATOMIC_AAF(&execution_time_us_, now_us - start_time_us);
  }
  ATOMIC_STORE(&finish_time_us_, now_us);
  ATOMIC_STORE(&ret_code_, ret_code);
  (void)ATOMIC_AAF(&running_count_, -1);
}

void ObDDLDagMonitorNode::close(const int64_t now_us, const int ret_code)
{
  const int64_t create_time_us = ATOMIC_LOAD(&create_time_us_);
  if ((TASK4_OP9_DAG_TOTAL == stage_ || TASK4_OP9_FTS_SCAN == stage_)
      && create_time_us > 0 && now_us > create_time_us) {
    ATOMIC_STORE(&execution_time_us_, now_us - create_time_us);
  }
  ATOMIC_STORE(&finish_time_us_, now_us);
  ATOMIC_STORE(&ret_code_, ret_code);
  ATOMIC_STORE(&running_count_, 0);
  ATOMIC_STORE(&is_closed_, 1);
}

void ObDDLDagMonitorNode::snapshot(ObDDLDagMonitorInfo &info) const
{
  info.tenant_id_ = tenant_id_;
  info.ddl_task_id_ = ddl_task_id_;
  info.target_table_id_ = target_table_id_;
  info.dag_id_ = dag_id_;
  info.stage_ = stage_;
  info.create_time_us_ = ATOMIC_LOAD(&create_time_us_);
  info.finish_time_us_ = ATOMIC_LOAD(&finish_time_us_);
  info.schedule_count_ = ATOMIC_LOAD(&schedule_count_);
  info.running_count_ = ATOMIC_LOAD(&running_count_);
  info.execution_time_us_ = ATOMIC_LOAD(&execution_time_us_);
  info.ret_code_ = ATOMIC_LOAD(&ret_code_);
  info.is_closed_ = 0 != ATOMIC_LOAD(&is_closed_);
}

bool ObDDLDagMonitorNode::matches(const uint64_t tenant_id,
                                  const int64_t ddl_task_id,
                                  const void *dag,
                                  const ObDDLDagMonitorStage stage) const
{
  return tenant_id_ == tenant_id && ddl_task_id_ == ddl_task_id
      && dag_id_ == reinterpret_cast<uintptr_t>(dag) && stage_ == stage;
}

bool ObDDLDagMonitorNode::matches_dag(const uint64_t tenant_id,
                                      const int64_t ddl_task_id,
                                      const void *dag) const
{
  return tenant_id_ == tenant_id && ddl_task_id_ == ddl_task_id
      && dag_id_ == reinterpret_cast<uintptr_t>(dag);
}

bool ObDDLDagMonitorNode::is_expired(const int64_t now_us, const int64_t retention_us) const
{
  const int64_t finish_time_us = ATOMIC_LOAD(&finish_time_us_);
  return 0 != ATOMIC_LOAD(&is_closed_) && finish_time_us > 0
      && now_us - finish_time_us >= retention_us;
}

ObDDLDagMonitorMgr &ObDDLDagMonitorMgr::get_instance()
{
  static ObDDLDagMonitorMgr instance;
  return instance;
}

ObDDLDagMonitorMgr::ObDDLDagMonitorMgr()
  : mutex_(), nodes_(), last_cleanup_time_us_(0), allocated_bytes_(0)
{
}

ObDDLDagMonitorMgr::~ObDDLDagMonitorMgr()
{
  for (int64_t i = 0; i < nodes_.count(); ++i) {
    destroy_node(nodes_.at(i));
  }
  nodes_.reset();
}

void ObDDLDagMonitorMgr::destroy_node(ObDDLDagMonitorNode *node)
{
  if (nullptr != node) {
    node->~ObDDLDagMonitorNode();
    ob_free(node);
    allocated_bytes_ -= sizeof(ObDDLDagMonitorNode);
  }
}

void ObDDLDagMonitorMgr::cleanup_expired_nodes(const int64_t now_us)
{
  if (now_us - last_cleanup_time_us_ >= CLEANUP_INTERVAL_US) {
    for (int64_t i = nodes_.count() - 1; i >= 0; --i) {
      if (nodes_.at(i)->is_expired(now_us, RETENTION_US)) {
        destroy_node(nodes_.at(i));
        (void)nodes_.remove(i);
      }
    }
    last_cleanup_time_us_ = now_us;
  }
}

ObDDLDagMonitorNode *ObDDLDagMonitorMgr::get_or_create_node(
    const uint64_t tenant_id,
    const ObDDLTaskParam &task_param,
    const void *dag,
    const ObDDLDagMonitorStage stage)
{
  ObDDLDagMonitorNode *node = nullptr;
  if (OB_INVALID_TENANT_ID != tenant_id && task_param.ddl_task_id_ > 0
      && nullptr != dag && stage >= TASK4_OP9_DAG_TOTAL && stage < TASK4_OP9_STAGE_MAX) {
    const int64_t now_us = ObTimeUtility::current_time();
    lib::ObMutexGuard guard(mutex_);
    cleanup_expired_nodes(now_us);
    for (int64_t i = 0; nullptr == node && i < nodes_.count(); ++i) {
      if (nodes_.at(i)->matches(tenant_id, task_param.ddl_task_id_, dag, stage)) {
        node = nodes_.at(i);
      }
    }
    if (nullptr == node && nodes_.count() < MAX_NODE_COUNT
        && allocated_bytes_ + static_cast<int64_t>(sizeof(ObDDLDagMonitorNode)) <= MAX_ALLOCATOR_BYTES) {
      void *buf = ob_malloc(sizeof(ObDDLDagMonitorNode), ObMemAttr("Op9DagMonitor"));
      if (nullptr != buf) {
        node = new (buf) ObDDLDagMonitorNode(tenant_id, task_param, dag, stage);
        allocated_bytes_ += sizeof(ObDDLDagMonitorNode);
        if (OB_SUCCESS != nodes_.push_back(node)) {
          destroy_node(node);
          node = nullptr;
        }
      }
    }
  }
  return node;
}

void ObDDLDagMonitorMgr::close_dag(const uint64_t tenant_id,
                                    const ObDDLTaskParam &task_param,
                                    const void *dag,
                                    const int ret_code)
{
  if (task_param.ddl_task_id_ > 0 && nullptr != dag) {
    const int64_t now_us = ObTimeUtility::current_time();
    lib::ObMutexGuard guard(mutex_);
    for (int64_t i = 0; i < nodes_.count(); ++i) {
      if (nodes_.at(i)->matches_dag(tenant_id, task_param.ddl_task_id_, dag)) {
        nodes_.at(i)->close(now_us, ret_code);
      }
    }
  }
}

int ObDDLDagMonitorMgr::get_snapshot(ObIArray<ObDDLDagMonitorInfo> &infos)
{
  int ret = OB_SUCCESS;
  const int64_t now_us = ObTimeUtility::current_time();
  lib::ObMutexGuard guard(mutex_);
  cleanup_expired_nodes(now_us);
  for (int64_t i = 0; OB_SUCC(ret) && i < nodes_.count(); ++i) {
    ObDDLDagMonitorInfo info;
    nodes_.at(i)->snapshot(info);
    if (OB_FAIL(infos.push_back(info))) {
      LOG_WARN("Task4 Op9 failed to append DDL DAG monitor snapshot", K(ret));
    }
  }
  return ret;
}

ObDDLDagStageGuard::ObDDLDagStageGuard(ObDDLIndependentDag *dag,
                                       const ObDDLDagMonitorStage stage)
  : node_(nullptr), start_time_us_(ObTimeUtility::current_time()), ret_code_(OB_SUCCESS)
{
  if (nullptr != dag) {
    node_ = dag->get_or_create_dag_monitor_node(stage);
    if (nullptr != node_) {
      node_->start(start_time_us_);
    }
  }
}

ObDDLDagStageGuard::ObDDLDagStageGuard(const ObDDLTaskParam &task_param,
                                       const void *dag,
                                       const ObDDLDagMonitorStage stage)
  : node_(nullptr), start_time_us_(ObTimeUtility::current_time()), ret_code_(OB_SUCCESS)
{
  node_ = ObDDLDagMonitorMgr::get_instance().get_or_create_node(
      OB_SERVER_TENANT_ID, task_param, dag, stage);
  if (nullptr != node_) {
    node_->start(start_time_us_);
  }
}

ObDDLDagStageGuard::~ObDDLDagStageGuard()
{
  if (nullptr != node_) {
    node_->finish(start_time_us_, ObTimeUtility::current_time(), ret_code_);
  }
}

} // namespace storage
} // namespace oceanbase
