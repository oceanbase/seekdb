/**
 * Copyright (c) 2021 OceanBase
 * OceanBase CE is licensed under Mulan PubL v2.
 * You can use this software according to the terms and conditions of the Mulan PubL v2.
 * You may obtain a copy of Mulan PubL v2 at:
 *          http://license.coscl.org.cn/MulanPubL-2.0
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND,
 * EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
 * MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
 * See the Mulan PubL v2 for more details.
 */

#include "storage/ddl/ob_ddl_dag_monitor_mgr.h"
#include "lib/oblog/ob_log_module.h"
#include "lib/allocator/ob_malloc.h"
#include "lib/utility/ob_macro_utils.h"
#include "share/rc/ob_tenant_base.h"
#include "share/ob_unit_getter.h"
#include "share/ob_server_struct.h"

#define USING_LOG_PREFIX STORAGE

namespace oceanbase
{
namespace storage
{

using namespace common;

ObDDLDagMonitorMgr::MonitorAllocator::MonitorAllocator()
    : mgr_(nullptr), fifo_allocator_()
{
}

int ObDDLDagMonitorMgr::MonitorAllocator::init(ObDDLDagMonitorMgr *mgr,
                                               const int64_t block_size,
                                               const ObMemAttr &attr,
                                               const int64_t total_limit)
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(mgr)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("mgr is null", K(ret));
  } else if (OB_FAIL(fifo_allocator_.init(block_size, attr, total_limit))) {
    LOG_WARN("failed to init fifo allocator", K(ret));
  } else {
    mgr_ = mgr;
  }
  return ret;
}

void ObDDLDagMonitorMgr::MonitorAllocator::destroy()
{
  fifo_allocator_.destroy();
  mgr_ = nullptr;
}

void *ObDDLDagMonitorMgr::MonitorAllocator::alloc(const int64_t size)
{
  void *ptr = inner_alloc(size);
  if (OB_ISNULL(ptr) && OB_NOT_NULL(mgr_)) {
    IGNORE_RETURN mgr_->clean_nodes(ObDDLDagMonitorMgr::CLEAN_FINISHED_INFO);
    ptr = inner_alloc(size);
  }
  return ptr;
}

void *ObDDLDagMonitorMgr::MonitorAllocator::alloc(const int64_t size, const ObMemAttr &attr)
{
  void *ptr = inner_alloc(size, attr);
  if (OB_ISNULL(ptr) && OB_NOT_NULL(mgr_)) {
    IGNORE_RETURN mgr_->clean_nodes(ObDDLDagMonitorMgr::CLEAN_FINISHED_INFO);
    ptr = inner_alloc(size, attr);
  }
  return ptr;
}

void *ObDDLDagMonitorMgr::MonitorAllocator::inner_alloc(const int64_t size)
{
  return fifo_allocator_.alloc(size);
}

void *ObDDLDagMonitorMgr::MonitorAllocator::inner_alloc(const int64_t size, const ObMemAttr &attr)
{
  return fifo_allocator_.alloc(size, attr);
}

void ObDDLDagMonitorMgr::MonitorAllocator::free(void *ptr)
{
  fifo_allocator_.free(ptr);
}

int64_t ObDDLDagMonitorMgr::MonitorAllocator::total() const
{
  return fifo_allocator_.total();
}

int64_t ObDDLDagMonitorMgr::MonitorAllocator::used() const
{
  return fifo_allocator_.used();
}

int64_t ObDDLDagMonitorMgr::MonitorAllocator::allocated() const
{
  return fifo_allocator_.allocated();
}

void ObDDLDagMonitorMgr::MonitorAllocator::set_total_limit(const int64_t total_limit)
{
  fifo_allocator_.set_total_limit(total_limit);
}

ObDDLDagMonitorMgr::ObDDLDagMonitorMgr()
    : is_inited_(false),
      allocator_(),
      max_node_count_(DEFAULT_MAX_NODE_COUNT),
      ttl_us_(DEFAULT_TTL_US),
      memory_limit_(DEFAULT_ALLOCATOR_SIZE),
      rwlock_(common::ObLatchIds::DDL_DAG_MONITOR_LOCK),
      node_map_(),
      clean_timer_(),
      clean_task_(*this),
      clean_interval_us_(DEFAULT_CLEAN_INTERVAL_US)
{
}

ObDDLDagMonitorMgr::~ObDDLDagMonitorMgr()
{
  destroy();
}

int ObDDLDagMonitorMgr::mtl_init(ObDDLDagMonitorMgr *&m)
{
  return OB_NOT_NULL(m) ? m->init() : OB_INVALID_ARGUMENT;
}

