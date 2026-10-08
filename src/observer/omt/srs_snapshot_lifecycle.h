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
#pragma once
#include <cstdint>
#include "lib/ob_errno.h"

namespace oceanbase::omt {
// The provider lock excludes new acquisitions; releases may run concurrently.
// Retiring a referenced snapshot transfers ownership to the queue. Never test
// its count again and dispose it while that queue still contains its address.
template <typename Snapshot, typename Retired, typename Dispose>
int publish_srs_snapshot(Snapshot *&current, Snapshot *&candidate, Retired &retired, Dispose dispose)
{
  if (candidate == nullptr || candidate == current) return common::OB_INVALID_ARGUMENT;
  int ret = common::OB_SUCCESS;
  if (current != nullptr) {
    if (current->get_ref_count() > 0) ret = retired.push_back(current);
    else dispose(current);
  }
  if (ret == common::OB_SUCCESS) {
    current = candidate;
  } else {
    // Keep the old current snapshot on queue-allocation failure, and release
    // the unpublished candidate. The caller will keep its stale flag set.
    dispose(candidate);
  }
  candidate = nullptr;
  return ret;
}

// Called under the same acquisition/publication lock. A retired snapshot can
// only lose references; after observing zero, no reader may acquire it again.
template <typename Retired, typename Dispose>
void collect_srs_snapshots(Retired &retired, Dispose dispose)
{
  for (int64_t i = retired.size() - 1; i >= 0; --i) {
    auto *snapshot = retired[i];
    if (snapshot != nullptr && snapshot->get_ref_count() == 0) {
      retired.remove(i);
      dispose(snapshot);
    }
  }
}
} // namespace oceanbase::omt
