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
#include "ob_storage_struct.h"
#include "storage/tx/ob_trans_ctx_mgr.h"

using namespace oceanbase;
using namespace storage;
using namespace compaction;
using namespace common;
using namespace oceanbase::share::schema;
using namespace oceanbase::share;


#ifdef ERRSIM
static const char *OB_ERRSIM_POINT_TYPES[] = {
    "POINT_NONE",
    "START_BACKFILL_BEFORE",
    "REPLACE_SWAP_BEFORE",
    "REPLACE_AFTER",
};

void ObErrsimBackfillPointType::reset()
{
  type_ = ObErrsimBackfillPointType::ERRSIM_POINT_NONE;
}

bool ObErrsimBackfillPointType::is_valid() const
{
  return true;
}

bool ObErrsimBackfillPointType::operator == (const ObErrsimBackfillPointType &other) const
{
  bool is_same = true;
  if (this == &other) {
    // same
  } else {
    is_same = type_ == other.type_;
  }
  return is_same;
}

int64_t ObErrsimBackfillPointType::hash() const
{
  int64_t hash_value = 0;
  hash_value = common::murmurhash(
      &type_, sizeof(type_), hash_value);
  return hash_value;
}

int ObErrsimBackfillPointType::hash(uint64_t &hash_val) const
{
  hash_val = hash();
  return OB_SUCCESS;
}

OB_SERIALIZE_MEMBER(ObErrsimBackfillPointType, type_);

ObErrsimTransferBackfillPoint::ObErrsimTransferBackfillPoint()
  : point_type_(ObErrsimBackfillPointType::ERRSIM_MODULE_MAX),
    point_start_time_(0)
{
}

ObErrsimTransferBackfillPoint::~ObErrsimTransferBackfillPoint()
{
}

bool ObErrsimTransferBackfillPoint::is_valid() const
{
  return point_type_.is_valid() && point_start_time_ > 0;
}

int ObErrsimTransferBackfillPoint::set_point_type(const ObErrsimBackfillPointType &point_type)
{
  int ret = OB_SUCCESS;
  if (!point_type.is_valid()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("point type is invalid", K(ret), K(point_type));
  } else if (is_valid()) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("The point type is in effect, reset is not allowed", K(ret), K(point_type_), K(point_type));
  } else {
    point_type_ = point_type;
  }

  return ret;
}
int ObErrsimTransferBackfillPoint::set_point_start_time(int64_t start_time)
{
  int ret = OB_SUCCESS;
  if (start_time < 0) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("point type is invalid", K(ret), K(start_time));
  } else if (is_valid()) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("The point type is in effect, reset is not allowed", K(ret), K(point_start_time_), K(start_time));
  } else {
    point_start_time_ = start_time;
  }

  return ret;
}

void ObErrsimTransferBackfillPoint::reset()
{
  point_type_.type_ = ObErrsimBackfillPointType::ERRSIM_MODULE_MAX;
  point_start_time_ = 0;
}

bool ObErrsimTransferBackfillPoint::is_errsim_point(const ObErrsimBackfillPointType &point_type) const
{
  bool is_point = false;
  if (!is_valid()) {
    is_point = false;
  } else if (point_type.type_ == point_type_.type_) {
    is_point = true;
  } else {
    is_point = false;
  }
  return is_point;
}

#endif

OB_SERIALIZE_MEMBER(ObTabletReportStatus,
    merge_snapshot_version_,
    cur_report_version_,
    data_checksum_,
    row_count_);

OB_SERIALIZE_MEMBER(ObReportStatus,
    data_version_,
    row_count_,
    row_checksum_,
    data_checksum_,
    data_size_,
    required_size_,
    snapshot_version_);

OB_SERIALIZE_MEMBER(ObPGReportStatus,
    data_version_,
    data_size_,
    required_size_,
    snapshot_version_);

ObPartitionBarrierLogState::ObPartitionBarrierLogState()
  : state_(BARRIER_LOG_INIT), log_id_(0), scn_(), schema_version_(0)
{
}

