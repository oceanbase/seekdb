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

#ifndef OCEANBASE_ROOTSERVER_FREEZE_OB_MAJOR_FREEZE_HELPER_H_
#define OCEANBASE_ROOTSERVER_FREEZE_OB_MAJOR_FREEZE_HELPER_H_
#include "rootserver/freeze/ob_major_freeze_util.h"
#include "share/scn.h"
#include "common/ob_tablet_id.h"

namespace oceanbase
{
namespace common
{
class ObISQLClient;
}
namespace share
{
class ObFreezeInfo;
}
namespace rootserver
{
struct ObMajorFreezeParam
{
public:
  ObMajorFreezeParam()
    : freeze_reason_(MF_REASON_MAX)
  {}

  void reset()
  {
    freeze_reason_ = MF_REASON_MAX;
  }

  bool is_valid() const
  {
    return is_valid_major_freeze_reason(freeze_reason_);
  }

  TO_STRING_KV("freeze_reason", major_freeze_reason_to_str(freeze_reason_));

  ObMajorFreezeReason freeze_reason_;
};

struct ObTabletMajorFreezeParam
{
public:
  ObTabletMajorFreezeParam()
    : tablet_id_()
    {}
  ~ObTabletMajorFreezeParam() = default;
  bool is_valid() const
  {
    return tablet_id_.is_valid();
  }
  TO_STRING_KV(K_(tablet_id));
  common::ObTabletID tablet_id_;
};

class ObMajorFreezeHelper
{
public:
  ObMajorFreezeHelper() {}
  ~ObMajorFreezeHelper() {}

  static int major_freeze(const ObMajorFreezeParam &param);

  static int tablet_major_freeze(const ObTabletMajorFreezeParam &param);

  static int suspend_merge();

  static int resume_merge();

  static int clear_merge_error();

  static int get_frozen_status(const share::SCN &frozen_scn, 
                               share::ObFreezeInfo &frozen_status);
  static int get_frozen_status(const share::SCN &frozen_scn,
                               share::ObFreezeInfo &frozen_status,
                               common::ObISQLClient *proxy);
  static int get_frozen_scn(share::SCN &frozen_scn);
  static int get_frozen_scn(share::SCN &frozen_scn,
                            common::ObISQLClient *proxy);

private:
  enum class AdminMergeType
  {
    SUSPEND,
    RESUME,
    CLEAR_ERROR,
  };

  static int check_runtime_ready(bool &is_restore);
  static int do_local_major_freeze(const ObMajorFreezeReason freeze_reason);

  static int do_admin_merge(const AdminMergeType admin_type);
private:
};

} // namespace rootserver
} // namespace oceanbase

#endif // OCEANBASE_ROOTSERVER_FREEZE_OB_MAJOR_FREEZE_HELPER_H_
