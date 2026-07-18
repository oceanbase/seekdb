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

#ifndef OCEABASE_STORAGE_RPC
#define OCEABASE_STORAGE_RPC

#include "lib/net/ob_addr.h"
#include "storage/ob_storage_rpc_arg.h"
#include "storage/tx/ob_tx_result_struct.h"
#include "lib/utility/ob_unify_serialize.h"
#include "rpc/frame/ob_result_code.h"
#include "common/ob_member.h"
#include "storage/ob_storage_struct.h"
#include "observer/ob_server_struct.h"
#include "storage/ob_storage_schema.h"
#include "storage/ob_storage_ha_struct.h"
#include "storage/blocksstable/ob_sstable_meta.h"
#include "storage/ls/ob_ls_meta_package.h"
#include "tablet/ob_tablet_meta.h"
#include "share/ls/ob_ls_restore_status.h"
#include "share/transfer/ob_transfer_info.h"
#include "storage/lob/ob_lob_rpc_struct.h"
#include "storage/blocksstable/ob_logic_macro_id.h"
#include "storage/meta_mem/ob_tablet_pointer.h"

namespace oceanbase
{
namespace rpc { namespace frame { class ObReqTransport; } }
namespace storage
{
class ObLogStreamService;
class ObICopySSTableMacroRangeObProducer;
}

namespace obcall
{

struct ObCopyMacroBlockArg
{
  OB_UNIS_VERSION(2);
public:
  ObCopyMacroBlockArg();
  virtual ~ObCopyMacroBlockArg() {}
  TO_STRING_KV(K_(logic_macro_block_id));
  blocksstable::ObLogicMacroBlockId logic_macro_block_id_;
};

struct ObCopyMacroBlockListArg
{
  OB_UNIS_VERSION(2);
public:
  ObCopyMacroBlockListArg();
  virtual ~ObCopyMacroBlockListArg() {}

  bool is_valid() const;

  TO_STRING_KV(K_(ls_id), K_(table_key), "arg_count", arg_list_.count());
  share::ObLSID ls_id_;
  storage::ObITable::TableKey table_key_;
  common::ObSArray<ObCopyMacroBlockArg> arg_list_;
};

enum ObCopyMacroBlockDataType {
  MACRO_DATA = 0,
  MACRO_META_ROW = 1,
  MAX
};

struct ObCopyMacroBlockInfo final
{
  OB_UNIS_VERSION(1);
public:
  ObCopyMacroBlockInfo();
  ~ObCopyMacroBlockInfo() {}

  TO_STRING_KV(K_(logical_id), K_(data_type));
public:
  ObLogicMacroBlockId logical_id_;
  ObCopyMacroBlockDataType data_type_;
};

struct ObCopyMacroBlockRangeArg final
{
  OB_UNIS_VERSION(2);
public:
  ObCopyMacroBlockRangeArg();
  ~ObCopyMacroBlockRangeArg() {}

  bool is_valid() const;
  TO_STRING_KV(K_(ls_id), K_(table_key), K_(data_version), K_(backfill_tx_scn), K_(copy_macro_range_info));

  share::ObLSID ls_id_;
  storage::ObITable::TableKey table_key_;
  int64_t data_version_;
  share::SCN backfill_tx_scn_;
  storage::ObCopyMacroRangeInfo copy_macro_range_info_;
  bool need_check_seq_;
  int64_t ls_rebuild_seq_;
  ObSArray<ObCopyMacroBlockInfo> copy_macro_block_infos_;
  DISALLOW_COPY_AND_ASSIGN(ObCopyMacroBlockRangeArg);
};

// Simplified version for single-replica scenario (no tenant/ls_id needed)
struct ObRestoreCopyMacroBlockRangeArg final
{
  OB_UNIS_VERSION(1);
public:
  ObRestoreCopyMacroBlockRangeArg();
  ~ObRestoreCopyMacroBlockRangeArg() {}

  bool is_valid() const;
  TO_STRING_KV(K_(table_key), K_(data_version), K_(backfill_tx_scn), K_(copy_macro_range_info));

