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

#ifndef OB_DDL_DAG_MONITOR_MGR_H_
#define OB_DDL_DAG_MONITOR_MGR_H_

#include "lib/allocator/page_arena.h"
#include "lib/hash/ob_hashmap.h"
#include "lib/ob_errno.h"
#include "lib/lock/ob_mutex.h"
#include "storage/ddl/ob_ddl_dag_monitor_node.h"

namespace oceanbase
{
namespace storage
{

class ObDDLDagMonitorMgr
{
public:
  static const int64_t MAX_NODE_COUNT = 100000;
  static const int64_t NODE_TTL_US = 24LL * 3600 * 1000000;
  static const int64_t CLEANUP_INTERVAL_US = 60LL * 1000000;
  static const int64_t FIFO_ALLOC_SIZE = 16LL * 1024 * 1024;
  static const int64_t HASH_BUCKET_COUNT = 1024;

  struct DagKey
  {
    uint64_t dag_ptr_;
    uint64_t trace_id_;
    DagKey() : dag_ptr_(0), trace_id_(0) {}
    DagKey(uint64_t dag_ptr, uint64_t trace_id) : dag_ptr_(dag_ptr), trace_id_(trace_id) {}
    bool operator==(const DagKey &other) const
    {
      return dag_ptr_ == other.dag_ptr_ && trace_id_ == other.trace_id_;
    }
    uint64_t hash() const { return (dag_ptr_ ^ trace_id_) % HASH_BUCKET_COUNT; }
  };

  static ObDDLDagMonitorMgr &instance();

  int init();
  void destroy();

  int register_dag(uint64_t dag_ptr, uint64_t trace_id);
  int unregister_dag(uint64_t dag_ptr, uint64_t trace_id);

  int add_monitor_info(uint64_t dag_ptr, uint64_t trace_id, ObDDLDagMonitorInfo *info);

  int foreach_node(int (*callback)(const ObDDLDagMonitorNode &node, void *user_data),
                   void *user_data);

private:
  ObDDLDagMonitorMgr()
    : is_inited_(false), node_count_(0), last_cleanup_ts_(0)
  {
  }
  ~ObDDLDagMonitorMgr() { destroy(); }

  int do_cleanup_if_needed();

  bool is_inited_;
  int64_t node_count_;
  int64_t last_cleanup_ts_;
  lib::ObMutex mutex_;
  common::ObArenaAllocator allocator_;
  common::hash::ObHashMap<DagKey, ObDDLDagMonitorNode *> node_map_;
};

} // end namespace storage
} // end namespace oceanbase

#endif /* OB_DDL_DAG_MONITOR_MGR_H_ */
