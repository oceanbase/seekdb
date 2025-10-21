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
#include "storage/high_availability/ob_storage_ha_reader.h"
#include "logservice/ob_log_service.h"
#include "observer/ob_server_event_history_table_operator.h"
#include "storage/tablet/ob_tablet_iterator.h"
#include "storage/high_availability/ob_storage_ha_utils.h"
#ifdef OB_BUILD_SHARED_STORAGE
#include "storage/shared_storage/prewarm/ob_replica_prewarm_struct.h"
#endif
#include "storage/ddl/ob_direct_load_mgr_utils.h"
#include "lib/thread/thread.h"
#include "lib/worker.h"

namespace oceanbase
{
using namespace lib;
using namespace common;
using namespace share;
using namespace obrpc;
using namespace storage;
using namespace blocksstable;
using namespace memtable;
using namespace share::schema;

namespace obrpc
{


ObCopyMacroBlockArg::ObCopyMacroBlockArg()
  : logic_macro_block_id_()
{
}



OB_SERIALIZE_MEMBER(ObCopyMacroBlockArg,
    logic_macro_block_id_);


ObCopyMacroBlockListArg::ObCopyMacroBlockListArg()
  : tenant_id_(OB_INVALID_ID),
    ls_id_(),
    table_key_(),
    arg_list_()
{
}


bool ObCopyMacroBlockListArg::is_valid() const
{
  return tenant_id_ != OB_INVALID_ID
      && ls_id_.is_valid()
      && table_key_.is_valid()
      && arg_list_.count() > 0;
}


OB_SERIALIZE_MEMBER(ObCopyMacroBlockListArg, tenant_id_, ls_id_, table_key_, arg_list_);

ObCopyMacroBlockInfo::ObCopyMacroBlockInfo()
  : logical_id_(),
    data_type_(ObCopyMacroBlockDataType::MAX)
{
}



OB_SERIALIZE_MEMBER(ObCopyMacroBlockInfo, logical_id_, data_type_);

ObCopyMacroBlockRangeArg::ObCopyMacroBlockRangeArg()
  : tenant_id_(OB_INVALID_ID),
    ls_id_(),
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
  return OB_INVALID_ID != tenant_id_
      && ls_id_.is_valid()
      && table_key_.is_valid()
      && data_version_ >= 0
      && backfill_tx_scn_ >= SCN::min_scn()
      && copy_macro_range_info_.is_valid()
      && ((need_check_seq_ && ls_rebuild_seq_ >= 0) || !need_check_seq_);
}


OB_SERIALIZE_MEMBER(ObCopyMacroBlockRangeArg, tenant_id_, ls_id_, table_key_, data_version_,
    backfill_tx_scn_, copy_macro_range_info_, need_check_seq_, ls_rebuild_seq_, copy_macro_block_infos_);

ObCopyMacroBlockHeader::ObCopyMacroBlockHeader()
  : is_reuse_macro_block_(false),
    occupy_size_(0),
    data_type_(ObCopyMacroBlockDataType::MACRO_DATA) // default value for compat, previous version won't contain data_type_ and will pass macro data all the time
{
}



OB_SERIALIZE_MEMBER(ObCopyMacroBlockHeader, is_reuse_macro_block_, occupy_size_, data_type_);

ObCopyTabletInfoArg::ObCopyTabletInfoArg()
  : tenant_id_(OB_INVALID_ID),
    ls_id_(),
    tablet_id_list_(),
    need_check_seq_(false),
    ls_rebuild_seq_(-1),
    is_only_copy_major_(false),
    version_(OB_INVALID_ID)
{
}



OB_SERIALIZE_MEMBER(ObCopyTabletInfoArg,
    tenant_id_, ls_id_, tablet_id_list_, need_check_seq_, ls_rebuild_seq_,
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
  : tenant_id_(OB_INVALID_ID),
    ls_id_(),
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
  tenant_id_ = OB_INVALID_ID;
  ls_id_.reset();
  need_check_seq_ = false;
  ls_rebuild_seq_ = -1;
  is_only_copy_major_ = false;
  tablet_sstable_info_arg_list_.reset();
  version_ = OB_INVALID_ID;
}


OB_SERIALIZE_MEMBER(ObCopyTabletsSSTableInfoArg,
    tenant_id_, ls_id_, need_check_seq_, ls_rebuild_seq_, is_only_copy_major_,
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
  : tenant_id_(OB_INVALID_ID),
    ls_id_(),
    version_(OB_INVALID_ID)
{
}



OB_SERIALIZE_MEMBER(ObCopyLSInfoArg,
    tenant_id_, ls_id_, version_);


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
  : tenant_id_(OB_INVALID_ID),
    ls_id_(),
    version_(OB_INVALID_ID)
{
}



OB_SERIALIZE_MEMBER(ObFetchLSMetaInfoArg, tenant_id_, ls_id_, version_);


ObFetchLSMetaInfoResp::ObFetchLSMetaInfoResp()
  : ls_meta_package_(),
    version_(OB_INVALID_ID),
    has_transfer_table_(false)
{
}


bool ObFetchLSMetaInfoResp::is_valid() const
{
  return ls_meta_package_.is_valid()
      && version_ != OB_INVALID_ID;
}

OB_SERIALIZE_MEMBER(ObFetchLSMetaInfoResp, ls_meta_package_, version_, has_transfer_table_);

ObFetchLSMemberListArg::ObFetchLSMemberListArg()
  : tenant_id_(OB_INVALID_ID),
    ls_id_()
{
}



OB_SERIALIZE_MEMBER(ObFetchLSMemberListArg, tenant_id_, ls_id_);

ObFetchLSMemberListInfo::ObFetchLSMemberListInfo()
  : member_list_()
{
}



OB_SERIALIZE_MEMBER(ObFetchLSMemberListInfo, member_list_);

ObFetchLSMemberAndLearnerListArg::ObFetchLSMemberAndLearnerListArg()
  : tenant_id_(OB_INVALID_ID),
    ls_id_()
{
}



OB_SERIALIZE_MEMBER(ObFetchLSMemberAndLearnerListArg, tenant_id_, ls_id_);

ObFetchLSMemberAndLearnerListInfo::ObFetchLSMemberAndLearnerListInfo()
  : member_list_(),
    learner_list_()
{
}



OB_SERIALIZE_MEMBER(ObFetchLSMemberAndLearnerListInfo, member_list_, learner_list_);

ObCopySSTableMacroRangeInfoArg::ObCopySSTableMacroRangeInfoArg()
  : tenant_id_(OB_INVALID_ID),
    ls_id_(),
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
  return tenant_id_ != OB_INVALID_ID
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
    tenant_id_ = arg.tenant_id_;
    ls_id_ = arg.ls_id_;
    tablet_id_ = arg.tablet_id_;
    macro_range_max_marco_count_ = arg.macro_range_max_marco_count_;
    need_check_seq_ = arg.need_check_seq_;
    ls_rebuild_seq_ = arg.ls_rebuild_seq_;
  }
  return ret;
}

