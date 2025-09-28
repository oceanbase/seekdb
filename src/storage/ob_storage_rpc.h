/**
 * Copyright (c) 2021 OceanBase
 * OceanBase CE is licensed under Mulan PubL v2.
 * You can use this software according to the terms and conditions of the Mulan PubL v2.
 * You may obtain a copy of Mulan PubL v2 at:
 *          http://license.coscl.org.cn/MulanPubL-2.0
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND,
 * EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
 * MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
 * See the Mulan PubL v2 for more details.
 */

#ifndef OCEABASE_STORAGE_RPC
#define OCEABASE_STORAGE_RPC

#include "lib/net/ob_addr.h"
#include "lib/utility/ob_unify_serialize.h"
#include "rpc/obrpc/ob_rpc_packet.h"
#include "rpc/obrpc/ob_rpc_proxy.h"
#include "rpc/obrpc/ob_rpc_processor.h"
#include "rpc/obrpc/ob_rpc_result_code.h"
#include "share/rpc/ob_async_rpc_proxy.h"
#include "common/ob_member.h"
#include "storage/ob_storage_struct.h"
#include "observer/ob_server_struct.h"
#include "storage/ob_storage_schema.h"
#include "storage/high_availability/ob_storage_ha_struct.h"
#include "storage/blocksstable/ob_sstable_meta.h"
#include "storage/ls/ob_ls_meta_package.h"
#include "tablet/ob_tablet_meta.h"
#include "share/restore/ob_ls_restore_status.h"
#include "share/transfer/ob_transfer_info.h"
#include "storage/lob/ob_lob_rpc_struct.h"
#include "storage/blocksstable/ob_logic_macro_id.h"
#include "share/rpc/ob_async_rpc_proxy.h"
#include "storage/meta_mem/ob_tablet_pointer.h"
#ifdef OB_BUILD_SHARED_STORAGE
#include "close_modules/shared_storage/storage/high_availability/ob_migration_warmup_struct.h"
#include "close_modules/shared_storage/storage/shared_storage/micro_cache/ob_ss_micro_cache_common_meta.h"
#include "close_modules/shared_storage/storage/shared_storage/prewarm/ob_ha_prewarm_struct.h"
#include "close_modules/shared_storage/storage/shared_storage/ob_ss_micro_cache.h"
#endif

namespace oceanbase
{
namespace storage
{
class ObLogStreamService;
class ObICopySSTableMacroRangeObProducer;
}

namespace obrpc
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

  TO_STRING_KV(K_(tenant_id), K_(ls_id), K_(table_key), "arg_count", arg_list_.count());
  uint64_t tenant_id_;
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
  TO_STRING_KV(K_(tenant_id), K_(ls_id), K_(table_key), K_(data_version), K_(backfill_tx_scn), K_(copy_macro_range_info));

  uint64_t tenant_id_;
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

struct ObCopyMacroBlockHeader
{
  OB_UNIS_VERSION(2);
public:
  ObCopyMacroBlockHeader();
  virtual ~ObCopyMacroBlockHeader() {}

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

  TO_STRING_KV(K_(tenant_id), K_(ls_id), K_(tablet_id_list), K_(need_check_seq),
      K_(ls_rebuild_seq), K_(is_only_copy_major), K_(version));
  uint64_t tenant_id_;
  share::ObLSID ls_id_;
  common::ObSArray<common::ObTabletID> tablet_id_list_;
  bool need_check_seq_;
  int64_t ls_rebuild_seq_;
  bool is_only_copy_major_;
  uint64_t version_;
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

  TO_STRING_KV(K_(tenant_id), K_(ls_id), K_(need_check_seq),
      K_(ls_rebuild_seq), K_(is_only_copy_major), K_(tablet_sstable_info_arg_list),
      K_(version));

  uint64_t tenant_id_;
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

  TO_STRING_KV(K_(tenant_id), K_(ls_id));
  uint64_t tenant_id_;
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

  TO_STRING_KV(K_(tenant_id), K_(ls_id), K_(version));
  uint64_t tenant_id_;
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

