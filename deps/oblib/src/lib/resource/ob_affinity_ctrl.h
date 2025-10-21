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

#ifndef OB_AFFINITY_CTRL_H
#define OB_AFFINITY_CTRL_H

#include <sched.h>
#include <stdint.h>
#include <sys/types.h>
#include <linux/mempolicy.h>
#include "lib/ob_define.h"

#define AFFINITY_CTRL (oceanbase::lib::ObAffinityCtrl::get_instance())

namespace oceanbase
{
namespace lib
{

class ObAffinityCtrl
{
public:
  ObAffinityCtrl();
  ~ObAffinityCtrl() {}
  int init();

  int run_on_node(const int node);
  int thread_bind_to_node(const int node_hint);
  int get_num_nodes() { return inited_ ? num_nodes_ : 1; }
  static int &get_tls_node() {
    static thread_local int node_ = OB_NUMA_SHARED_INDEX;

    return node_;
  }

  int32_t get_numa_id()
  {
    int32_t numa_id = 0;
    if (inited_) {
      numa_id = get_tls_node();
      if (OB_NUMA_SHARED_INDEX == numa_id) {
        numa_id = GETTID() % num_nodes_;
      }
    }
    return numa_id;
  }

  // global single instance ObAffinityCtrl
  static ObAffinityCtrl &get_instance();

private:
  int num_nodes_;
  int inited_;

  struct ObNumaNode {
    cpu_set_t cpu_set_mask;
  };

  ObNumaNode nodes_[OB_MAX_NUMA_NUM];
};

class ObNumaNodeGuard
{
public:
  explicit ObNumaNodeGuard(int numa_node): prev_numa_node_(ObAffinityCtrl::get_tls_node())
  {
    ObAffinityCtrl::get_tls_node() = numa_node;
  }
  ~ObNumaNodeGuard()
  {
    ObAffinityCtrl::get_tls_node() = prev_numa_node_;
  }
private:
  int prev_numa_node_;
};

}  // lib
}  // oceanbase

#endif  // OB_AFFINITY_CTRL_H
