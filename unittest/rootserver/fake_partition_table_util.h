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

#ifndef OCEANBASE_ROOTSERVER_FAKE_PARTITION_TABLE_UTIL_H_
#define OCEANBASE_ROOTSERVER_FAKE_PARTITION_TABLE_UTIL_H_

#include <utility>
#include "rootserver/ob_partition_table_util.h"
#include "rootserver/ob_zone_manager.h"

namespace oceanbase
{
namespace rootserver
{
class FakePartitionTableUtil : public ObPartitionTableUtil
{
public:
  FakePartitionTableUtil(ObZoneManager &cm) : cm_(cm) {}

  virtual int check_merge_progress(const volatile bool &stop, const int64_t version,
      ObZoneMergeProgress &all_progress, bool &all_merged)
  {
    UNUSED(version);
    int ret = common::OB_SUCCESS;
    all_progress.reset();
    int64_t zone_count = 0;
    if (OB_FAIL(cm_.get_zone_count(zone_count))) {
      RS_LOG(WARN, "get_zone_count failed", K(ret));
    }
    for (int64_t i = 0; !stop && common::OB_SUCCESS == ret && i < zone_count; ++i) {
      share::ObZoneInfo info;
      cm_.get_zone(i, info);
      MergeProgress progress;
      progress.zone_ = info.zone_;
      progress.merged_data_size_ = 100;
      progress.merged_partition_cnt_ = 100;
      all_progress.push_back(progress);
    }
    all_merged = true;
    return ret;
  }

  virtual int set_leader_backup_flag(const volatile bool &, const bool , ObPartitionTableUtil::ObLeaderInfoArray *)
  {
    return common::OB_SUCCESS;
  }

private:
  ObZoneManager &cm_;
};

} // end namespace rootserver
} // end namespace oceanbase

#endif // OCEANBASE_ROOTSERVER_FAKE_PARTITION_TABLE_UTIL_H_
