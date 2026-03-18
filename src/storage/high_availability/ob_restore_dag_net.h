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

#ifndef OCEANBASE_STORAGE_RESTORE_DAG_NET_
#define OCEANBASE_STORAGE_RESTORE_DAG_NET_

#include "share/scheduler/ob_tenant_dag_scheduler.h"
#include "ob_storage_ha_dag.h"
#include "ob_restore_helper.h"
#include "ob_physical_copy_ctx.h"
#include "ob_tablet_copy_finish_task.h"
#include "ob_restore_handler.h"

namespace oceanbase
{
namespace restore
{

class ObRestoreDagNetCtx : public ObIHADagNetCtx
{
public:
  ObRestoreDagNetCtx();
  virtual ~ObRestoreDagNetCtx();
  virtual int fill_comment(char *buf, const int64_t buf_len) const override;
  virtual DagNetCtxType get_dag_net_ctx_type() override { return ObIHADagNetCtx::LS_RESTORE; }
  virtual bool is_valid() const override;
  void reset();
  void reuse();
public:
  typedef hash::ObHashMap<common::ObTabletID, ObCopyTabletSimpleInfo> CopyTabletSimpleInfoMap;
public:
  share::SCN local_clog_checkpoint_scn_;
  ObRestoreTask task_;
  common::ObArenaAllocator allocator_;
  ObStorageHATableInfoMgr ha_table_info_mgr_;
  ObHATabletGroupMgr tablet_group_mgr_;
  ObLSMetaPackage src_ls_meta_package_;
  ObArray<ObLogicTabletID> sys_tablet_id_array_;
  ObArray<ObLogicTabletID> data_tablet_id_array_;
  int64_t start_ts_;
  int64_t finish_ts_;
  CopyTabletSimpleInfoMap tablet_simple_info_map_;
  INHERIT_TO_STRING_KV("ObIHADagNetCtx", ObIHADagNetCtx, K_(local_clog_checkpoint_scn),
                          K_(task), K_(src_ls_meta_package),
                          K_(start_ts), K_(finish_ts));
  DISALLOW_COPY_AND_ASSIGN(ObRestoreDagNetCtx);
};

struct ObCopyTabletCtx final: public ObICopyTabletCtx
{
public:
  ObCopyTabletCtx();
  virtual ~ObCopyTabletCtx();
  bool is_valid() const;
  void reset();
  int set_copy_tablet_status(const ObCopyTabletStatus::STATUS &status) override;
  int get_copy_tablet_status(ObCopyTabletStatus::STATUS &status) const override;
  int get_copy_tablet_record_extra_info(ObCopyTabletRecordExtraInfo *&extra_info) override;
  VIRTUAL_TO_STRING_KV(K_(tablet_id), K_(status));

public:
  common::ObTabletID tablet_id_;
  ObTabletHandle tablet_handle_;
  ObMacroBlockReuseMgr macro_block_reuse_mgr_;
  ObCopyTabletRecordExtraInfo extra_info_;
private:
  common::SpinRWLock lock_;
  ObCopyTabletStatus::STATUS status_;
  DISALLOW_COPY_AND_ASSIGN(ObCopyTabletCtx);
};

class ObRestoreDagNetInitParam : public share::ObIDagInitParam
{
public:
  ObRestoreDagNetInitParam();
  virtual ~ObRestoreDagNetInitParam() {};
  virtual bool is_valid() const override;
  VIRTUAL_TO_STRING_KV(K_(task));

public:
  ObRestoreTask task_;
  ObRestoreHandler *handler_;
  common::ObInOutBandwidthThrottle *bandwidth_throttle_;
};

class ObRestoreDagNet : public share::ObIDagNet
{
public:
  ObRestoreDagNet();
  virtual ~ObRestoreDagNet();
  virtual int init_by_param(const share::ObIDagInitParam *param) override;
  virtual bool is_valid() const override;
  virtual int start_running() override;
  virtual bool operator == (const share::ObIDagNet &other) const override;
  virtual uint64_t hash() const override;
  virtual int fill_comment(char *buf, const int64_t buf_len) const override;
  virtual int fill_dag_net_key(char *buf, const int64_t buf_len) const override;
  virtual int clear_dag_net_ctx() override;
  virtual int deal_with_cancel() override;