OB_SERIALIZE_MEMBER(ObCopySSTableMacroRangeInfoArg, tenant_id_, ls_id_,
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
  : tenant_id_(OB_INVALID_ID), ls_id_(), tablet_id_array_(), restore_status_(), leader_proposal_id_(0)
{
}


bool ObNotifyRestoreTabletsArg::is_valid() const
{
  return OB_INVALID_ID != tenant_id_
         && ls_id_.is_valid()
         && restore_status_.is_valid()
         && leader_proposal_id_ > 0;
}

OB_SERIALIZE_MEMBER(ObNotifyRestoreTabletsArg, tenant_id_, ls_id_, tablet_id_array_, restore_status_, leader_proposal_id_);


ObNotifyRestoreTabletsResp::ObNotifyRestoreTabletsResp()
  : tenant_id_(OB_INVALID_ID), ls_id_(), restore_status_()
{
}



OB_SERIALIZE_MEMBER(ObNotifyRestoreTabletsResp, tenant_id_, ls_id_, restore_status_);


ObInquireRestoreArg::ObInquireRestoreArg()
  : tenant_id_(OB_INVALID_ID), ls_id_(), restore_status_()
{
}


bool ObInquireRestoreArg::is_valid() const
{
  return OB_INVALID_ID != tenant_id_
         && ls_id_.is_valid()
	       && restore_status_.is_valid();
}

OB_SERIALIZE_MEMBER(ObInquireRestoreArg, tenant_id_, ls_id_, restore_status_);

ObInquireRestoreResp::ObInquireRestoreResp()
  : tenant_id_(OB_INVALID_ID), ls_id_(), is_leader_(false), restore_status_()
{
}



OB_SERIALIZE_MEMBER(ObInquireRestoreResp, tenant_id_, ls_id_, is_leader_, restore_status_);


ObRestoreUpdateLSMetaArg::ObRestoreUpdateLSMetaArg()
  : tenant_id_(OB_INVALID_ID), ls_meta_package_()
{
}


bool ObRestoreUpdateLSMetaArg::is_valid() const
{
  return OB_INVALID_ID != tenant_id_
         && ls_meta_package_.is_valid();
}

OB_SERIALIZE_MEMBER(ObRestoreUpdateLSMetaArg, tenant_id_, ls_meta_package_);


ObCheckSrcTransferTabletsArg::ObCheckSrcTransferTabletsArg()
  : tenant_id_(OB_INVALID_ID),
    src_ls_id_(),
    tablet_info_array_()
{
}




OB_SERIALIZE_MEMBER(ObCheckSrcTransferTabletsArg, tenant_id_, src_ls_id_, tablet_info_array_);


ObGetLSActiveTransCountArg::ObGetLSActiveTransCountArg()
  : tenant_id_(OB_INVALID_ID),
    src_ls_id_()
{
}



OB_SERIALIZE_MEMBER(ObGetLSActiveTransCountArg, tenant_id_, src_ls_id_);

ObCopyLSViewArg::ObCopyLSViewArg()
  : tenant_id_(OB_INVALID_ID),
    ls_id_()
{
}



OB_SERIALIZE_MEMBER(ObCopyLSViewArg, tenant_id_, ls_id_);


ObUpdateTransferMetaInfoArg::ObUpdateTransferMetaInfoArg()
  : tenant_id_(OB_INVALID_ID),
    dest_ls_id_(),
    transfer_meta_info_()
{
}


bool ObUpdateTransferMetaInfoArg::is_valid() const
{
  return OB_INVALID_ID != tenant_id_
      && dest_ls_id_.is_valid()
      && transfer_meta_info_.is_valid();
}


OB_SERIALIZE_MEMBER(ObUpdateTransferMetaInfoArg, tenant_id_, dest_ls_id_, transfer_meta_info_);

template <ObRpcPacketCode RPC_CODE>
ObStorageStreamRpcP<RPC_CODE>::ObStorageStreamRpcP(common::ObInOutBandwidthThrottle *bandwidth_throttle)
  : bandwidth_throttle_(bandwidth_throttle),
    last_send_time_(0),
    allocator_("SSRpcP")
{
}

template <ObRpcPacketCode RPC_CODE>
template <typename Data>
int ObStorageStreamRpcP<RPC_CODE>::fill_data(const Data &data)
{
  int ret = OB_SUCCESS;
  const int64_t curr_ts = ObTimeUtil::current_time();
  if (NULL == (this->result_.get_data())) {
    STORAGE_LOG(WARN, "fail to alloc migration data buffer.");
    ret = OB_ALLOCATE_MEMORY_FAILED;
  } else if (serialization::encoded_length(data) > this->result_.get_remain()
      || (curr_ts - last_send_time_ >= FLUSH_TIME_INTERVAL
          && this->result_.get_capacity() != this->result_.get_remain())) {
    if (0 == this->result_.get_position()) {
      ret = OB_ERR_UNEXPECTED;
      STORAGE_LOG(ERROR, "data is too large", K(ret));
    } else if (OB_FAIL(flush_and_wait())) {
      STORAGE_LOG(WARN, "failed to flush_and_wait", K(ret));
    }
  }

  if (OB_SUCC(ret)) {
    if (OB_FAIL(serialization::encode(this->result_.get_data(),
                                      this->result_.get_capacity(),
                                      this->result_.get_position(),
                                      data))) {
      STORAGE_LOG(WARN, "failed to encode", K(ret));
    }
  }
  return ret;
}

template <ObRpcPacketCode RPC_CODE>
int ObStorageStreamRpcP<RPC_CODE>::fill_buffer(blocksstable::ObBufferReader &data)
{
  int ret = OB_SUCCESS;
  const int64_t curr_ts = ObTimeUtil::current_time();
  if (NULL == (this->result_.get_data())) {
    STORAGE_LOG(WARN, "fail to alloc migration data buffer.");
    ret = OB_ALLOCATE_MEMORY_FAILED;
  } else {
    while (OB_SUCC(ret) && data.remain() > 0) {
      if (0 == this->result_.get_remain()
          || (curr_ts - last_send_time_ >= FLUSH_TIME_INTERVAL
              && this->result_.get_capacity() != this->result_.get_remain())) {
        if (OB_FAIL(flush_and_wait())) {
          STORAGE_LOG(WARN, "failed to flush_and_wait", K(ret));
        }
      } else {
        int64_t fill_length = std::min(this->result_.get_remain(), data.remain());
        if (fill_length <= 0) {
          ret = OB_ERR_UNEXPECTED;
          STORAGE_LOG(ERROR, "fill_length must larger than 0", K(ret), K(fill_length), K(this->result_), K(data));
        } else {
          MEMCPY(this->result_.get_cur_pos(), data.current(), fill_length);
          this->result_.get_position() += fill_length;
          if (OB_FAIL(data.advance(fill_length))) {
            STORAGE_LOG(WARN, "failed to advance fill length", K(ret), K(fill_length), K(data));
          }
        }
      }
    }
  }
  return ret;
}



template <ObRpcPacketCode RPC_CODE>
int ObStorageStreamRpcP<RPC_CODE>::flush_and_wait()
{
  int ret = OB_SUCCESS;
  int tmp_ret = OB_SUCCESS;
  const int64_t max_idle_time = OB_DEFAULT_STREAM_WAIT_TIMEOUT - OB_DEFAULT_STREAM_RESERVE_TIME;

  if (NULL == bandwidth_throttle_) {
    ret = OB_NOT_INIT;
    STORAGE_LOG(WARN, "bandwidth_throttle_ must not null", K(ret));
  } else {
    Thread::WaitGuard guard(Thread::WAIT_FOR_IO_EVENT);
    if (OB_SUCCESS != (tmp_ret = bandwidth_throttle_->limit_out_and_sleep(
        this->result_.get_position(), last_send_time_, max_idle_time))) {
      STORAGE_LOG(WARN, "failed limit out band", K(tmp_ret));
    }

    if (OB_FAIL(this->check_timeout())) {
      LOG_WARN("rpc is timeout, no need flush", K(ret));
    } else if (OB_FAIL(this->flush())) {
      STORAGE_LOG(WARN, "failed to flush", K(ret));
    } else {
      this->result_.get_position() = 0;
      last_send_time_ = ObTimeUtility::current_time();
    }
  }
  return ret;
}


template <ObRpcPacketCode RPC_CODE>
int ObStorageStreamRpcP<RPC_CODE>::is_follower_ls(logservice::ObLogService *log_srv, ObLS *ls, bool &is_ls_follower)
{
  int ret = OB_SUCCESS;
  logservice::ObLogHandler *log_handler = nullptr;
  int64_t proposal_id = 0;
  ObRole role;
  if (OB_ISNULL(log_srv) || OB_ISNULL(ls)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("log handler should not be NULL", K(ret), KP(log_srv), K(ls));
  } else if (OB_FAIL(log_srv->get_palf_role(ls->get_ls_id(), role, proposal_id))) {
    LOG_WARN("fail to get role", K(ret), "ls_id", ls->get_ls_id());
  } else if (!is_follower(role)) {
    is_ls_follower = false;
    STORAGE_LOG(WARN, "I am not follower", K(ret), K(role), K(proposal_id));
  } else {
    is_ls_follower = true;
  }
  return ret;
}

#ifdef OB_BUILD_SHARED_STORAGE
ObGetMicroBlockCacheInfoArg::ObGetMicroBlockCacheInfoArg()
  : tenant_id_(OB_INVALID_ID),
    ls_id_()
{
}

bool ObGetMicroBlockCacheInfoArg::is_valid() const
{
  return OB_INVALID_ID != tenant_id_ && ls_id_.is_valid();
}


OB_SERIALIZE_MEMBER(ObGetMicroBlockCacheInfoArg, tenant_id_, ls_id_);

ObGetMicroBlockCacheInfoRes::ObGetMicroBlockCacheInfoRes()
  : ls_cache_info_()
{
}



OB_SERIALIZE_MEMBER(ObGetMicroBlockCacheInfoRes, ls_cache_info_);

ObGetMigrationCacheJobInfoArg::ObGetMigrationCacheJobInfoArg()
  : tenant_id_(OB_INVALID_ID),
    ls_id_(),
    task_count_(0)
{
}

bool ObGetMigrationCacheJobInfoArg::is_valid() const
{
  return OB_INVALID_ID != tenant_id_ && ls_id_.is_valid() && task_count_ > 0;
}


OB_SERIALIZE_MEMBER(ObGetMigrationCacheJobInfoArg, tenant_id_, ls_id_, task_count_);

ObGetMigrationCacheJobInfoRes::ObGetMigrationCacheJobInfoRes()
  : job_infos_()
{
}


void ObGetMigrationCacheJobInfoRes::reset()
{
  job_infos_.reset();
}

OB_SERIALIZE_MEMBER(ObGetMigrationCacheJobInfoRes, job_infos_);

ObGetMicroBlockKeyArg::ObGetMicroBlockKeyArg()
  : tenant_id_(OB_INVALID_ID),
    ls_id_(),
    job_info_()
{
}

bool ObGetMicroBlockKeyArg::is_valid() const
{
  return OB_INVALID_ID != tenant_id_ 
      && ls_id_.is_valid() 
      && job_info_.is_valid();
}


OB_SERIALIZE_MEMBER(ObGetMicroBlockKeyArg, tenant_id_, ls_id_, job_info_);

ObCopyMicroBlockKeySetRes::ObCopyMicroBlockKeySetRes()
  : header_(),
    key_set_array_()
{
}

ObCopyMicroBlockKeySetRes::~ObCopyMicroBlockKeySetRes()
{
}

bool ObCopyMicroBlockKeySetRes::is_valid() const
{
  return header_.is_valid();
}

void ObCopyMicroBlockKeySetRes::reset()
{
  header_.reset();
  key_set_array_.reset();
}


OB_SERIALIZE_MEMBER(ObCopyMicroBlockKeySetRes, header_, key_set_array_);

ObMigrateWarmupKeySet::ObMigrateWarmupKeySet()
  : tenant_id_(OB_INVALID_ID),
    key_sets_()
{
}

bool ObMigrateWarmupKeySet::is_valid() const
{
  return OB_INVALID_ID != tenant_id_ && !key_sets_.empty();
}

void ObMigrateWarmupKeySet::reset()
{
  tenant_id_ = OB_INVALID_ID;
  key_sets_.reset();
}

int ObMigrateWarmupKeySet::assign(const ObMigrateWarmupKeySet &arg)
{
  int ret = OB_SUCCESS;
  if (!arg.is_valid()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", K(ret), K(arg));
  } else if (OB_FAIL(key_sets_.assign(arg.key_sets_))) {
    LOG_WARN("failed to assign arg list", K(ret), K(arg));
  } else {
    tenant_id_ = arg.tenant_id_;
  }
  return ret;
}

OB_SERIALIZE_MEMBER(ObMigrateWarmupKeySet, tenant_id_, key_sets_);

ObSSLSFetchMicroBlockArg::ObSSLSFetchMicroBlockArg()
  : tenant_id_(OB_INVALID_TENANT_ID), micro_metas_()
{
}

bool ObSSLSFetchMicroBlockArg::is_valid() const
{
  return (is_valid_tenant_id(tenant_id_) && !micro_metas_.empty());
}


int ObSSLSFetchMicroBlockArg::assign(const ObSSLSFetchMicroBlockArg &other)
{
  int ret = OB_SUCCESS;
  if (OB_LIKELY(this != &other)) {
    tenant_id_ = other.tenant_id_;
    if (OB_FAIL(micro_metas_.assign(other.micro_metas_))) {
      LOG_WARN("fail to assign micro keys", KR(ret));
    }
  }
  return ret;
}

OB_SERIALIZE_MEMBER(ObSSLSFetchMicroBlockArg, tenant_id_, micro_metas_);

#endif


ObNotifyRestoreTabletsP::ObNotifyRestoreTabletsP(
    common::ObInOutBandwidthThrottle *bandwidth_throttle)
    : ObStorageStreamRpcP(bandwidth_throttle)
{

}

int ObNotifyRestoreTabletsP::process()
{
  int ret = OB_SUCCESS;
  MTL_SWITCH(arg_.tenant_id_) {
    CONSUMER_GROUP_FUNC_GUARD(ObFunctionType::PRIO_HA_HIGH);
    ObLSHandle ls_handle;
    ObLSService *ls_service = nullptr;
    ObLS *ls = nullptr;
    ObDeviceHealthStatus dhs = DEVICE_HEALTH_NORMAL;
    logservice::ObLogService *log_srv = nullptr;
    int64_t disk_abnormal_time = 0;
    bool is_follower = false;

    LOG_INFO("start to notify follower restore tablets", K(arg_));

#ifdef ERRSIM
    if (OB_SUCC(ret) && DEVICE_HEALTH_NORMAL == dhs && GCONF.fake_disk_error) {
      dhs = DEVICE_HEALTH_ERROR;
    }
#endif

    if (!arg_.is_valid()) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("notify follower restore tablets get invalid argument", K(ret), K(arg_));
    } else if (DEVICE_HEALTH_NORMAL == dhs
        && OB_FAIL(ObIOManager::get_instance().get_device_health_status(dhs, disk_abnormal_time))) {
      STORAGE_LOG(WARN, "failed to check is disk error", K(ret));
    } else if (DEVICE_HEALTH_ERROR == dhs) {
      ret = OB_DISK_ERROR;
      STORAGE_LOG(ERROR, "observer has disk error, cannot restore", KR(ret),
          "disk_health_status", device_health_status_to_str(dhs), K(disk_abnormal_time));
    } else if (OB_ISNULL(ls_service = MTL(ObLSService *))) {
      ret = OB_ERR_UNEXPECTED;
      STORAGE_LOG(WARN, "ls service should not be null", K(ret), KP(ls_service));
    } else if (OB_FAIL(ls_service->get_ls(arg_.ls_id_, ls_handle, ObLSGetMod::STORAGE_MOD))) {
      LOG_WARN("failed to get log stream", K(ret), K(arg_));
    } else if (OB_ISNULL(ls = ls_handle.get_ls())) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("log stream should not be NULL", K(ret), KP(ls), K(arg_));
    } else if (OB_ISNULL(log_srv = MTL(logservice::ObLogService*))) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("log srv should not be null", K(ret), KP(log_srv));
    } else if (OB_FAIL(is_follower_ls(log_srv, ls, is_follower))) {
      LOG_WARN("failed to check is follower", K(ret), KP(ls), K(arg_));
    } else if (!is_follower) {
      ret = OB_NOT_FOLLOWER;
      STORAGE_LOG(WARN, "I am not follower", K(ret), K(arg_));
    } else {
      ObLSRestoreHandler *ls_restore_handler = ls->get_ls_restore_handler();
      if (OB_FAIL(ls_restore_handler->handle_pull_tablet(
          arg_.tablet_id_array_, arg_.restore_status_, arg_.leader_proposal_id_))) {
        LOG_WARN("fail to handle pull tablet", K(ret), K(arg_));
      } else if (OB_FAIL(ls->get_restore_status(result_.restore_status_))) {
        LOG_WARN("fail to get restore status", K(ret));
      } else {
        result_.tenant_id_ = arg_.tenant_id_;
        result_.ls_id_ = arg_.ls_id_;
      }
    }
  }
  return ret;
}