  storage::ObITable::TableKey table_key_;
  int64_t data_version_;
  share::SCN backfill_tx_scn_;
  storage::ObCopyMacroRangeInfo copy_macro_range_info_;
  ObSArray<ObCopyMacroBlockInfo> copy_macro_block_infos_;
  DISALLOW_COPY_AND_ASSIGN(ObRestoreCopyMacroBlockRangeArg);
};

struct ObCopyMacroBlockHeader
{
  OB_UNIS_VERSION(2);
public:
  ObCopyMacroBlockHeader();
  virtual ~ObCopyMacroBlockHeader() {}
  void reset();

  TO_STRING_KV(K_(is_reuse_macro_block), K_(occupy_size), K_(data_type));
  bool is_reuse_macro_block_;
  int64_t occupy_size_;
  ObCopyMacroBlockDataType data_type_; // FARM COMPAT WHITELIST FOR data_type_: renamed
};

struct ObCopyTabletInfoArg
{
  OB_UNIS_VERSION(2);
public:
  ObCopyTabletInfoArg();
  virtual ~ObCopyTabletInfoArg() {}

  TO_STRING_KV(K_(ls_id), K_(tablet_id_list), K_(need_check_seq),
      K_(ls_rebuild_seq), K_(is_only_copy_major), K_(version));
  share::ObLSID ls_id_;
  common::ObSArray<common::ObTabletID> tablet_id_list_;
  bool need_check_seq_;
  int64_t ls_rebuild_seq_;
  bool is_only_copy_major_;
  uint64_t version_;
};

struct ObRestoreCopyTabletInfoArg final
{
  OB_UNIS_VERSION(1);
public:
  ObRestoreCopyTabletInfoArg();
  virtual ~ObRestoreCopyTabletInfoArg() {}

  bool is_valid() const { return true; }
  TO_STRING_KV(K_(tablet_id_list));
  common::ObSArray<common::ObTabletID> tablet_id_list_;
};

struct ObCopyTabletInfo
{
  OB_UNIS_VERSION(2);
public:
  ObCopyTabletInfo();
  virtual ~ObCopyTabletInfo() {}
  void reset();
  bool is_valid() const;
  TO_STRING_KV(K_(tablet_id), K_(status), K_(param), K_(data_size), K_(version));

  common::ObTabletID tablet_id_;
  storage::ObCopyTabletStatus::STATUS status_;
  storage::ObMigrationTabletParam param_;
  int64_t data_size_; //need copy ssttablet size
  uint64_t version_;
};

struct ObCopyTabletSSTableInfoArg final
{
  OB_UNIS_VERSION(2);
public:
  ObCopyTabletSSTableInfoArg();
  ~ObCopyTabletSSTableInfoArg();
  bool is_valid() const;
  void reset();
  TO_STRING_KV(K_(tablet_id), K_(max_major_sstable_snapshot), K_(minor_sstable_scn_range),
      K_(ddl_sstable_scn_range));

  common::ObTabletID tablet_id_;
  int64_t max_major_sstable_snapshot_;
  share::ObScnRange minor_sstable_scn_range_;
  share::ObScnRange ddl_sstable_scn_range_;
};

struct ObCopyTabletsSSTableInfoArg final
{
  OB_UNIS_VERSION(2);
public:
  ObCopyTabletsSSTableInfoArg();
  ~ObCopyTabletsSSTableInfoArg();
  void reset();
  int assign(const ObCopyTabletsSSTableInfoArg &arg);

  TO_STRING_KV(K_(ls_id), K_(need_check_seq),
      K_(ls_rebuild_seq), K_(is_only_copy_major), K_(tablet_sstable_info_arg_list),
      K_(version));

  share::ObLSID ls_id_;
  bool need_check_seq_;
  int64_t ls_rebuild_seq_;
  bool is_only_copy_major_;
  common::ObSArray<ObCopyTabletSSTableInfoArg> tablet_sstable_info_arg_list_;
  uint64_t version_;
  DISALLOW_COPY_AND_ASSIGN(ObCopyTabletsSSTableInfoArg);
};

struct ObCopyTabletSSTableInfo
{
  OB_UNIS_VERSION(2);
public:
  ObCopyTabletSSTableInfo();
  virtual ~ObCopyTabletSSTableInfo() {}
  void reset();
  int assign(const ObCopyTabletSSTableInfo &info);
  bool is_valid() const;
  TO_STRING_KV(K_(tablet_id), K_(table_key), K_(param));

