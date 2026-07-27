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
#ifndef OB_STORAGE_COMPACTION_COMPACTION_SCHEDUL_ITERATOR_H_
#define OB_STORAGE_COMPACTION_COMPACTION_SCHEDUL_ITERATOR_H_
#include "storage/tablet/ob_tablet_common.h"
#include "lib/container/ob_se_array.h"
#include "lib/literals/ob_literals.h"
#include "common/ob_tablet_id.h"
#include "storage/ls/ob_ls.h"

namespace oceanbase
{
namespace common
{
class ObTabletID;
}
namespace storage
{
class ObLS;
class ObTabletHandle;
class ObLSTabletService;
}
namespace compaction
{

class ObBasicMergeScheduleIterator
{
public:
  struct ObTabletArray
  {
    ObTabletArray();
    ~ObTabletArray() { reset(); }
    bool is_tablet_iter_end() const
    {
      return is_inited_ && (array_.empty() || tablet_idx_ >= array_.count());
    }
    void reset()
    {
      is_inited_ = false;
      array_.reuse();
      tablet_idx_ = 0;
    }
    void mark_inited()
    {
      is_inited_ = true;
      tablet_idx_ = 0;
    }
    int64_t count() const { return array_.count(); }
    int consume_tablet_id(ObTabletID &tablet_id);
    TO_STRING_KV(K_(tablet_idx), "tablet_cnt", count(), K_(array), K_(is_inited));
    static const int64_t TABLET_ID_ARRAY_CNT = 2000;
    int64_t tablet_idx_;
    common::ObSEArray<common::ObTabletID, TABLET_ID_ARRAY_CNT> array_;
    bool is_inited_;
  };
public:
  ObBasicMergeScheduleIterator();
  ~ObBasicMergeScheduleIterator() = default;
  int init(const int64_t schedule_batch_size, storage::ObLS *ls);
  storage::ObLS *get_ls() const { return ls_; }
  int get_next_tablet(storage::ObTabletHandle &tablet_handle);
  bool is_scan_finish() const { return scan_finish_; }
  bool database_merge_finish() const { return merge_finish_ & scan_finish_; }
  void update_merge_finish(const bool merge_finish) {
    merge_finish_ &= merge_finish;
  }
  void reset_basic_iter();
  bool is_valid() const;
  void finish_scan()
  {
    scan_finish_ = true;
    ls_ = nullptr;
    tablet_ids_.reset();
  }
  void start_cur_batch()
  {
    schedule_tablet_cnt_ = 0;
  }
  int64_t to_string(char *buf, const int64_t buf_len) const;
protected:
  virtual int get_tablet_ids() = 0;
  virtual int get_tablet_handle(const ObTabletID &tablet_id, storage::ObTabletHandle &tablet_handle) = 0;
protected:
  bool scan_finish_;
  bool merge_finish_;
  int64_t schedule_tablet_cnt_;
  int64_t max_batch_tablet_cnt_;
  storage::ObLS *ls_;
  ObTabletArray tablet_ids_;
};


class ObCompactionScheduleIterator : public ObBasicMergeScheduleIterator
{
public:
  ObCompactionScheduleIterator(const bool is_major);
  ~ObCompactionScheduleIterator() { reset(); }
  int build_iter(const int64_t schedule_batch_size);
  void set_report_scn_flag() { report_scn_flag_ = true; }
  bool need_report_scn() const { return report_scn_flag_; }
  void set_tablet_get_mode(const storage::ObMDSGetTabletMode mode) { tablet_get_mode_ = mode; }
  void reset();
protected:
  virtual int get_tablet_ids() override;
  virtual int get_tablet_handle(const ObTabletID &tablet_id, storage::ObTabletHandle &tablet_handle) override;
protected:
  static const int64_t CHECK_REPORT_SCN_INTERVAL = 5_min;
  bool is_major_;
  bool report_scn_flag_;
  storage::ObMDSGetTabletMode tablet_get_mode_;
};

} // namespace compaction
} // namespace oceanbase

#endif // OB_STORAGE_COMPACTION_COMPACTION_SCHEDUL_ITERATOR_H_
