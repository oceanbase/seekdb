/*
 * Copyright (c) 2026 OceanBase.
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

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <ctime>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>
#define OB_SUCCESS 0
#define OB_ITER_END 1
#define OB_ENTRY_NOT_EXIST 2
#define OB_SUCC(x) ((x)==0)
#define OB_FAIL(x) ((ret=(x))!=0)
#define OB_TMP_FAIL(x) ((tmp_ret=(x))!=0)
#define OB_ISNULL(x) ((x)==nullptr)
#define COMMON_LOG(...) ((void)0)
#define ob_abort() std::abort()
namespace ObDagType { enum ObDagTypeEnum { DAG_TYPE_MINI_MERGE, DAG_TYPE_MDS_MINI_MERGE, DAG_TYPE_TX_TABLE_MERGE, DAG_TYPE_OTHER, DAG_TYPE_MAX }; }
namespace ObDagPrio { enum { DAG_PRIO_COMPACTION_HIGH, OTHER }; }
struct ObITask { int owner; };
struct ObIDag {
  enum ObDagStatus { DAG_STATUS_READY,DAG_STATUS_RETRY,DAG_STATUS_NODE_FAILED,DAG_STATUS_ABORT,DAG_STATUS_MAX,DAG_STATUS_NODE_RUNNING };
  ObIDag *next=nullptr,*prev=nullptr;
  ObDagType::ObDagTypeEnum type;
  ObDagStatus state=DAG_STATUS_READY;
  bool emergency=false,blocked=false,ready=true,stop=false,check=true,delete_on_finish=false;
  int indegree=0,running_tasks=0,task_error=0;
  int64_t size=0;
  ObITask task;
  ObIDag(int id=-1, ObDagType::ObDagTypeEnum t=ObDagType::DAG_TYPE_MINI_MERGE, int64_t bytes=0):type(t),size(bytes),task{id}{}
  ObIDag *get_next(){return next;}
  auto get_type(){return type;}
  bool get_emergency(){return emergency;}
  auto get_dag_status(){return state;}
  bool check_with_lock(){return check;}
  int get_indegree(){return indegree;}
  bool has_set_stop(){return stop;}
  int get_running_task_count(){return running_tasks;}
  int64_t get_data_size(){return size;}
  int get_next_ready_task(ObITask *&out){
    if(task_error)return task_error;
    if(!ready)return OB_ITER_END;
    out=&task;ready=false;return OB_SUCCESS;
  }
};
struct List {
  ObIDag head;
  List(){head.next=head.prev=&head;}
  bool is_empty(){return head.next==&head;}
  ObIDag *get_header(){return &head;}
  void add(ObIDag &d){d.prev=head.prev;d.next=&head;head.prev->next=&d;head.prev=&d;}
  void add_first(ObIDag &d){d.prev=&head;d.next=head.next;head.next->prev=&d;head.next=&d;}
  void erase(ObIDag &d){d.prev->next=d.next;d.next->prev=d.prev;d.next=d.prev=nullptr;}
  bool contains(const ObIDag &d){for(auto *p=head.next;p!=&head;p=p->next)if(p==&d)return true;return false;}
  void verify(){
    size_t n=0;
    for(auto *p=&head;;){
      if(!p->next || p->next->prev!=p)throw std::runtime_error("corrupt intrusive list");
      p=p->next;if(p==&head)break;
      if(++n>100000)throw std::runtime_error("list cycle");
    }
  }
};
struct Counts {
  int running[ObDagType::DAG_TYPE_MAX]={},count[ObDagType::DAG_TYPE_MAX]={},scheduled[ObDagType::DAG_TYPE_MAX]={};
  int get_running_dag_cnts(int t){return running[t];}
  int get_type_dag_cnt(int t){return count[t];}
  int get_scheduled_task_cnts(int t){return scheduled[t];}
};
struct ObDagPrioScheduler {
  enum {READY_DAG_LIST,WAITING_DAG_LIST};
  List dag_list_[2]; Counts counts; Counts *scheduler_=&counts;
  int priority_=ObDagPrio::DAG_PRIO_COMPACTION_HIGH;
  ObDagType::ObDagTypeEnum last_high_compaction_type_=ObDagType::DAG_TYPE_MAX;
  bool prefer_large_mini_=true;
  int64_t high_prio_dispatch_turn_=0;
  int pop_task_from_ready_list_(ObITask *&);
  void record_ready_task_dispatch_(const ObDagType::ObDagTypeEnum);
  std::vector<int> finished;
  void add(ObIDag &d){
    if(d.emergency)dag_list_[0].add_first(d);else dag_list_[0].add(d);
    counts.count[d.type]++;
  }
  int schedule_dag_(ObIDag &d,bool &wait){
    wait=d.blocked;
    if(!wait)d.state=ObIDag::DAG_STATUS_NODE_RUNNING;
    return OB_SUCCESS;
  }
  int move_dag_to_list_(ObIDag &d,int a,int b){dag_list_[a].erase(d);dag_list_[b].add(d);return OB_SUCCESS;}
  int finish_dag_(int,ObIDag *d,bool){
    finished.push_back(d->task.owner);counts.count[d->type]--;dag_list_[0].erase(*d);
    if(d->delete_on_finish)delete d;
    return OB_SUCCESS;
  }
};