ObPartitionBarrierLogStateEnum ObPartitionBarrierLogState::to_persistent_state() const
{
  ObPartitionBarrierLogStateEnum persistent_state = BARRIER_LOG_INIT;
  switch (state_) {
    case BARRIER_LOG_INIT:
      // fall through
    case BARRIER_LOG_WRITTING:
      persistent_state = BARRIER_LOG_INIT;
      break;
    case BARRIER_SOURCE_LOG_WRITTEN:
      persistent_state = BARRIER_SOURCE_LOG_WRITTEN;
      break;
    case BARRIER_DEST_LOG_WRITTEN:
      persistent_state = BARRIER_DEST_LOG_WRITTEN;
      break;
  }
  return persistent_state;
}






ObGetMergeTablesParam::ObGetMergeTablesParam()
  : merge_type_(INVALID_MERGE_TYPE),
    merge_version_(0)
{
}

bool ObGetMergeTablesParam::is_valid() const
{
  return is_valid_merge_type(merge_type_)
    && (!compaction::is_major_merge_type(merge_type_) || merge_version_ > 0);
}

ObGetMergeTablesResult::ObGetMergeTablesResult()
  : version_range_(),
    handle_(),
    merge_version_(),
    update_tablet_directly_(false),
    schedule_major_(false),
    is_simplified_(false),
    scn_range_(),
    error_location_(nullptr),
    snapshot_info_(),
    is_backfill_(false),
    backfill_scn_(),
    transfer_seq_(ObStorageObjectOpt::INVALID_TABLET_TRANSFER_SEQ)
{
}

bool ObGetMergeTablesResult::is_valid() const
{
  bool valid = scn_range_.is_valid()
            && (is_simplified_ || handle_.get_count() >= 1)
            && merge_version_ >= 0
            && (!is_backfill_ || backfill_scn_.is_valid());
  if (valid && GCTX.is_shared_storage_mode()) {
    valid &= (ObStorageObjectOpt::INVALID_TABLET_TRANSFER_SEQ != transfer_seq_);
  }
  return valid;
}

void ObGetMergeTablesResult::reset_handle_and_range()
{
  handle_.reset();
  version_range_.reset();
  scn_range_.reset();
}

void ObGetMergeTablesResult::simplify_handle()
{
  handle_.reset();
  is_simplified_ = true;
}

void ObGetMergeTablesResult::reset()
{
  version_range_.reset();
  handle_.reset();
  merge_version_ = ObVersionRange::MIN_VERSION;
  schedule_major_ = false;
  scn_range_.reset();
  error_location_ = nullptr;
  is_simplified_ = false;
  snapshot_info_.reset();
  is_backfill_ = false;
  backfill_scn_.reset();
  transfer_seq_ = ObStorageObjectOpt::INVALID_TABLET_TRANSFER_SEQ;
}

int ObGetMergeTablesResult::copy_basic_info(const ObGetMergeTablesResult &src)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(!src.is_valid())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", K(ret), K(src));
  } else {
    version_range_ = src.version_range_;
    merge_version_ = src.merge_version_;
    schedule_major_ = src.schedule_major_;
    scn_range_ = src.scn_range_;
    error_location_ = src.error_location_;
    is_simplified_ = src.is_simplified_;
    is_backfill_ = src.is_backfill_;
    backfill_scn_ = src.backfill_scn_;
    snapshot_info_ = src.snapshot_info_;
    transfer_seq_ = src.transfer_seq_;
  }
  return ret;
}

int ObGetMergeTablesResult::assign(const ObGetMergeTablesResult &src)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(!src.is_valid())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", K(ret), K(src));
  } else if (OB_FAIL(handle_.assign(src.handle_))) {
    LOG_WARN("failed to assign table handle", K(ret), K(src));
  } else if (OB_FAIL(copy_basic_info(src))) {
    LOG_WARN("failed to copy basic info", K(ret), K(src));
  }
  return ret;
}

share::SCN ObGetMergeTablesResult::get_merge_scn() const
{
  return is_backfill_ ? backfill_scn_ : scn_range_.end_scn_;
}