  ObRestoreDagNetCtx *get_ctx() const { return ctx_; }
  common::ObInOutBandwidthThrottle *get_bandwidth_throttle() { return bandwidth_throttle_; }
  ObIRestoreHelper *get_helper() const { return helper_; }
  INHERIT_TO_STRING_KV("ObIDagNet", share::ObIDagNet, KPC_(ctx), KPC_(helper));
private:
  int start_running_for_restore_();
  int alloc_restore_ctx_();
  void free_restore_ctx_();
  int alloc_restore_helper_();
  void free_restore_helper_();
  int generate_restore_dags_();

private:
  bool is_inited_;
  ObRestoreDagNetCtx *ctx_;
  common::ObInOutBandwidthThrottle *bandwidth_throttle_;
  ObIRestoreHelper *helper_;
  ObRestoreHandler *handler_;
  DISALLOW_COPY_AND_ASSIGN(ObRestoreDagNet);
};

class ObRestoreDag : public ObStorageHADag
{
public:
  explicit ObRestoreDag(const share::ObDagType::ObDagTypeEnum &dag_type);
  virtual ~ObRestoreDag();
  ObRestoreDagNetCtx *get_ctx() const { return static_cast<ObRestoreDagNetCtx *>(ha_dag_net_ctx_); }
  virtual int fill_info_param(compaction::ObIBasicInfoParam *&out_param, ObIAllocator &allocator) const override;
  int prepare_ctx(share::ObIDagNet *dag_net);
  INHERIT_TO_STRING_KV("ObStorageHADag", ObStorageHADag, KP(this));
private:
  DISALLOW_COPY_AND_ASSIGN(ObRestoreDag);
};

class ObInitialRestoreDag : public ObRestoreDag
{
public:
  ObInitialRestoreDag();
  virtual ~ObInitialRestoreDag();
  virtual bool operator == (const share::ObIDag &other) const override;
  virtual uint64_t hash() const override;
  virtual int fill_dag_key(char *buf, const int64_t buf_len) const override;
  virtual int create_first_task() override;
  int init(share::ObIDagNet *dag_net);
  INHERIT_TO_STRING_KV("ObRestoreDag", ObRestoreDag, KP(this));
private:
  bool is_inited_;
  DISALLOW_COPY_AND_ASSIGN(ObInitialRestoreDag);
};

class ObInitialRestoreTask : public share::ObITask
{
public:
  ObInitialRestoreTask();
  virtual ~ObInitialRestoreTask();
  int init();
  virtual int process() override;
  VIRTUAL_TO_STRING_KV(K("ObInitialRestoreTask"), KP(this), KPC_(ctx));
private:
  int generate_restore_dags_();
  int record_server_event_();
private:
  bool is_inited_;
  ObRestoreDagNetCtx *ctx_;
  share::ObIDagNet *dag_net_;
  DISALLOW_COPY_AND_ASSIGN(ObInitialRestoreTask);
};

class ObStartRestoreDag : public ObRestoreDag
{
public:
  ObStartRestoreDag();
  virtual ~ObStartRestoreDag();
  virtual bool operator == (const share::ObIDag &other) const override;
  virtual uint64_t hash() const override;
  virtual int fill_dag_key(char *buf, const int64_t buf_len) const override;
  virtual int create_first_task() override;
  int init(share::ObIDagNet *dag_net);
  INHERIT_TO_STRING_KV("ObRestoreDag", ObRestoreDag, KP(this));
private:
  bool is_inited_;
  DISALLOW_COPY_AND_ASSIGN(ObStartRestoreDag);
};

class ObStartRestoreTask : public share::ObITask
{
public:
  ObStartRestoreTask();
  virtual ~ObStartRestoreTask();
  int init();
  virtual int process() override;
  VIRTUAL_TO_STRING_KV(K("ObStartRestoreTask"), KP(this), KPC_(ctx), KPC_(helper));
private:
  int check_restore_precondition_();
  int build_ls_();
  int update_ls_();
  int generate_tablets_restore_dag_();
  int inner_build_ls_();
  int create_all_tablets_();
  int fetch_next_tablet_info_(obrpc::ObCopyTabletInfo &tablet_info);
  int create_tablet_(const obrpc::ObCopyTabletInfo &tablet_info, ObLS *ls);
  int inner_create_all_tablets_(ObLS *ls);
  int record_server_event_();
private:
  bool is_inited_;
  ObRestoreDagNetCtx *ctx_;
  common::ObInOutBandwidthThrottle *bandwidth_throttle_;
  common::ObArenaAllocator allocator_;
  ObIRestoreHelper *helper_;
  share::ObIDagNet *dag_net_;
  DISALLOW_COPY_AND_ASSIGN(ObStartRestoreTask);
};

class ObSysTabletsRestoreDag : public ObRestoreDag
{
public:
  ObSysTabletsRestoreDag();
  virtual ~ObSysTabletsRestoreDag();
  virtual bool operator == (const share::ObIDag &other) const override;
  virtual uint64_t hash() const override;
  virtual int fill_dag_key(char *buf, const int64_t buf_len) const override;
  virtual int create_first_task() override;
  int init(share::ObIDagNet *dag_net);
  INHERIT_TO_STRING_KV("ObRestoreDag", ObRestoreDag, KP(this));
private:
  bool is_inited_;
  DISALLOW_COPY_AND_ASSIGN(ObSysTabletsRestoreDag);
};

class ObSysTabletsRestoreTask : public share::ObITask
{
public:
  ObSysTabletsRestoreTask();
  virtual ~ObSysTabletsRestoreTask();
  int init();
  virtual int process() override;
  VIRTUAL_TO_STRING_KV(K("ObSysTabletsRestoreTask"), KP(this), KPC_(ctx), KPC_(helper));
private:
  int build_tablets_sstable_info_();
  int generate_sys_tablet_restore_dag_();
  int record_server_event_();
private:
  bool is_inited_;
  ObRestoreDagNetCtx *ctx_;
  ObLSHandle ls_handle_;
  common::ObInOutBandwidthThrottle *bandwidth_throttle_;
  common::ObArenaAllocator allocator_;
  ObIRestoreHelper *helper_;
  ObIDagNet *dag_net_;
  DISALLOW_COPY_AND_ASSIGN(ObSysTabletsRestoreTask);
};

class ObTabletRestoreDag : public ObRestoreDag
{
public:
  enum class ObTabletType {
    SYS_TABLET_TYPE  = 0,
    DATA_TABLET_TYPE = 1,
    MAX_TYPE
  };
public:
  ObTabletRestoreDag();
  virtual ~ObTabletRestoreDag();
  virtual bool operator == (const share::ObIDag &other) const override;
  virtual uint64_t hash() const override;
  virtual int fill_dag_key(char *buf, const int64_t buf_len) const override;
  virtual int create_first_task() override;
  virtual int inner_reset_status_for_retry() override;
  virtual int generate_next_dag(share::ObIDag *&dag);
  virtual int fill_info_param(compaction::ObIBasicInfoParam *&out_param, ObIAllocator &allocator) const override;
  int init(
      const common::ObTabletID &tablet_id,
      ObTabletHandle &tablet_handle,
      share::ObIDagNet *dag_net,
      ObHATabletGroupCtx *tablet_group_ctx,
      ObTabletType tablet_type);
  int get_tablet_group_ctx(ObHATabletGroupCtx *&tablet_group_ctx);
  int get_ls(ObLS *&ls);
  INHERIT_TO_STRING_KV("ObRestoreDag", ObRestoreDag, KP(this), K(copy_tablet_ctx_), K(tablet_type_));

protected:
  bool is_inited_;
  ObLSHandle ls_handle_;
  ObCopyTabletCtx copy_tablet_ctx_;
  ObHATabletGroupCtx *tablet_group_ctx_;
  ObTabletType tablet_type_;
  DISALLOW_COPY_AND_ASSIGN(ObTabletRestoreDag);
};

class ObTabletFinishRestoreTask;
class ObTabletRestoreTask : public share::ObITask
{
public:
  ObTabletRestoreTask();
  virtual ~ObTabletRestoreTask();
  int init(ObCopyTabletCtx &ctx);
  virtual int process() override;
  VIRTUAL_TO_STRING_KV(K("ObTabletRestoreTask"), KP(this), KPC(ctx_));
private:
  typedef bool (*IsRightTypeSSTableFunc)(const ObITable::TableType table_type);
  int generate_restore_tasks_();
  int generate_minor_copy_tasks_(
      ObTabletCopyFinishTask *tablet_copy_finish_task,
      share::ObITask *&parent_task);
  int generate_major_copy_tasks_(
      ObTabletCopyFinishTask *tablet_copy_finish_task,
      share::ObITask *&parent_task);
  int generate_ddl_copy_tasks_(
      ObTabletCopyFinishTask *tablet_copy_finish_task,
      share::ObITask *&parent_task);
  int generate_mds_copy_tasks_(
      ObTabletCopyFinishTask *tablet_copy_finish_task,
      share::ObITask *&parent_task);
  int generate_copy_tasks_(
      IsRightTypeSSTableFunc is_right_type_sstable,
      ObTabletCopyFinishTask *tablet_copy_finish_task,
      share::ObITask *&parent_task);
  int generate_physical_copy_task_(
      const ObITable::TableKey &copy_table_key,
      ObTabletCopyFinishTask *tablet_copy_finish_task,
      ObITask *parent_task,
      ObITask *child_task);
  int generate_tablet_finish_restore_task_(
      ObTabletFinishRestoreTask *&tablet_finish_restore_task);
  int build_copy_table_key_info_();
  int build_copy_sstable_info_mgr_();
  int generate_tablet_copy_finish_task_(
      ObTabletCopyFinishTask *&tablet_copy_finish_task);
  int record_server_event_(const int64_t cost_us, const int64_t result);
  int try_update_tablet_();
  int check_tablet_replica_validity_(const common::ObTabletID &tablet_id);
  int update_ha_expected_status_(const ObCopyTabletStatus::STATUS &status);
  int check_need_copy_sstable_(
      const ObITable::TableKey &table_key,
      bool &need_copy);
  int get_need_copy_sstable_info_key_(
      const common::ObIArray<ObITable::TableKey> &copy_table_key_array,
      common::ObIArray<ObITable::TableKey> &filter_table_key_array);

private:
  bool is_inited_;
  ObRestoreDagNetCtx *ctx_;
  common::ObInOutBandwidthThrottle *bandwidth_throttle_;
  common::ObArenaAllocator allocator_;
  ObIRestoreHelper *helper_;
  ObCopyTabletCtx *copy_tablet_ctx_;
  common::ObArray<ObITable::TableKey> copy_table_key_array_;
  ObStorageHACopySSTableInfoMgr copy_sstable_info_mgr_;

