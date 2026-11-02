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
#define USING_LOG_PREFIX RPC
#include "logservice/logrpc/ob_log_rpc_arg.h"
#include "storage/ob_storage_rpc_arg.h"
#include "storage/tx/ob_trans_service.h"  // used by IncCommitLogArg::release
#include "share/rc/ob_tenant_base.h"  // MTL_WITH_CHECK
namespace oceanbase
{
namespace obcall
{
using namespace oceanbase::transaction;
using namespace oceanbase::storage;
OB_SERIALIZE_MEMBER(ObBatchGetTabletBindingRes, binding_datas_);

OB_SERIALIZE_MEMBER(ObBatchGetTabletSplitRes, split_datas_);

OB_SERIALIZE_MEMBER(ObRpcRemoteWriteDDLCommitLogArg, ls_id_, table_key_, start_scn_,
                    table_id_, execution_id_, ddl_task_id_);

#ifdef OB_BUILD_SHARED_STORAGE
OB_SERIALIZE_MEMBER(ObRpcRemoteWriteDDLFinishLogArg, log_info_);
#endif

OB_DEF_SERIALIZE(ObRegisterTxDataArg)
{
  int ret = OB_SUCCESS;
  OB_UNIS_ENCODE(*tx_desc_);
  LST_DO_CODE(OB_UNIS_ENCODE, ls_id_, type_, buf_, request_id_, register_flag_, seq_no_);
  return ret;
}

OB_DEF_DESERIALIZE(ObRegisterTxDataArg)
{
  int ret = OB_SUCCESS;
  if (OB_SUCC(ret)) {
    ObTransService *tx_svc = MTL_WITH_CHECK(ObTransService *);
    if (OB_ISNULL(tx_svc)) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("null tx service ptr", KR(ret));
    } else if (OB_FAIL(tx_svc->acquire_tx(buf, data_len, pos, tx_desc_))) {
      LOG_WARN("acquire tx by deserialize fail", K(data_len), K(pos), KR(ret));
    } else {
      LST_DO_CODE(OB_UNIS_DECODE, ls_id_, type_, buf_, request_id_, register_flag_, seq_no_);
      LOG_INFO("deserialize txDesc from session", KPC_(tx_desc), KPC(this));
    }
  }
  return ret;
}

OB_DEF_SERIALIZE_SIZE(ObRegisterTxDataArg)
{
  int64_t len = 0;
  OB_UNIS_ADD_LEN(*tx_desc_);
  LST_DO_CODE(OB_UNIS_ADD_LEN, ls_id_, type_, buf_, request_id_, register_flag_, seq_no_);
  return len;
}
int ObRegisterTxDataArg::init(const ObTxDesc &tx_desc,
                              const ObLSID &ls_id,
                              const ObTxDataSourceType &type,
                              const ObString &buf,
                              const transaction::ObTxSEQ seq_no,
                              const int64_t base_request_id,
                              const transaction::ObRegisterMdsFlag &register_flag)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(!tx_desc.is_valid() || !ls_id.is_valid()
                  || type == ObTxDataSourceType::UNKNOWN || !seq_no.is_valid())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", KR(ret), K(tx_desc), K(ls_id), K(type), K(seq_no));
  } else {
    tx_desc_ = const_cast<ObTxDesc *>(&tx_desc);
    ls_id_ = ls_id;
    type_ = type;
    buf_ = buf;
    seq_no_ = seq_no;
    request_id_ = base_request_id;
    register_flag_ = register_flag;
  }
  return ret;
}

void ObRegisterTxDataArg::inc_request_id(const int64_t base_request_id)
{
  if (-1 != base_request_id) {
    request_id_ = base_request_id + 1;
  } else {
    request_id_++;
  }
}

int ObRpcRemoteWriteDDLCommitLogArg::init(const share::ObLSID &ls_id,
                                          const storage::ObITable::TableKey &table_key,
                                          const SCN &start_scn)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(!ls_id.is_valid() || !table_key.is_valid() || !start_scn.is_valid_and_not_min())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("tablet id is not valid", K(ret), K(ls_id), K(table_key), K(start_scn));
  } else {
    ls_id_ = ls_id;
    table_key_ = table_key;
    start_scn_ = start_scn;
  }
  return ret;
}

