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

#define USING_LOG_PREFIX SHARE
#include "share/ob_ddl_sim_point.h"

using namespace oceanbase::common;
using namespace oceanbase::share;


ObDDLSimPointMgr &ObDDLSimPointMgr::get_instance()
{
  static ObDDLSimPointMgr instance;
  return instance;
}

ObDDLSimPointMgr::ObDDLSimPointMgr()
  : is_inited_(false), arena_("ddl_sim_pnt_mgr")
{
  memset(all_points_, 0, sizeof(all_points_));
}



class SimCountUpdater
{
public:
  explicit SimCountUpdater(int64_t step) : step_(step), old_trigger_count_(0) {}
  ~SimCountUpdater() = default;
  int operator() (hash::HashMapPair<ObDDLSimPointMgr::TaskSimPoint, int64_t> &entry) {
    old_trigger_count_ = entry.second;
    entry.second += step_;
    return OB_SUCCESS;
  }
public:
  int64_t step_;
  int64_t old_trigger_count_;
};



class SimCountCollector
{
public:
  SimCountCollector(ObIArray<ObDDLSimPointMgr::TaskSimPoint> &task_sim_points, ObIArray<int64_t> &sim_counts)
    : task_sim_points_(task_sim_points), sim_counts_(sim_counts) {}
  ~SimCountCollector() = default;
  int operator() (hash::HashMapPair<ObDDLSimPointMgr::TaskSimPoint, int64_t> &entry) {
    int ret = OB_SUCCESS;
    if (OB_FAIL(task_sim_points_.push_back(entry.first))) {
    } else if (OB_FAIL(sim_counts_.push_back(entry.second))) {
    }
    return ret;
  }
public:
  ObIArray<ObDDLSimPointMgr::TaskSimPoint> &task_sim_points_;
  ObIArray<int64_t> &sim_counts_;
};