  TO_STRING_KV(K_(tenant_id), K_(ls_id));
  uint64_t tenant_id_;
  share::ObLSID ls_id_;
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

  TO_STRING_KV(K_(tenant_id), K_(ls_id));
  uint64_t tenant_id_;
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

  TO_STRING_KV(K_(tenant_id), K_(ls_id), K_(tablet_id), K_(copy_table_key_array), K_(macro_range_max_marco_count));
  uint64_t tenant_id_;
  share::ObLSID ls_id_;
  common::ObTabletID tablet_id_;
  common::ObSArray<ObITable::TableKey> copy_table_key_array_;
  int64_t macro_range_max_marco_count_;
  bool need_check_seq_;
  int64_t ls_rebuild_seq_;
  DISALLOW_COPY_AND_ASSIGN(ObCopySSTableMacroRangeInfoArg);
};

struct ObCopySSTableMacroRangeInfoHeader final
{
  OB_UNIS_VERSION(2);
public:
  ObCopySSTableMacroRangeInfoHeader();
  ~ObCopySSTableMacroRangeInfoHeader();
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

  TO_STRING_KV(K_(tenant_id), K_(ls_id), K_(tablet_id_array), K_(restore_status), K_(leader_proposal_id));
  uint64_t tenant_id_;
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

  TO_STRING_KV(K_(tenant_id), K_(ls_id), K_(restore_status));
  uint64_t tenant_id_;
  share::ObLSID ls_id_;
  share::ObLSRestoreStatus restore_status_; // restore status
};


struct ObInquireRestoreResp
{
  OB_UNIS_VERSION(2);
public:
  ObInquireRestoreResp();
  virtual ~ObInquireRestoreResp() {}

  TO_STRING_KV(K_(tenant_id), K_(ls_id), K_(is_leader), K_(restore_status));
  uint64_t tenant_id_;
  share::ObLSID ls_id_;
  bool is_leader_;
  share::ObLSRestoreStatus restore_status_; // leader restore status
};

struct ObInquireRestoreArg
{
  OB_UNIS_VERSION(2);
public:
  ObInquireRestoreArg();
  virtual ~ObInquireRestoreArg() {}
  bool is_valid() const;

  TO_STRING_KV(K_(tenant_id), K_(ls_id), K_(restore_status));
  uint64_t tenant_id_;
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

  TO_STRING_KV(K_(tenant_id), K_(ls_meta_package));
  uint64_t tenant_id_;
  storage::ObLSMetaPackage ls_meta_package_;
};

//transfer
struct ObCheckSrcTransferTabletsArg final
{
  OB_UNIS_VERSION(1);
public:
  ObCheckSrcTransferTabletsArg();
  ~ObCheckSrcTransferTabletsArg() {}

  TO_STRING_KV(K_(tenant_id), K_(src_ls_id), K_(tablet_info_array));
  uint64_t tenant_id_;
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

  TO_STRING_KV(K_(tenant_id), K_(src_ls_id));
  uint64_t tenant_id_;
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

  TO_STRING_KV(K_(tenant_id), K_(ls_id));
  uint64_t tenant_id_;
  share::ObLSID ls_id_;
};

struct ObUpdateTransferMetaInfoArg final
{
  OB_UNIS_VERSION(1);
public:
  ObUpdateTransferMetaInfoArg();
  ~ObUpdateTransferMetaInfoArg() {}
  bool is_valid() const;

  TO_STRING_KV(K_(tenant_id), K_(dest_ls_id), K_(transfer_meta_info));
  uint64_t tenant_id_;
  share::ObLSID dest_ls_id_;
  ObLSTransferMetaInfo transfer_meta_info_;
private:
  DISALLOW_COPY_AND_ASSIGN(ObUpdateTransferMetaInfoArg);
};

#ifdef OB_BUILD_SHARED_STORAGE
// migration micro cache related
struct ObGetMicroBlockCacheInfoArg final
{
  OB_UNIS_VERSION(1);
public:
  ObGetMicroBlockCacheInfoArg();
  ~ObGetMicroBlockCacheInfoArg() {}
  bool is_valid() const;

