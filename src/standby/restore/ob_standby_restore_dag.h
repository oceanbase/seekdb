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

#ifndef OCEANBASE_STORAGE_STANDBY_RESTORE_DAG_
#define OCEANBASE_STORAGE_STANDBY_RESTORE_DAG_

#include "data_plane/scheduler/ob_dag_scheduler.h"
#include "standby/restore/ob_standby_restore_rpc.h"
#include "ob_standby_restore_storage_struct.h"
#include "ob_storage_restore_struct.h"

namespace oceanbase
{

namespace storage
{

class ObLS;

struct ObStandbyRestoreResultMgr final
{
public:
  ObStandbyRestoreResultMgr();
  ~ObStandbyRestoreResultMgr();
  int get_result(int32_t &result);
  int set_result(const int32_t result, const bool allow_retry,
      const enum share::ObDagType::ObDagTypeEnum type = share::ObDagType::DAG_TYPE_MAX);
  bool is_failed() const;
  int check_allow_retry(bool &allow_retry);
  void reuse();
  void reset();
  int get_retry_count(int32_t &retry_count);
  int get_first_failed_task_id(share::ObTaskId &task_id);
  TO_STRING_KV(K_(result), K_(retry_count), K_(allow_retry), K_(failed_task_id_list));

private:
  static const int64_t MAX_RETRY_CNT = 3;
  common::SpinRWLock lock_;
  int32_t result_;
  int32_t retry_count_;
  bool allow_retry_;
  common::ObSEArray<share::ObTaskId, MAX_RETRY_CNT> failed_task_id_list_;

private:
  DISALLOW_COPY_AND_ASSIGN(ObStandbyRestoreResultMgr);
};

struct ObIStandbyRestoreDagNetCtx
{
public:
  enum DagNetCtxType {
    LS_PREPARE_MIGRATION = 0,
    LS_MIGRATION = 1,
    LS_COMPLETE_MIGRATION = 2,
    LS_RESTORE = 3,
    TABLET_GROUP_RESTORE = 4,
    BACKFILL_TX = 5,
    TRANSFER_BACKFILL_TX = 6,
    REBUILD_TABLET = 7,
    RESTORE_COMPLETE = 8,
    MAX
  };

  ObIStandbyRestoreDagNetCtx();
  virtual ~ObIStandbyRestoreDagNetCtx();
  virtual int fill_comment(char *buf, const int64_t buf_len) const = 0;
  virtual DagNetCtxType get_dag_net_ctx_type() = 0;
  virtual bool is_valid() const = 0;
  int set_result(const int32_t result, const bool need_retry,
      const enum share::ObDagType::ObDagTypeEnum type = share::ObDagType::DAG_TYPE_MAX);
  bool is_failed() const;
  virtual int check_allow_retry_with_stop(bool &allow_retry);
  virtual int check_allow_retry(bool &allow_retry);
  int get_result(int32_t &result);
  void reuse();
  void reset();
  int get_first_failed_task_id(share::ObTaskId &task_id);

  VIRTUAL_TO_STRING_KV(K("ObIStandbyRestoreDagNetCtx"), K_(result_mgr));
private:
  ObStandbyRestoreResultMgr result_mgr_;
private:
  DISALLOW_COPY_AND_ASSIGN(ObIStandbyRestoreDagNetCtx);
};

class ObStandbyRestoreDag : public share::ObIDag
{
public:
  explicit ObStandbyRestoreDag(const share::ObDagType::ObDagTypeEnum &dag_type);
  virtual ~ObStandbyRestoreDag();
  virtual int inner_reset_status_for_retry();
  virtual bool inner_check_can_retry();

  int set_result(const int32_t result, const bool allow_retry = true,
      const enum share::ObDagType::ObDagTypeEnum type = share::ObDagType::DAG_TYPE_MAX);
  virtual int report_result();
  ObIStandbyRestoreDagNetCtx *get_standby_restore_dag_net_ctx() const { return standby_restore_dag_net_ctx_; }

