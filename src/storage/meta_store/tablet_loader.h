
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

#include "storage/meta_mem/ob_tablet_map_key.h"
#include "storage/meta_store/ob_startup_accel_task_handler.h"
#include "storage/meta_mem/ob_meta_obj_struct.h"

namespace oceanbase
{

namespace storage
{
class ObLS;
class TabletLoader;
class ObLSTabletService;

struct TabletLoadItem
{
public:
  TabletLoadItem(const ObTabletMapKey &key, const ObMetaDiskAddr &addr)
    : key_(key), addr_(addr) {}
  TabletLoadItem()
    : key_(), addr_() {}
  ~TabletLoadItem() {}
  bool operator<(const TabletLoadItem &r) const;
  TO_STRING_KV(K_(key), K_(addr));

  ObTabletMapKey key_;
  ObMetaDiskAddr addr_;
};

using TabletLoadRange = std::pair<int64_t, int64_t>; // record the start_item_idx and end_item_idx in total_tablet_item_arr

class TabletLoadTask : public storage::ObStartupAccelTask
{
public:
  enum Type
  {
    DISCRETE = 0,
    AGGREGATE = 1,
    MAX = 2,
  };
  TabletLoadTask()
    : is_inited_(false),
      idx_(-1),
      type_(Type::MAX),
      ranges_(),
      tablet_cnt_(0),
      handler_(nullptr) {}

  virtual ~TabletLoadTask()
  {
    destroy();
  }
  int init(const int64_t task_idx, const TabletLoadTask::Type type,
      TabletLoader *handler);

  int execute() override;
  int add_item_range(const TabletLoadRange &range, bool &is_enough);

  VIRTUAL_TO_STRING_KV(K_(idx), K_(type), KP(this),
      "range_count", ranges_.count(), K_(tablet_cnt));

private:
  static const int64_t MAX_DISCRETE_TABLET_CNT_PER_TASK = 200;
  static const int64_t MAX_AGGREGATE_BLOCK_CNT_PER_TASK = 3;
  void destroy();

private:
  bool is_inited_;
  int64_t idx_;
  Type type_;
  common::ObSEArray<TabletLoadRange, MAX_DISCRETE_TABLET_CNT_PER_TASK> ranges_;
  int64_t tablet_cnt_;
  TabletLoader *handler_;
};

//============================= TabletLoader ==============================//

class TabletLoader
{
public:
  // The caller owns the array; load() waits for every task before returning.
  explicit TabletLoader(common::ObIArray<TabletLoadItem> &tablets);
  int load(storage::ObStartupAccelTaskHandler* startup_accel_handler);

  int load_discrete_tablets(const ObIArray<TabletLoadRange> &range_arr);
  int load_aggregate_tablets(const ObIArray<TabletLoadRange> &range_arr);

  void inc_inflight_task_cnt() { ATOMIC_INC(&inflight_task_cnt_); }
  void dec_inflight_task_cnt() { ATOMIC_DEC(&inflight_task_cnt_); }
  void inc_finished_tablet_cnt(const int64_t cnt) { (void)ATOMIC_FAA(&finished_tablet_cnt_, cnt); }
  void set_errcode(const int errcode) { ATOMIC_STORE(&errcode_, errcode); };

private:
  static bool is_suitable_to_aggregate_(const int64_t tablet_cnt_in_block, const int64_t valid_size_in_block)
  {
    return tablet_cnt_in_block > AGGREGATE_CNT_THRESHOLD && valid_size_in_block > AGGREGATE_SIZE_THRESHOLD; 
  }
  static int replay_create_tablet(const TabletLoadItem &item, const char *buf, const int64_t buf_len);
  int add_item_range_to_task_(
      storage::ObStartupAccelTaskHandler* startup_accel_handler,
      const TabletLoadTask::Type type,
      const TabletLoadRange &range, TabletLoadTask *&task);
  int add_task_(
      storage::ObStartupAccelTaskHandler* startup_accel_handler,
      TabletLoadTask *task);

private:
  static const int64_t AGGREGATE_CNT_THRESHOLD = 16;
  static const int64_t AGGREGATE_SIZE_THRESHOLD = 256 << 10; // 256K

  int64_t task_idx_;
  int64_t inflight_task_cnt_;
  int64_t finished_tablet_cnt_;
  int errcode_;
  TabletLoadItem *total_tablet_item_arr_;
  int64_t total_tablet_cnt_;
  TabletLoadTask *aggregate_task_;
  TabletLoadTask *discrete_task_;
};

} // namespace storage
} // namespace oceanbase