  TO_STRING_KV(K_(tenant_id), K_(ls_id));
public:
  uint64_t tenant_id_;
  share::ObLSID ls_id_;
};

struct ObGetMicroBlockCacheInfoRes final
{
  OB_UNIS_VERSION(1);
public:
  ObGetMicroBlockCacheInfoRes();
  ~ObGetMicroBlockCacheInfoRes() {}

  TO_STRING_KV(K_(ls_cache_info));
public:
  ObSSLSCacheInfo ls_cache_info_;
};

struct ObGetMigrationCacheJobInfoArg final
{
  OB_UNIS_VERSION(1);
public:
  ObGetMigrationCacheJobInfoArg();
  ~ObGetMigrationCacheJobInfoArg() {}
  bool is_valid() const;
  
  TO_STRING_KV(K_(tenant_id), K_(ls_id), K_(task_count));
public:
  uint64_t tenant_id_;
  share::ObLSID ls_id_;
  int64_t task_count_;
};

struct ObGetMigrationCacheJobInfoRes final
{
  OB_UNIS_VERSION(1);
public:
  ObGetMigrationCacheJobInfoRes();
  ~ObGetMigrationCacheJobInfoRes() {}
  void reset();
  int assign(const ObGetMigrationCacheJobInfoRes &res);
  TO_STRING_KV(K_(job_infos));
public:
  common::ObSArray<ObMigrationCacheJobInfo> job_infos_;
private:
  DISALLOW_COPY_AND_ASSIGN(ObGetMigrationCacheJobInfoRes);
};

struct ObGetMicroBlockKeyArg final
{
  OB_UNIS_VERSION(1);
public:
  ObGetMicroBlockKeyArg();
  ~ObGetMicroBlockKeyArg() {}
  bool is_valid() const;
  TO_STRING_KV(K_(tenant_id), K_(ls_id), K_(job_info));
public:
  uint64_t tenant_id_;
  share::ObLSID ls_id_;
  ObMigrationCacheJobInfo job_info_;
};

struct ObMigrateWarmupKeySet final
{
  OB_UNIS_VERSION(1);
public:
  ObMigrateWarmupKeySet();
  ~ObMigrateWarmupKeySet() {}
  bool is_valid() const;
  void reset();
  int assign(const ObMigrateWarmupKeySet &arg);
  TO_STRING_KV(K_(tenant_id), K_(key_sets));
public:
  uint64_t tenant_id_;
  common::ObSArray<ObCopyMicroBlockKeySet> key_sets_;
private:
  DISALLOW_COPY_AND_ASSIGN(ObMigrateWarmupKeySet);
};

struct ObCopyMicroBlockKeySetRes final
{
  OB_UNIS_VERSION(1);
public:
  ObCopyMicroBlockKeySetRes();
  ~ObCopyMicroBlockKeySetRes();
  bool is_valid() const;
  void reset();
  TO_STRING_KV(
      K_(header),
      K_(key_set_array));
public:
  ObCopyMicroBlockKeySetRpcHeader header_;
  obrpc::ObMigrateWarmupKeySet key_set_array_;
private:
  DISALLOW_COPY_AND_ASSIGN(ObCopyMicroBlockKeySetRes);
};

struct ObSSLSFetchMicroBlockArg final
{
public:
  static const int64_t OB_SS_LS_FETCH_MICRO_BLOCK_ARG_VERSION = 1;
  OB_UNIS_VERSION(OB_SS_LS_FETCH_MICRO_BLOCK_ARG_VERSION);
public:
  ObSSLSFetchMicroBlockArg();
  virtual ~ObSSLSFetchMicroBlockArg() {}
  bool is_valid() const;
  int assign(const ObSSLSFetchMicroBlockArg &other);
  TO_STRING_KV(K_(tenant_id), K_(micro_metas));

public:
  uint64_t tenant_id_;
  ObSArray<storage::ObSSMicroBlockCacheKeyMeta> micro_metas_;
private:
  DISALLOW_COPY_AND_ASSIGN(ObSSLSFetchMicroBlockArg);
};
#endif

//src
class ObStorageRpcProxy : public obrpc::ObRpcProxy
{
public:
  static const int64_t STREAM_RPC_TIMEOUT = 30 * 1000 * 1000LL; // 30s
  DEFINE_TO(ObStorageRpcProxy);
  //stream
  RPC_SS(PR5 lob_query, OB_LOB_QUERY, (ObLobQueryArg), common::ObDataBuffer);
#ifdef OB_BUILD_SHARED_STORAGE
  RPC_SS(PR5 fetch_micro_block, OB_HA_FETCH_MICRO_BLOCK, (ObMigrateWarmupKeySet), common::ObDataBuffer);
  RPC_SS(PR5 fetch_replica_prewarm_micro_block, OB_REPLICA_PREWARM_FETCH_MICRO_BLOCK, (ObSSLSFetchMicroBlockArg), common::ObDataBuffer);
#endif
  //single
  RPC_S(PR5 notify_restore_tablets, OB_HA_NOTIFY_RESTORE_TABLETS, (ObNotifyRestoreTabletsArg), ObNotifyRestoreTabletsResp);
  RPC_S(PR5 inquire_restore, OB_HA_NOTIFY_FOLLOWER_RESTORE, (ObInquireRestoreArg), ObInquireRestoreResp);
  RPC_S(PR5 update_ls_meta, OB_HA_UPDATE_LS_META, (ObRestoreUpdateLSMetaArg));
#ifdef OB_BUILD_SHARED_STORAGE
  RPC_S(PR5 fetch_micro_block_keys, OB_HA_FETCH_MICRO_BLOCK_KEYS, (ObGetMicroBlockKeyArg), ObCopyMicroBlockKeySetRes);
  RPC_S(PR5 get_micro_block_cache_info, OB_HA_GET_MICRO_BLOCK_CACHE_INFO, (ObGetMicroBlockCacheInfoArg), ObGetMicroBlockCacheInfoRes);
  RPC_S(PR5 get_migration_cache_job_info, OB_HA_GET_MIGRATION_CACHE_JOB_INFO, (ObGetMigrationCacheJobInfoArg), ObGetMigrationCacheJobInfoRes);
#endif
};

template <ObRpcPacketCode RPC_CODE>
class ObStorageStreamRpcP : public ObRpcProcessor<obrpc::ObStorageRpcProxy::ObRpc<RPC_CODE> >
{
public:
  explicit ObStorageStreamRpcP(common::ObInOutBandwidthThrottle *bandwidth_throttle);
  virtual ~ObStorageStreamRpcP() {}
protected:
  template <typename Data>
  int fill_data(const Data &data);
  int fill_buffer(blocksstable::ObBufferReader &data);
  int flush_and_wait();

