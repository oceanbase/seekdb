/**
 * Copyright (c) 2023 OceanBase
 * OceanBase CE is licensed under Mulan PubL v2.
 * You can use this software according to the terms and conditions of the Mulan PubL v2.
 * You may obtain a copy of Mulan PubL v2 at:
 *          http://license.coscl.org.cn/MulanPubL-2.0
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND,
 * EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
 * MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
 * See the Mulan PubL v2 for more details.
 */

#define USING_LOG_PREFIX SHARE
#include "share/ob_ddl_sim_point.h"

using namespace oceanbase::common;
using namespace oceanbase::share;


int64_t ObTenantDDLSimContext::to_string(char* buf, const int64_t buf_len) const
{
  int64_t pos = 0;
  J_OBJ_START();
  const int64_t MAX_FIXED_POINT_COUNT = 64;
  ObDDLSimPointID fixed_points[MAX_FIXED_POINT_COUNT];
  int fixed_point_count = 0;
  for (int64_t i = 0; i < MAX_DDL_SIM_POINT_ID; ++i) {
    if (nullptr != fixed_points_ && fixed_points_[i] && i < MAX_FIXED_POINT_COUNT) {
      fixed_points[fixed_point_count++] = static_cast<ObDDLSimPointID>(i);
    }
  }
  J_KV(K(tenant_id_), K(type_), K(seed_), K(trigger_percent_),
      "fixed_points_", ObArrayWrap<ObDDLSimPointID>(fixed_points, fixed_point_count));
  J_OBJ_END();
  return pos;
}

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



class TenantContextUpdater
{
public:
  TenantContextUpdater(const ObTenantDDLSimContext &tenant_context, const ObIArray<int64_t> &fixed_points_array)
    : new_context_(tenant_context), fixed_point_array_(fixed_points_array) {}
  ~TenantContextUpdater() = default;
  int operator() (hash::HashMapPair<uint64_t, ObTenantDDLSimContext> &entry) {
    int ret = OB_SUCCESS;
    if (new_context_.trigger_percent_ > 0 && 0 == entry.second.trigger_percent_) {
      entry.second.seed_ = new_context_.seed_;
      entry.second.trigger_percent_ = new_context_.trigger_percent_;
      entry.second.type_ = new_context_.type_;
    }
    const int64_t point_map_size = sizeof(bool) * MAX_DDL_SIM_POINT_ID;
    if (fixed_point_array_.count() > 0) {
      if (nullptr == entry.second.fixed_points_) {
        void *buf = ObDDLSimPointMgr::get_instance().get_arena_allocator().alloc(point_map_size);
        if (OB_ISNULL(buf)) {
          ret = OB_ALLOCATE_MEMORY_FAILED;
          LOG_WARN("allocate memory failed", K(ret), K(point_map_size));
        } else {
          bool *tmp_map = new bool[MAX_DDL_SIM_POINT_ID];
          memset(tmp_map, 0, point_map_size);
          entry.second.fixed_points_ = tmp_map;
        }
      }
    }
    if (nullptr != entry.second.fixed_points_) {
      memset(entry.second.fixed_points_, 0, point_map_size);
    }
    for (int64_t i = 0; OB_SUCC(ret) && i < fixed_point_array_.count(); ++i) {
      const int64_t point_id = fixed_point_array_.at(i);
      entry.second.fixed_points_[point_id] = true;
    }
    LOG_INFO("update tenant param of ddl sim point success", K(new_context_), K(fixed_point_array_), K(entry.second));
    return OB_SUCCESS;
  }
public:
  const ObTenantDDLSimContext &new_context_;
  const ObIArray<int64_t> &fixed_point_array_;
};


int ObDDLSimPointMgr::generate_task_sim_map(const ObTenantDDLSimContext &tenant_context, const int64_t current_task_id, const std::initializer_list<ObDDLSimPointID> &point_ids)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(!is_inited_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", K(ret), K(is_inited_));
  } else if (OB_UNLIKELY(tenant_context.trigger_percent_ <= 0)) {
    // skip
  } else {
    const uint64_t tenant_id = tenant_context.tenant_id_;
    const int64_t seed = tenant_context.seed_;
    const int64_t trigger_percent = tenant_context.trigger_percent_;
    for (std::initializer_list<ObDDLSimPointID>::iterator it = point_ids.begin(); OB_SUCC(ret) && it != point_ids.end(); ++it) {
      ObDDLSimPointID point_id = *it;
      const ObDDLSimPoint &cur_sim_point = all_points_[point_id];
      if (cur_sim_point.is_valid() && (SIM_TYPE_ALL == tenant_context.type_ || tenant_context.type_ == cur_sim_point.type_)) {
        srand(static_cast<int32_t>((tenant_id + seed) * current_task_id * point_id));
        if (rand() % 100 < trigger_percent) {
          if (OB_FAIL(task_sim_map_.set_refactored(TaskSimPoint(tenant_id, current_task_id, point_id), 0))) {
            if (OB_HASH_EXIST != ret) {
              LOG_WARN("set task sim point into map failed", K(ret), K(tenant_id), K(current_task_id), K(point_id));
            } else {
              ret = OB_SUCCESS;
            }
          }
        }
      }
    }
  }
  return ret;
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
      LOG_WARN("push back task sim point failed", K(ret), K(entry.first));
    } else if (OB_FAIL(sim_counts_.push_back(entry.second))) {
      LOG_WARN("push back sim count failed", K(ret), K(entry.second));
    }
    return ret;
  }
public:
  ObIArray<ObDDLSimPointMgr::TaskSimPoint> &task_sim_points_;
  ObIArray<int64_t> &sim_counts_;
};

