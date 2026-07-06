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
#include "rootserver/freeze/ob_major_freeze_rpc_define.h"
#include "share/scn.h"
#include "rootserver/freeze/ob_major_freeze_util.h"

namespace oceanbase
{
namespace share
{
class SCN;
class ObFreezeInfo;
}
namespace rootserver
{
struct ObMajorFreezeParam
{
public:
  ObMajorFreezeParam()
    : freeze_info_array_(), freeze_all_(false), 
      freeze_all_user_(false), freeze_all_meta_(false),
      freeze_reason_(MF_REASON_MAX)
  {}

  void reset()
  {
    freeze_info_array_.reset();
    freeze_all_ = false;
    freeze_all_user_ = false;
    freeze_all_meta_ = false;
    freeze_reason_ = MF_REASON_MAX;
  }

  bool is_valid() const
  {
    return true;
  }

  int add_freeze_info();

  TO_STRING_KV(K_(freeze_info_array), K_(freeze_all), 
               K_(freeze_all_user), K_(freeze_all_meta),
               "freeze_reason", major_freeze_reason_to_str(freeze_reason_));

  common::ObArray<obcall::ObSimpleFreezeInfo> freeze_info_array_;
  bool freeze_all_;
  bool freeze_all_user_;
  bool freeze_all_meta_;
  ObMajorFreezeReason freeze_reason_;
};

struct ObTenantAdminMergeParam
{
public:
  ObTenantAdminMergeParam()
    : specified_(false), need_all_(false), 
      need_all_user_(false), need_all_meta_(false)
  {}

  void reset()
  {
    specified_ = false;
    need_all_ = false;
    need_all_user_ = false;
    need_all_meta_ = false;
  }

  bool is_valid() const
  {
    return (specified_ || 
            (need_all_ || need_all_user_ || need_all_meta_));
  }

  TO_STRING_KV(K_(specified), K_(need_all), K_(need_all_user), K_(need_all_meta));

  bool specified_;
  bool need_all_;
  bool need_all_user_;
  bool need_all_meta_;
};

struct ObTabletMajorFreezeParam
{
public:
  ObTabletMajorFreezeParam()
    : tablet_id_(),
      is_rebuild_column_group_(false)
    {}
  ~ObTabletMajorFreezeParam() = default;
  bool is_valid() const
  {
    return true && tablet_id_.is_valid();
  }
  TO_STRING_KV(K_(tablet_id), K_(is_rebuild_column_group));
  common::ObTabletID tablet_id_;
  bool is_rebuild_column_group_;
};

class ObMajorFreezeHelper
{
public:
  ObMajorFreezeHelper() {}
  ~ObMajorFreezeHelper() {}

  // @param, contains some tenants which need to launch major freeze
  // @merge_results, save each tenant's major_freeze result
  static int major_freeze(const ObMajorFreezeParam &param,
                          common::ObIArray<int> &merge_results);

  static int major_freeze(const ObMajorFreezeParam &param);

  static int tablet_major_freeze(const ObTabletMajorFreezeParam &param);

  static int suspend_merge(const ObTenantAdminMergeParam &param);

  static int resume_merge(const ObTenantAdminMergeParam &param);

  static int clear_merge_error(const ObTenantAdminMergeParam &param);

  static int get_frozen_status(const share::SCN &frozen_scn, 
                               share::ObFreezeInfo &frozen_status);
  static int get_frozen_status(const share::SCN &frozen_scn, 
                               share::ObFreezeInfo &frozen_status,
                               ObISQLClient *proxy);
  static int get_frozen_scn(share::SCN &frozen_scn);
  static int get_frozen_scn(share::SCN &frozen_scn, ObISQLClient *proxy);

private:
  static int get_freeze_info(
      const ObMajorFreezeParam &param,
      common::ObIArray<obcall::ObSimpleFreezeInfo> &freeze_info_array);
  static int get_all_tenant_freeze_info(
      common::ObIArray<obcall::ObSimpleFreezeInfo> &freeze_info_array);
  static int get_specific_tenant_freeze_info( 
      bool freeze_all, 
      bool freeze_all_user, 
      bool freeze_all_meta, 
      common::ObIArray<obcall::ObSimpleFreezeInfo> &freeze_info_array);
  static int check_tenant_is_restore(bool &is_restore);

  static int do_major_freeze(
      const ObMajorFreezeReason freeze_reason,
      const common::ObIArray<obcall::ObSimpleFreezeInfo> &freeze_info_array,
      common::ObIArray<int> &merge_results);
  static int do_one_tenant_major_freeze(
      const ObMajorFreezeReason freeze_reason,
      const obcall::ObSimpleFreezeInfo &freeze_info);

  static int do_tenant_admin_merge(
      const ObTenantAdminMergeParam &param,
      const obcall::ObTenantAdminMergeType &admin_type);
  static int do_one_tenant_admin_merge(const obcall::ObTenantAdminMergeType &admin_type);
  static int add_user_warning(const char *buf);

private:
  static const int64_t MAX_PROCESS_TIME_US = 10 * 1000 * 1000L;
};

} // namespace rootserver
} // namespace oceanbase

#endif // OCEANBASE_ROOTSERVER_FREEZE_OB_MAJOR_FREEZE_HELPER_H_