  int is_follower_ls(logservice::ObLogService *log_srv, ObLS *ls, bool &is_ls_follower);
protected:
  common::ObInOutBandwidthThrottle *bandwidth_throttle_;
  int64_t last_send_time_;
  common::ObArenaAllocator allocator_;
  static const int64_t FLUSH_TIME_INTERVAL = ObStorageRpcProxy::STREAM_RPC_TIMEOUT / 2;
};

class ObNotifyRestoreTabletsP :
    public ObStorageStreamRpcP<OB_HA_NOTIFY_RESTORE_TABLETS>
{
public:
  explicit ObNotifyRestoreTabletsP(common::ObInOutBandwidthThrottle *bandwidth_throttle);
  virtual ~ObNotifyRestoreTabletsP() {}
protected:
  int process();
};

class ObInquireRestoreP :
    public ObStorageStreamRpcP<OB_HA_NOTIFY_FOLLOWER_RESTORE>
{
public:
  explicit ObInquireRestoreP(common::ObInOutBandwidthThrottle *bandwidth_throttle);
  virtual ~ObInquireRestoreP() {}
protected:
  int process();
};

class ObUpdateLSMetaP :
    public ObStorageStreamRpcP<OB_HA_UPDATE_LS_META>
{
public:
  explicit ObUpdateLSMetaP(common::ObInOutBandwidthThrottle *bandwidth_throttle);
  virtual ~ObUpdateLSMetaP() {}
protected:
  int process();
};

class ObLobQueryP : public ObStorageStreamRpcP<OB_LOB_QUERY>
{
public:
  explicit ObLobQueryP(common::ObInOutBandwidthThrottle *bandwidth_throttle);
  virtual ~ObLobQueryP() {}
protected:
  int process();
private:
  int process_read();
  int process_getlength();
  int64_t get_timeout() const;
};
#ifdef OB_BUILD_SHARED_STORAGE
class ObFetchMicroBlockKeysP:
    public ObStorageRpcProxy::Processor<OB_HA_FETCH_MICRO_BLOCK_KEYS>
{
public:
  ObFetchMicroBlockKeysP() = default;
  virtual ~ObFetchMicroBlockKeysP() {}
protected:
  int process();
private:
  int set_header_attr_(
      const ObCopyMicroBlockKeySetRpcHeader::ConnectStatus connect_status,
      const int64_t blk_idx,
      const int64_t count,
      ObCopyMicroBlockKeySetRpcHeader &header);
};

class ObFetchMicroBlockP:
    public ObStorageStreamRpcP<OB_HA_FETCH_MICRO_BLOCK>
{
public:
  explicit ObFetchMicroBlockP(common::ObInOutBandwidthThrottle *bandwidth_throttle);
  virtual ~ObFetchMicroBlockP() {}
protected:
  int process();
};

class ObGetMicroBlockCacheInfoP:
    public ObStorageRpcProxy::Processor<OB_HA_GET_MICRO_BLOCK_CACHE_INFO>
{
public:
  ObGetMicroBlockCacheInfoP() = default;
  virtual ~ObGetMicroBlockCacheInfoP() {}
protected:
  int process();
};

class ObGetMigrationCacheJobInfoP:
    public ObStorageRpcProxy::Processor<OB_HA_GET_MIGRATION_CACHE_JOB_INFO>
{
public:
  ObGetMigrationCacheJobInfoP() = default;
  virtual ~ObGetMigrationCacheJobInfoP() {}
protected:
  int process();
  private:
  int convert_block_range_to_job_infos_(
      const ObIArray<ObSSPhyBlockIdxRange> &block_ranges, ObIArray<ObMigrationCacheJobInfo> &job_infos);
};

class ObFetchReplicaPrewarmMicroBlockP:
    public ObStorageStreamRpcP<OB_REPLICA_PREWARM_FETCH_MICRO_BLOCK>
{
public:
  explicit ObFetchReplicaPrewarmMicroBlockP(common::ObInOutBandwidthThrottle *bandwidth_throttle);
  virtual ~ObFetchReplicaPrewarmMicroBlockP() {}
protected:
  int process();
};
#endif

} // obrpc


