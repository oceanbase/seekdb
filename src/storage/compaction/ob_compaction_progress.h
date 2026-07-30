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

#ifndef SRC_STORAGE_COMPACTION_OB_COMPACTION_PROGRESS_H_
#define SRC_STORAGE_COMPACTION_OB_COMPACTION_PROGRESS_H_

#include "ob_compaction_suggestion.h" // for ObInfoRingArray
#include "ob_partition_merge_progress.h"
#include "observer/scheduler/ob_dag_scheduler.h"
#include "storage/compaction/ob_tablet_merge_ctx.h"
namespace oceanbase
{
namespace compaction
{
struct ObCompactionProgressBase
{
  ObCompactionProgressBase()
    : merge_type_(compaction::INVALID_MERGE_TYPE),
      merge_version_(0),
      status_(share::ObIDag::DAG_STATUS_MAX),
      data_size_(0),
      unfinished_data_size_(0),
      original_size_(0),
      compressed_size_(0),
      start_time_(0),
      estimated_finish_time_(0)
  {
  }
  bool is_valid() const;
  void reset();

  TO_STRING_KV("merge_type", merge_type_to_str(merge_type_), K_(merge_version), K_(status), K_(data_size), K_(unfinished_data_size),
      K_(original_size), K_(compressed_size), K_(start_time), K_(estimated_finish_time));

  constexpr static double MERGE_SPEED = 1;  // almost 2 sec per macro_block
  constexpr static double EXTRA_TIME = 15 * 1000 * 1000; // 15 sec


  compaction::ObMergeType merge_type_;
  int64_t merge_version_;
  share::ObIDag::ObDagStatus status_;
  int64_t data_size_;
  int64_t unfinished_data_size_;
  int64_t original_size_;
  int64_t compressed_size_;
  int64_t start_time_;
  int64_t estimated_finish_time_;
};

struct ObCompactionProgress : public ObCompactionProgressBase
{
  ObCompactionProgress()
    : ObCompactionProgressBase(),
      is_inited_(false),
      total_tablet_cnt_(0),
      unfinished_tablet_cnt_(0),
      real_finish_cnt_(0),
      sum_time_guard_()
  {
  }
  ObCompactionProgress &operator=(const ObCompactionProgress &other);
  INHERIT_TO_STRING_KV("ObCompactionProgressBase", ObCompactionProgressBase, K_(is_inited), K_(total_tablet_cnt),
      K_(unfinished_tablet_cnt), K_(real_finish_cnt), K_(sum_time_guard));

  bool is_inited_;
  int64_t total_tablet_cnt_;
  int64_t unfinished_tablet_cnt_;
  int64_t real_finish_cnt_;
  ObStorageCompactionTimeGuard sum_time_guard_;
};

/*
 * ObCompactionProgressMgr
 * */
class ObCompactionProgressMgr : public ObInfoRingArray<ObCompactionProgress> {
public:
  static const int64_t SERVER_PROGRESS_MAX_CNT = 30;

  ObCompactionProgressMgr()
   : ObInfoRingArray(allocator_)
  {
    allocator_.set_attr(lib::ObMemAttr("CompProgMgr"));
  }
  ~ObCompactionProgressMgr() {}
  static int server_module_init(ObCompactionProgressMgr* &progress_mgr);
  int init();
  void destroy();
  int init_progress(const int64_t major_snapshot_version);
  int finish_progress(const int64_t major_snapshot_version);
  int update_progress(
      const int64_t major_snapshot_version,
      const int64_t total_data_size_delta,
      const int64_t scanned_data_size_delta,
      const int64_t estimate_finish_time,
      const bool finish_flag,
      const ObCompactionTimeGuard *time_guard = nullptr);
  int update_unfinish_tablet(
      const int64_t major_snapshot_version,
      const int64_t reduce_tablet_cnt = 1,
      const int64_t reduce_data_size = 0);
  int update_compression_ratio(const int64_t major_snapshot_version, compaction::ObSSTableMergeHistory &merge_history);

private:
  int loop_major_sstable_(int64_t version, int64_t &cnt, int64_t &size);
  int finish_progress_(ObCompactionProgress &progress);
  int get_pos_(const int64_t major_snapshot_version, int64_t &pos) const;

private:
  static const int64_t FINISH_TIME_UPDATE_FROM_SCHEDULER_INTERVAL = 10 * 1000 * 1000; // 1 second

private:
  ObArenaAllocator allocator_;
};

/*
 * ObCompactionSuggestionIterator
 * */

class ObCompactionProgressIterator
{
public:
  ObCompactionProgressIterator()
   : progress_array_(),
     cur_idx_(0),
     is_opened_(false)
  {
  }
  virtual ~ObCompactionProgressIterator() { reset(); }
  int open();
  int get_next_info(ObCompactionProgress &info);
  void reset();

private:
  ObArray<ObCompactionProgress> progress_array_;
  int64_t cur_idx_;
  bool is_opened_;
};


struct ObTabletCompactionProgress : public ObCompactionProgressBase
{
  ObTabletCompactionProgress()
    : ObCompactionProgressBase(),
      tablet_id_(0),
      dag_id_(),
      progressive_merge_round_(0),
      create_time_(0)
  {
  }
  INHERIT_TO_STRING_KV("ObCompactionProgressBase", ObCompactionProgressBase, K_(tablet_id),
      K_(dag_id), K_(progressive_merge_round), K_(create_time));

  int64_t tablet_id_;
  share::ObDagId dag_id_;
  int64_t progressive_merge_round_;
  int64_t create_time_;
};

struct ObDiagnoseTabletCompProgress : public ObCompactionProgressBase
{
  ObDiagnoseTabletCompProgress()
    : ObCompactionProgressBase(),
      is_suspect_abormal_(false),
      dag_id_(),
      create_time_(0),
      latest_update_ts_(0),
      base_version_(0),
      snapshot_version_(0)
  {
  }
  bool is_valid() const;
  INHERIT_TO_STRING_KV("ObCompactionProgressBase", ObCompactionProgressBase, K_(is_suspect_abormal),
      K_(create_time), K_(latest_update_ts), K_(dag_id), K_(base_version), K_(snapshot_version), K_(status));

  bool is_suspect_abormal_;
  share::ObDagId dag_id_;
  int64_t create_time_;
  int64_t latest_update_ts_;
  int64_t base_version_;
  int64_t snapshot_version_;
};

/*
 * ObCompactionSuggestionIterator
 * */

class ObTabletCompactionProgressIterator
{
public:
  ObTabletCompactionProgressIterator()
   : allocator_("PartProgress"),
     progress_array_(),
     cur_idx_(0),
     is_opened_(false)
  {
  }
  virtual ~ObTabletCompactionProgressIterator() { reset(); }
  int open();
  int get_next_info(ObTabletCompactionProgress &info);
  void reset();

private:
  ObArenaAllocator allocator_;
  common::ObArray<ObTabletCompactionProgress *> progress_array_;
  int64_t cur_idx_;
  bool is_opened_;
};

}//compaction
}//oceanbase

#endif