int ObDDLDagMonitorMgr::init()
{
  int ret = OB_SUCCESS;
  if (is_inited_) {
    ret = OB_INIT_TWICE;
    LOG_WARN("ObDDLDagMonitorMgr already inited", K(ret));
  } else if (OB_FAIL(allocator_.init(this, OB_MALLOC_NORMAL_BLOCK_SIZE, ObMemAttr("IndDagMonAlloc"), DEFAULT_ALLOCATOR_SIZE))) {
    LOG_WARN("failed to init allocator", K(ret));
  } else if (OB_FAIL(node_map_.create(max_node_count_ * 2, ObMemAttr("IndDagMonMap")))) {
    LOG_WARN("failed to create node map", K(ret), K(max_node_count_));
  } else if (OB_FAIL(clean_timer_.set_run_wrapper_with_ret(MTL_CTX()))) {
    LOG_WARN("failed to set timer run wrapper", K(ret));
  } else if (OB_FAIL(clean_timer_.init("IndDagMonitor", ObMemAttr("IndDagMonitor")))) {
    LOG_WARN("failed to init timer", K(ret));
  } else if (OB_FAIL(clean_timer_.schedule(clean_task_, clean_interval_us_, true /*repeat*/))) {
    LOG_WARN("failed to schedule clean task", K(ret), K_(clean_interval_us));
  } else {
    is_inited_ = true;
    LOG_INFO("ObDDLDagMonitorMgr mtl_init success", K(max_node_count_), K(ttl_us_));
  }
  if (OB_FAIL(ret) && !is_inited_) {
    destroy();
  }
  return ret;
}

void ObDDLDagMonitorMgr::destroy()
{
  if (node_map_.created()) {
    SpinWLockGuard guard(rwlock_);
    IGNORE_RETURN clean_nodes_unlock(CLEAN_ALL_NODE);
    node_map_.destroy();
  }
  if (clean_timer_.inited()) {
    clean_timer_.stop();
    clean_timer_.wait();
    clean_timer_.destroy();
  }
  allocator_.destroy();
  is_inited_ = false;
  LOG_INFO("ObDDLDagMonitorMgr destroyed");
}

int ObDDLDagMonitorMgr::register_node(const void *dag_ptr,
                                              const ObCurTraceId::TraceId &trace_id,
                                              ObDDLDagMonitorNode *&node)
{
  int ret = OB_SUCCESS;
  node = nullptr;
  if (OB_UNLIKELY(!is_inited_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("independent dag monitor mgr not inited", K(ret));
  } else if (OB_UNLIKELY(nullptr == dag_ptr || !trace_id.is_valid())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", K(ret), KP(dag_ptr), K(trace_id));
  } else {
    NodeKey key(dag_ptr, trace_id);
    SpinWLockGuard guard(rwlock_);
    // If already exists, return it (idempotent register).
    if (OB_FAIL(node_map_.get_refactored(key, node))) {
      if (OB_HASH_NOT_EXIST != ret) {
        LOG_WARN("get node from map failed", K(ret), K(key));
      } else {
        // Best-effort: clean expired nodes when reaching max count.
        if (node_map_.size() >= max_node_count_) {
          int tmp_ret = clean_nodes_unlock(CLEAN_EXPIRED_NODE);
          if (OB_SUCCESS != tmp_ret) {
            LOG_WARN("clean expired nodes failed before register", K(tmp_ret), K_(max_node_count));
          }
        }
        if (OB_FAIL(inner_create_node(key, node))) {
          LOG_WARN("create node failed", K(ret), K(key));
        }
      }
    }
  }
  return ret;
}

int ObDDLDagMonitorMgr::get_all_nodes(ObIArray<ObDDLDagMonitorNode *> &nodes)
{
  int ret = OB_SUCCESS;
  nodes.reset();
  if (OB_UNLIKELY(!is_inited_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("independent dag monitor mgr not inited", K(ret));
  } else {
    SpinRLockGuard guard(rwlock_);
    NodeMap::const_iterator iter = node_map_.begin();
    for (; OB_SUCC(ret) && iter != node_map_.end(); ++iter) {
      if (OB_ISNULL(iter->second)) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("node is null", K(ret));
      } else if (OB_FAIL(nodes.push_back(iter->second))) {
        LOG_WARN("push back node failed", K(ret));
      }
    }
    if (OB_SUCC(ret)) {
      for (int64_t i = 0; i < nodes.count(); ++i) {
        nodes.at(i)->inc_ref(); // paired with dec_ref() by the caller (e.g. virtual table iterator reset)
      }
    } else {
      nodes.reset();
    }
  }
  return ret;
}

int ObDDLDagMonitorMgr::clean_nodes(const CleanMode mode)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(!is_inited_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("independent dag monitor mgr not inited", K(ret));
  } else {
    SpinWLockGuard guard(rwlock_);
    if (OB_FAIL(clean_nodes_unlock(mode))) {
      LOG_WARN("clean nodes unlock failed", K(ret), K(mode));
    }
  }
  return ret;
}