ObDDLTableStoreParam::ObDDLTableStoreParam()
  : keep_old_ddl_sstable_(true),
    update_with_major_flag_(false),
    ddl_start_scn_(SCN::min_scn()),
    ddl_commit_scn_(SCN::min_scn()),
    ddl_checkpoint_scn_(SCN::min_scn()),
    ddl_snapshot_version_(0),
    ddl_execution_id_(-1),
    data_format_version_(0),
    ddl_redo_callback_(nullptr),
    ddl_finish_callback_(nullptr),
    ddl_replay_status_(CS_REPLICA_REPLAY_NONE)
{

}


UpdateUpperTransParam::UpdateUpperTransParam()
  : new_upper_trans_(nullptr),
    last_minor_end_scn_()
{
  last_minor_end_scn_.set_min();
}

UpdateUpperTransParam::~UpdateUpperTransParam()
{
  reset();
}

void UpdateUpperTransParam::reset()
{
  new_upper_trans_ = nullptr;
  last_minor_end_scn_.set_min();
}


ObHATableStoreParam::ObHATableStoreParam()
  : transfer_seq_(-1),
    need_check_transfer_seq_(false),
    need_replace_remote_sstable_(false),
    is_only_replace_major_(false)
{}

ObHATableStoreParam::ObHATableStoreParam(
    const int64_t transfer_seq,
    const bool need_check_transfer_seq,
    const bool need_replace_remote_sstable,
    const bool is_only_replace_major)
  : transfer_seq_(transfer_seq),
    need_check_transfer_seq_(need_check_transfer_seq),
    need_replace_remote_sstable_(need_replace_remote_sstable),
    is_only_replace_major_(is_only_replace_major)
   
{}

bool ObHATableStoreParam::is_valid() const
{
  return (need_check_transfer_seq_ && transfer_seq_ >= 0) || !need_check_transfer_seq_;
}

ObCompactionTableStoreParam::ObCompactionTableStoreParam()
  : merge_type_(MERGE_TYPE_MAX),
    clog_checkpoint_scn_(SCN::min_scn()),
    major_ckm_info_(),
    need_report_(false),
    has_truncate_info_(false)
{
}

ObCompactionTableStoreParam::ObCompactionTableStoreParam(
    const compaction::ObMergeType merge_type,
    const share::SCN clog_checkpoint_scn,
    const bool need_report,
    const bool has_truncate_info)
  : merge_type_(merge_type),
    clog_checkpoint_scn_(clog_checkpoint_scn),
    major_ckm_info_(),
    need_report_(need_report),
    has_truncate_info_(has_truncate_info)
{
}

bool ObCompactionTableStoreParam::is_valid() const
{
  return clog_checkpoint_scn_.is_valid() && major_ckm_info_.is_valid();
}

int ObCompactionTableStoreParam::assign(
  const ObCompactionTableStoreParam &other,
  ObArenaAllocator *allocator)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(!other.is_valid())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", KR(ret), K(other));
  } else if (OB_FAIL(major_ckm_info_.assign(other.major_ckm_info_, allocator))) {
    LOG_WARN("failed to assign major ckm info", KR(ret), K(other));
  } else {
    merge_type_ = other.merge_type_;
    clog_checkpoint_scn_ = other.clog_checkpoint_scn_;
    need_report_ = other.need_report_;
    has_truncate_info_ = other.has_truncate_info_;
  }
  return ret;
}

int64_t ObCompactionTableStoreParam::get_report_scn() const
{
  int64_t report_scn = 0;
  if (need_report_
      && !major_ckm_info_.is_empty()
      && is_output_exec_mode(major_ckm_info_.get_exec_mode())) {
    report_scn = major_ckm_info_.get_compaction_scn();
  }
  return report_scn;
}

bool ObCompactionTableStoreParam::is_valid_with_sstable(const bool have_sstable) const
{
  return is_valid() && (!have_sstable || major_ckm_info_.is_empty());
}

ObUpdateTableStoreParam::ObUpdateTableStoreParam()
    : compaction_info_(),
      ddl_info_(),
      ha_info_(),
      snapshot_version_(ObVersionRange::MIN_VERSION),
      multi_version_start_(ObVersionRange::MIN_VERSION),
      storage_schema_(NULL),
      rebuild_seq_(-1),
      sstable_(NULL),
      allow_duplicate_sstable_(false),
      upper_trans_param_()
{
}

