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

#ifndef OCEANBASE_DATA_PLANE_DDL_OB_DDL_SCHEDULE_H_
#define OCEANBASE_DATA_PLANE_DDL_OB_DDL_SCHEDULE_H_

#include <stdint.h>
#include <map>
#include <memory>
#include <mutex>
#include <vector>

#include "lib/container/ob_iarray.h"
#include "lib/ob_errno.h"
#include "lib/utility/ob_print_utils.h"

namespace oceanbase
{
namespace data_plane
{

// The only schedule detail Storage needs in order to assign local DDL slices.
// Rootserver's persisted task record and SQL range representation stay private
// to the coordinator adapter.
struct ObDDLTabletSliceCount final
{
  ObDDLTabletSliceCount() : tablet_id_(0), slice_count_(0) {}
  ObDDLTabletSliceCount(const int64_t tablet_id, const int64_t slice_count)
      : tablet_id_(tablet_id), slice_count_(slice_count) {}
  TO_STRING_KV(K_(tablet_id), K_(slice_count));

  int64_t tablet_id_;
  int64_t slice_count_;
};

// A DDL sampling result exists only while at least one direct-insert session
// for the task is alive.  It bridges the PX sampling and root-insert DFOs;
// storage receives a value copy over its normal direct-insert request.
class DirectInsertSchedule final
{
public:
  int publish(const common::ObIArray<ObDDLTabletSliceCount> &slice_counts)
  {
    int ret = common::OB_SUCCESS;
    std::vector<ObDDLTabletSliceCount> staged;
    if (slice_counts.empty()) {
      ret = common::OB_INVALID_ARGUMENT;
    } else {
      staged.reserve(slice_counts.count());
      for (int64_t i = 0; common::OB_SUCCESS == ret && i < slice_counts.count(); ++i) {
        const ObDDLTabletSliceCount &entry = slice_counts.at(i);
        // ObPxTabletRange uses int64_t as a bit container. Namespace storage
        // IDs may set the uint64_t high bit, and an unpartitioned PX schedule
        // uses zero as its tablet placeholder.
        if (entry.slice_count_ <= 0) {
          ret = common::OB_INVALID_ARGUMENT;
        } else {
          staged.push_back(entry);
        }
      }
    }
    if (common::OB_SUCCESS == ret) {
      std::lock_guard<std::mutex> guard(mutex_);
      slice_counts_.swap(staged);
      ready_ = true;
    }
    return ret;
  }

  int snapshot(std::vector<ObDDLTabletSliceCount> &slice_counts) const
  {
    int ret = common::OB_SUCCESS;
    std::lock_guard<std::mutex> guard(mutex_);
    if (!ready_) {
      ret = common::OB_STATE_NOT_MATCH;
    } else {
      slice_counts = slice_counts_;
    }
    return ret;
  }

private:
  mutable std::mutex mutex_;
  std::vector<ObDDLTabletSliceCount> slice_counts_;
  bool ready_ = false;
};

// The registry keeps weak references only. Sessions own schedules and release
// their key during teardown, so completed DDL tasks leave no resident entry.
class DirectInsertScheduleRegistry final
{
public:
  std::shared_ptr<DirectInsertSchedule> acquire(const int64_t task_id)
  {
    std::shared_ptr<DirectInsertSchedule> schedule;
    if (task_id > 0) {
      std::lock_guard<std::mutex> guard(mutex_);
      auto &entry = schedules_[task_id];
      schedule = entry.lock();
      if (!schedule) {
        schedule = std::make_shared<DirectInsertSchedule>();
        entry = schedule;
      }
    }
    return schedule;
  }

  int publish(const int64_t task_id,
              const common::ObIArray<ObDDLTabletSliceCount> &slice_counts)
  {
    int ret = common::OB_SUCCESS;
    std::shared_ptr<DirectInsertSchedule> schedule;
    {
      std::lock_guard<std::mutex> guard(mutex_);
      auto iter = schedules_.find(task_id);
      if (iter != schedules_.end()) {
        schedule = iter->second.lock();
        if (!schedule) {
          schedules_.erase(iter);
        }
      }
    }
    // Dynamic sampling is also used without direct insert. In that case there
    // is deliberately no consumer to publish to.
    if (schedule) {
      ret = schedule->publish(slice_counts);
    }
    return ret;
  }

  void release(const int64_t task_id,
               std::shared_ptr<DirectInsertSchedule> &schedule)
  {
    schedule.reset();
    std::lock_guard<std::mutex> guard(mutex_);
    auto iter = schedules_.find(task_id);
    if (iter != schedules_.end() && iter->second.expired()) {
      schedules_.erase(iter);
    }
  }

private:
  std::mutex mutex_;
  std::map<int64_t, std::weak_ptr<DirectInsertSchedule>> schedules_;
};

} // namespace data_plane
} // namespace oceanbase

#endif // OCEANBASE_DATA_PLANE_DDL_OB_DDL_SCHEDULE_H_
