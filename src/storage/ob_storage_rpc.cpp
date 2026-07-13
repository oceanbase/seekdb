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

#define USING_LOG_PREFIX STORAGE
#include "ob_storage_rpc.h"
#include "logservice/ob_log_service.h"
#include "observer/ob_server_event_history_table_operator.h"
#include "storage/tablet/ob_tablet_iterator.h"
#include "storage/ddl/ob_direct_load_mgr_utils.h"
#include "lib/thread/thread.h"
#include "lib/worker.h"

namespace oceanbase
{
using namespace lib;
using namespace common;
using namespace share;
using namespace obcall;
using namespace storage;
using namespace blocksstable;
using namespace memtable;
using namespace share::schema;

namespace obcall
{


ObCopyMacroBlockArg::ObCopyMacroBlockArg()
  : logic_macro_block_id_()
{
}



OB_SERIALIZE_MEMBER(ObCopyMacroBlockArg,
    logic_macro_block_id_);


ObCopyMacroBlockListArg::ObCopyMacroBlockListArg()
  : ls_id_(),
    table_key_(),
    arg_list_()
{
}


bool ObCopyMacroBlockListArg::is_valid() const
{
  return true
      && ls_id_.is_valid()
      && table_key_.is_valid()
      && arg_list_.count() > 0;
}


OB_SERIALIZE_MEMBER(ObCopyMacroBlockListArg, ls_id_, table_key_, arg_list_);

ObCopyMacroBlockInfo::ObCopyMacroBlockInfo()
  : logical_id_(),
    data_type_(ObCopyMacroBlockDataType::MAX)
{
}



OB_SERIALIZE_MEMBER(ObCopyMacroBlockInfo, logical_id_, data_type_);

ObCopyMacroBlockRangeArg::ObCopyMacroBlockRangeArg()
  : ls_id_(),
    table_key_(),
    data_version_(0),
    backfill_tx_scn_(SCN::min_scn()),
    copy_macro_range_info_(),
    need_check_seq_(false),
    ls_rebuild_seq_(-1)
{
}


bool ObCopyMacroBlockRangeArg::is_valid() const
{
  return ls_id_.is_valid()
      && table_key_.is_valid()
      && data_version_ >= 0
      && backfill_tx_scn_ >= SCN::min_scn()
      && copy_macro_range_info_.is_valid()
      && ((need_check_seq_ && ls_rebuild_seq_ >= 0) || !need_check_seq_);
}


OB_SERIALIZE_MEMBER(ObCopyMacroBlockRangeArg, ls_id_, table_key_, data_version_,
    backfill_tx_scn_, copy_macro_range_info_, need_check_seq_, ls_rebuild_seq_, copy_macro_block_infos_);

ObCopyMacroBlockHeader::ObCopyMacroBlockHeader()
  : is_reuse_macro_block_(false),
    occupy_size_(0),
    data_type_(ObCopyMacroBlockDataType::MACRO_DATA) // default value for compat, previous version won't contain data_type_ and will pass macro data all the time
{
}

void ObCopyMacroBlockHeader::reset()
{
  is_reuse_macro_block_ = false;
  occupy_size_ = 0;
  data_type_ = ObCopyMacroBlockDataType::MACRO_DATA;
}

OB_SERIALIZE_MEMBER(ObCopyMacroBlockHeader, is_reuse_macro_block_, occupy_size_, data_type_);

ObCopyTabletInfoArg::ObCopyTabletInfoArg()
  : ls_id_(),
    tablet_id_list_(),
    need_check_seq_(false),
    ls_rebuild_seq_(-1),
    is_only_copy_major_(false),
    version_(OB_INVALID_ID)
{
}



OB_SERIALIZE_MEMBER(ObCopyTabletInfoArg,
    ls_id_, tablet_id_list_, need_check_seq_, ls_rebuild_seq_,
    is_only_copy_major_, version_);

ObCopyTabletInfo::ObCopyTabletInfo()
  : tablet_id_(),
    status_(ObCopyTabletStatus::MAX_STATUS),
    param_(),
    data_size_(0),
    version_(OB_INVALID_ID)
{
}

void ObCopyTabletInfo::reset()
{
  tablet_id_.reset();
  status_ = ObCopyTabletStatus::MAX_STATUS;
  param_.reset();
  data_size_ = 0;
  version_ = OB_INVALID_ID;
}

bool ObCopyTabletInfo::is_valid() const
{
  return tablet_id_.is_valid()
      && ObCopyTabletStatus::is_valid(status_)
      && ((ObCopyTabletStatus::TABLET_EXIST == status_ && param_.is_valid() && data_size_ >= 0)
        || ObCopyTabletStatus::TABLET_NOT_EXIST == status_)
      && version_ != OB_INVALID_ID;
}


OB_SERIALIZE_MEMBER(ObCopyTabletInfo, tablet_id_, status_, param_, data_size_, version_);

/******************ObCopyTabletSSTableInfoArg*********************/
ObCopyTabletSSTableInfoArg::ObCopyTabletSSTableInfoArg()
  : tablet_id_(),
    max_major_sstable_snapshot_(0),
    minor_sstable_scn_range_(),
    ddl_sstable_scn_range_()
{
}

ObCopyTabletSSTableInfoArg::~ObCopyTabletSSTableInfoArg()
{
}

void ObCopyTabletSSTableInfoArg::reset()
{
  tablet_id_.reset();
  max_major_sstable_snapshot_ = 0;
  minor_sstable_scn_range_.reset();
  ddl_sstable_scn_range_.reset();
}

bool ObCopyTabletSSTableInfoArg::is_valid() const
{
  return tablet_id_.is_valid()
      && max_major_sstable_snapshot_ >= 0
      && minor_sstable_scn_range_.is_valid()
      && ddl_sstable_scn_range_.is_valid();
}

OB_SERIALIZE_MEMBER(ObCopyTabletSSTableInfoArg,
    tablet_id_, max_major_sstable_snapshot_, minor_sstable_scn_range_, ddl_sstable_scn_range_);

ObCopyTabletsSSTableInfoArg::ObCopyTabletsSSTableInfoArg()
  : ls_id_(),
    need_check_seq_(false),
    ls_rebuild_seq_(-1),
    is_only_copy_major_(false),
    tablet_sstable_info_arg_list_(),
    version_(OB_INVALID_ID)
{
}

ObCopyTabletsSSTableInfoArg::~ObCopyTabletsSSTableInfoArg()
{
  reset();
}

void ObCopyTabletsSSTableInfoArg::reset()
{
  ls_id_.reset();
  need_check_seq_ = false;
  ls_rebuild_seq_ = -1;
  is_only_copy_major_ = false;
  tablet_sstable_info_arg_list_.reset();
  version_ = OB_INVALID_ID;
}


OB_SERIALIZE_MEMBER(ObCopyTabletsSSTableInfoArg,
    ls_id_, need_check_seq_, ls_rebuild_seq_, is_only_copy_major_,
    tablet_sstable_info_arg_list_, version_);


ObCopyTabletSSTableInfo::ObCopyTabletSSTableInfo()
  : tablet_id_(),
    table_key_(),
    param_()
{
}

void ObCopyTabletSSTableInfo::reset()
{
  tablet_id_.reset();
  table_key_.reset();
  param_.reset();
}

bool ObCopyTabletSSTableInfo::is_valid() const
{
  return tablet_id_.is_valid()
      && table_key_.is_valid()
      && param_.is_valid();
}

OB_SERIALIZE_MEMBER(ObCopyTabletSSTableInfo,
    tablet_id_, table_key_, param_);


ObCopyLSInfoArg::ObCopyLSInfoArg()
  : ls_id_(),
    version_(OB_INVALID_ID)
{
}



OB_SERIALIZE_MEMBER(ObCopyLSInfoArg,
    ls_id_, version_);


ObCopyLSInfo::ObCopyLSInfo()
  : ls_meta_package_(),
    tablet_id_array_(),
    is_log_sync_(false),
    version_(OB_INVALID_ID)
{
}



OB_SERIALIZE_MEMBER(ObCopyLSInfo,
    ls_meta_package_, tablet_id_array_, is_log_sync_, version_);

ObFetchLSMetaInfoArg::ObFetchLSMetaInfoArg()
  : ls_id_(),
    version_(OB_INVALID_ID)
{
}



OB_SERIALIZE_MEMBER(ObFetchLSMetaInfoArg, ls_id_, version_);


ObFetchLSMetaInfoResp::ObFetchLSMetaInfoResp()
  : ls_meta_package_(),
    version_(OB_INVALID_ID)
{
}


bool ObFetchLSMetaInfoResp::is_valid() const
{
  return ls_meta_package_.is_valid()
      && version_ != OB_INVALID_ID;
}

OB_SERIALIZE_MEMBER(ObFetchLSMetaInfoResp, ls_meta_package_, version_);

ObFetchLSMemberListArg::ObFetchLSMemberListArg()
  : ls_id_()
{
}



OB_SERIALIZE_MEMBER(ObFetchLSMemberListArg, ls_id_);

ObCheckRestorePreconditionResult::ObCheckRestorePreconditionResult()
  : required_disk_size_(0),
    total_tablet_size_(0),
    cluster_version_(0)
{
}

OB_SERIALIZE_MEMBER(ObCheckRestorePreconditionResult, required_disk_size_, total_tablet_size_, cluster_version_);

ObRestoreCopyTabletInfoArg::ObRestoreCopyTabletInfoArg()
  : tablet_id_list_()
{
}

ObRestoreCopySSTableMacroRangeInfoArg::ObRestoreCopySSTableMacroRangeInfoArg()
  : tablet_id_(),
    copy_table_key_array_(),
    macro_range_max_marco_count_(0)
{
}

ObRestoreCopySSTableMacroRangeInfoArg::~ObRestoreCopySSTableMacroRangeInfoArg()
{
}

bool ObRestoreCopySSTableMacroRangeInfoArg::is_valid() const
{
  return tablet_id_.is_valid()
      && copy_table_key_array_.count() > 0
      && macro_range_max_marco_count_ > 0;
}

int ObRestoreCopySSTableMacroRangeInfoArg::assign(const ObRestoreCopySSTableMacroRangeInfoArg &arg)
{
  int ret = OB_SUCCESS;
  if (!arg.is_valid()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("copy sstable macro range info arg is invalid", K(ret), K(arg));
  } else if (OB_FAIL(copy_table_key_array_.assign(arg.copy_table_key_array_))) {
    LOG_WARN("failed to assign copy table key array", K(ret), K(arg));
  } else {
    tablet_id_ = arg.tablet_id_;
    macro_range_max_marco_count_ = arg.macro_range_max_marco_count_;
  }
  return ret;
}

ObRestoreCopyMacroBlockRangeArg::ObRestoreCopyMacroBlockRangeArg()
  : table_key_(),
    data_version_(0),
    backfill_tx_scn_(SCN::min_scn()),
    copy_macro_range_info_(),
    copy_macro_block_infos_()
{
}

bool ObRestoreCopyMacroBlockRangeArg::is_valid() const
{
  return table_key_.is_valid()
      && data_version_ >= 0
      && backfill_tx_scn_.is_valid()
      && copy_macro_range_info_.is_valid();
}

OB_SERIALIZE_MEMBER(ObRestoreCopyTabletInfoArg, tablet_id_list_);
OB_SERIALIZE_MEMBER(ObRestoreCopySSTableMacroRangeInfoArg, tablet_id_, copy_table_key_array_, macro_range_max_marco_count_);
OB_SERIALIZE_MEMBER(ObRestoreCopyMacroBlockRangeArg, table_key_, data_version_, backfill_tx_scn_, copy_macro_range_info_, copy_macro_block_infos_);

ObFetchLSMemberListInfo::ObFetchLSMemberListInfo()
  : member_list_()
{
}



OB_SERIALIZE_MEMBER(ObFetchLSMemberListInfo, member_list_);

ObFetchLSMemberAndLearnerListArg::ObFetchLSMemberAndLearnerListArg()
  : ls_id_()
{
}



OB_SERIALIZE_MEMBER(ObFetchLSMemberAndLearnerListArg, ls_id_);

ObFetchLSMemberAndLearnerListInfo::ObFetchLSMemberAndLearnerListInfo()
  : member_list_(),
    learner_list_()
{
}



OB_SERIALIZE_MEMBER(ObFetchLSMemberAndLearnerListInfo, member_list_, learner_list_);

ObCopySSTableMacroRangeInfoArg::ObCopySSTableMacroRangeInfoArg()
  : ls_id_(),
    tablet_id_(),
    copy_table_key_array_(),
    macro_range_max_marco_count_(0),
    need_check_seq_(false),
    ls_rebuild_seq_(0)
{
}

ObCopySSTableMacroRangeInfoArg::~ObCopySSTableMacroRangeInfoArg()
{
}


bool ObCopySSTableMacroRangeInfoArg::is_valid() const
{
  return true
      && ls_id_.is_valid()
      && tablet_id_.is_valid()
      && copy_table_key_array_.count() > 0
      && macro_range_max_marco_count_ > 0;
}

int ObCopySSTableMacroRangeInfoArg::assign(const ObCopySSTableMacroRangeInfoArg &arg)
{
  int ret = OB_SUCCESS;
  if (!arg.is_valid()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("copy sstable macro range info arg is invalid", K(ret), K(arg));
  } else if (OB_FAIL(copy_table_key_array_.assign(arg.copy_table_key_array_))) {
    LOG_WARN("failed to assign src table array", K(ret), K(arg));
  } else {
    ls_id_ = arg.ls_id_;
    tablet_id_ = arg.tablet_id_;
    macro_range_max_marco_count_ = arg.macro_range_max_marco_count_;
    need_check_seq_ = arg.need_check_seq_;
    ls_rebuild_seq_ = arg.ls_rebuild_seq_;
  }
  return ret;
}

OB_SERIALIZE_MEMBER(ObCopySSTableMacroRangeInfoArg, ls_id_,
    tablet_id_, copy_table_key_array_, macro_range_max_marco_count_,
    need_check_seq_, ls_rebuild_seq_);

ObCopySSTableMacroRangeInfoHeader::ObCopySSTableMacroRangeInfoHeader()
  : copy_table_key_(),
    macro_range_count_(0)
{
}

ObCopySSTableMacroRangeInfoHeader::~ObCopySSTableMacroRangeInfoHeader()
{
}

bool ObCopySSTableMacroRangeInfoHeader::is_valid() const
{
  return copy_table_key_.is_valid() && macro_range_count_ >= 0;
}

void ObCopySSTableMacroRangeInfoHeader::reset()
{
  copy_table_key_.reset();
  macro_range_count_ = 0;
}

OB_SERIALIZE_MEMBER(ObCopySSTableMacroRangeInfoHeader,
    copy_table_key_, macro_range_count_);

ObCopyTabletSSTableHeader::ObCopyTabletSSTableHeader()
  : tablet_id_(),
    status_(ObCopyTabletStatus::MAX_STATUS),
    sstable_count_(0),
    tablet_meta_(),
    version_(OB_INVALID_ID)
{
}

void ObCopyTabletSSTableHeader::reset()
{
  tablet_id_.reset();
  status_ = ObCopyTabletStatus::MAX_STATUS;
  sstable_count_ = 0;
  tablet_meta_.reset();
  version_ = OB_INVALID_ID;
}

bool ObCopyTabletSSTableHeader::is_valid() const
{
  return tablet_id_.is_valid()
      && ObCopyTabletStatus::is_valid(status_)
      && sstable_count_ >= 0
      && ((ObCopyTabletStatus::TABLET_EXIST == status_ && tablet_meta_.is_valid())
          || ObCopyTabletStatus::TABLET_NOT_EXIST == status_)
      && version_ != OB_INVALID_ID;
}

OB_SERIALIZE_MEMBER(ObCopyTabletSSTableHeader,
    tablet_id_, status_, sstable_count_, tablet_meta_, version_);

ObNotifyRestoreTabletsArg::ObNotifyRestoreTabletsArg()
  : ls_id_(), tablet_id_array_(), restore_status_(), leader_proposal_id_(0)
{
}


bool ObNotifyRestoreTabletsArg::is_valid() const
{
  return ls_id_.is_valid()
         && restore_status_.is_valid()
         && leader_proposal_id_ > 0;
}

OB_SERIALIZE_MEMBER(ObNotifyRestoreTabletsArg, ls_id_, tablet_id_array_, restore_status_, leader_proposal_id_);


ObNotifyRestoreTabletsResp::ObNotifyRestoreTabletsResp()
  : ls_id_(), restore_status_()
{
}



OB_SERIALIZE_MEMBER(ObNotifyRestoreTabletsResp, ls_id_, restore_status_);


ObInquireRestoreArg::ObInquireRestoreArg()
  : ls_id_(), restore_status_()
{
}


bool ObInquireRestoreArg::is_valid() const
{
  return ls_id_.is_valid()
	       && restore_status_.is_valid();
}

OB_SERIALIZE_MEMBER(ObInquireRestoreArg, ls_id_, restore_status_);

ObInquireRestoreResp::ObInquireRestoreResp()
  : ls_id_(), is_leader_(false), restore_status_()
{
}



OB_SERIALIZE_MEMBER(ObInquireRestoreResp, ls_id_, is_leader_, restore_status_);


ObRestoreUpdateLSMetaArg::ObRestoreUpdateLSMetaArg()
  : ls_meta_package_()
{
}


bool ObRestoreUpdateLSMetaArg::is_valid() const
{
  return ls_meta_package_.is_valid();
}

OB_SERIALIZE_MEMBER(ObRestoreUpdateLSMetaArg, ls_meta_package_);


ObCopyLSViewArg::ObCopyLSViewArg()
  : ls_id_()
{
}



OB_SERIALIZE_MEMBER(ObCopyLSViewArg, ls_id_);


// ObStorageStreamRpcP<> obcall stream-RPC processor impls deleted — dead in seekdb.


// Legacy shared-storage migrate-warmup obcall RPC arg/result struct impls removed
// (replaced by gRPC; see ob_storage_grpc.cpp).


// cross-tenant LOB obcall RPC removed: ObLobQueryP processor (OB_LOB_QUERY) deleted —
// cross-tenant LOB read now runs in-process (storage/lob/ob_lob_remote.cpp).
// Legacy shared-storage migrate-warmup obcall RPC processor impls removed
// (ObFetchMicroBlockKeysP / ObFetchMicroBlockP / ObGetMicroBlockCacheInfoP /
//  ObGetMigrationCacheJobInfoP / ObFetchReplicaPrewarmMicroBlockP) — replaced by gRPC.

} //namespace obcall

namespace storage
{

ObStorageRpc::ObStorageRpc()
    : is_inited_(false),
      rpc_proxy_(NULL)
{
}

ObStorageRpc::~ObStorageRpc()
{
  destroy();
}

int ObStorageRpc::init(
    obcall::ObStorageRpcProxy *rpc_proxy,
    const common::ObAddr &self)
{
  int ret = OB_SUCCESS;
  if (is_inited_) {
    ret = OB_INIT_TWICE;
    STORAGE_LOG(WARN, "storage rpc has inited", K(ret));
  } else if (OB_ISNULL(rpc_proxy) || !self.is_valid()) {
    ret = OB_INVALID_ARGUMENT;
    STORAGE_LOG(WARN, "ObStorageRpc init with invalid argument",
        KP(rpc_proxy), K(self));
  } else {
    rpc_proxy_ = rpc_proxy;
    self_ = self;
    is_inited_ = true;
  }
  return ret;
}

void ObStorageRpc::destroy()
{
  if (is_inited_) {
    is_inited_ = false;
    rpc_proxy_ = NULL;
    self_ = ObAddr();
  }
}


// Legacy shared-storage migrate-warmup ObStorageRpc wrapper impls removed
// (get_ls_micro_block_cache_info / get_ls_migration_cache_job_info /
//  get_micro_block_key_set) — replaced by gRPC.
} // storage
} // oceanbase