#ifdef OB_BUILD_SHARED_STORAGE
int ObRpcRemoteWriteDDLFinishLogArg::init(const storage::ObDDLFinishLogInfo &log)
{
  int ret = OB_SUCCESS;
  if (!log.is_valid()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", K(ret), K(log));
  } else if (OB_FAIL(log_info_.assign(log))) {
    LOG_WARN("fail to get assign log", K(ret), K(log));
  }
  return ret;
}
#endif
int ObRpcRemoteWriteDDLRedoLogArg::init(const share::ObLSID &ls_id,
                                        const storage::ObDDLMacroBlockRedoInfo &redo_info,
                                        const int64_t task_id)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(task_id == 0 || !ls_id.is_valid() || !redo_info.is_valid())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("args are not valid", K(ret), K(task_id), K(ls_id), K(redo_info));
  } else {
    ls_id_ = ls_id;
    redo_info_ = redo_info;
    task_id_ = task_id;
  }
  return ret;
}

OB_SERIALIZE_MEMBER(ObRpcRemoteWriteDDLRedoLogArg, ls_id_, redo_info_, task_id_);

int ObRpcRemoteWriteDDLIncCommitLogArg::init(const share::ObLSID &ls_id,
                                             const common::ObTabletID tablet_id,
                                             const common::ObTabletID lob_meta_tablet_id,
                                             transaction::ObTxDesc *tx_desc)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(!ls_id.is_valid() || !tablet_id.is_valid() ||
                  OB_ISNULL(tx_desc) || !tx_desc->is_valid())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("tablet id is not valid", K(ret), K(ls_id), K(tablet_id), K(lob_meta_tablet_id), KPC(tx_desc));
  } else if (OB_FAIL(release())) {
    LOG_WARN("fail to release tx_desc", K(ret));
  } else {
    ls_id_ = ls_id;
    tablet_id_ = tablet_id;
    lob_meta_tablet_id_ = lob_meta_tablet_id;
    tx_desc_ = tx_desc;
  }
  return ret;
}

int ObRpcRemoteWriteDDLIncCommitLogArg::release()
{
  int ret = OB_SUCCESS;
  if (tx_desc_ != nullptr && need_release_) {
    ObTransService *tx_svc = MTL_WITH_CHECK(ObTransService *);
    if (OB_ISNULL(tx_svc)) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("null tx service ptr", K(ret));
    } else if (OB_FAIL(tx_svc->release_tx(*tx_desc_))) {
      LOG_WARN("release tx fail", K(ret));
    } else {
      need_release_ = false;
      tx_desc_ = nullptr;
    }
  }

  return ret;
}

ObRpcRemoteWriteDDLRedoLogArg::ObRpcRemoteWriteDDLRedoLogArg()
  : ls_id_(), redo_info_(), task_id_(0)
{}



ObRpcRemoteWriteDDLCommitLogArg::ObRpcRemoteWriteDDLCommitLogArg()
  : ls_id_(), table_key_(), start_scn_(SCN::min_scn()),
    table_id_(0), execution_id_(-1), ddl_task_id_(0)
{}



#ifdef OB_BUILD_SHARED_STORAGE
ObRpcRemoteWriteDDLFinishLogArg::ObRpcRemoteWriteDDLFinishLogArg()
  : log_info_()
{}



OB_SERIALIZE_MEMBER(ObGetSSMacroBlockArg, tenant_id_, macro_id_, offset_, size_);
OB_DEF_SERIALIZE(ObGetSSMacroBlockResult)
{
  int ret = OB_SUCCESS;
  LST_DO_CODE(OB_UNIS_ENCODE,
              macro_buf_);
  return ret;
}

#endif

ObRpcRemoteWriteDDLIncCommitLogArg::ObRpcRemoteWriteDDLIncCommitLogArg()
  : ls_id_(), tablet_id_(), lob_meta_tablet_id_(), tx_desc_(nullptr), need_release_(false)
{}

ObRpcRemoteWriteDDLIncCommitLogArg::~ObRpcRemoteWriteDDLIncCommitLogArg()
{
  int ret = OB_SUCCESS;
  if (OB_FAIL(release())) {
    LOG_WARN("fail to release tx_desc", K(ret));
  }
}

OB_SERIALIZE_MEMBER(ObRpcRemoteWriteDDLIncCommitLogRes, tx_result_);

