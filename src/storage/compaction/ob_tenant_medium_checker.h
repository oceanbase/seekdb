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

#ifndef SRC_STORAGE_COMPACTION_OB_TENANT_MEDIUM_CHECKER_H_
#define SRC_STORAGE_COMPACTION_OB_TENANT_MEDIUM_CHECKER_H_

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

class ObTenantMediumChecker
{
public:
  static int mtl_init(ObTenantMediumChecker *&tablet_medium_checker);
  ObTenantMediumChecker();
  virtual ~ObTenantMediumChecker();
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
  int64_t get_error_tablet_cnt() { return ATOMIC_LOAD(&error_tablet_cnt_); }
  void clear_error_tablet_cnt() { ATOMIC_STORE(&error_tablet_cnt_, 0); }
  void update_error_tablet_cnt(const int64_t delta_cnt)
  {
    // called when check tablet checksum error
    (void)ATOMIC_AAF(&error_tablet_cnt_, delta_cnt);
  }
  TO_STRING_KV(K_(is_inited));

private:
  int reput_check_info(ObIArray<ObTabletCheckInfo> &tablet_check_infos);

public:
  static const int64_t DEFAULT_MAP_BUCKET = 1024;
  static const int64_t CLEAR_CKM_ERROR_INTERVAL = 2 * 60 * 1000 * 1000L; // 2m
  typedef common::ObArray<ObTabletCheckInfo> TabletCheckArray;
  typedef hash::ObHashSet<ObTabletCheckInfo, hash::NoPthreadDefendMode> TabletCheckSet;
private:
  bool is_inited_;
  int64_t error_tablet_cnt_; // for diagnose
  TabletCheckSet tablet_check_set_;
  lib::ObMutex lock_;
};

}
}
#endif