namespace storage
{
//dst
class ObIStorageRpc
{
public:
  ObIStorageRpc() {}
  virtual ~ObIStorageRpc() {}
  virtual int init(
      obrpc::ObStorageRpcProxy *rpc_proxy,
      const common::ObAddr &self,
      obrpc::ObCommonRpcProxy *rs_rpc_proxy) = 0;
  virtual void destroy() = 0;
public:
  // Notify follower restore some tablets from leader.
  virtual int notify_restore_tablets(
      const uint64_t tenant_id,
      const ObStorageHASrcInfo &follower_info,
      const share::ObLSID &ls_id,
      const int64_t &proposal_id,
      const common::ObIArray<common::ObTabletID>& tablet_id_array,
      const share::ObLSRestoreStatus &restore_status,
      obrpc::ObNotifyRestoreTabletsResp &restore_resp) = 0;

  // inquire restore status from src.
  virtual int inquire_restore(
      const uint64_t tenant_id,
      const ObStorageHASrcInfo &src_info,
      const share::ObLSID &ls_id,
      const share::ObLSRestoreStatus &restore_status,
      obrpc::ObInquireRestoreResp &restore_resp) = 0;

  virtual int update_ls_meta(
      const uint64_t tenant_id,
      const ObStorageHASrcInfo &dest_info,
      const storage::ObLSMetaPackage &ls_meta) = 0;
};

class ObStorageRpc: public ObIStorageRpc
{
public:
  ObStorageRpc();
  ~ObStorageRpc();
  int init(obrpc::ObStorageRpcProxy *rpc_proxy,
      const common::ObAddr &self, obrpc::ObCommonRpcProxy *rs_rpc_proxy);
  void destroy();
public:
  // Notify follower restore some tablets from leader.
  virtual int notify_restore_tablets(
      const uint64_t tenant_id,
      const ObStorageHASrcInfo &follower_info,
      const share::ObLSID &ls_id,
      const int64_t &proposal_id,
      const common::ObIArray<common::ObTabletID>& tablet_id_array,
      const share::ObLSRestoreStatus &restore_status,
      obrpc::ObNotifyRestoreTabletsResp &restore_resp);

