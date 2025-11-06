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

#ifndef OCEANBASE_STORAGE_BACKUP_FUSE_TABLET_TASK_H_
#define OCEANBASE_STORAGE_BACKUP_FUSE_TABLET_TASK_H_

#include "storage/backup/ob_backup_fuse_tablet_ctx.h"
#include "storage/backup/ob_backup_tablet_meta_fuser.h"

namespace oceanbase
{
namespace backup 
{

class ObInitialBackupTabletGroupFuseTask : public share::ObITask
{
public:
  ObInitialBackupTabletGroupFuseTask();
  virtual ~ObInitialBackupTabletGroupFuseTask();
  int init();
  virtual int process() override;
  VIRTUAL_TO_STRING_KV(K("ObInitialBackupTabletGroupFuseTask"), KP(this), KPC(ctx_));
private:
  int build_tablet_group_ctx_();
  int generate_tablet_fuse_dags_();
  int record_server_event_();

private:
  bool is_inited_;
  share::ObIDagNet *dag_net_;
  ObBackupTabletGroupFuseCtx *ctx_;
  DISALLOW_COPY_AND_ASSIGN(ObInitialBackupTabletGroupFuseTask);
};

class ObStartBackupTabletGroupFuseTask : public share::ObITask
{
public:
  ObStartBackupTabletGroupFuseTask();
  virtual ~ObStartBackupTabletGroupFuseTask();
  int init(share::ObIDag *finish_dag);
  virtual int process() override;
  VIRTUAL_TO_STRING_KV(K("ObStartBackupTabletGroupFuseTask"), KP(this), KPC(group_ctx_));
private:
  int check_need_fuse_tablet_(bool &need_fuse);
  int generate_tablet_fuse_dag_();
  int record_server_event_();

private:
  bool is_inited_;
  share::ObIDag *finish_dag_;
  ObBackupTabletGroupFuseCtx *group_ctx_;
  DISALLOW_COPY_AND_ASSIGN(ObStartBackupTabletGroupFuseTask);
};

class ObFinishBackupTabletGroupFuseTask : public share::ObITask
{
public:
  ObFinishBackupTabletGroupFuseTask();
  virtual ~ObFinishBackupTabletGroupFuseTask();
  int init();
  virtual int process() override;
  VIRTUAL_TO_STRING_KV(K("ObFinishBackupTabletGroupFuseTask"), KP(this), KPC(group_ctx_));
private:
  int close_extern_writer_();
  int abort_extern_writer_();
  int generate_init_dag_();
  int record_server_event_();
  int report_task_result_();
private:
  bool is_inited_;
  share::ObIDagNet *dag_net_;
  ObBackupTabletGroupFuseCtx *group_ctx_;
  DISALLOW_COPY_AND_ASSIGN(ObFinishBackupTabletGroupFuseTask);
};

class ObBackupTabletFuseTask : public share::ObITask
{
public:
  ObBackupTabletFuseTask();
  virtual ~ObBackupTabletFuseTask();
  int init(ObBackupTabletFuseCtx &fuse_ctx, ObBackupTabletGroupFuseCtx &group_ctx);
  virtual int process() override;
  VIRTUAL_TO_STRING_KV(K("ObBackupTabletFuseTask"), KP(this), KPC_(fuse_ctx), KPC_(group_ctx));

private:
  int inner_process_(const ObBackupTabletFuseItem &fuse_item);
  int check_tablet_deleted_(
      const uint64_t tenant_id,
      const common::ObTabletID &tablet_id,
      bool &tablet_deleted);
  int check_tablet_reorganized_(
      const uint64_t tenant_id,
      const common::ObTabletID &tablet_id,
      bool &tablet_reoragnized);
  int fetch_tablet_meta_in_user_data_(
      const ObBackupMetaIndex &meta_index, 
      ObMigrationTabletParam &tablet_param);
  int fuse_tablet_item_(
      const ObBackupTabletFuseItem &fuse_item,
      ObMigrationTabletParam &output_tablet_param);
  int inner_fuse_tablet_item_(
      const ObMigrationTabletParam &param_v1,
      const ObMigrationTabletParam &param_v2,
      ObMigrationTabletParam &output_param);
  int write_new_tablet_info_(
      const ObMigrationTabletParam &output);
  int record_server_event_();

private:
  bool is_inited_;
  ObMySQLProxy *sql_proxy_;
  ObBackupTabletFuseCtx *fuse_ctx_;
  ObBackupTabletGroupFuseCtx *group_ctx_;
  ObBackupFuseTabletType fuse_type_;
  DISALLOW_COPY_AND_ASSIGN(ObBackupTabletFuseTask);
};

}
}
#endif