ObInquireRestoreP::ObInquireRestoreP(
    common::ObInOutBandwidthThrottle *bandwidth_throttle)
    : ObStorageStreamRpcP(bandwidth_throttle)
{

}

int ObInquireRestoreP::process()
{
  int ret = OB_SUCCESS;
  MTL_SWITCH(arg_.tenant_id_) {
    CONSUMER_GROUP_FUNC_GUARD(ObFunctionType::PRIO_HA_HIGH);
    ObLSHandle ls_handle;
    ObLSService *ls_service = nullptr;
    ObLS *ls = nullptr;
    logservice::ObLogService *log_srv = nullptr;
    ObDeviceHealthStatus dhs = DEVICE_HEALTH_NORMAL;
    int64_t disk_abnormal_time = 0;
    bool is_follower = false;

    LOG_INFO("start to inquire restore status", K(arg_));

#ifdef ERRSIM
    if (OB_SUCC(ret) && DEVICE_HEALTH_NORMAL == dhs && GCONF.fake_disk_error) {
      dhs = DEVICE_HEALTH_ERROR;
    }
#endif

    if (!arg_.is_valid()) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("notify follower restore get invalid argument", K(ret), K(arg_));
    } else if (DEVICE_HEALTH_NORMAL == dhs
        && OB_FAIL(ObIOManager::get_instance().get_device_health_status(dhs, disk_abnormal_time))) {
      STORAGE_LOG(WARN, "failed to check is disk error", KR(ret));
    } else if (DEVICE_HEALTH_ERROR == dhs) {
      ret = OB_DISK_ERROR;
      STORAGE_LOG(ERROR, "observer has disk error, cannot restore", KR(ret),
          "disk_health_status", device_health_status_to_str(dhs), K(disk_abnormal_time));
    } else if (OB_ISNULL(ls_service = MTL(ObLSService *))) {
      ret = OB_ERR_UNEXPECTED;
      STORAGE_LOG(WARN, "ls service should not be null", K(ret), KP(ls_service));
    } else if (OB_FAIL(ls_service->get_ls(arg_.ls_id_, ls_handle, ObLSGetMod::STORAGE_MOD))) {
      LOG_WARN("failed to get log stream", K(ret), K(arg_));
    } else if (OB_ISNULL(ls = ls_handle.get_ls())) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("log stream should not be NULL", K(ret), KP(ls), K(arg_));
    } else if (OB_ISNULL(log_srv = MTL(logservice::ObLogService*))) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("log srv should not be null", K(ret), KP(log_srv));
    } else if (OB_FAIL(is_follower_ls(log_srv, ls, is_follower))) {
      LOG_WARN("failed to check is follower", K(ret), KP(ls), K(arg_));
    } else if (OB_FAIL(ls->get_restore_status(result_.restore_status_))) {
      LOG_WARN("fail to get restore status", K(ret));
    } else if (is_follower) {
      result_.tenant_id_ = arg_.tenant_id_;
      result_.ls_id_ = arg_.ls_id_;
      result_.is_leader_ = false;
      LOG_INFO("succ to inquire restore status from follower", K(result_));
    } else {
      result_.tenant_id_ = arg_.tenant_id_;
      result_.ls_id_ = arg_.ls_id_;
      result_.is_leader_ = true;
      LOG_INFO("succ to inquire restore status from leader", K(ret), K(arg_), K(result_));
    }
  }
  return ret;
}