  // inquire restore status from src.
  virtual int inquire_restore(
      const uint64_t tenant_id,
      const ObStorageHASrcInfo &src_info,
      const share::ObLSID &ls_id,
      const share::ObLSRestoreStatus &restore_status,
      obrpc::ObInquireRestoreResp &restore_resp);

  virtual int update_ls_meta(
      const uint64_t tenant_id,
      const ObStorageHASrcInfo &dest_info,
      const storage::ObLSMetaPackage &ls_meta);

#ifdef OB_BUILD_SHARED_STORAGE
  virtual int get_ls_micro_block_cache_info(
      const uint64_t tenant_id,
      const share::ObLSID &ls_id,
      const ObStorageHASrcInfo &src_info,
      ObSSLSCacheInfo &cache_info);
  virtual int get_ls_migration_cache_job_info(
      const uint64_t tenant_id,
      const share::ObLSID &ls_id,
      const ObStorageHASrcInfo &src_info,
      const int64_t task_count,
      obrpc::ObGetMigrationCacheJobInfoRes &res);
  virtual int get_micro_block_key_set(
      const uint64_t tenant_id,
      const share::ObLSID &ls_id,
      const ObStorageHASrcInfo &src_info,
      const ObMigrationCacheJobInfo &job_info,
      obrpc::ObCopyMicroBlockKeySetRes &res);
#endif
private:
  bool is_inited_;
  obrpc::ObStorageRpcProxy *rpc_proxy_;
  common::ObAddr self_;
  obrpc::ObCommonRpcProxy *rs_rpc_proxy_;
};

template<obrpc::ObRpcPacketCode RPC_CODE>
class ObStorageStreamRpcReader
{
public:
  ObStorageStreamRpcReader();
  virtual ~ObStorageStreamRpcReader() {}
  int init(
      common::ObInOutBandwidthThrottle &bandwidth_throttle);
  int fetch_next_buffer_if_need();
  int check_need_fetch_next_buffer(bool &need_fetch);
  int fetch_next_buffer();
  template<typename Data>
  int fetch_and_decode(Data &data);
  template<typename Data>
  int fetch_and_decode(common::ObIAllocator &allocator, Data &data);
  template<typename Data>
  int fetch_and_decode_list(common::ObIAllocator &allocator,
                            common::ObIArray<Data> &data_list);
  template<typename Data>
  int fetch_and_decode_list(
      const int64_t data_list_count,
      common::ObIArray<Data> &data_list);
  common::ObDataBuffer &get_rpc_buffer() { return rpc_buffer_; }
  const common::ObAddr &get_dst_addr() const { return handle_.get_dst_addr(); }
  obrpc::ObStorageRpcProxy::SSHandle<RPC_CODE> &get_handle() { return handle_; }
  void reuse()
  {
    rpc_buffer_.get_position() = 0;
    rpc_buffer_parse_pos_ = 0;
  }
private:
  bool is_inited_;
  obrpc::ObStorageRpcProxy::SSHandle<RPC_CODE> handle_;
  common::ObInOutBandwidthThrottle *bandwidth_throttle_;
  common::ObDataBuffer rpc_buffer_;
  int64_t rpc_buffer_parse_pos_;
  common::ObArenaAllocator allocator_;
  int64_t last_send_time_;
  int64_t data_size_;
};

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