OB_DEF_SERIALIZE(ObRpcRemoteWriteDDLIncCommitLogArg)
{
  int ret = OB_SUCCESS;
  LST_DO_CODE(OB_UNIS_ENCODE, ls_id_, tablet_id_, lob_meta_tablet_id_);
  if (OB_SUCC(ret)) {
    if (OB_ISNULL(tx_desc_)) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("tx_desc_ is nullptr", K(ret));
    } else {
      LST_DO_CODE(OB_UNIS_ENCODE, *tx_desc_);
    }
  }
  return ret;
}

OB_DEF_DESERIALIZE(ObRpcRemoteWriteDDLIncCommitLogArg)
{
  int ret = OB_SUCCESS;
  LST_DO_CODE(OB_UNIS_DECODE, ls_id_, tablet_id_, lob_meta_tablet_id_);
  if (OB_SUCC(ret)) {
    ObTransService *tx_svc = nullptr;
    if (OB_FAIL(release())) {
      LOG_WARN("fail to release tx_desc", K(ret));
    } else if (OB_ISNULL(tx_svc = MTL_WITH_CHECK(ObTransService *))) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("null tx service ptr", KR(ret));
    } else if (OB_FAIL(tx_svc->acquire_tx(buf, data_len, pos, tx_desc_))) {
      LOG_WARN("acquire tx by deserialize fail", K(data_len), K(pos), KR(ret));
    } else {
      need_release_ = true;
    }
  }
  return ret;
}

OB_DEF_SERIALIZE_SIZE(ObRpcRemoteWriteDDLIncCommitLogArg)
{
  int64_t len = 0;
  LST_DO_CODE(OB_UNIS_ADD_LEN, ls_id_, tablet_id_, lob_meta_tablet_id_);
  if (tx_desc_ != nullptr) {
    LST_DO_CODE(OB_UNIS_ADD_LEN, *tx_desc_);
  }
  return len;
}
ObRegisterTxDataArg::ObRegisterTxDataArg()
  : tx_desc_(nullptr),
    ls_id_(),
    type_(transaction::ObTxDataSourceType::UNKNOWN),
    buf_(),
    seq_no_(),
    request_id_(0),
    register_flag_()
{
}
int ObFetchStableMemberListInfo::init(const common::ObMemberList &member_list, const palf::LogConfigVersion &config_version)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(!member_list.is_valid() || !config_version.is_valid())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid arguments", KR(ret), K(member_list), K(config_version));
  } else if (OB_FAIL(member_list_.deep_copy(member_list))) {
    LOG_WARN("fail to assign memberlist", KR(ret), K(member_list));
  } else if (OB_FALSE_IT(config_version_ = config_version)) {
  }
  return ret;
}
int ObLSAccessModeInfo::init(const share::ObLSID &ls_idd,
                             const int64_t mode_version,
                             const palf::AccessMode &access_mode,
                             const share::SCN &ref_scn,
                             const share::SCN &sys_ls_end_scn)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(!ls_idd.is_valid()
                  || palf::INVALID_PROPOSAL_ID == mode_version
                  || palf::AccessMode::INVALID_ACCESS_MODE == access_mode)) {
    ret = OB_INVALID_ARGUMENT;
    SHARE_LOG(WARN, "invalid argument", KR(ret), K(ls_idd),
              K(mode_version), K(access_mode));
  } else {
    ls_id_ = ls_idd;
    mode_version_ = mode_version;
    access_mode_ = access_mode;
    ref_scn_ = ref_scn;
    sys_ls_end_scn_ = sys_ls_end_scn;
  }
  return ret;
}
int ObLSAccessModeInfo::assign(const ObLSAccessModeInfo &other)
{
  int ret = OB_SUCCESS;
  if (this != &other) {
    ls_id_ = other.ls_id_;
    mode_version_ = other.mode_version_;
    access_mode_ = other.access_mode_;
    ref_scn_ = other.ref_scn_;
    addr_ = other.addr_;
    sys_ls_end_scn_ = other.sys_ls_end_scn_;
  }
  return ret;
}
bool ObLSAccessModeInfo::is_valid() const
{
  return true
         && ls_id_.is_valid()
         && palf::INVALID_PROPOSAL_ID != mode_version_
         && palf::AccessMode::INVALID_ACCESS_MODE != access_mode_;
}
#ifdef OB_BUILD_SHARED_STORAGE
OB_DEF_DESERIALIZE(ObGetSSMacroBlockResult)
{
  int ret = OB_SUCCESS;
  ObString tmp_str;
  LST_DO_CODE(OB_UNIS_DECODE,
        tmp_str);
  if (OB_FAIL(ob_write_string(allocator_, tmp_str, macro_buf_))) {
    LOG_WARN("failed to copy string", KR(ret), K(tmp_str));
  }
  return ret;
}

