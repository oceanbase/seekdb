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

#ifndef SRC_STORAGE_COMPACTION_OB_MEDIUM_CHECKER_H_
#define SRC_STORAGE_COMPACTION_OB_MEDIUM_CHECKER_H_

#include "lib/ob_define.h"
#include "lib/utility/ob_print_utils.h"
#include "storage/compaction/ob_tablet_check_info.h"
#include "share/tablet/ob_tablet_info.h"
#include "share/ob_occam_time_guard.h"
#include "storage/ob_i_store.h"
#include "storage/meta_mem/ob_tablet_handle.h"
#include "storage/compaction/ob_medium_compaction_mgr.h"

namespace oceanbase
{
namespace compaction
{

struct ObBatchFinishCheckStat
{
public:
  ObBatchFinishCheckStat()
    : succ_cnt_(0),
      finish_cnt_(0),
      fail_cnt_(0),
      filter_cnt_(0),
      failed_info_()
  {}
  ~ObBatchFinishCheckStat() {}
  DECLARE_TO_STRING;
  int64_t succ_cnt_;
  int64_t finish_cnt_;
  int64_t fail_cnt_;
  int64_t filter_cnt_;
  ObTabletCheckInfo failed_info_; // remain only one
};

class ObMediumChecker
{
public:
  static int server_module_init(ObMediumChecker *&tablet_medium_checker);
  ObMediumChecker();
  virtual ~ObMediumChecker();
  int init();
  void destroy();
  int check_medium_finish_schedule();
  int check_medium_finish(
      const ObIArray<ObTabletCheckInfo> &tablet_check_infos,
      int64_t start_idx,
      int64_t end_idx,
      ObArray<ObTabletCheckInfo> &check_tablet_infos,
      ObArray<ObTabletCheckInfo> &finish_tablet_infos,
      ObBatchFinishCheckStat &stat);
  int add_tablet(const ObTabletID &tablet_id, const int64_t medium_scn);
  TO_STRING_KV(K_(is_inited));

private:
  int reput_check_info(ObIArray<ObTabletCheckInfo> &tablet_check_infos);

public:
  static const int64_t DEFAULT_MAP_BUCKET = 1024;
  typedef common::ObArray<ObTabletCheckInfo> TabletCheckArray;
  typedef hash::ObHashSet<ObTabletCheckInfo, hash::NoPthreadDefendMode> TabletCheckSet;
private:
  bool is_inited_;
  TabletCheckSet tablet_check_set_;
  lib::ObMutex lock_;
};

}
}
#endif