  DISALLOW_COPY_AND_ASSIGN(ObTabletRestoreTask);
};

class ObTabletFinishRestoreTask final : public share::ObITask
{
public:
  ObTabletFinishRestoreTask();
  virtual ~ObTabletFinishRestoreTask();
  int init(const int64_t task_gen_time, const int64_t copy_table_count,
      ObCopyTabletCtx &ctx, ObLS &ls);
  virtual int process() override;
  VIRTUAL_TO_STRING_KV(K("ObTabletFinishRestoreTask"), KP(this), KPC(ha_dag_net_ctx_), KPC(copy_tablet_ctx_), KPC(ls_));
private:
  int update_data_and_expected_status_();
private:
  bool is_inited_;
  int64_t task_gen_time_;
  int64_t copy_table_count_;
  ObIHADagNetCtx *ha_dag_net_ctx_;
  ObCopyTabletCtx *copy_tablet_ctx_;
  ObLS *ls_;
  DISALLOW_COPY_AND_ASSIGN(ObTabletFinishRestoreTask);
};

class ObDataTabletsRestoreDag : public ObRestoreDag
{
public:
  ObDataTabletsRestoreDag();
  virtual ~ObDataTabletsRestoreDag();
  virtual bool operator == (const share::ObIDag &other) const override;
  virtual int fill_dag_key(char *buf, const int64_t buf_len) const override;
  virtual uint64_t hash() const override;
  virtual int create_first_task() override;
  int init(share::ObIDagNet *dag_net);
  INHERIT_TO_STRING_KV("ObRestoreDag", ObRestoreDag, KP(this));
private:
  bool is_inited_;
  DISALLOW_COPY_AND_ASSIGN(ObDataTabletsRestoreDag);
};
class ObTabletGroupRestoreDag;
class ObDataTabletsRestoreTask : public share::ObITask
{
public:
  ObDataTabletsRestoreTask();
  virtual ~ObDataTabletsRestoreTask();
  int init();
  virtual int process() override;
  VIRTUAL_TO_STRING_KV(K("ObDataTabletsRestoreTask"), KP(this), KPC_(ctx), KPC_(helper));
private:
  int ls_online_();
  int set_restore_status_();
  int generate_tablet_group_dag_(
      const common::ObIArray<common::ObTabletID> &tablet_id_array,
      common::ObIArray<ObTabletGroupRestoreDag *> &tablet_group_dag_array);
  int build_tablet_group_info_();
  int generate_tablet_group_dag_();
  int try_offline_ls_();
  int check_tx_data_continue_();
  int record_server_event_();
  int trigger_log_sync_();

private:
  static const int64_t MAX_TABLET_GROUP_SIZE = 2 * 1024L * 1024L * 1024L; //2G
  static const int64_t MAX_TABLET_COUNT = 100;
  bool is_inited_;
  ObRestoreDagNetCtx *ctx_;
  common::ObInOutBandwidthThrottle *bandwidth_throttle_;
  common::ObArenaAllocator allocator_;
  ObIRestoreHelper *helper_;
  share::ObIDag *finish_dag_;
  ObStorageHATabletsBuilder ha_tablets_builder_;
  DISALLOW_COPY_AND_ASSIGN(ObDataTabletsRestoreTask);
};

class ObTabletGroupRestoreDag : public ObRestoreDag
{
public:
  ObTabletGroupRestoreDag();
  virtual ~ObTabletGroupRestoreDag();
  virtual bool operator == (const share::ObIDag &other) const override;
  virtual uint64_t hash() const override;
  virtual int fill_dag_key(char *buf, const int64_t buf_len) const override;
  virtual int create_first_task() override;
  virtual int generate_next_dag(share::ObIDag *&dag);
  virtual int fill_info_param(compaction::ObIBasicInfoParam *&out_param, ObIAllocator &allocator) const override;
  int init(
      const common::ObIArray<ObLogicTabletID> &tablet_id_array,
      share::ObIDagNet *dag_net,
      share::ObIDag *finish_dag,
      ObHATabletGroupCtx *tablet_group_ctx);
  int check_is_in_retry(bool &is_in_retry);