OB_DEF_SERIALIZE_SIZE(ObGetSSMacroBlockResult)
{
  int len = 0;
    LST_DO_CODE(OB_UNIS_ADD_LEN,
          macro_buf_);
  return len;
}


OB_SERIALIZE_MEMBER(ObGetSSPhyBlockInfoArg, tenant_id_, phy_block_idx_);
OB_SERIALIZE_MEMBER(ObGetSSPhyBlockInfoResult, ss_phy_block_info_, ret_);

OB_SERIALIZE_MEMBER(ObSSMicroMetaInfo, reuse_version_, data_dest_, access_time_, length_, is_in_l1_, is_in_ghost_,
    is_persisted_, is_reorganizing_, ref_cnt_, crc_, micro_key_);
OB_SERIALIZE_MEMBER(ObGetSSMicroBlockMetaArg, tenant_id_, micro_key_);
OB_SERIALIZE_MEMBER(ObGetSSMicroBlockMetaResult, micro_meta_info_, ret_);

OB_SERIALIZE_MEMBER(ObGetSSMacroBlockByURIArg, tenant_id_, uri_, offset_, size_);
OB_DEF_SERIALIZE(ObGetSSMacroBlockByURIResult)
{
  int ret = OB_SUCCESS;
  LST_DO_CODE(OB_UNIS_ENCODE,
              macro_buf_);
  return ret;
}

OB_DEF_DESERIALIZE(ObGetSSMacroBlockByURIResult)
{
  int ret = OB_SUCCESS;
  ObString tmp_str;
  LST_DO_CODE(OB_UNIS_DECODE,
        tmp_str);
  if (OB_FAIL(ob_write_string(allocator_, tmp_str, macro_buf_))) {
    LOG_WARN("failed to copy string", KR(ret), K(tmp_str));
  }
  return ret;
}

OB_DEF_SERIALIZE_SIZE(ObGetSSMacroBlockByURIResult)
{
  int len = 0;
    LST_DO_CODE(OB_UNIS_ADD_LEN,
          macro_buf_);
  return len;
}


OB_SERIALIZE_MEMBER(ObDelSSTabletMetaArg, tenant_id_, macro_id_);
OB_SERIALIZE_MEMBER(ObEnableSSMicroCacheArg, tenant_id_, is_enabled_);
OB_SERIALIZE_MEMBER(ObGetSSMicroCacheInfoArg, tenant_id_);
OB_SERIALIZE_MEMBER(ObGetSSMicroCacheInfoResult, micro_cache_stat_, super_block_, arc_info_);
OB_SERIALIZE_MEMBER(ObClearSSMicroCacheArg, tenant_id_);
OB_SERIALIZE_MEMBER(ObDelSSLocalTmpFileArg, tenant_id_, macro_id_);
OB_SERIALIZE_MEMBER(ObDelSSLocalMajorArg, tenant_id_);
OB_SERIALIZE_MEMBER(ObCalibrateSSDiskSpaceArg, tenant_id_);
OB_SERIALIZE_MEMBER(ObDelSSTabletMicroArg, tenant_id_, tablet_id_);
OB_SERIALIZE_MEMBER(ObSetSSCkptCompressorArg, tenant_id_, block_type_, compressor_type_);
OB_SERIALIZE_MEMBER(ObSetSSCacheSizeRatioArg, tenant_id_, micro_cache_size_ratio_, macro_cache_size_ratio_);
#endif
#ifdef OB_BUILD_SHARED_STORAGE
OB_SERIALIZE_MEMBER(ObLSSyncHotMicroKeyArg, tenant_id_, ls_id_, leader_addr_, micro_keys_);
int ObLSSyncHotMicroKeyArg::assign(const ObLSSyncHotMicroKeyArg &other)
{
  int ret = OB_SUCCESS;
  if (OB_FAIL(micro_keys_.assign(other.micro_keys_))) {
    LOG_WARN("micro_keys_ assign failed", KR(ret));
  } else {
    tenant_id_ = other.tenant_id_;
    ls_id_ = other.ls_id_;
    leader_addr_ = other.leader_addr_;
  }
  return ret;
}