ObUpdateLSMetaP::ObUpdateLSMetaP(
    common::ObInOutBandwidthThrottle *bandwidth_throttle)
    : ObStorageStreamRpcP(bandwidth_throttle)
{

}

int ObUpdateLSMetaP::process()
{
  int ret = OB_SUCCESS;
  MTL_SWITCH(arg_.tenant_id_) {
    CONSUMER_GROUP_FUNC_GUARD(ObFunctionType::PRIO_HA_HIGH);
    ObLSHandle ls_handle;
    ObLSService *ls_service = nullptr;
    ObLS *ls = nullptr;
    bool is_follower = false;
    ObDeviceHealthStatus dhs = DEVICE_HEALTH_NORMAL;
    int64_t disk_abnormal_time = 0;

    LOG_INFO("start to update ls meta", K(arg_));

#ifdef ERRSIM
    if (OB_SUCC(ret) && DEVICE_HEALTH_NORMAL == dhs && GCONF.fake_disk_error) {
      dhs = DEVICE_HEALTH_ERROR;
    }
#endif
    if (!arg_.is_valid()) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("notify follower restore get invalid argument", K(ret), K(arg_));
    } else if (DEVICE_HEALTH_NORMAL == dhs
        && OB_FAIL(ObIOManager::get_instance().get_device_health_status(dhs, disk_abnormal_time))) {
      STORAGE_LOG(WARN, "failed to check is disk error", KR(ret));
    } else if (DEVICE_HEALTH_ERROR == dhs) {
      ret = OB_DISK_ERROR;
      STORAGE_LOG(ERROR, "observer has disk error, cannot restore", KR(ret),
          "disk_health_status", device_health_status_to_str(dhs), K(disk_abnormal_time));
    } else if (OB_ISNULL(ls_service = MTL(ObLSService *))) {
      ret = OB_ERR_UNEXPECTED;
      STORAGE_LOG(WARN, "ls service should not be null", K(ret), KP(ls_service));
    } else if (OB_FAIL(ls_service->restore_update_ls(arg_.ls_meta_package_))) {
      LOG_WARN("failed to get log stream", K(ret), K(arg_));
    } else {
      LOG_INFO("succ to update ls meta", K(ret), K(arg_));
    }
  }
  return ret;
}

ObLobQueryP::ObLobQueryP(common::ObInOutBandwidthThrottle *bandwidth_throttle)
  : ObStorageStreamRpcP(bandwidth_throttle)
{
  // the streaming interface may return multi packet. The memory may be freed after the first packet has been sended.
  // the deserialization of arg_ is shallow copy, so we need deep copy data to processor
  set_preserve_recv_data();
}