ObUpdateTableStoreParam::ObUpdateTableStoreParam(
    const int64_t snapshot_version,
    const int64_t multi_version_start,
    const ObStorageSchema *storage_schema,
    const int64_t rebuild_seq,
    const UpdateUpperTransParam upper_trans_param)
  : compaction_info_(),
    ddl_info_(),
    ha_info_(),
    snapshot_version_(snapshot_version),
    multi_version_start_(multi_version_start),
    storage_schema_(storage_schema),
    rebuild_seq_(rebuild_seq),
    sstable_(NULL),
    allow_duplicate_sstable_(false),
    upper_trans_param_(upper_trans_param)
{
}

ObUpdateTableStoreParam::ObUpdateTableStoreParam(
    const int64_t snapshot_version,
    const int64_t multi_version_start,
    const ObStorageSchema *storage_schema,
    const int64_t rebuild_seq,
    const blocksstable::ObSSTable *sstable,
    const bool allow_duplicate_sstable,
    const bool need_wait_check_flag)
    : compaction_info_(),
      ddl_info_(),
      ha_info_(),
      snapshot_version_(snapshot_version),
      multi_version_start_(multi_version_start),
      storage_schema_(storage_schema),
      rebuild_seq_(rebuild_seq),
      sstable_(sstable),
      allow_duplicate_sstable_(allow_duplicate_sstable),
      upper_trans_param_()
{
}

bool ObUpdateTableStoreParam::is_valid() const
{
  bool bret = false;
  bret = multi_version_start_ >= ObVersionRange::MIN_VERSION
      && snapshot_version_ >= ObVersionRange::MIN_VERSION
      && nullptr != storage_schema_
      && storage_schema_->is_valid()
      && rebuild_seq_ >= 0
      && compaction_info_.is_valid_with_sstable(NULL != sstable_/*have_sstable*/)
      && ha_info_.is_valid();
  return bret;
}

bool ObUpdateTableStoreParam::need_report_major() const
{
  return compaction_info_.need_report_
    && ((nullptr != sstable_ && sstable_->is_major_sstable())
        || compaction_info_.get_report_scn() > 0);
}


int ObUpdateTableStoreParam::init_with_compaction_info(
  const ObCompactionTableStoreParam &input_param,
  ObArenaAllocator *allocator)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(!input_param.is_valid())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", KR(ret), K(input_param));
  } else if (OB_FAIL(compaction_info_.assign(input_param))) {
    LOG_WARN("failed to assign compaction info", KR(ret));
  }
  return ret;
}

int ObUpdateTableStoreParam::init_with_ha_info(const ObHATableStoreParam &ha_param)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(!ha_param.is_valid())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", KR(ret), K(ha_param));
  } else {
    ha_info_ = ha_param;
  }
  return ret;
}

ObBatchUpdateTableStoreParam::ObBatchUpdateTableStoreParam()
  : tables_handle_(),
#ifdef ERRSIM
    errsim_point_info_(),
#endif
    rebuild_seq_(OB_INVALID_VERSION),
    is_transfer_replace_(false),
    start_scn_(SCN::min_scn()),
    tablet_meta_(nullptr),
    restore_status_(ObTabletRestoreStatus::FULL),
    tablet_split_param_(),
    tablet_fork_param_(),
    need_replace_remote_sstable_(false),
    release_mds_scn_()
{
}

void ObBatchUpdateTableStoreParam::reset()
{
  tables_handle_.reset();
  rebuild_seq_ = OB_INVALID_VERSION;
  is_transfer_replace_ = false;
  start_scn_.set_min();
  tablet_meta_ = nullptr;
  restore_status_ = ObTabletRestoreStatus::FULL;
  tablet_split_param_.reset();
  tablet_fork_param_.reset();
  need_replace_remote_sstable_ = false;
  release_mds_scn_.reset();
}

bool ObBatchUpdateTableStoreParam::is_valid() const
{
  return rebuild_seq_ > OB_INVALID_VERSION
      && ObTabletRestoreStatus::is_valid(restore_status_)
      && release_mds_scn_.is_valid();
}



