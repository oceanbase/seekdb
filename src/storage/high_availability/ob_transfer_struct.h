/**
 * Copyright (c) 2022 OceanBase
 * OceanBase CE is licensed under Mulan PubL v2.
 * You can use this software according to the terms and conditions of the Mulan PubL v2.
 * You may obtain a copy of Mulan PubL v2 at:
 *          http://license.coscl.org.cn/MulanPubL-2.0
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND,
 * EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
 * MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
 * See the Mulan PubL v2 for more details.
 */

#ifndef OCEABASE_STORAGE_TRANSFER_STRUCT_
#define OCEABASE_STORAGE_TRANSFER_STRUCT_

#include "share/ob_define.h"
#include "lib/utility/ob_unify_serialize.h"
#include "share/transfer/ob_transfer_info.h"
#include "share/ob_balance_define.h"
#include "share/scn.h"
#include "storage/memtable/ob_memtable.h"
#include "storage/tablet/ob_tablet_meta.h"
#include "storage/tablet/ob_tablet_create_delete_mds_user_data.h"
#include "lib/hash/ob_hashmap.h"

namespace oceanbase
{
namespace storage
{

struct ObTXStartTransferOutInfo final
{
  OB_UNIS_VERSION(1);
public:
  ObTXStartTransferOutInfo();
  ~ObTXStartTransferOutInfo() = default;
  bool is_valid() const;
  bool empty_tx() { return filter_tx_need_transfer_ && move_tx_ids_.count() == 0; }
  int64_t to_string(char *buf, const int64_t buf_len) const;

  share::ObLSID src_ls_id_;
  share::ObLSID dest_ls_id_;
  common::ObSArray<share::ObTransferTabletInfo> tablet_list_;
  share::ObTransferTaskID task_id_;
  share::SCN data_end_scn_;
  int64_t transfer_epoch_;
  uint64_t data_version_;
  bool filter_tx_need_transfer_;
  common::ObSEArray<transaction::ObTransID, 1> move_tx_ids_;
  DISALLOW_COPY_AND_ASSIGN(ObTXStartTransferOutInfo);
};

struct ObTXStartTransferInInfo final
{
  OB_UNIS_VERSION(1);
public:
  ObTXStartTransferInInfo();
  ~ObTXStartTransferInInfo() = default;
  bool is_valid() const;

  int get_tablet_id_list(common::ObIArray<common::ObTabletID> &tablet_id_list) const;
  int64_t to_string(char *buf, const int64_t buf_len) const;

  share::ObLSID src_ls_id_;
  share::ObLSID dest_ls_id_;
  share::SCN start_scn_;
  common::ObSArray<ObMigrationTabletParam> tablet_meta_list_;
  share::ObTransferTaskID task_id_;
  uint64_t data_version_;

  DISALLOW_COPY_AND_ASSIGN(ObTXStartTransferInInfo);
};

struct ObTXFinishTransferOutInfo final
{
  OB_UNIS_VERSION(1);
public:
  ObTXFinishTransferOutInfo();
  ~ObTXFinishTransferOutInfo() = default;
  bool is_valid() const;
  int64_t to_string(char *buf, const int64_t buf_len) const;

  share::ObLSID src_ls_id_;
  share::ObLSID dest_ls_id_;
  share::SCN finish_scn_;
  common::ObSArray<share::ObTransferTabletInfo> tablet_list_;
  share::ObTransferTaskID task_id_;
  uint64_t data_version_;
  DISALLOW_COPY_AND_ASSIGN(ObTXFinishTransferOutInfo);
};

struct ObTXFinishTransferInInfo final
{
  OB_UNIS_VERSION(1);
public:
  ObTXFinishTransferInInfo();
  ~ObTXFinishTransferInInfo() = default;
  bool is_valid() const;
  int64_t to_string(char *buf, const int64_t buf_len) const;

  share::ObLSID src_ls_id_;
  share::ObLSID dest_ls_id_;
  share::SCN start_scn_;
  common::ObSArray<share::ObTransferTabletInfo> tablet_list_;
  share::ObTransferTaskID task_id_;
  uint64_t data_version_;
  DISALLOW_COPY_AND_ASSIGN(ObTXFinishTransferInInfo);
};

struct ObTXTransferInAbortedInfo final
{
  OB_UNIS_VERSION(1);
public:
  ObTXTransferInAbortedInfo();
  ~ObTXTransferInAbortedInfo() = default;
  bool is_valid() const;
  int64_t to_string(char *buf, const int64_t buf_len) const;