  common::ObTabletID tablet_id_;
  storage::ObITable::TableKey table_key_;
  blocksstable::ObMigrationSSTableParam param_;
};

struct ObCopyLSInfoArg
{
  OB_UNIS_VERSION(2);
public:
  ObCopyLSInfoArg();
  virtual ~ObCopyLSInfoArg() {}

  TO_STRING_KV(K_(ls_id));
  share::ObLSID ls_id_;
  uint64_t version_;
};

struct ObCopyLSInfo
{
  OB_UNIS_VERSION(2);
public:
  ObCopyLSInfo();
  virtual ~ObCopyLSInfo() {}

  TO_STRING_KV(K_(ls_meta_package), K_(tablet_id_array), K_(is_log_sync), K_(version));
  storage::ObLSMetaPackage ls_meta_package_;
  common::ObSArray<common::ObTabletID> tablet_id_array_;
  bool is_log_sync_;
  uint64_t version_;
};

struct ObFetchLSMetaInfoArg
{
  OB_UNIS_VERSION(2);
public:
  ObFetchLSMetaInfoArg();
  virtual ~ObFetchLSMetaInfoArg() {}

  TO_STRING_KV(K_(ls_id), K_(version));
  share::ObLSID ls_id_;
  uint64_t version_;
};

struct ObFetchLSMetaInfoResp
{
  OB_UNIS_VERSION(2);
public:
  ObFetchLSMetaInfoResp();
  virtual ~ObFetchLSMetaInfoResp() {}
  bool is_valid() const;

  TO_STRING_KV(K_(ls_meta_package), K_(has_transfer_table), K_(version));
  storage::ObLSMetaPackage ls_meta_package_;
  uint64_t version_;
  bool has_transfer_table_;
};

struct ObFetchLSMemberListArg
{
  OB_UNIS_VERSION(2);
public:
  ObFetchLSMemberListArg();
  virtual ~ObFetchLSMemberListArg() {}

  TO_STRING_KV(K_(ls_id));
  share::ObLSID ls_id_;
};

struct ObCheckRestorePreconditionResult final
{
  OB_UNIS_VERSION(1);
public:
  ObCheckRestorePreconditionResult();
  virtual ~ObCheckRestorePreconditionResult() {}

  TO_STRING_KV(K_(required_disk_size), K_(total_tablet_size), K_(cluster_version));
  int64_t required_disk_size_;  // From ls_info.required_data_disk_size_
  int64_t total_tablet_size_;    // Sum of all tablet sizes
  uint64_t cluster_version_;
};

struct ObFetchLSMemberListInfo
{
  OB_UNIS_VERSION(2);
public:
  ObFetchLSMemberListInfo();
  virtual ~ObFetchLSMemberListInfo() {}

  TO_STRING_KV(K_(member_list));
  common::ObMemberList member_list_;
};

struct ObFetchLSMemberAndLearnerListArg
{
  OB_UNIS_VERSION(2);
public:
  ObFetchLSMemberAndLearnerListArg();
  virtual ~ObFetchLSMemberAndLearnerListArg() {}

  TO_STRING_KV(K_(ls_id));
  share::ObLSID ls_id_;
};

struct ObFetchLSMemberAndLearnerListInfo
{
  OB_UNIS_VERSION(2);
public:
  ObFetchLSMemberAndLearnerListInfo();
  virtual ~ObFetchLSMemberAndLearnerListInfo() {}

  TO_STRING_KV(K_(member_list), K_(learner_list));
  common::ObMemberList member_list_;
  common::GlobalLearnerList learner_list_;
};

struct ObCopySSTableMacroRangeInfoArg final
{
  OB_UNIS_VERSION(2);
public:
  ObCopySSTableMacroRangeInfoArg();
  ~ObCopySSTableMacroRangeInfoArg();
  bool is_valid() const;
  int assign(const ObCopySSTableMacroRangeInfoArg &arg);

  TO_STRING_KV(K_(ls_id), K_(tablet_id), K_(copy_table_key_array), K_(macro_range_max_marco_count));
  share::ObLSID ls_id_;
  common::ObTabletID tablet_id_;
  common::ObSArray<ObITable::TableKey> copy_table_key_array_;
  int64_t macro_range_max_marco_count_;
  bool need_check_seq_;
  int64_t ls_rebuild_seq_;
  DISALLOW_COPY_AND_ASSIGN(ObCopySSTableMacroRangeInfoArg);
};

struct ObRestoreCopySSTableMacroRangeInfoArg final
{
  OB_UNIS_VERSION(1);
public:
  ObRestoreCopySSTableMacroRangeInfoArg();
  ~ObRestoreCopySSTableMacroRangeInfoArg();
  bool is_valid() const;
  int assign(const ObRestoreCopySSTableMacroRangeInfoArg &arg);

