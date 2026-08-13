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

#ifndef SRC_DATA_PLANE_API_SCHEDULER_OB_SYS_TASK_STAT_H_
#define SRC_DATA_PLANE_API_SCHEDULER_OB_SYS_TASK_STAT_H_

#include "lib/allocator/page_arena.h"
#include "lib/container/ob_se_array.h"
#include "lib/container/ob_se_array_iterator.h"
#include "lib/list/ob_dlist.h"
#include "lib/profile/ob_trace_id.h"
#include "lib/string/ob_string.h"
#include "share/ob_define.h"
#include "data_plane/scheduler/ob_sys_task_type.h"

namespace oceanbase
{
namespace share
{

const char *sys_task_type_to_str(const ObSysTaskType &type);

struct ObSysTaskStat
{
  ObSysTaskStat();
  int64_t start_time_;
  ObTaskId task_id_;
  ObSysTaskType task_type_;
  common::ObAddr svr_ip_;
  common::ObString comment_;
  bool is_cancel_;

  TO_STRING_KV(K_(start_time), K_(task_id), K_(task_type), K_(svr_ip), K_(is_cancel), K_(comment));
};

class ObSysStatMgrIter
{
public:
  ObSysStatMgrIter();
  ~ObSysStatMgrIter();

  void reset();
  int push(const ObSysTaskStat &item);
  int set_ready();
  bool is_ready() const { return is_ready_; }
  int get_next(ObSysTaskStat &item);

private:
  bool is_ready_;
  common::ObArenaAllocator allocator_;
  common::ObSEArray<ObSysTaskStat, 0> item_arr_;
  common::ObSEArray<ObSysTaskStat, 0>::iterator it_;
  DISALLOW_COPY_AND_ASSIGN(ObSysStatMgrIter);
};

class ObSysTaskStatMgr
{
public:
  ObSysTaskStatMgr();
  virtual ~ObSysTaskStatMgr();

  static ObSysTaskStatMgr &get_instance();

  int add_task(ObSysTaskStat &status);
  int get_iter(ObSysStatMgrIter &iter);
  int del_task(const ObTaskId &task_id);
  int set_self_addr(const common::ObAddr addr);
  int task_exist(const ObTaskId &task_id, bool &is_exist);
  int cancel_task(const ObTaskId &task_id);
  int is_task_cancel(const ObTaskId &task_id, bool &is_cancel);
  int generate_task_id(ObTaskId &task_id);
private:
  struct ObSysTaskStatNode : public common::ObDLinkBase<ObSysTaskStatNode>
  {
    explicit ObSysTaskStatNode(const ObSysTaskStat &task)
      : common::ObDLinkBase<ObSysTaskStatNode>(), task_(task)
    {
    }

    ObSysTaskStat task_;
  };

  int alloc_task_node_(const ObSysTaskStat &task, ObSysTaskStatNode *&node);
  void free_task_node_(ObSysTaskStatNode *node);
  void clear_task_list_();

private:
  common::SpinRWLock lock_;
  common::ObDList<ObSysTaskStatNode> task_list_;
  common::ObAddr self_addr_;
  DISALLOW_COPY_AND_ASSIGN(ObSysTaskStatMgr);
};

}//share
}//oceanbase

#define SYS_TASK_STATUS_MGR (::oceanbase::share::ObSysTaskStatMgr::get_instance())

#endif /* SRC_DATA_PLANE_API_SCHEDULER_OB_SYS_TASK_STAT_H_ */