bool ObLSSyncHotMicroKeyArg::is_valid() const
{
  return is_valid_tenant_id(tenant_id_) && (micro_keys_.count() > 0) && (ls_id_ != ObLSID::INVALID_LS_ID) && leader_addr_.is_valid();
}
#endif
OB_DEF_SERIALIZE(ObDDLBuildSingleReplicaRequestArg)
{
  int ret = OB_SUCCESS;
  LST_DO_CODE(OB_UNIS_ENCODE, ls_id_, source_tablet_id_, dest_tablet_id_,
    source_table_id_, dest_schema_id_, schema_version_, snapshot_version_, ddl_type_, task_id_, execution_id_,
    parallelism_, tablet_task_id_, data_format_version_, consumer_group_id_,
    dest_ls_id_, dest_schema_version_,
    compaction_scn_, can_reuse_macro_block_, split_sstable_type_,
    lob_col_idxs_, parallel_datum_rowkey_list_, is_no_logging_,
    min_split_start_scn_);
  return ret;
}
OB_DEF_DESERIALIZE(ObDDLBuildSingleReplicaRequestArg)
{
  int ret = OB_SUCCESS;
  LST_DO_CODE(OB_UNIS_DECODE, ls_id_, source_tablet_id_, dest_tablet_id_,
      source_table_id_, dest_schema_id_, schema_version_, snapshot_version_, ddl_type_, task_id_, execution_id_,
      parallelism_, tablet_task_id_, data_format_version_, consumer_group_id_,
      dest_ls_id_, dest_schema_version_,
      compaction_scn_, can_reuse_macro_block_, split_sstable_type_,
      lob_col_idxs_);
  if (FAILEDx(ObSplitUtil::deserializ_parallel_datum_rowkey(
        rowkey_allocator_, buf, data_len, pos, parallel_datum_rowkey_list_))) {
    LOG_WARN("deserialzie parallel info failed", K(ret));
  }

  if (OB_SUCC(ret)) {
    LST_DO_CODE(OB_UNIS_DECODE, is_no_logging_, min_split_start_scn_);
  }
  return ret;
}
OB_DEF_SERIALIZE_SIZE(ObDDLBuildSingleReplicaRequestArg)
{
  int64_t len = 0;
  LST_DO_CODE(OB_UNIS_ADD_LEN, ls_id_, source_tablet_id_, dest_tablet_id_,
    source_table_id_, dest_schema_id_, schema_version_, snapshot_version_, ddl_type_, task_id_, execution_id_,
    parallelism_, tablet_task_id_, data_format_version_, consumer_group_id_,
    dest_ls_id_, dest_schema_version_,
    compaction_scn_, can_reuse_macro_block_, split_sstable_type_,
    lob_col_idxs_, parallel_datum_rowkey_list_, is_no_logging_,
    min_split_start_scn_);
  return len;
}
bool ObDDLBuildSingleReplicaRequestArg::is_valid() const
{
  bool is_valid = ls_id_.is_valid() && source_tablet_id_.is_valid() && dest_tablet_id_.is_valid()
               && OB_INVALID_ID != source_table_id_ && OB_INVALID_ID != dest_schema_id_
               && schema_version_ > 0 && snapshot_version_ > 0 && task_id_ > 0 && parallelism_ > 0
               && tablet_task_id_ > 0 && data_format_version_ > 0 && consumer_group_id_ >= 0
               && dest_ls_id_.is_valid() && dest_schema_version_ > 0;
  return is_valid;
}
int ObDDLBuildSingleReplicaRequestArg::assign(const ObDDLBuildSingleReplicaRequestArg &other)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(!other.is_valid())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid arg", K(ret), K(other));
  } else if (OB_FAIL(lob_col_idxs_.assign(other.lob_col_idxs_))) {
    LOG_WARN("failed to assign to lob col idxs", K(ret));
  } else if (OB_FAIL(parallel_datum_rowkey_list_.assign(other.parallel_datum_rowkey_list_))) { // shallow copy.
    LOG_WARN("assign failed", K(ret));
  } else {
    ls_id_ = other.ls_id_;
    dest_ls_id_ = other.dest_ls_id_;
    source_tablet_id_ = other.source_tablet_id_;
    dest_tablet_id_ = other.dest_tablet_id_;
    source_table_id_ = other.source_table_id_;
    dest_schema_id_ = other.dest_schema_id_;
    schema_version_ = other.schema_version_;
    dest_schema_version_ = other.dest_schema_version_;
    snapshot_version_ = other.snapshot_version_;
    ddl_type_ = other.ddl_type_;
    task_id_ = other.task_id_;
    parallelism_ = other.parallelism_;
    execution_id_ = other.execution_id_;
    tablet_task_id_ = other.tablet_task_id_;
    data_format_version_ = other.data_format_version_;
    consumer_group_id_ = other.consumer_group_id_;
    compaction_scn_ = other.compaction_scn_;
    can_reuse_macro_block_ = other.can_reuse_macro_block_;
    split_sstable_type_ = other.split_sstable_type_;
    min_split_start_scn_ = other.min_split_start_scn_;
    is_no_logging_ = other.is_no_logging_;
  }
  return ret;
}
OB_SERIALIZE_MEMBER(ObDDLBuildSingleReplicaRequestResult, ret_code_, row_inserted_, row_scanned_, physical_row_count_);
OB_SERIALIZE_MEMBER(ObPrepareSplitRangesArg, ls_id_, tablet_id_,
    user_parallelism_, schema_tablet_size_, ddl_type_);
