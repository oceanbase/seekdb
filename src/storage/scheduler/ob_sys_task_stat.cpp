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

#include "data_plane/scheduler/ob_sys_task_stat.h"
#include "lib/allocator/ob_malloc.h"

namespace oceanbase
{
using namespace common;
namespace share
{
const static char *ObSysTaskTypeStr[] = {
    "DDL",
    "SSTABLE_MINI_MERGE",
    "SPECIAL_TABLE_MERGE",
    "SSTABLE_MINOR_MERGE",
    "SSTABLE_MAJOR_MERGE",
    "WRITE_CKPT",
    "DDL_KV_MERGE",
    "COMPLEMENT_DATA",
    "BACKFILL_TX",
    "MDS_MINI_MERGE",
    "BATCH_FREEZE_TABLET_TASK",
    "VECTOR_INDEX_TASK",
    "SSTABLE_MICRO_MINI_MERGE",
    "VECTOR_INDEX_ASYNC_TASK"
};

const char *sys_task_type_to_str(const ObSysTaskType &type)
{
  STATIC_ASSERT(static_cast<int64_t>(MAX_SYS_TASK_TYPE) == ARRAYSIZEOF(ObSysTaskTypeStr), "sys_task_type str len is mismatch");
  const char *str = "";
  if (OB_UNLIKELY(type < 0 || type >= MAX_SYS_TASK_TYPE)) {
    str = "invalid task type";
  } else {
    str = ObSysTaskTypeStr[type];
  }
  return str;
}

ObSysTaskStat::ObSysTaskStat()
  : start_time_(0),
    task_id_(),
    task_type_(MAX_SYS_TASK_TYPE),
    svr_ip_(),
    comment_(),
    is_cancel_(false)
{
}

ObSysStatMgrIter::ObSysStatMgrIter()
  : is_ready_(false),
    allocator_(ObModIds::OB_SYS_TASK_STATUS),
    item_arr_(ObModIds::OB_SYS_TASK_STATUS, OB_MALLOC_NORMAL_BLOCK_SIZE),
    it_()
{
}

ObSysStatMgrIter::~ObSysStatMgrIter()
{
  reset();
}

void ObSysStatMgrIter::reset()
{
  is_ready_ = false;
  item_arr_.reset();
  allocator_.reset();
}

int ObSysStatMgrIter::push(const ObSysTaskStat &item)
{
  int ret = OB_SUCCESS;
  ObSysTaskStat snapshot = item;
  if (is_ready_) {
    ret = OB_ERR_UNEXPECTED;
    SERVER_LOG(WARN, "sys task status iterator is already ready", K(ret));
  } else if (!item.comment_.empty()) {
    char *comment_buf = static_cast<char *>(allocator_.alloc(item.comment_.length()));
    if (OB_ISNULL(comment_buf)) {
      ret = OB_ALLOCATE_MEMORY_FAILED;
      SERVER_LOG(WARN, "failed to allocate sys task comment snapshot", K(ret),
          "comment_length", item.comment_.length());
    } else {
      MEMCPY(comment_buf, item.comment_.ptr(), item.comment_.length());
      snapshot.comment_.assign_ptr(comment_buf, item.comment_.length());
    }
  }

  if (OB_SUCC(ret) && OB_FAIL(item_arr_.push_back(snapshot))) {
    SERVER_LOG(WARN, "failed to add sys task status snapshot", K(ret));
  }
  return ret;
}

int ObSysStatMgrIter::set_ready()
{
  int ret = OB_SUCCESS;
  if (is_ready_) {
    ret = OB_ERR_UNEXPECTED;
    SERVER_LOG(WARN, "sys task status iterator is already ready", K(ret));
  } else {
    is_ready_ = true;
    it_ = item_arr_.begin();
  }
  return ret;
}

int ObSysStatMgrIter::get_next(ObSysTaskStat &item)
{
  int ret = OB_SUCCESS;
  if (!is_ready_) {
    ret = OB_ERR_UNEXPECTED;
    SERVER_LOG(WARN, "sys task status iterator is not ready", K(ret));
  } else if (item_arr_.end() == it_) {
    ret = OB_ITER_END;
  } else {
    item = *it_;
    ++it_;
  }
  return ret;
}

ObSysTaskStatMgr::ObSysTaskStatMgr()
  : lock_(common::ObLatchIds::SYS_TASK_STAT_LOCK),
    task_list_()
{
}

ObSysTaskStatMgr::~ObSysTaskStatMgr()
{
  clear_task_list_();
}

ObSysTaskStatMgr &ObSysTaskStatMgr::get_instance()
{
  static ObSysTaskStatMgr mgr_;
  return mgr_;
}

int ObSysTaskStatMgr::get_iter(ObSysStatMgrIter &iter)
{
  int ret = OB_SUCCESS;
  iter.reset();

  SpinRLockGuard guard(lock_);

  for (ObSysTaskStatNode *node = task_list_.get_first();
       OB_SUCC(ret) && node != task_list_.get_header();
       node = node->get_next()) {
    if (OB_FAIL(iter.push(node->task_))) {
    }
  }

  if (OB_SUCC(ret)) {
    if (OB_FAIL(iter.set_ready())) {
    }
  }

  return ret;
}

int ObSysTaskStatMgr::alloc_task_node_(
    const ObSysTaskStat &task,
    ObSysTaskStatNode *&node)
{
  int ret = OB_SUCCESS;
  node = NULL;
  const int64_t comment_length = std::min(
      static_cast<int64_t>(task.comment_.length()),
      common::OB_MAX_TASK_COMMENT_LENGTH - 1);
  ObMemAttr attr(ObModIds::OB_SYS_TASK_STATUS);
  void *buf = ob_malloc(sizeof(ObSysTaskStatNode) + comment_length, attr);
  if (OB_ISNULL(buf)) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
    SERVER_LOG(WARN, "failed to allocate sys task status node", K(ret));
  } else {
    node = new(buf) ObSysTaskStatNode(task);
    if (comment_length > 0) {
      char *comment_buf = reinterpret_cast<char *>(node + 1);
      MEMCPY(comment_buf, task.comment_.ptr(), comment_length);
      node->task_.comment_.assign_ptr(comment_buf, comment_length);
    } else {
      node->task_.comment_.reset();
    }
  }
  return ret;
}

void ObSysTaskStatMgr::free_task_node_(ObSysTaskStatNode *node)
{
  if (OB_NOT_NULL(node)) {
    node->~ObSysTaskStatNode();
    ob_free(node);
  }
}

void ObSysTaskStatMgr::clear_task_list_()
{
  ObSysTaskStatNode *node = NULL;
  while (OB_NOT_NULL(node = task_list_.remove_first())) {
    free_task_node_(node);
  }
}

int ObSysTaskStatMgr::generate_task_id(ObTaskId &task_id)
{
  int ret = OB_SUCCESS;
  if (!self_addr_.is_valid()) {
    ret = OB_INVALID_ERROR;
    SERVER_LOG(ERROR, "self_addr_ is invalid", K(ret), K(self_addr_));
  } else {
    task_id.reset();
    task_id.init(self_addr_);
  }
  return ret;
}

int ObSysTaskStatMgr::add_task(ObSysTaskStat &task)
{
  int ret = OB_SUCCESS;
  ObSysTaskStatNode *new_node = NULL;

  if (!self_addr_.is_valid()) {
    ret = OB_INVALID_ERROR;
    SERVER_LOG(ERROR, "self_addr_ is invalid", K(ret), K(self_addr_));
  } else {
    task.svr_ip_ = self_addr_;
    if (!task.task_id_.is_invalid()) {
      SERVER_LOG(INFO, "task id is valid, no need set new", K(ret), K(task));
    } else {
      task.task_id_.init(self_addr_);
    }
  }

  if (OB_SUCC(ret) && OB_FAIL(alloc_task_node_(task, new_node))) {
    SERVER_LOG(WARN, "failed to allocate task node", K(ret), K(task));
  }

  if (OB_SUCC(ret)) {
    SpinWLockGuard guard(lock_);
    for (ObSysTaskStatNode *node = task_list_.get_first();
         OB_SUCC(ret) && node != task_list_.get_header();
         node = node->get_next()) {
      if (node->task_.task_id_.equals(task.task_id_)) {
        ret = OB_ENTRY_EXIST;
        if (DDL_TASK != task.task_type_) {
          SERVER_LOG(ERROR, "task id is exist, cannot add again",
              K(ret), K(task), "existing_task", node->task_);
        } else {
          SERVER_LOG(WARN, "ddl task id is exist, cannot add again",
              K(ret), K(task), "existing_task", node->task_);
        }
      }
    }

    if (OB_SUCC(ret)) {
      if (!task_list_.add_last(new_node)) {
        ret = OB_ERR_UNEXPECTED;
        SERVER_LOG(WARN, "failed to add task status node", K(ret), K(task));
      } else {
        new_node = NULL;
        SERVER_LOG(INFO, "succeed to add sys task", K(task));
      }
    }
  }

  if (OB_NOT_NULL(new_node)) {
    free_task_node_(new_node);
  }

  return ret;
}

int ObSysTaskStatMgr::del_task(const ObTaskId &task_id)
{
  int ret = OB_SUCCESS;
  ObSysTaskStatNode *removed_node = NULL;

  if (task_id.is_invalid()) {
    ret = OB_INVALID_ARGUMENT;
    SERVER_LOG(WARN, "invalid task_id", K(ret), K(task_id));
  } else {
    SpinWLockGuard guard(lock_);
    for (ObSysTaskStatNode *node = task_list_.get_first();
         OB_ISNULL(removed_node) && node != task_list_.get_header();
         node = node->get_next()) {
      if (node->task_.task_id_.equals(task_id)) {
        removed_node = task_list_.remove(node);
      }
    }

    if (OB_ISNULL(removed_node)) {
      ret = OB_ENTRY_NOT_EXIST;
      SERVER_LOG(WARN, "sys task not exist", K(ret), K(task_id));
    }
  }

  if (OB_NOT_NULL(removed_node)) {
    SERVER_LOG(INFO, "succeed to del sys task", "removed_task", removed_node->task_);
    free_task_node_(removed_node);
  }

  return ret;
}


int ObSysTaskStatMgr::set_self_addr(const ObAddr addr)
{
  int ret = OB_SUCCESS;
  if (!addr.is_valid()) {
    ret = OB_INVALID_ARGUMENT;
  } else {
      self_addr_ = addr;
  }
  return ret;
}

int ObSysTaskStatMgr::task_exist(const ObTaskId &task_id, bool &is_exist)
{
  int ret = OB_SUCCESS;
  is_exist = false;

  if (task_id.is_invalid()) {
    ret = OB_INVALID_ARGUMENT;
    STORAGE_LOG(WARN, "invalid task id", K(ret));
  } else {
    SpinRLockGuard guard(lock_);
    for (ObSysTaskStatNode *node = task_list_.get_first();
         !is_exist && node != task_list_.get_header();
         node = node->get_next()) {
      if (task_id.equals(node->task_.task_id_)) {
        is_exist = true;
      }
    }
  }

  return ret;
}

int ObSysTaskStatMgr::cancel_task(const ObTaskId &task_id)
{
  int ret = OB_SUCCESS;

  if (task_id.is_invalid()) {
    ret = OB_INVALID_ARGUMENT;
    SERVER_LOG(WARN, "invalid task id", K(ret));
  } else {
    SpinWLockGuard guard(lock_);
    bool found_task = false;
    for (ObSysTaskStatNode *node = task_list_.get_first();
         !found_task && node != task_list_.get_header();
         node = node->get_next()) {
      if (task_id.equals(node->task_.task_id_)) {
        found_task = true;
        node->task_.is_cancel_ = true;
        SERVER_LOG(INFO, "cancel task", "task", node->task_);
      }
    }

    if (!found_task) {
      ret = OB_ENTRY_NOT_EXIST;
      SERVER_LOG(WARN, "task not exist, cannot cancel", K(ret), K(task_id));
    }
  }

  return ret;
}

int ObSysTaskStatMgr::is_task_cancel(const ObTaskId &task_id, bool &is_cancel)
{
  int ret = OB_SUCCESS;
  is_cancel = false;

  if (task_id.is_invalid()) {
    ret = OB_INVALID_ARGUMENT;
    SERVER_LOG(WARN, "invalid task id", K(ret), K(task_id));
  } else {
    SpinRLockGuard guard(lock_);
    bool found_task = false;
    for (ObSysTaskStatNode *node = task_list_.get_first();
         !found_task && node != task_list_.get_header();
         node = node->get_next()) {
      if (task_id.equals(node->task_.task_id_)) {
        found_task = true;
        is_cancel = node->task_.is_cancel_;
      }
    }
    if (!found_task) {
      ret = OB_ENTRY_NOT_EXIST;
      SERVER_LOG(WARN, "task not exist, cannot check is cancel", K(ret), K(task_id));
    }
  }

  return ret;
}
}//common
}//oceanbase