int64_t ObLobQueryP::get_timeout() const
{
  int64_t timeout = 0;
  const int64_t rpc_timeout = rpc_pkt_->get_timeout();
  const int64_t send_timestamp = get_send_timestamp();
  // oversize int64_t if rpc_timeout + send_timestamp > INT64_MAX
  if (INT64_MAX - rpc_timeout - send_timestamp < 0) {
    timeout = INT64_MAX;
  } else {
    timeout = rpc_timeout + send_timestamp;
  }
  return timeout;
}

int ObLobQueryP::process_read()
{
  int ret = OB_SUCCESS;
  ObLobManager *lob_mngr = MTL(ObLobManager*);
  ObLobQueryBlock header;
  blocksstable::ObBufferReader data;
  char *out_buf = nullptr;
  int64_t buf_len = ObLobQueryArg::OB_LOB_QUERY_BUFFER_LEN - sizeof(ObLobQueryBlock);
  if (OB_ISNULL(out_buf = reinterpret_cast<char*>(allocator_.alloc(buf_len)))) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
    STORAGE_LOG(WARN, "failed to alloc out data buffer.", K(ret));
  } else {
    ObString out;
    ObLobAccessParam param;
    param.scan_backward_ = arg_.scan_backward_;
    param.from_rpc_ = true;
    param.enable_remote_retry_ = arg_.enable_remote_retry_;
    ObLobQueryIter *iter = nullptr;
    int64_t timeout = get_timeout();
    if (OB_FAIL(lob_mngr->build_lob_param(param, allocator_, arg_.cs_type_, arg_.offset_,
        arg_.len_, timeout, arg_.lob_locator_))) {
      LOG_WARN("failed to build lob param", K(ret));
    } else if (OB_FAIL(lob_mngr->query(param, iter))) {
      LOG_WARN("failed to query lob.", K(ret), K(param));
    } else {
      while (OB_SUCC(ret)) {
        out.assign_buffer(out_buf, buf_len);
        if (OB_FAIL(iter->get_next_row(out))) {
          if (OB_ITER_END != ret) {
            STORAGE_LOG(WARN, "failed to get next buffer", K(ret));
          }
        } else {
          header.size_ = out.length();
          data.assign(out.ptr(), out.length());
          // only scan backward need header
          if (OB_FAIL(fill_data(header))) {
            STORAGE_LOG(WARN, "failed to fill header", K(ret), K(header));
          } else if (OB_FAIL(fill_buffer(data))) {
            STORAGE_LOG(WARN, "failed to fill buffer", K(ret), K(data));
          }
        }
      }
      if (ret == OB_ITER_END) {
        ret = OB_SUCCESS;
      }
    }
    if (OB_NOT_NULL(iter)) {
      iter->reset();
      OB_DELETE(ObLobQueryIter, "unused", iter);
    }
  }
  return ret;
}

int ObLobQueryP::process_getlength()
{
  int ret = OB_SUCCESS;
  ObLobManager *lob_mngr = MTL(ObLobManager*);
  ObLobQueryBlock header;
  blocksstable::ObBufferReader data;
  ObLobAccessParam param;
  param.scan_backward_ = arg_.scan_backward_;
  param.from_rpc_ = true;
  param.enable_remote_retry_ = arg_.enable_remote_retry_;
  header.reset();
  uint64_t len = 0;
  int64_t timeout = get_timeout();
  if (OB_FAIL(lob_mngr->build_lob_param(param, allocator_, arg_.cs_type_, arg_.offset_,
      arg_.len_, timeout, arg_.lob_locator_))) {
    LOG_WARN("failed to build lob param", K(ret));
  } else if (OB_FAIL(lob_mngr->getlength(param, len))) { // reuse size_ for lob_len
    LOG_WARN("failed to getlength lob.", K(ret), K(param));
  } else if (FALSE_IT(header.size_ = static_cast<int64_t>(len))) {
  } else if (OB_FAIL(fill_data(header))) {
    STORAGE_LOG(WARN, "failed to fill header", K(ret), K(header));
  }
  return ret;
}

int ObLobQueryP::process()
{
  int ret = OB_SUCCESS;
  MTL_SWITCH(arg_.tenant_id_) {
    ObLobManager *lob_mngr = MTL(ObLobManager*);
    // init result_
    char *buf = nullptr;
    int64_t buf_len = ObLobQueryArg::OB_LOB_QUERY_BUFFER_LEN;
    if (OB_ISNULL(buf = reinterpret_cast<char*>(allocator_.alloc(buf_len)))) {
      ret = OB_ALLOCATE_MEMORY_FAILED;
      STORAGE_LOG(WARN, "failed to alloc result data buffer.", K(ret));
    } else if (!result_.set_data(buf, buf_len)) {
      ret = OB_ERR_UNEXPECTED;
      STORAGE_LOG(WARN, "failed set data to result", K(ret));
    } else if (!arg_.lob_locator_.is_valid()) {
      ret = OB_ERR_UNEXPECTED;
      STORAGE_LOG(WARN, "lob locator is invalid", K(ret));
    } else if (!arg_.lob_locator_.is_persist_lob()) {
      ret = OB_NOT_SUPPORTED;
      LOG_WARN("unsupport remote query non-persist lob.", K(ret), K(arg_.lob_locator_));
    } else if (arg_.qtype_ == ObLobQueryArg::QueryType::READ) {
      if (OB_FAIL(process_read())) {
        LOG_WARN("fail to process read", K(ret), K(arg_));
      }
    } else if (arg_.qtype_ == ObLobQueryArg::QueryType::GET_LENGTH) {
      if (OB_FAIL(process_getlength())) {
        LOG_WARN("fail to process read", K(ret), K(arg_));
      }
    } else {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("invalid arg qtype.", K(ret), K(arg_));
    }
  }
  return ret;
}
#ifdef OB_BUILD_SHARED_STORAGE
int ObFetchMicroBlockKeysP::set_header_attr_(
    const ObCopyMicroBlockKeySetRpcHeader::ConnectStatus connect_status,
    const int64_t blk_idx, const int64_t count,
    ObCopyMicroBlockKeySetRpcHeader &header)
{
  int ret = OB_SUCCESS;
  header.reset();
  if (connect_status < ObCopyMicroBlockKeySetRpcHeader::ConnectStatus::RECONNECT
      || connect_status >= ObCopyMicroBlockKeySetRpcHeader::ConnectStatus::MAX_STATUS
      || blk_idx < 0
      || count < 0) {
    ret = OB_INVALID_ARGUMENT;
    STORAGE_LOG(WARN, "header attr is invalid", K(ret), K(connect_status), K(blk_idx), K(count));
  } else {
    header.connect_status_ = connect_status;
    header.end_blk_idx_ = blk_idx;
    header.object_count_ = count;
  }
  return ret;
}