OB_DEF_SERIALIZE(ObPrepareSplitRangesRes)
{
  int ret = OB_SUCCESS;
  LST_DO_CODE(OB_UNIS_ENCODE, parallel_datum_rowkey_list_);
  return ret;
}
OB_DEF_DESERIALIZE(ObPrepareSplitRangesRes)
{
  int ret = OB_SUCCESS;
  if (OB_FAIL(ObSplitUtil::deserializ_parallel_datum_rowkey(
      rowkey_allocator_, buf, data_len, pos, parallel_datum_rowkey_list_))) {
    LOG_WARN("deserialzie parallel info failed", K(ret));
  }
  return ret;
}
OB_DEF_SERIALIZE_SIZE(ObPrepareSplitRangesRes)
{
  int64_t len = 0;
  LST_DO_CODE(OB_UNIS_ADD_LEN, parallel_datum_rowkey_list_);
  return len;
}
bool ObTabletSplitArg::is_valid() const
{
  bool is_valid = ls_id_.is_valid() && OB_INVALID_ID != table_id_
      && schema_version_ > 0 && task_id_ > 0
      && source_tablet_id_.is_valid() && dest_tablets_id_.count() > 0
      && compaction_scn_ > 0
      && data_format_version_ > 0 && consumer_group_id_ >= 0
      && split_sstable_type_ >= share::ObSplitSSTableType::SPLIT_BOTH
      && split_sstable_type_ <= share::ObSplitSSTableType::SPLIT_MINOR;
  if (!lob_col_idxs_.empty()) {
    is_valid = is_valid && (OB_INVALID_ID != lob_table_id_);
  }
  return is_valid;
}
int ObTabletSplitArg::assign(const ObTabletSplitArg &other)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(!other.is_valid())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid arg", K(ret), K(other));
  } else if (OB_FAIL(dest_tablets_id_.assign(other.dest_tablets_id_))) {
    LOG_WARN("assign failed", K(ret), K(other));
  } else if (OB_FAIL(lob_col_idxs_.assign(other.lob_col_idxs_))) {
    LOG_WARN("assign failed", K(ret));
  } else if (OB_FAIL(parallel_datum_rowkey_list_.assign(other.parallel_datum_rowkey_list_))) { // shallow cpy.
    LOG_WARN("assign failed", K(ret), K(other));
  } else {
    ls_id_                 = other.ls_id_;
    table_id_              = other.table_id_;
    lob_table_id_          = other.lob_table_id_;
    schema_version_        = other.schema_version_;
    task_id_               = other.task_id_;
    source_tablet_id_      = other.source_tablet_id_;
    compaction_scn_        = other.compaction_scn_;
    data_format_version_   = other.data_format_version_;
    consumer_group_id_     = other.consumer_group_id_;
    can_reuse_macro_block_ = other.can_reuse_macro_block_;
    split_sstable_type_    = other.split_sstable_type_;
    min_split_start_scn_   = other.min_split_start_scn_;
  }
  return ret;
}
OB_DEF_SERIALIZE(ObTabletSplitArg)
{
  int ret = OB_SUCCESS;
  LST_DO_CODE(OB_UNIS_ENCODE, ls_id_, table_id_, lob_table_id_,
    schema_version_, task_id_, source_tablet_id_,
    dest_tablets_id_, compaction_scn_, data_format_version_,
    consumer_group_id_, can_reuse_macro_block_, split_sstable_type_,
    lob_col_idxs_, parallel_datum_rowkey_list_, min_split_start_scn_);
  return ret;
}
OB_DEF_DESERIALIZE(ObTabletSplitArg)
{
  int ret = OB_SUCCESS;
  LST_DO_CODE(OB_UNIS_DECODE, ls_id_, table_id_, lob_table_id_,
    schema_version_, task_id_, source_tablet_id_,
    dest_tablets_id_, compaction_scn_, data_format_version_,
    consumer_group_id_, can_reuse_macro_block_, split_sstable_type_,
    lob_col_idxs_);
  if (FAILEDx(ObSplitUtil::deserializ_parallel_datum_rowkey(
      rowkey_allocator_, buf, data_len, pos, parallel_datum_rowkey_list_))) {
    LOG_WARN("deserialzie parallel info failed", K(ret));
  } else {
    LST_DO_CODE(OB_UNIS_DECODE, min_split_start_scn_);
  }
  return ret;
}
OB_DEF_SERIALIZE_SIZE(ObTabletSplitArg)
{
  int64_t len = 0;
  LST_DO_CODE(OB_UNIS_ADD_LEN, ls_id_, table_id_, lob_table_id_,
    schema_version_, task_id_, source_tablet_id_,
    dest_tablets_id_, compaction_scn_, data_format_version_,
    consumer_group_id_, can_reuse_macro_block_, split_sstable_type_,
    lob_col_idxs_, parallel_datum_rowkey_list_, min_split_start_scn_);
  return len;
}
bool ObTabletSplitStartArg::is_valid() const
{
  bool is_valid = true;
  for (int64_t i = 0; is_valid && i < split_info_array_.count(); i++) {
    is_valid = is_valid && split_info_array_.at(i).is_valid();
  }
  return is_valid;
}
bool ObTabletSplitFinishArg::is_valid() const
{
  bool is_valid = true;
  for (int64_t i = 0; is_valid && i < split_info_array_.count(); i++) {
    is_valid = is_valid && split_info_array_.at(i).is_valid();
  }
  return is_valid;
}
OB_SERIALIZE_MEMBER(ObTabletSplitStartArg, split_info_array_);
OB_SERIALIZE_MEMBER(ObTabletSplitStartResult, ret_codes_, min_split_start_scn_);
OB_SERIALIZE_MEMBER(ObTabletSplitFinishArg, split_info_array_);
OB_SERIALIZE_MEMBER(ObTabletSplitFinishResult, ret_codes_);

