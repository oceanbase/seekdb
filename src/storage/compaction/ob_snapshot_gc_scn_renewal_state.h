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

#ifndef OCEANBASE_STORAGE_COMPACTION_OB_SNAPSHOT_GC_SCN_RENEWAL_STATE_
#define OCEANBASE_STORAGE_COMPACTION_OB_SNAPSHOT_GC_SCN_RENEWAL_STATE_

#include <stdint.h>
#include "lib/atomic/ob_atomic.h"

namespace oceanbase
{
namespace storage
{

class ObSnapshotGcScnRenewalState
{
public:
  ObSnapshotGcScnRenewalState() : renew_target_scn_(0) {}
  ~ObSnapshotGcScnRenewalState() = default;

  void update_target_scn(const int64_t target_scn)
  {
    if (target_scn > 0 && INT64_MAX != target_scn) {
      int64_t old_scn = ATOMIC_LOAD(&renew_target_scn_);
      while (old_scn < target_scn) {
        const int64_t actual_scn = ATOMIC_VCAS(
            &renew_target_scn_, old_scn, target_scn);
        if (old_scn == actual_scn) {
          break;
        } else {
          old_scn = actual_scn;
        }
      }
    }
  }
  int64_t get_target_scn() const { return ATOMIC_LOAD(&renew_target_scn_); }

private:
  int64_t renew_target_scn_;

private:
  DISALLOW_COPY_AND_ASSIGN(ObSnapshotGcScnRenewalState);
};

} // namespace storage
} // namespace oceanbase

#endif // OCEANBASE_STORAGE_COMPACTION_OB_SNAPSHOT_GC_SCN_RENEWAL_STATE_
