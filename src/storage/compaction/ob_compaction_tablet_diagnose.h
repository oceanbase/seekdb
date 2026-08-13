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

#ifndef SRC_STORAGE_COMPACTION_OB_COMPACTION_TABLET_DIAGNOSE_H_
#define SRC_STORAGE_COMPACTION_OB_COMPACTION_TABLET_DIAGNOSE_H_

#include "data_plane/scheduler/ob_diagnose_config.h"

namespace oceanbase
{
namespace compaction
{
struct ObDiagnoseTablet {
  ObDiagnoseTablet()
    : tablet_id_()
  {}
  explicit ObDiagnoseTablet(const ObTabletID &tablet_id)
    : tablet_id_(tablet_id)
  {}
  ~ObDiagnoseTablet() {}
  inline bool operator == (const ObDiagnoseTablet &other) const
  {
    return tablet_id_ == other.tablet_id_;
  }
  inline int hash(uint64_t &hash_value) const
  {
    int ret = common::OB_SUCCESS;
    hash_value = murmurhash(&tablet_id_, sizeof(tablet_id_), 0);
    return ret;
  }
  inline bool is_valid() const
  {
    return tablet_id_.is_valid();
  }
  static bool is_flagged(int64_t flag, const share::ObDiagnoseTabletType type)
  {
    return flag & (1 << static_cast<int64_t>(type));
  }
  static void set_flag(int64_t &flag, const share::ObDiagnoseTabletType type)
  {
    flag |= (1 << static_cast<int64_t>(type));
  }
  static void del_flag(int64_t &flag, const share::ObDiagnoseTabletType type)
  {
    flag &= ~(1 << static_cast<int64_t>(type));
  }
  // input_flag = 100100, other_flag = 011100 -> input_flag  = 000100
  static void sub_flag(int64_t &input_flag, const int64_t other_flag)
  {
    int64_t bits = 0;
    while (bits < share::ObDiagnoseTabletType::TYPE_DIAGNOSE_TABLET_MAX) {
      const int64_t flag = other_flag & (1 << bits++);
      input_flag &= ~flag;
    }
  }
  TO_STRING_KV(K_(tablet_id));

  ObTabletID tablet_id_;
};

class ObDiagnoseTabletMgr {
public:
  static int server_module_init(ObDiagnoseTabletMgr *&diagnose_tablet_mgr);
  ObDiagnoseTabletMgr();
  virtual ~ObDiagnoseTabletMgr() { destroy(); }

  int init();
  void destroy();

  // for diagnose
  int add_diagnose_tablet(
      const ObTabletID &tablet_id, 
      const share::ObDiagnoseTabletType type);
  int get_diagnose_tablets(ObIArray<ObDiagnoseTablet> &diagnose_tablets);
  int delete_diagnose_tablet(
      const ObTabletID &tablet_id,
      const share::ObDiagnoseTabletType type);
  void remove_diagnose_tablets(
      ObIArray<ObDiagnoseTablet> &tablets);

public:
  static const int64_t DEFAULT_DIAGNOSE_TABLET_COUNT = 128;
  static const int64_t MAX_DIAGNOSE_TABLET_BUCKET_NUM = 1024;
  typedef common::hash::ObHashMap<ObDiagnoseTablet, int64_t, common::hash::NoPthreadDefendMode> DiagnoseTabletMap;

private:
  bool is_inited_;
  DiagnoseTabletMap diagnose_tablet_map_;
  lib::ObMutex diagnose_lock_;
};

}
}

#endif