ObCallRemoteWriteDDLRedoLogArg::ObCallRemoteWriteDDLRedoLogArg()
  : ls_id_(), redo_info_(), task_id_(0)
{}

int ObCallRemoteWriteDDLRedoLogArg::init(const share::ObLSID &ls_id,
                                        const storage::ObDDLMacroBlockRedoInfo &redo_info,
                                        const int64_t task_id)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(task_id == 0 || !ls_id.is_valid() || !redo_info.is_valid())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("args are not valid", K(ret), K(task_id), K(ls_id), K(redo_info));
  } else {
    ls_id_ = ls_id;
    redo_info_ = redo_info;
    task_id_ = task_id;
  }
  return ret;
}

OB_SERIALIZE_MEMBER(ObCallRemoteWriteDDLRedoLogArg, ls_id_, redo_info_, task_id_);

ObCallRemoteWriteDDLCommitLogArg::ObCallRemoteWriteDDLCommitLogArg()
  : ls_id_(), table_key_(), start_scn_(SCN::min_scn()),
    table_id_(0), execution_id_(-1), ddl_task_id_(0)
{}

int ObCallRemoteWriteDDLCommitLogArg::init(const share::ObLSID &ls_id,
                                          const storage::ObITable::TableKey &table_key,
                                          const SCN &start_scn)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(!ls_id.is_valid() || !table_key.is_valid() || !start_scn.is_valid_and_not_min())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("tablet id is not valid", K(ret), K(ls_id), K(table_key), K(start_scn));
  } else {
    ls_id_ = ls_id;
    table_key_ = table_key;
    start_scn_ = start_scn;
  }
  return ret;
}