int ObDDLDagMonitorMgr::clean_nodes_unlock(const CleanMode mode)
{
  int ret = OB_SUCCESS;
  update_memory_limit();
  const int64_t now = ObTimeUtility::current_time();
  NodeMap::const_iterator it = node_map_.begin();
  while (it != node_map_.end() && OB_SUCC(ret)) {
    ObDDLDagMonitorNode *node = it->second;
    ++it; // advance iterator before erase
    if (OB_ISNULL(node)) {
      continue;
    }
    const bool node_expired = node->is_expired(now, ttl_us_);
    const bool need_remove = (CLEAN_ALL_NODE == mode)
                             || (CLEAN_EXPIRED_NODE == mode && node_expired)
                             || (CLEAN_FINISHED_INFO == mode && node_expired);
    if (need_remove) {
      if (OB_FAIL(inner_remove_node(node))) {
        LOG_WARN("remove node failed", K(ret), K(mode), KPC(node));
      }
    } else if (CLEAN_FINISHED_INFO == mode) {
      if (OB_FAIL(node->clean_infos(true /*only_finished*/))) {
        LOG_WARN("clean finished monitor infos failed", K(ret), KPC(node));
      }
    }
  }
  return ret;
}

int ObDDLDagMonitorMgr::inner_create_node(const NodeKey &key, ObDDLDagMonitorNode *&node)
{
  int ret = OB_SUCCESS;
  node = nullptr;
  if (OB_UNLIKELY(!is_inited_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("independent dag monitor mgr not inited", K(ret));
  } else if (OB_UNLIKELY(!key.is_valid())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("node key is invalid", K(ret), K(key));
  } else {
    ObDDLDagMonitorNode *tmp = nullptr;
    void *buf = allocator_.inner_alloc(sizeof(ObDDLDagMonitorNode));
    if (OB_ISNULL(buf)) {
      // Retry after cleaning expired nodes while holding the lock.
      IGNORE_RETURN clean_nodes_unlock(CLEAN_FINISHED_INFO);
      buf = allocator_.inner_alloc(sizeof(ObDDLDagMonitorNode));
    }
    if (OB_ISNULL(buf)) {
      ret = OB_ALLOCATE_MEMORY_FAILED;
      LOG_WARN("allocate node failed", K(ret));
    } else {
      tmp = new (buf) ObDDLDagMonitorNode(&allocator_);
      if (OB_FAIL(tmp->init(key))) {
        LOG_WARN("init node failed", K(ret), K(key));
      } else if (OB_FAIL(node_map_.set_refactored(key, tmp))) {
        LOG_WARN("insert node to map failed", K(ret), K(key));
      } else {
        tmp->inc_ref(); // manager holds 1 base reference
        node = tmp;
      }
    }
    if (OB_FAIL(ret) && OB_NOT_NULL(tmp)) {
      tmp->~ObDDLDagMonitorNode();
      allocator_.free(tmp);
      tmp = nullptr;
    }
  }
  return ret;
}

int ObDDLDagMonitorMgr::inner_remove_node(ObDDLDagMonitorNode *node)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(!is_inited_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("independent dag monitor mgr not inited", K(ret));
  } else if (OB_ISNULL(node)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("node is null", K(ret));
  } else {
    const NodeKey key(node->get_key());
    if (OB_FAIL(node_map_.erase_refactored(key))) {
      if (OB_HASH_NOT_EXIST == ret) {
        ret = OB_SUCCESS;
      } else {
        LOG_WARN("erase node from map failed", K(ret), K(key));
      }
    } else {
      // Release manager's base reference after removing from map.
      node->dec_ref();
    }
  }
  return ret;
}

int64_t ObDDLDagMonitorMgr::get_node_count() const
{
  SpinRLockGuard guard(rwlock_);
  return node_map_.size();
}

int64_t ObDDLDagMonitorMgr::get_allocated_size() const
{
  return allocator_.allocated();
}

void ObDDLDagMonitorMgr::update_memory_limit()
{
  // seekdb has one process-level execution context; keep the bounded default
  // instead of deriving a per-tenant quota from the removed multi-tenant unit service.
  memory_limit_ = DEFAULT_ALLOCATOR_SIZE;
  allocator_.set_total_limit(memory_limit_);
}

} // namespace storage
} // namespace oceanbase
