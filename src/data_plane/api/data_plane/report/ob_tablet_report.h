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

#ifndef OCEANBASE_DATA_PLANE_REPORT_OB_TABLET_REPORT_H_
#define OCEANBASE_DATA_PLANE_REPORT_OB_TABLET_REPORT_H_

#include <stdint.h>

#include "common/ob_tablet_id.h"
#include "lib/utility/ob_print_utils.h"

namespace oceanbase
{
namespace common
{
template <typename T>
class ObIArray;
}
namespace data_plane
{

// Stable diagnostic data exposed by the tablet-report module. Queue task
// classes, scheduling policy, and worker implementation remain private to
// Observer.
struct ObTabletUpdateTaskInfo final
{
  ObTabletUpdateTaskInfo()
      : tablet_id_(), add_timestamp_(0), start_timestamp_(0)
  {}
  ObTabletUpdateTaskInfo(
      const common::ObTabletID &tablet_id,
      const int64_t add_timestamp,
      const int64_t start_timestamp)
      : tablet_id_(tablet_id),
        add_timestamp_(add_timestamp),
        start_timestamp_(start_timestamp)
  {}

  TO_STRING_KV(K_(tablet_id), K_(add_timestamp), K_(start_timestamp));

  common::ObTabletID tablet_id_;
  int64_t add_timestamp_;
  int64_t start_timestamp_;
};

// Enqueue a tablet-table refresh. The Observer queue and retry policy are
// implementation details and are not visible to Storage.
int submit_tablet_update(
    const common::ObTabletID &tablet_id,
    bool need_diagnose = false);

// Recalculate the tenant-local worker count from the current runtime config.
int refresh_tablet_update_worker_count();

// Return whether a tablet update is waiting or currently being processed.
int get_tablet_update_task_status(
    const common::ObTabletID &tablet_id,
    bool &is_waiting,
    bool &is_processing);

// Replace both output arrays with a bounded sample of stalled tasks.
int get_stalled_tablet_update_tasks(
    common::ObIArray<ObTabletUpdateTaskInfo> &waiting_tasks,
    common::ObIArray<ObTabletUpdateTaskInfo> &processing_tasks);

} // namespace data_plane
} // namespace oceanbase

#endif // OCEANBASE_DATA_PLANE_REPORT_OB_TABLET_REPORT_H_
