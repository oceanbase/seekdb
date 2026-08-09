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

#ifndef OCEANBASE_STORAGE_STANDBY_SSTABLE_COPIER_
#define OCEANBASE_STORAGE_STANDBY_SSTABLE_COPIER_

#include "lib/net/ob_addr.h"
#include "share/scn.h"
#include "share/log/palf/palf_base_info.h"
#include "standby/restore/ob_standby_restore_rpc.h"
#include "standby/restore/ob_physical_copy_ctx.h"
#include "standby/restore/ob_restore_helper.h"
#include "storage/ls/ob_ls_meta.h"

namespace oceanbase
{
namespace standby
{
struct StandbyConfig;
}
namespace common
{
class ObInOutBandwidthThrottle;
}
namespace restore
{
class ObStandbyRestoreHelper;
}
namespace storage
{

class ObLS;
class ObTabletCopyFinishTask;
class ObStandbyRestoreTableInfoMgr;

class ObStandbySSTableCopier final
{
public:
  ObStandbySSTableCopier();
  ~ObStandbySSTableCopier() = default;

  int init(
      const common::ObAddr &src,
      common::ObInOutBandwidthThrottle *bandwidth_throttle,
      const standby::StandbyConfig &config);
  int prepare_replay_base(
      share::SCN &restore_checkpoint_scn,
      palf::PalfBaseInfo &palf_base_info,
      share::SCN &source_end_scn);
  int copy(share::SCN &restore_checkpoint_scn);

private:
  struct CopyTabletCtx final : public ObICopyTabletCtx
  {
    CopyTabletCtx();
    virtual ~CopyTabletCtx() = default;
    virtual int set_copy_tablet_status(const ObCopyTabletStatus::STATUS &status) override;
    virtual int get_copy_tablet_status(ObCopyTabletStatus::STATUS &status) const override;
    virtual int get_copy_tablet_record_extra_info(ObCopyTabletRecordExtraInfo *&extra_info) override;

    ObCopyTabletStatus::STATUS status_;
    ObCopyTabletRecordExtraInfo extra_info_;
  };

  int init_helper_(restore::ObStandbyRestoreHelper &helper) const;
  int get_sys_ls_(ObLS *&ls);
  int fetch_ls_view_and_create_tablets_(
      ObLS *&ls,
      common::ObIArray<common::ObTabletID> &tablet_id_array,
      share::SCN &restore_checkpoint_scn);
  int create_or_update_tablet_(ObLS *ls, obcall::ObCopyTabletInfo &tablet_info);
  int build_table_info_(
      ObLS *ls,
      const common::ObIArray<common::ObTabletID> &tablet_id_array,
      ObStandbyRestoreTableInfoMgr &table_info_mgr);
  int copy_all_tablets_(
      ObLS *ls,
      const common::ObIArray<common::ObTabletID> &tablet_id_array,
      ObStandbyRestoreTableInfoMgr &table_info_mgr);
  int finish_all_tablets_restore_(
      ObLS *ls,
      const common::ObIArray<common::ObTabletID> &tablet_id_array);
  int finish_tablet_restore_(
      ObLS *ls,
      const common::ObTabletID &tablet_id);
  int finish_ls_restore_(ObLS *ls, const share::SCN &restore_checkpoint_scn);
  int copy_tablet_(
      ObLS *ls,
      const common::ObTabletID &tablet_id,
      ObStandbyRestoreTableInfoMgr &table_info_mgr);
  int copy_sstable_(
      ObLS *ls,
      const common::ObTabletID &tablet_id,
      const blocksstable::ObMigrationSSTableParam *sstable_param,
      const ObCopySSTableMacroRangeInfo &macro_range_info,
      restore::ObStandbyRestoreHelper &helper,
      CopyTabletCtx &copy_tablet_ctx,
      ObTabletCopyFinishTask &tablet_finish_task,
      const share::ObTaskId &copy_id);

private:
  bool is_inited_;
  bool replay_base_prepared_;
  common::ObAddr src_;
  common::ObInOutBandwidthThrottle *bandwidth_throttle_;
  const standby::StandbyConfig *config_;
  ObLSMeta source_ls_meta_;
  share::SCN physical_checkpoint_scn_;
  restore::ObStandbyRestoreHelper ls_view_helper_;
};

} // namespace storage
} // namespace oceanbase

#endif