  INHERIT_TO_STRING_KV("ObRestoreDag", ObRestoreDag, KP(this));
protected:
  bool is_inited_;
  ObArray<ObLogicTabletID> tablet_id_array_;
  share::ObIDag *finish_dag_;
  ObHATabletGroupCtx *tablet_group_ctx_;
  DISALLOW_COPY_AND_ASSIGN(ObTabletGroupRestoreDag);
};

class ObTabletGroupRestoreTask : public share::ObITask
{
public:
  ObTabletGroupRestoreTask();
  virtual ~ObTabletGroupRestoreTask();
  int init(
      const common::ObIArray<ObLogicTabletID> &tablet_id_array,
      share::ObIDag *finish_dag,
      ObHATabletGroupCtx *tablet_group_ctx);
  virtual int process() override;
  VIRTUAL_TO_STRING_KV(K("ObTabletGroupRestoreTask"), KP(this), KPC(ctx_));
private:
  int build_tablets_sstable_info_();
  int generate_tablet_restore_dag_();
  int try_remove_tablets_info_();
  int remove_tablets_info_();
  int record_server_event_();
private:
  bool is_inited_;
  ObLSHandle ls_handle_;
  ObRestoreDagNetCtx *ctx_;
  common::ObInOutBandwidthThrottle *bandwidth_throttle_;
  common::ObArenaAllocator allocator_;
  ObIRestoreHelper *helper_;
  common::ObArray<ObLogicTabletID> tablet_id_array_;
  share::ObIDag *finish_dag_;
  ObStorageHATabletsBuilder ha_tablets_builder_;
  ObHATabletGroupCtx *tablet_group_ctx_;
  DISALLOW_COPY_AND_ASSIGN(ObTabletGroupRestoreTask);
};

class ObRestoreFinishDag : public ObRestoreDag
{
public:
  ObRestoreFinishDag();
  virtual ~ObRestoreFinishDag();
  virtual int fill_dag_key(char *buf, const int64_t buf_len) const override;
  virtual bool operator == (const share::ObIDag &other) const override;
  virtual uint64_t hash() const override;
  virtual int create_first_task() override;
  int init(share::ObIDagNet *dag_net);
  INHERIT_TO_STRING_KV("ObRestoreDag", ObRestoreDag, KP(this));
private:
  bool is_inited_;
  DISALLOW_COPY_AND_ASSIGN(ObRestoreFinishDag);
};

class ObRestoreFinishTask : public share::ObITask
{
public:
  ObRestoreFinishTask();
  virtual ~ObRestoreFinishTask();
  int init();
  virtual int process() override;
  VIRTUAL_TO_STRING_KV(K("ObRestoreFinishTask"), KP(this), KPC_(ctx));
private:
  int generate_initial_restore_dag_();
  int record_server_event_();
private:
  bool is_inited_;
  ObRestoreDagNetCtx *ctx_;
  share::ObIDagNet *dag_net_;
  DISALLOW_COPY_AND_ASSIGN(ObRestoreFinishTask);
};

struct ObLSRestoreUtils
{
public:
  static int init_ha_tablets_builder(
      const uint64_t tenant_id,
      const common::ObIArray<common::ObTabletID> &tablet_id_array,
      const ObStorageHASrcInfo src_info,
      const int64_t local_rebuild_seq,
      const ObRestoreType &type,
      ObLS *ls,
      ObStorageHATableInfoMgr *ha_table_info_mgr,
      ObStorageHATabletsBuilder &ha_tablets_builder);
};

class ObRestoreDagNetUtils
{
public:
  static int build_tablets_sstable_info_with_helper(
      const common::ObIArray<ObTabletID> &tablet_id_array,
      ObIRestoreHelper *helper,
      ObStorageHATableInfoMgr *ha_table_info_mgr,
      share::ObIDagNet *dag_net,
      ObLS *ls);
  static int create_or_update_tablets_with_helper(
      const common::ObIArray<ObTabletID> &tablet_id_array,
      ObIRestoreHelper *helper,
      share::ObIDagNet *dag_net,
      ObLS *ls);
  static int create_or_update_tablet(
      const obrpc::ObCopyTabletInfo &tablet_info,
      const bool need_check_tablet_limit,
      ObLS *ls);
private:
  static int hold_local_tablet_(
      const common::ObIArray<ObTabletID> &tablet_id_array,
      ObLS *ls,
      common::ObIArray<ObTabletHandle> &tablet_handle_array);
  static int modified_tablet_info_(obrpc::ObCopyTabletInfo &tablet_info);
  static int remove_uncomplete_tablet_(const common::ObTabletID &tablet_id, ObLS *ls);
  static int hold_local_complete_tablet_major_sstables_(ObTablet *tablet, ObTablesHandleArray &tables_handle);
  static int hold_local_reuse_major_sstables_(
      const common::ObTabletID &tablet_id,
      ObLS *ls,
      ObTabletHandle &local_tablet_hdl,
      ObTablesHandleArray &tables_handle,
      ObStorageSchema &storage_schema,
      common::ObIAllocator &allocator);
};

} // namespace restore
} // namespace oceanbase

#endif // OCEANBASE_STORAGE_RESTORE_DAG_NET_
