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
#ifndef OB_SHARE_COMPACTION_SCHEDULE_BATCH_SIZE_MGR_H_
#define OB_SHARE_COMPACTION_SCHEDULE_BATCH_SIZE_MGR_H_
#include "/usr/include/stdint.h"
namespace oceanbase
{
namespace compaction
{
struct ObScheduleBatchSizeMgr
{
  static const int64_t DEFAULT_TABLET_BATCH_CNT = 50 * 1000L; // 5w
  ObScheduleBatchSizeMgr()
    : tablet_batch_size_(DEFAULT_TABLET_BATCH_CNT)
  {}
  ~ObScheduleBatchSizeMgr() {}
  void set_tablet_batch_size(const int64_t tablet_batch_size);
  int64_t get_schedule_batch_size() const { return tablet_batch_size_; }
  int64_t get_checker_batch_size() const;
  void get_rs_check_batch_size(
    const int64_t table_cnt,
    int64_t &table_id_batch_size) const;
  int64_t get_inner_table_scan_batch_size() const;
  static bool need_rebuild_map(
    const int64_t default_map_bucket_cnt,
    const int64_t item_cnt,
    const int64_t cur_bucket_cnt,
    int64_t &recommend_map_bucked_cnt);
private:
  const static int64_t TABLET_ID_BATCH_CHECK_SIZE = 3000;
  const static int64_t TABLE_ID_BATCH_CHECK_SIZE = 200;
  const static int64_t TOTAL_TABLE_CNT_THREASHOLD = 100 * 1000; // 10w
  const static int64_t DEFAULT_INNER_TABLE_SCAN_BATCH_SIZE = 500;
  const static int64_t DEFAULT_CHECKER_BATCH_SIZE = 200;
  // cached compaction_schedule_tablet_batch_cnt: [10000,200000]
  int64_t tablet_batch_size_;
};


} // namespace compaction
} // namespace oceanbase

#endif // OB_SHARE_COMPACTION_SCHEDULE_BATCH_SIZE_MGR_H_
