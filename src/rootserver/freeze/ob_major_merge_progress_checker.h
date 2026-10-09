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

#ifndef OCEANBASE_ROOTSERVER_FREEZE_OB_MAJOR_MERGE_PROGRESS_CHECKER_
#define OCEANBASE_ROOTSERVER_FREEZE_OB_MAJOR_MERGE_PROGRESS_CHECKER_
#include "rootserver/freeze/ob_major_merge_progress_util.h"
#include "share/ob_freeze_info_proxy.h"
#include <map>

namespace oceanbase {
namespace rootserver {
class ObBasicMergeProgressChecker
{
public:
  virtual ~ObBasicMergeProgressChecker() {}
  virtual int init(bool is_primary_service) = 0;
  virtual int set_basic_info(const share::ObFreezeInfo &freeze_info) = 0;
  virtual int clear_cached_info() = 0;
  virtual int check_progress() = 0;
  virtual void reset_uncompacted_tablets() = 0;
  virtual int get_uncompacted_tablets(common::ObArray<share::ObTabletRuntimeInfo> &tablets,
      common::ObArray<uint64_t> &table_ids) const = 0;
  virtual const compaction::ObBasicMergeProgress &get_merge_progress() const = 0;
};

class ObMajorMergeProgressChecker final : public ObBasicMergeProgressChecker
{
public:
  explicit ObMajorMergeProgressChecker(volatile bool &stop) : stop_(stop) {}
  int init(bool is_primary_service) override;
  int set_basic_info(const share::ObFreezeInfo &freeze_info) override;
  int clear_cached_info() override;
  int check_progress() override;
  void reset_uncompacted_tablets() override { pending_.reset(); }
  int get_uncompacted_tablets(common::ObArray<share::ObTabletRuntimeInfo> &tablets,
      common::ObArray<uint64_t> &table_ids) const override
  { return pending_.get_uncompact_info(tablets, table_ids); }
  const compaction::ObBasicMergeProgress &get_merge_progress() const override { return progress_; }
private:
  bool initialized_ = false;
  bool primary_ = false;
  volatile bool &stop_;
  share::ObFreezeInfo freeze_;
  compaction::ObMergeProgress progress_;
  compaction::ObUncompactInfo pending_;
  // Only completed owners' counters in this F. No schemas or checksums
  // survive a pass. Recovery verifies again before releasing any result.
  std::map<uint64_t, compaction::ObMergeProgress> completed_;
  DISALLOW_COPY_AND_ASSIGN(ObMajorMergeProgressChecker);
};
} // namespace rootserver
} // namespace oceanbase
#endif