ERRSIM_POINT_DEF(EN_MICRO_KEY_SET_RECONNECT);
int ObFetchMicroBlockKeysP::process()
{
  int ret = OB_SUCCESS;

  MTL_SWITCH(arg_.tenant_id_) {
    ObCopyMicroBlockKeySetProducer producer;
    ObCopyMicroBlockKeySet key_set;
    ObCopyMicroBlockKeySetRpcHeader rpc_header;
    int64_t max_key_set_size = WARMUP_MAX_KEY_SET_SIZE_IN_RPC; // 4M;
    const int64_t start_ts = ObTimeUtil::current_time();
    int64_t end_blk_idx = 0;
    int64_t key_set_count = 0;
    int64_t key_count = 0;
    ObCopyMicroBlockKeySetRpcHeader::ConnectStatus connect_status = ObCopyMicroBlockKeySetRpcHeader::ConnectStatus::MAX_STATUS;
    LOG_INFO("start to fetch micro block header", K(arg_));

    if (!arg_.is_valid()) {
      ret = OB_INVALID_ARGUMENT;
      LOG_WARN("get invalid args", K(ret), K_(arg));
    } else if (OB_FAIL(producer.init(arg_.job_info_, arg_.ls_id_))) {
      LOG_WARN("failed to init micro block key producer", K(ret), K(arg_));
    } else {
      while (OB_SUCC(ret)) {
        key_set.reset();
        if (OB_FAIL(producer.get_next_micro_block_key_set(key_set))) {
          if (OB_ITER_END == ret) {
            ret = OB_SUCCESS;
            end_blk_idx = key_set.blk_idx_;
            connect_status = ObCopyMicroBlockKeySetRpcHeader::ConnectStatus::ENDCONNECT;
            break;
          } else {
            STORAGE_LOG(WARN, "failed to get next micro block key set", K(ret));
          }
        } else if (!key_set.is_valid()) {
          LOG_INFO("skip this key set", K(arg_), K(key_set));
        } else {
          // dest will judge ObMigrateWarmupKeySet serialize size,
          if (OB_FAIL(result_.key_set_array_.key_sets_.push_back(key_set))) {
            STORAGE_LOG(WARN, "fail to fill key set", K(ret), K(key_set));
          } 
#ifdef ERRSIM
          else if (EN_MICRO_KEY_SET_RECONNECT && key_set_count > 0) {
            result_.key_set_array_.key_sets_.pop_back();
            connect_status = ObCopyMicroBlockKeySetRpcHeader::ConnectStatus::RECONNECT;
            break;
          }
#endif
          else if (result_.key_set_array_.get_serialize_size() > max_key_set_size) {
            result_.key_set_array_.key_sets_.pop_back();
            connect_status = ObCopyMicroBlockKeySetRpcHeader::ConnectStatus::RECONNECT;
            break;
          } else {
            key_set_count++;
            key_count += key_set.micro_block_key_metas_.count();
            end_blk_idx = key_set.blk_idx_;
          }
        }
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_FAIL(set_header_attr_(connect_status, end_blk_idx, key_set_count, rpc_header))) {
        LOG_WARN("failed to set header attr", K(ret), K(rpc_header), K(arg_),
            K(connect_status), K(end_blk_idx), K(key_set_count));
      } else {
        result_.header_ = rpc_header;
      }
    }
    LOG_INFO("finish fetch micro block header", K(ret), "cost_ts", ObTimeUtil::current_time() - start_ts, 
        K(key_count), K(arg_), K(rpc_header));
  }
  return ret;
}

ObFetchMicroBlockP::ObFetchMicroBlockP(
      common::ObInOutBandwidthThrottle *bandwidth_throttle)
    : ObStorageStreamRpcP(bandwidth_throttle)
{
}

int ObFetchMicroBlockP::process()
{
  int ret = OB_SUCCESS;

  MTL_SWITCH(arg_.tenant_id_) {
    blocksstable::ObBufferReader data;
    char *buf = NULL;
    last_send_time_ = this->get_receive_timestamp();
    int64_t key_count = 0;
    ObSArray<ObSSMicroBlockCacheKeyMeta> key_meta_array;
    const int64_t start_ts = ObTimeUtil::current_time();
    const int64_t first_receive_ts = this->get_receive_timestamp();
    LOG_INFO("start to fetch micro block", K(arg_));
    if (!arg_.is_valid()) {
      ret = OB_INVALID_ARGUMENT;
      LOG_WARN("get invalid args", K(ret), K_(arg));
    }
    // The reason that apply 6M buffer:
    // buffer struct: key_meta_array + data
    // key_meta: key info + other cache info
    // other cache info: uint32(crc) + bool(in t1/t2), less than key info
    // data less than 2M, it comes from a cache block
    // key info array also less than 2M
    // other cache info array also less than 2M
    // so key_meta_array + data less than 6M
    else if (NULL == (buf = reinterpret_cast<char*>(allocator_.alloc(OB_MALLOC_BIG_BLOCK_SIZE * 3)))) {
      ret = OB_ALLOCATE_MEMORY_FAILED;
      STORAGE_LOG(WARN, "failed to alloc migrate data buffer.", K(ret));
    } else if (!result_.set_data(buf, OB_MALLOC_BIG_BLOCK_SIZE * 3)) {
      ret = OB_ALLOCATE_MEMORY_FAILED;
      STORAGE_LOG(WARN, "failed set data to result", K(ret));
    } else if (OB_ISNULL(bandwidth_throttle_)) {
      ret = OB_ERR_UNEXPECTED;
      STORAGE_LOG(ERROR, "bandwidth_throttle must not null", K(ret), KP_(bandwidth_throttle));
    } else {
      SMART_VAR(storage::ObCopyMicroBlockDataProducer, producer) {
        if (OB_FAIL(producer.init(arg_.key_sets_))) {
          LOG_WARN("failed to init micro block data producer", K(ret), K(arg_));
        } else {
          while (OB_SUCC(ret)) {
            key_meta_array.reset();
            if (OB_FAIL(producer.get_next_micro_block_data(key_meta_array, data))) {
              if (OB_ITER_END != ret) {
                STORAGE_LOG(WARN, "failed to get next micro block set", K(ret));
              } else {
                ret = OB_SUCCESS;
              }
              break;
            } else if (key_meta_array.empty()) {
              LOG_INFO("skip this key and size arr", K(arg_));
            } else if (OB_FAIL(fill_data(key_meta_array))) {
              STORAGE_LOG(WARN, "failed to fill data length", K(ret), K(data.pos()), K(key_meta_array));
            } else if (OB_FAIL(fill_buffer(data))) {
              STORAGE_LOG(WARN, "failed to fill data", K(ret), K(key_meta_array));
            } else {
              key_count += key_meta_array.count();
              STORAGE_LOG(INFO, "succeed to fill micro block set",
                  "key and size array", key_meta_array, K(data));
            }
          }
        }
      }
    }

    LOG_INFO("finish fetch micro block set", K(ret),
        "cost_ts", ObTimeUtil::current_time() - start_ts,
        "in rpc queue time", start_ts - first_receive_ts, K(key_count));
  }
  return ret;
}

int ObGetMicroBlockCacheInfoP::process()
{
  int ret = OB_SUCCESS;

  MTL_SWITCH(arg_.tenant_id_) {
    ObSSMicroCache *micro_cache = nullptr;
    if (!arg_.is_valid()) {
      ret = OB_INVALID_ARGUMENT;
      LOG_WARN("get invalid args", K(ret), K_(arg));
    } else if (OB_ISNULL(micro_cache = MTL(ObSSMicroCache*))) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("micro cache should not be nullptr", K(ret));
    } else {
      if (OB_FAIL(micro_cache->get_ls_cache_info(arg_.ls_id_, result_.ls_cache_info_))) {
        LOG_WARN("fail to get ls cache info", KR(ret), K_(arg));
      }
      LOG_INFO("send cache info", K(ret), K(result_), K(arg_));
    }
  }
  return ret;
}