  INHERIT_TO_STRING_KV("ObIDag", ObIDag, KPC_(standby_restore_dag_net_ctx), K_(result_mgr));
protected:
  ObIStandbyRestoreDagNetCtx *standby_restore_dag_net_ctx_;
  ObStandbyRestoreResultMgr result_mgr_;
  DISALLOW_COPY_AND_ASSIGN(ObStandbyRestoreDag);
};

class ObStandbyRestoreDagUtils
{
public:
  static int deal_with_fo(
      const int err,
      share::ObIDag *dag,
      const bool allow_retry = true);
  static int get_ls(
      const share::ObLSID &ls_id,
      ObLS *&ls);
};

class ObStandbyRestoreTabletGroupCtx
{
public:
  enum class TabletGroupCtxType
  {
    NORMAL_TYPE     = 0,
    CS_REPLICA_TYPE = 1,
    MAX_TYPE
  };
public:
  ObStandbyRestoreTabletGroupCtx(const TabletGroupCtxType type = TabletGroupCtxType::NORMAL_TYPE);
  virtual ~ObStandbyRestoreTabletGroupCtx();
  int init(const common::ObIArray<common::ObTabletID> &tablet_id_array);
  int get_next_tablet_id(common::ObTabletID &tablet_id);
  int get_all_tablet_ids(common::ObIArray<common::ObTabletID> &tablet_id);
public:
  virtual void reuse();
  virtual void inner_reuse();
  virtual int inner_init() { return OB_SUCCESS; }
  TO_STRING_KV(K_(tablet_id_array), K_(index));
protected:
  bool is_inited_;
  common::SpinRWLock lock_;
  ObArray<common::ObTabletID> tablet_id_array_;
  int64_t index_;
  TabletGroupCtxType type_;
  DISALLOW_COPY_AND_ASSIGN(ObStandbyRestoreTabletGroupCtx);
};

class ObStandbyRestoreTabletGroupMgr
{
public:
  ObStandbyRestoreTabletGroupMgr();
  virtual ~ObStandbyRestoreTabletGroupMgr();
  int init();
  int get_next_tablet_group_ctx(
      ObStandbyRestoreTabletGroupCtx *&tablet_group_ctx);
  int build_tablet_group_ctx(
      const common::ObIArray<common::ObTabletID> &tablet_id_array,
      const ObStandbyRestoreTabletGroupCtx::TabletGroupCtxType type = ObStandbyRestoreTabletGroupCtx::TabletGroupCtxType::NORMAL_TYPE);
  int alloc_and_new_tablet_group_ctx(
      const ObStandbyRestoreTabletGroupCtx::TabletGroupCtxType type,
      ObStandbyRestoreTabletGroupCtx *&tablet_group_ctx);
  void reuse();

  TO_STRING_KV(K_(tablet_group_ctx_array), K_(index));
private:
  bool is_inited_;
  common::SpinRWLock lock_;
  ObArenaAllocator allocator_;
  ObArray<ObStandbyRestoreTabletGroupCtx *> tablet_group_ctx_array_;
  int64_t index_;
  DISALLOW_COPY_AND_ASSIGN(ObStandbyRestoreTabletGroupMgr);
};

class ObStandbyRestoreTaskUtils
{
public:
  static int check_need_copy_sstable(
      const blocksstable::ObMigrationSSTableParam &param,
      const bool &is_restore,
      ObTabletHandle &tablet_handle,
      bool &need_copy);
  static int check_need_copy_macro_blocks(
      const blocksstable::ObMigrationSSTableParam &param,
      const bool is_leader_restore,
      bool &need_copy);

private:
  static int check_major_sstable_need_copy_(
      const blocksstable::ObMigrationSSTableParam &param,
      const bool &is_restore,
      ObTabletHandle &tablet_handle,
      bool &need_copy);

  static int check_minor_sstable_need_copy_(
      const blocksstable::ObMigrationSSTableParam &param,
      ObTabletHandle &tablet_handle,
      bool &need_copy);

  static int check_ddl_sstable_need_copy_(
      const blocksstable::ObMigrationSSTableParam &param,
      ObTabletHandle &tablet_handle,
      bool &need_copy);
};

}
}
#endif // OCEANBASE_STORAGE_STANDBY_RESTORE_DAG_