  share::ObLSID dest_ls_id_;
  common::ObSArray<share::ObTransferTabletInfo> tablet_list_;
  uint64_t data_version_;
  DISALLOW_COPY_AND_ASSIGN(ObTXTransferInAbortedInfo);
};

struct ObTransferEventRecorder final
{
};

struct ObTXTransferUtils
{
  static int set_tablet_freeze_flag(storage::ObLS &ls, ObTablet *tablet);

private:
  static int get_tablet_status_(
      const bool get_commit,
      const ObTablet *tablet,
      ObTabletCreateDeleteMdsUserData &user_data);
  static int build_empty_minor_sstable_param_(
      const share::SCN start_scn,
      const share::SCN end_scn,
      const ObStorageSchema &table_schema,
      const common::ObTabletID &tablet_id,
      ObTabletCreateSSTableParam &param);
};

struct ObTransferLockStatus final
{
public:
  enum STATUS : uint8_t
  {
    START = 0,
    DOING = 1,
    ABORTED = 2,
    MAX_STATUS
  };
public:
  ObTransferLockStatus() : status_(MAX_STATUS) {}
  ~ObTransferLockStatus() = default;
  explicit ObTransferLockStatus(const STATUS &status) : status_(status) {}

  bool is_valid() const { return START <= status_ && status_ < MAX_STATUS; }
  void reset() { status_ = MAX_STATUS; }
  const char *str() const;
  STATUS get_status() const { return status_; }

  TO_STRING_KV(K_(status), "status", str());
private:
  STATUS status_;
};

struct ObTransferLockInfoRowKey final {
public:
  ObTransferLockInfoRowKey();
  ~ObTransferLockInfoRowKey() = default;
  TO_STRING_KV(K_(tenant_id), K_(ls_id));
  uint64_t tenant_id_;
  share::ObLSID ls_id_;
};

struct ObTransferTaskLockInfo final {
public:
  ObTransferTaskLockInfo();
  ~ObTransferTaskLockInfo() = default;
  bool is_valid() const;
  TO_STRING_KV(K_(tenant_id), K_(ls_id), K_(task_id), K_(status), K_(lock_owner), K_(comment));

public:
  uint64_t tenant_id_;
  share::ObLSID ls_id_;
  int64_t task_id_;
  ObTransferLockStatus status_;
  int64_t lock_owner_;
  ObSqlString comment_;
};

struct ObTransferBuildTabletInfoCtx final
{
public:
  ObTransferBuildTabletInfoCtx();
  ~ObTransferBuildTabletInfoCtx();
  common::ObCurTraceId::TraceId &get_task_id() { return task_id_; }
  share::ObLSID &get_dest_ls_id() { return dest_ls_id_; }
  uint64_t get_data_version() { return data_version_; }

  TO_STRING_KV(K_(index), K_(tablet_info_array), K_(child_task_num), K_(total_tablet_count),
      K_(result), K_(data_version), K_(task_id));
private:
  bool is_valid_() const;

private:
  class ObTransferStorageSchemaMgr final
  {
  public:
    ObTransferStorageSchemaMgr();
    ~ObTransferStorageSchemaMgr();
    int init(const int64_t bucket_num);
    int build_storage_schema(
        const share::ObTransferTaskInfo &task_info,
        ObTimeoutCtx &timeout_ctx);
    int get_storage_schema(
        const ObTabletID &tablet_id,
        ObStorageSchema *&storage_schema);
    void reset();
  private:
    int build_latest_storage_schema_(
        const share::ObTransferTaskInfo &task_info,
        ObTimeoutCtx &timeout_ctx);
    int build_tablet_storage_schema_(
        const share::ObTransferTaskInfo &task_info,
        const ObTabletID &tablet_id,
        const int64_t schema_version,
        ObLS *ls,
        ObMultiVersionSchemaService &schema_service);
  private:
    bool is_inited_;
    common::ObArenaAllocator allocator_;
    hash::ObHashMap<ObTabletID, ObStorageSchema *> storage_schema_map_;
    DISALLOW_COPY_AND_ASSIGN(ObTransferStorageSchemaMgr);
  };

  struct ObTransferTabletInfoMgr final
  {
  public:
    ObTransferTabletInfoMgr();
    ~ObTransferTabletInfoMgr();
    int add_tablet_info(const ObMigrationTabletParam &param);
    int64_t get_tablet_info_num() const;
    int get_tablet_info(const int64_t index, const ObMigrationTabletParam *&param);
    void reuse();
    int build_storage_schema(
        const share::ObTransferTaskInfo &task_info,
        ObTimeoutCtx &timeout_ctx);
    TO_STRING_KV(K_(tablet_info_array));
  private:
    common::SpinRWLock lock_;
    common::ObArray<ObMigrationTabletParam> tablet_info_array_;
    ObTransferStorageSchemaMgr storage_schema_mgr_;
    DISALLOW_COPY_AND_ASSIGN(ObTransferTabletInfoMgr);
  };
private:
  common::SpinRWLock lock_;
  share::ObLSID dest_ls_id_;
  int64_t index_;
  common::ObArray<share::ObTransferTabletInfo> tablet_info_array_;
  int64_t child_task_num_;
  int64_t total_tablet_count_;
  int32_t result_;
  uint64_t data_version_;
  common::ObCurTraceId::TraceId task_id_;
  ObTransferTabletInfoMgr mgr_;
  DISALLOW_COPY_AND_ASSIGN(ObTransferBuildTabletInfoCtx);
};




}
}
#endif