int ObGetMigrationCacheJobInfoP::process()
{
  int ret = OB_SUCCESS;

  MTL_SWITCH(arg_.tenant_id_) {
    ObSSMicroCache *micro_cache = nullptr;
    ObArray<ObSSPhyBlockIdxRange> block_ranges;
    if (!arg_.is_valid()) {
      ret = OB_INVALID_ARGUMENT;
      LOG_WARN("get invalid args", K(ret), K_(arg));
    } else if (OB_ISNULL(micro_cache = MTL(ObSSMicroCache*))) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("micro cache should not be nullptr", K(ret));
    } else if (OB_FAIL(micro_cache->divide_phy_block_range(arg_.ls_id_, arg_.task_count_, block_ranges))) {
      LOG_WARN("failed to divide phy_block range", K(ret), K(arg_));
    } else if (block_ranges.empty()) {
      FLOG_INFO("block_ranges is empty", K_(arg));
    } else if (OB_FAIL(convert_block_range_to_job_infos_(block_ranges, result_.job_infos_))) {
      LOG_WARN("failed to convert job infos", K(ret), K(block_ranges), K(arg_));
    } else if (arg_.task_count_ < result_.job_infos_.count()) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("job info count is unexpected", K(ret), K(arg_), K(result_));
    } else {
      LOG_INFO("send job info", K(block_ranges), K(result_.job_infos_));
    }
  }
  return ret;
}

int ObGetMigrationCacheJobInfoP::convert_block_range_to_job_infos_(
    const ObIArray<ObSSPhyBlockIdxRange> &block_ranges, ObIArray<ObMigrationCacheJobInfo> &job_infos)
{
  int ret = OB_SUCCESS;
  job_infos.reset();
  if (block_ranges.count() <= 0) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", K(ret));
  } else {
    ObMigrationCacheJobInfo job_info;
    for (int i = 0; i < block_ranges.count() && OB_SUCC(ret); i++) {
      job_info.reset();
      if (OB_FAIL(job_info.convert_from(block_ranges.at(i)))) {
        LOG_WARN("failed to convert from block range", K(ret), "block range", block_ranges.at(i));
      } else if (OB_FAIL(job_infos.push_back(job_info))) {
        LOG_WARN("failed to push back to job infos", K(ret), K(job_info));
      }
    }
  }
  return ret;
}

ObFetchReplicaPrewarmMicroBlockP::ObFetchReplicaPrewarmMicroBlockP(
    common::ObInOutBandwidthThrottle *bandwidth_throttle)
  : ObStorageStreamRpcP(bandwidth_throttle)
{
}

int ObFetchReplicaPrewarmMicroBlockP::process()
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(!arg_.is_valid())) {
    ret = OB_INVALID_ARGUMENT;
    STORAGE_LOG(WARN, "invalid argument", KR(ret), K_(arg));
  } else {
    MTL_SWITCH(arg_.tenant_id_) {
      blocksstable::ObBufferReader data;
      char *buf = nullptr;
      last_send_time_ = this->get_receive_timestamp();
      const int64_t start_us = ObTimeUtil::current_time();
      const int64_t first_receive_us = this->get_receive_timestamp();

      if (OB_ISNULL(buf = static_cast<char *>(allocator_.alloc(OB_MALLOC_BIG_BLOCK_SIZE)))) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        STORAGE_LOG(WARN, "fail to alloc data buffer.", KR(ret));
      } else if (!result_.set_data(buf, OB_MALLOC_BIG_BLOCK_SIZE)) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        STORAGE_LOG(WARN, "fail to set data to result", KR(ret));
      } else if (OB_ISNULL(bandwidth_throttle_)) {
        ret = OB_ERR_UNEXPECTED;
        STORAGE_LOG(ERROR, "bandwidth_throttle must not null", KR(ret), KP_(bandwidth_throttle));
      } else {
        SMART_VARS_2((storage::ObReplicaPrewarmMicroBlockProducer, producer),
                     (ObSSLSFetchMicroBlockArg, arg)) {
          if (OB_FAIL(arg.assign(arg_))) {
            LOG_WARN("fail to assign copy fetch micro block arg", KR(ret), K(arg_));
          } else if (OB_FAIL(producer.init(arg.micro_metas_))) {
            LOG_WARN("fail to init replica prewarm micro block producer", KR(ret), K(arg));
          } else {
            while (OB_SUCC(ret)) {
              ObSSMicroBlockCacheKeyMeta micro_meta;
              if (OB_FAIL(producer.get_next_micro_block(micro_meta, data))) {
                if (OB_ITER_END != ret) {
                  STORAGE_LOG(WARN, "fail to get next micro block", KR(ret));
                } else {
                  ret = OB_SUCCESS;
                }
                break;
              } else if (OB_FAIL(fill_data(micro_meta))) {
                STORAGE_LOG(WARN, "fail to fill data length", KR(ret), K(data.pos()), K(micro_meta));
              } else if (OB_FAIL(fill_buffer(data))) {
                STORAGE_LOG(WARN, "fail to fill data", KR(ret), K(micro_meta));
              } else {
                STORAGE_LOG(INFO, "succ to fill micro block", K(micro_meta));
              }
            }
          }
        }
      }

      const int64_t cost_us = ObTimeUtil::current_time() - start_us;
      LOG_INFO("finish fetch replica prewarm micro block", KR(ret), K(cost_us), "in rpc queue time",
              start_us - first_receive_us);
    }
  }
  return ret;
}

#endif

} //namespace obrpc