  TO_STRING_KV(K_(tablet_id), K_(copy_table_key_array), K_(macro_range_max_marco_count));
  common::ObTabletID tablet_id_;
  common::ObSArray<ObITable::TableKey> copy_table_key_array_;
  int64_t macro_range_max_marco_count_;
  DISALLOW_COPY_AND_ASSIGN(ObRestoreCopySSTableMacroRangeInfoArg);
};

struct ObCopySSTableMacroRangeInfoHeader final
{
  OB_UNIS_VERSION(2);
public:
  ObCopySSTableMacroRangeInfoHeader();
  ~ObCopySSTableMacroRangeInfoHeader();
  bool is_valid() const;
  void reset();
  TO_STRING_KV(K_(copy_table_key), K_(macro_range_count));

  ObITable::TableKey copy_table_key_;
  int64_t macro_range_count_;
};

struct ObCopyTabletSSTableHeader final
{
  OB_UNIS_VERSION(2);
public:
  ObCopyTabletSSTableHeader();
  ~ObCopyTabletSSTableHeader() {}
  void reset();
  bool is_valid() const;
  TO_STRING_KV(K_(tablet_id), K_(status), K_(sstable_count), K_(tablet_meta), K_(version));

  common::ObTabletID tablet_id_;
  storage::ObCopyTabletStatus::STATUS status_;
  int64_t sstable_count_;
  ObMigrationTabletParam tablet_meta_;
  uint64_t version_; // source observer version.
};

// Leader notify follower to restore some tablets.
struct ObNotifyRestoreTabletsArg
{
  OB_UNIS_VERSION(2);
public:
  ObNotifyRestoreTabletsArg();
  virtual ~ObNotifyRestoreTabletsArg() {}
  bool is_valid() const;

  TO_STRING_KV(K_(ls_id), K_(tablet_id_array), K_(restore_status), K_(leader_proposal_id));
  share::ObLSID ls_id_;
  common::ObSArray<common::ObTabletID> tablet_id_array_;
  share::ObLSRestoreStatus restore_status_; // indicate the type of data to restore
  int64_t leader_proposal_id_;
};

struct ObNotifyRestoreTabletsResp
{
  OB_UNIS_VERSION(2);
public:
  ObNotifyRestoreTabletsResp();
  virtual ~ObNotifyRestoreTabletsResp() {}

  TO_STRING_KV(K_(ls_id), K_(restore_status));
  share::ObLSID ls_id_;
  ObRestoreStatus restore_status_; // restore status
};


struct ObInquireRestoreResp
{
  OB_UNIS_VERSION(2);
public:
  ObInquireRestoreResp();
  virtual ~ObInquireRestoreResp() {}

  TO_STRING_KV(K_(ls_id), K_(is_leader), K_(restore_status));
  share::ObLSID ls_id_;
  bool is_leader_;
  ObRestoreStatus restore_status_; // leader restore status
};

struct ObInquireRestoreArg
{
  OB_UNIS_VERSION(2);
public:
  ObInquireRestoreArg();
  virtual ~ObInquireRestoreArg() {}
  bool is_valid() const;

  TO_STRING_KV(K_(ls_id), K_(restore_status));
  share::ObLSID ls_id_;
  share::ObLSRestoreStatus restore_status_; // restore status
};

struct ObRestoreUpdateLSMetaArg
{
  OB_UNIS_VERSION(2);
public:
  ObRestoreUpdateLSMetaArg();
  virtual ~ObRestoreUpdateLSMetaArg() {}
  bool is_valid() const;

  TO_STRING_KV(K_(ls_meta_package));
  storage::ObLSMetaPackage ls_meta_package_;
};

//transfer
struct ObCheckSrcTransferTabletsArg final
{
  OB_UNIS_VERSION(1);
public:
  ObCheckSrcTransferTabletsArg();
  ~ObCheckSrcTransferTabletsArg() {}