OB_SERIALIZE_MEMBER(ObCallRemoteWriteDDLCommitLogArg, ls_id_, table_key_, start_scn_,
                    table_id_, execution_id_, ddl_task_id_);


ObCallRemoteWriteDDLIncCommitLogArg::ObCallRemoteWriteDDLIncCommitLogArg()
  : ls_id_(), tablet_id_(), lob_meta_tablet_id_(), tx_desc_(nullptr), need_release_(false)
{}

ObCallRemoteWriteDDLIncCommitLogArg::~ObCallRemoteWriteDDLIncCommitLogArg()
{
  int ret = OB_SUCCESS;
  if (OB_FAIL(release())) {
    LOG_WARN("fail to release tx_desc", K(ret));
  }
}

int ObCallRemoteWriteDDLIncCommitLogArg::init(const share::ObLSID &ls_id,
                                             const common::ObTabletID tablet_id,
                                             const common::ObTabletID lob_meta_tablet_id,
                                             transaction::ObTxDesc *tx_desc)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(!ls_id.is_valid() || !tablet_id.is_valid() ||
                  OB_ISNULL(tx_desc) || !tx_desc->is_valid())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("tablet id is not valid", K(ret), K(ls_id), K(tablet_id), K(lob_meta_tablet_id), KPC(tx_desc));
  } else if (OB_FAIL(release())) {
    LOG_WARN("fail to release tx_desc", K(ret));
  } else {
    ls_id_ = ls_id;
    tablet_id_ = tablet_id;
    lob_meta_tablet_id_ = lob_meta_tablet_id;
    tx_desc_ = tx_desc;
  }
  return ret;
}

int ObCallRemoteWriteDDLIncCommitLogArg::release()
{
  int ret = OB_SUCCESS;
  if (tx_desc_ != nullptr && need_release_) {
    ObTransService *tx_svc = MTL_WITH_CHECK(ObTransService *);
    if (OB_ISNULL(tx_svc)) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("null tx service ptr", K(ret));
    } else if (OB_FAIL(tx_svc->release_tx(*tx_desc_))) {
      LOG_WARN("release tx fail", K(ret));
    } else {
      need_release_ = false;
      tx_desc_ = nullptr;
    }
  }

  return ret;
}

OB_DEF_SERIALIZE(ObCallRemoteWriteDDLIncCommitLogArg)
{
  int ret = OB_SUCCESS;
  LST_DO_CODE(OB_UNIS_ENCODE, ls_id_, tablet_id_, lob_meta_tablet_id_);
  if (OB_SUCC(ret)) {
    if (OB_ISNULL(tx_desc_)) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("tx_desc_ is nullptr", K(ret));
    } else {
      LST_DO_CODE(OB_UNIS_ENCODE, *tx_desc_);
    }
  }
  return ret;
}

OB_DEF_DESERIALIZE(ObCallRemoteWriteDDLIncCommitLogArg)
{
  int ret = OB_SUCCESS;
  LST_DO_CODE(OB_UNIS_DECODE, ls_id_, tablet_id_, lob_meta_tablet_id_);
  if (OB_SUCC(ret)) {
    ObTransService *tx_svc = nullptr;
    if (OB_FAIL(release())) {
      LOG_WARN("fail to release tx_desc", K(ret));
    } else if (OB_ISNULL(tx_svc = MTL_WITH_CHECK(ObTransService *))) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("null tx service ptr", KR(ret));
    } else if (OB_FAIL(tx_svc->acquire_tx(buf, data_len, pos, tx_desc_))) {
      LOG_WARN("acquire tx by deserialize fail", K(data_len), K(pos), KR(ret));
    } else {
      need_release_ = true;
    }
  }
  return ret;
}

OB_DEF_SERIALIZE_SIZE(ObCallRemoteWriteDDLIncCommitLogArg)
{
  int64_t len = 0;
  LST_DO_CODE(OB_UNIS_ADD_LEN, ls_id_, tablet_id_, lob_meta_tablet_id_);
  if (tx_desc_ != nullptr) {
    LST_DO_CODE(OB_UNIS_ADD_LEN, *tx_desc_);
  }
  return len;
}

OB_SERIALIZE_MEMBER(ObCallRemoteWriteDDLIncCommitLogRes, tx_result_);

}  // namespace obcall
}  // namespace oceanbase