namespace storage
{

ObStorageRpc::ObStorageRpc()
    : is_inited_(false),
      rpc_proxy_(NULL),
      rs_rpc_proxy_(NULL)
{
}

ObStorageRpc::~ObStorageRpc()
{
  destroy();
}

int ObStorageRpc::init(
    obrpc::ObStorageRpcProxy *rpc_proxy,
    const common::ObAddr &self,
    obrpc::ObCommonRpcProxy *rs_rpc_proxy)
{
  int ret = OB_SUCCESS;
  if (is_inited_) {
    ret = OB_INIT_TWICE;
    STORAGE_LOG(WARN, "storage rpc has inited", K(ret));
  } else if (OB_ISNULL(rpc_proxy) || !self.is_valid() || OB_ISNULL(rs_rpc_proxy)) {
    ret = OB_INVALID_ARGUMENT;
    STORAGE_LOG(WARN, "ObStorageRpc init with invalid argument",
        KP(rpc_proxy), K(self), KP(rs_rpc_proxy));
  } else {
    rpc_proxy_ = rpc_proxy;
    self_ = self;
    rs_rpc_proxy_ = rs_rpc_proxy;
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
    rs_rpc_proxy_ = NULL;
  }
}

int ObStorageRpc::notify_restore_tablets(
      const uint64_t tenant_id,
      const ObStorageHASrcInfo &follower_info,
      const share::ObLSID &ls_id,
      const int64_t &proposal_id,
      const common::ObIArray<common::ObTabletID>& tablet_id_array,
      const share::ObLSRestoreStatus &restore_status,
      obrpc::ObNotifyRestoreTabletsResp &restore_resp)
{
  int ret = OB_SUCCESS;
  ObNotifyRestoreTabletsArg arg;
  if (!is_inited_) {
    ret = OB_NOT_INIT;
    STORAGE_LOG(WARN, "storage rpc is not inited", K(ret));
  } else if (!follower_info.is_valid() || !ls_id.is_valid()
      || (tablet_id_array.empty() && (restore_status.is_restore_tablets_meta() || restore_status.is_quick_restore() || restore_status.is_restore_major_data()))) {
    ret = OB_INVALID_ARGUMENT;
    STORAGE_LOG(WARN, "notify follower restore get invalid argument", K(ret), K(follower_info), K(ls_id), K(tablet_id_array));
  } else if (OB_FAIL(arg.tablet_id_array_.assign(tablet_id_array))) {
    STORAGE_LOG(WARN, "failed to assign tablet id array", K(ret), K(follower_info), K(ls_id), K(tablet_id_array));
  } else {
    arg.tenant_id_ = tenant_id;
    arg.ls_id_ = ls_id;
    arg.restore_status_ = restore_status;
    arg.leader_proposal_id_ = proposal_id;
    if (OB_FAIL(rpc_proxy_->to(follower_info.src_addr_).dst_cluster_id(follower_info.cluster_id_)
        .by(tenant_id)
        .group_id(share::OBCG_STORAGE)
        .notify_restore_tablets(arg, restore_resp))) {
      LOG_WARN("failed to notify follower restore tablets", K(ret), K(arg), K(follower_info), K(ls_id), K(tablet_id_array));
    } else {
      FLOG_INFO("notify follower restore tablets successfully", K(arg), K(follower_info), K(ls_id), K(tablet_id_array));
    }
  }
  return ret;
}

int ObStorageRpc::inquire_restore(
    const uint64_t tenant_id,
    const ObStorageHASrcInfo &src_info,
    const share::ObLSID &ls_id,
    const share::ObLSRestoreStatus &restore_status,
    obrpc::ObInquireRestoreResp &restore_resp)
{
  int ret = OB_SUCCESS;
  if (!is_inited_) {
    ret = OB_NOT_INIT;
    STORAGE_LOG(WARN, "storage rpc is not inited", K(ret));
  } else if (!src_info.is_valid() || !ls_id.is_valid()) {
    ret = OB_INVALID_ARGUMENT;
    STORAGE_LOG(WARN, "inquire restore get invalid argument", K(ret), K(src_info), K(ls_id));
  } else {
    ObInquireRestoreArg arg;
    arg.tenant_id_ = tenant_id;
    arg.ls_id_ = ls_id;
    arg.restore_status_ = restore_status;
    if (OB_FAIL(rpc_proxy_->to(src_info.src_addr_).dst_cluster_id(src_info.cluster_id_)
        .by(tenant_id)
        .group_id(share::OBCG_STORAGE)
        .inquire_restore(arg, restore_resp))) {
      LOG_WARN("failed to inquire restore", K(ret), K(arg), K(src_info));
    } else {
      FLOG_INFO("inquire restore status successfully", K(arg), K(src_info));
    }
  }
  return ret;
}

int ObStorageRpc::update_ls_meta(
    const uint64_t tenant_id,
    const ObStorageHASrcInfo &dest_info,
    const storage::ObLSMetaPackage &ls_meta)
{
  int ret = OB_SUCCESS;
  if (!is_inited_) {
    ret = OB_NOT_INIT;
    STORAGE_LOG(WARN, "storage rpc is not inited", K(ret));
  } else if (!dest_info.is_valid() || !ls_meta.is_valid()) {
    ret = OB_INVALID_ARGUMENT;
    STORAGE_LOG(WARN, "invalid argument", K(ret), K(dest_info), K(ls_meta));
  } else {
    ObRestoreUpdateLSMetaArg arg;
    arg.tenant_id_ = tenant_id;
    arg.ls_meta_package_ = ls_meta;
    if (OB_FAIL(rpc_proxy_->to(dest_info.src_addr_).dst_cluster_id(dest_info.cluster_id_)
        .by(tenant_id)
        .group_id(share::OBCG_STORAGE)
        .update_ls_meta(arg))) {
      LOG_WARN("failed to update ls meta", K(ret), K(dest_info), K(ls_meta));
    } else {
      FLOG_INFO("update ls meta succ", K(dest_info), K(ls_meta));
    }
  }

  return ret;
}

#ifdef OB_BUILD_SHARED_STORAGE
int ObStorageRpc::get_ls_micro_block_cache_info(
    const uint64_t tenant_id,
    const share::ObLSID &ls_id,
    const ObStorageHASrcInfo &src_info,
    ObSSLSCacheInfo &ls_cache_info)
{
  int ret = OB_SUCCESS;
  obrpc::ObGetMicroBlockCacheInfoArg arg;
  obrpc::ObGetMicroBlockCacheInfoRes res;
  ls_cache_info.reset();

  if (!is_inited_) {
    ret = OB_NOT_INIT;
    STORAGE_LOG(WARN, "storage rpc is not inited", K(ret));
  } else if (OB_INVALID_ID == tenant_id || !ls_id.is_valid() || !src_info.is_valid()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument!", K(ret), K(tenant_id), K(ls_id), K(src_info));
  } else {
    arg.tenant_id_ = tenant_id;
    arg.ls_id_ = ls_id;
    if (OB_FAIL(rpc_proxy_->to(src_info.src_addr_).dst_cluster_id(src_info.cluster_id_)
        .by(tenant_id)
        .group_id(share::OBCG_STORAGE)
        .get_micro_block_cache_info(arg, res))) {
      LOG_WARN("fail to get micro block cache info", K(ret), K(tenant_id), K(ls_id), K(src_info));
    } else {
      ls_cache_info = res.ls_cache_info_;
    }
  }
  return ret;
}

int ObStorageRpc::get_ls_migration_cache_job_info(
    const uint64_t tenant_id,
    const share::ObLSID &ls_id,
    const ObStorageHASrcInfo &src_info,
    const int64_t task_count,
    obrpc::ObGetMigrationCacheJobInfoRes &res)
{
  int ret = OB_SUCCESS;
  res.reset();
  obrpc::ObGetMigrationCacheJobInfoArg arg;

  if (!is_inited_) {
    ret = OB_NOT_INIT;
    STORAGE_LOG(WARN, "storage rpc is not inited", K(ret));
  } else if (OB_INVALID_ID == tenant_id || !ls_id.is_valid() || !src_info.is_valid() || task_count <= 0) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", K(ret), K(tenant_id), K(ls_id), K(src_info), K(task_count));
  } else {
    arg.tenant_id_ = tenant_id;
    arg.ls_id_ = ls_id;
    arg.task_count_ = task_count;
    if (OB_FAIL(rpc_proxy_->to(src_info.src_addr_).dst_cluster_id(src_info.cluster_id_)
        .by(tenant_id)
        .group_id(share::OBCG_STORAGE)
        .get_migration_cache_job_info(arg, res))) {
      LOG_WARN("fail to get migration cache job info", K(ret), K(tenant_id), K(ls_id), K(src_info));
    }
  }
  return ret;
}

int ObStorageRpc::get_micro_block_key_set(
    const uint64_t tenant_id,
    const share::ObLSID &ls_id,
    const ObStorageHASrcInfo &src_info,
    const ObMigrationCacheJobInfo &job_info,
    obrpc::ObCopyMicroBlockKeySetRes &res)
{
  int ret = OB_SUCCESS;
  res.reset();
  obrpc::ObGetMicroBlockKeyArg arg;

  if (!is_inited_) {
    ret = OB_NOT_INIT;
    STORAGE_LOG(WARN, "storage rpc is not inited", K(ret));
  } else if (OB_INVALID_ID == tenant_id || !ls_id.is_valid() || !src_info.is_valid() || !job_info.is_valid()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", K(ret), K(tenant_id), K(ls_id), K(src_info), K(job_info));
  } else {
    arg.tenant_id_ = tenant_id;
    arg.ls_id_ = ls_id;
    arg.job_info_ = job_info;
    if (OB_FAIL(rpc_proxy_->to(src_info.src_addr_).dst_cluster_id(src_info.cluster_id_)
        .by(tenant_id)
        .group_id(share::OBCG_STORAGE)
        .fetch_micro_block_keys(arg, res))) {
      LOG_WARN("fail to fetch micro block keys", K(ret), K(tenant_id), K(ls_id), K(src_info));
    }
  }
  return ret;
}
#endif
} // storage
} // oceanbase
