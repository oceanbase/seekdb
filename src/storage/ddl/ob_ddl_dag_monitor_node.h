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

#ifndef OB_DDL_DAG_MONITOR_NODE_H_
#define OB_DDL_DAG_MONITOR_NODE_H_

#include "lib/allocator/ob_allocator.h"
#include "lib/list/ob_dlink_node.h"
#include "lib/list/ob_dlist.h"
#include "lib/ob_errno.h"
#include "lib/utility/ob_print_utils.h"
#include "storage/ddl/ob_ddl_dag_monitor_info.h"

namespace oceanbase
{
namespace storage
{

class ObDDLDagMonitorNode
{
public:
  ObDDLDagMonitorNode()
    : dag_ptr_(0), trace_id_(0), create_ts_(0), finish_ts_(0),
      ref_cnt_(0), is_finished_(false)
  {
  }

  int init(uint64_t dag_ptr, uint64_t trace_id)
  {
    int ret = OB_SUCCESS;
    dag_ptr_ = dag_ptr;
    trace_id_ = trace_id;
    create_ts_ = common::ObTimeUtility::current_time();
    ref_cnt_ = 1;
    return ret;
  }

  void inc_ref() { ATOMIC_INC(&ref_cnt_); }
  void dec_ref() { ATOMIC_DEC(&ref_cnt_); }
  OB_INLINE int64_t get_ref_cnt() const { return ATOMIC_LOAD(&ref_cnt_); }

  int add_info(ObDDLDagMonitorInfo *info)
  {
    int ret = OB_SUCCESS;
    if (OB_FAIL(info_list_.add_last(info))) {
    }
    return ret;
  }

  void mark_finished()
  {
    finish_ts_ = common::ObTimeUtility::current_time();
    is_finished_ = true;
    dec_ref();
  }

  OB_INLINE uint64_t get_dag_ptr() const { return dag_ptr_; }
  OB_INLINE uint64_t get_trace_id() const { return trace_id_; }
  OB_INLINE int64_t get_create_ts() const { return create_ts_; }
  OB_INLINE int64_t get_finish_ts() const { return finish_ts_; }
  OB_INLINE bool is_finished() const { return is_finished_; }

  OB_INLINE const common::ObDList<ObDDLDagMonitorInfo> &get_info_list() const { return info_list_; }

  TO_STRING_KV(K_(dag_ptr), K_(trace_id), K_(create_ts), K_(finish_ts),
               K_(ref_cnt), K_(is_finished), "info_count", info_list_.get_size());

private:
  uint64_t dag_ptr_;
  uint64_t trace_id_;
  int64_t create_ts_;
  int64_t finish_ts_;
  int64_t ref_cnt_;
  bool is_finished_;
  common::ObDList<ObDDLDagMonitorInfo> info_list_;
};

} // end namespace storage
} // end namespace oceanbase

#endif /* OB_DDL_DAG_MONITOR_NODE_H_ */