ObSplitTableStoreParam::ObSplitTableStoreParam()
  : snapshot_version_(-1),
    multi_version_start_(-1),
    merge_type_(INVALID_MERGE_TYPE),
    skip_split_keys_()
{
}

ObSplitTableStoreParam::~ObSplitTableStoreParam()
{
  reset();
}

bool ObSplitTableStoreParam::is_valid() const
{
  return snapshot_version_ > -1
    && multi_version_start_ >= 0
    && is_valid_merge_type(merge_type_);
}

void ObSplitTableStoreParam::reset()
{
  snapshot_version_ = -1;
  multi_version_start_ = -1;
  merge_type_ = INVALID_MERGE_TYPE;
  skip_split_keys_.reset();
}

ObForkTableStoreParam::ObForkTableStoreParam()
  : snapshot_version_(-1),
    multi_version_start_(-1),
    merge_type_(INVALID_MERGE_TYPE),
    clog_checkpoint_scn_(),
    mds_checkpoint_scn_()
{
}

ObForkTableStoreParam::~ObForkTableStoreParam()
{
  reset();
}

bool ObForkTableStoreParam::is_valid() const
{
  return snapshot_version_ > -1
    && multi_version_start_ >= 0
    && is_valid_merge_type(merge_type_)
    && clog_checkpoint_scn_.is_valid()
    && mds_checkpoint_scn_.is_valid();
}

void ObForkTableStoreParam::reset()
{
  snapshot_version_ = -1;
  multi_version_start_ = -1;
  merge_type_ = INVALID_MERGE_TYPE;
  clog_checkpoint_scn_.reset();
  mds_checkpoint_scn_.reset();
}

ObPartitionReadableInfo::ObPartitionReadableInfo()
  : min_log_service_ts_(0),
    min_trans_service_ts_(0),
    min_replay_engine_ts_(0),
    generated_ts_(0),
    max_readable_ts_(OB_INVALID_TIMESTAMP),
    force_(false)
{
}

ObPartitionReadableInfo::~ObPartitionReadableInfo()
{
}




ObTabletSplitTscInfo::ObTabletSplitTscInfo()
  : start_partkey_(),
    end_partkey_(),
    is_split_dst_(),
    split_cnt_(0),
    split_type_(ObTabletSplitType::MAX_TYPE),
    partkey_is_rowkey_prefix_(false)
{
}

bool ObTabletSplitTscInfo::is_split_dst_with_partkey() const
{
  return start_partkey_.is_valid() 
      && end_partkey_.is_valid()
      && is_split_dst_
      && split_type_ < ObTabletSplitType::MAX_TYPE;
}

// e.g., lob split dst tablet
bool ObTabletSplitTscInfo::is_split_dst_without_partkey() const
{
  return !start_partkey_.is_valid()
      && !end_partkey_.is_valid()
      && is_split_dst_
      && split_type_ < ObTabletSplitType::MAX_TYPE;
}

void ObTabletSplitTscInfo::reset()
{
  start_partkey_.reset();
  end_partkey_.reset();
  is_split_dst_ = false;
  split_type_ = ObTabletSplitType::MAX_TYPE;
  split_cnt_ = 0;
  partkey_is_rowkey_prefix_ = false;
}


ObRebuildListener::ObRebuildListener(transaction::ObLSTxCtxMgr &mgr)
  : ls_tx_ctx_mgr_(mgr)
{
  int tmp_ret = OB_SUCCESS;
  while (OB_SUCCESS != (tmp_ret = ls_tx_ctx_mgr_.lock_minor_merge_lock())) {
    STORAGE_LOG_RET(ERROR, tmp_ret, "lock minor merge lock failed, we need retry forever", K(tmp_ret));
  }
}

ObRebuildListener::~ObRebuildListener()
{
  int tmp_ret = OB_SUCCESS;
  while (OB_SUCCESS != (tmp_ret = ls_tx_ctx_mgr_.unlock_minor_merge_lock())) {
    STORAGE_LOG_RET(ERROR, tmp_ret, "unlock minor merge lock failed, we need retry forever", K(tmp_ret));
  }
}


/***********************ObBackupRestoreTableSchemaChecker***************************/