  TO_STRING_KV(K_(src_ls_id), K_(tablet_info_array));
  share::ObLSID src_ls_id_;
  common::ObSArray<share::ObTransferTabletInfo> tablet_info_array_;
private:
  DISALLOW_COPY_AND_ASSIGN(ObCheckSrcTransferTabletsArg);
};

struct ObGetLSActiveTransCountArg final
{
  OB_UNIS_VERSION(1);
public:
  ObGetLSActiveTransCountArg();
  ~ObGetLSActiveTransCountArg() {}

  TO_STRING_KV(K_(src_ls_id));
  share::ObLSID src_ls_id_;
};

struct ObGetLSActiveTransCountRes final
{
  OB_UNIS_VERSION(1);
public:
  ObGetLSActiveTransCountRes();
  ~ObGetLSActiveTransCountRes() {}
  bool is_valid() const;
  void reset();

  TO_STRING_KV(K_(active_trans_count));
  int64_t active_trans_count_;
};

// Fetch ls meta and all tablet metas by stream reader.
struct ObCopyLSViewArg final
{
  OB_UNIS_VERSION(1);
public:
  ObCopyLSViewArg();
  ~ObCopyLSViewArg() {}

  TO_STRING_KV(K_(ls_id));
  share::ObLSID ls_id_;
};

// Legacy shared-storage migrate-warmup obcall RPC arg/result structs removed
// (ObGetMicroBlockCacheInfo{Arg,Res}, ObGetMigrationCacheJobInfo{Arg,Res},
//  ObGetMicroBlockKeyArg, ObMigrateWarmupKeySet, ObCopyMicroBlockKeySetRes,
//  ObSSLSFetchMicroBlockArg) — send/recv path replaced by gRPC.

//src
// Inert shell: all obcall RPC methods are removed/dead in seekdb (single-replica;
// HA/migration is gRPC). Kept only as a pointer type for dead HA plumbing; no
// longer derives from the obcall RPC framework.
class ObStorageRpcProxy
{
public:
  static const int64_t STREAM_RPC_TIMEOUT = 30 * 1000 * 1000LL; // 30s
  int init(const common::ObAddr & = common::ObAddr())
  { return common::OB_SUCCESS; }
  void destroy() {}
};

// ObStorageStreamRpcP (obcall stream-RPC processor template) deleted — dead in seekdb.


// cross-tenant LOB obcall RPC removed: ObLobQueryP (OB_LOB_QUERY processor) deleted — the
// cross-tenant LOB read now runs in-process (see ObLobRemoteUtil in storage/lob/ob_lob_remote.cpp).
// Legacy shared-storage migrate-warmup obcall RPC processors removed
// (ObFetchMicroBlockKeysP / ObFetchMicroBlockP / ObGetMicroBlockCacheInfoP /
//  ObGetMigrationCacheJobInfoP / ObFetchReplicaPrewarmMicroBlockP) — replaced by gRPC.

} // obcall


namespace storage
{
//dst
class ObIStorageRpc
{
public:
  ObIStorageRpc() {}
  virtual ~ObIStorageRpc() {}
  virtual int init(
      obcall::ObStorageRpcProxy *rpc_proxy,
      const common::ObAddr &self) = 0;
  virtual void destroy() = 0;
public:


};

class ObStorageRpc: public ObIStorageRpc
{
public:
  ObStorageRpc();
  ~ObStorageRpc();
  int init(obcall::ObStorageRpcProxy *rpc_proxy,
      const common::ObAddr &self);
  void destroy();
public:



  // Legacy shared-storage migrate-warmup ObStorageRpc wrappers removed
  // (get_ls_micro_block_cache_info / get_ls_migration_cache_job_info /
  //  get_micro_block_key_set) — replaced by gRPC.
private:
  bool is_inited_;
  obcall::ObStorageRpcProxy *rpc_proxy_;
  common::ObAddr self_;
};

// ObStorageStreamRpcReader (obcall stream-RPC reader template) deleted — dead in seekdb.

class ObHasTransferTableFilterOp final : public ObITabletFilterOp
{
public:
  int do_filter(const ObTabletResidentInfo &info, bool &is_skipped) override
  {
    is_skipped = !info.has_transfer_table();
    return OB_SUCCESS;
  }
};

} // storage
} // oceanbase

#include "storage/ob_storage_rpc.ipp"

#endif //OCEANBASE_STORAGE_OB_PARTITION_SERVICE_RPC_
