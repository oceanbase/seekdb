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

#include "storage/tablet/ob_tablet_meta.h"

#include "storage/tablet/ob_tablet_binding_info.h"

namespace oceanbase
{
using namespace share;
using namespace blocksstable;
using namespace palf;
using namespace transaction;
namespace storage
{
const SCN ObTabletMeta::INIT_CLOG_CHECKPOINT_SCN = SCN::base_scn();
const SCN ObTabletMeta::INVALID_CREATE_SCN = SCN::min_scn();
// multi source transaction leader has no log scn when first time register
// create tablet buffer, so the init create scn for tablet could be -1.
const SCN ObTabletMeta::INIT_CREATE_SCN = SCN::invalid_scn();

ObTabletMeta::ObTabletMeta()
  : version_(TABLET_META_VERSION),
    length_(0),
    tablet_id_(),
    data_tablet_id_(),
    ref_tablet_id_(),
    create_scn_(ObTabletMeta::INVALID_CREATE_SCN),
    start_scn_(),
    clog_checkpoint_scn_(),
    ddl_checkpoint_scn_(SCN::min_scn()),
    snapshot_version_(OB_INVALID_TIMESTAMP),
    multi_version_start_(OB_INVALID_TIMESTAMP),
    restore_state_(),
    report_status_(),
    table_store_flag_(),
    ddl_start_scn_(SCN::min_scn()),
    ddl_snapshot_version_(OB_INVALID_TIMESTAMP),
    max_sync_storage_schema_version_(0),
    ddl_execution_id_(-1),
    ddl_data_format_version_(0),
    max_serialized_medium_scn_(0),
    ddl_commit_scn_(SCN::min_scn()),
    mds_checkpoint_scn_(),
    extra_medium_info_(),
    last_persisted_committed_tablet_status_(),
    space_usage_(),
    create_schema_version_(0),
    has_next_tablet_(false),
    is_empty_shell_(false),
    micro_index_clustered_(false),
    fork_info_(),
    has_truncate_info_(false),
    is_inited_(false)
{
}

ObTabletMeta::~ObTabletMeta()
{
  reset();
}

int ObTabletMeta::init(
    const ObTabletMeta &old_tablet_meta,
    const int64_t snapshot_version,
    const int64_t multi_version_start,
    const int64_t max_sync_storage_schema_version,
    const SCN clog_checkpoint_scn,
    const ObDDLTableStoreParam &ddl_info,
    const bool has_truncate_info)
{
  int ret = OB_SUCCESS;

  if (OB_UNLIKELY(is_inited_)) {
    ret = OB_INIT_TWICE;
    LOG_WARN("init twice", K(ret), K_(is_inited));
  } else if (OB_UNLIKELY(!old_tablet_meta.is_valid())
      || OB_UNLIKELY(OB_INVALID_VERSION == max_sync_storage_schema_version)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid args", K(ret), K(old_tablet_meta), K(max_sync_storage_schema_version));
  } else {
    version_ = TABLET_META_VERSION;
    tablet_id_ = old_tablet_meta.tablet_id_;
    data_tablet_id_ = old_tablet_meta.data_tablet_id_;
    ref_tablet_id_ = old_tablet_meta.ref_tablet_id_;
    create_scn_ = old_tablet_meta.create_scn_;
    create_schema_version_ = old_tablet_meta.create_schema_version_;
    micro_index_clustered_ = old_tablet_meta.micro_index_clustered_;
    start_scn_ = old_tablet_meta.start_scn_;
    ddl_start_scn_ = SCN::max(ddl_info.ddl_start_scn_, old_tablet_meta.ddl_start_scn_);
    ddl_commit_scn_ = SCN::max(ddl_info.ddl_commit_scn_, old_tablet_meta.ddl_commit_scn_);
    clog_checkpoint_scn_ = SCN::max(clog_checkpoint_scn, old_tablet_meta.clog_checkpoint_scn_);
    restore_state_ = old_tablet_meta.restore_state_;
    report_status_ = old_tablet_meta.report_status_;
    snapshot_version_ = MAX(snapshot_version, old_tablet_meta.snapshot_version_);
    multi_version_start_ = MIN(MAX(multi_version_start, old_tablet_meta.multi_version_start_), snapshot_version_);
    table_store_flag_ = old_tablet_meta.table_store_flag_;
    max_sync_storage_schema_version_ = max_sync_storage_schema_version;
    max_serialized_medium_scn_ = 0;
    ddl_checkpoint_scn_ = SCN::max(old_tablet_meta.ddl_checkpoint_scn_, ddl_info.ddl_checkpoint_scn_);
    ddl_snapshot_version_ = MAX(old_tablet_meta.ddl_snapshot_version_, ddl_info.ddl_snapshot_version_);
    ddl_execution_id_ = MAX(old_tablet_meta.ddl_execution_id_, ddl_info.ddl_execution_id_);
    ddl_data_format_version_ = MAX(old_tablet_meta.ddl_data_format_version_, ddl_info.data_format_version_);
    mds_checkpoint_scn_ = old_tablet_meta.mds_checkpoint_scn_;
    extra_medium_info_ = old_tablet_meta.extra_medium_info_;
    space_usage_ = old_tablet_meta.space_usage_;
    has_truncate_info_ = has_truncate_info || old_tablet_meta.has_truncate_info_;
    fork_info_ = old_tablet_meta.fork_info_;
    if (OB_FAIL(last_persisted_committed_tablet_status_.assign(
        old_tablet_meta.last_persisted_committed_tablet_status_))) {
      LOG_WARN("fail to init last persisted committed tablet status", K(ret), K(old_tablet_meta));
    }
  }

  if (OB_SUCC(ret)) {
    is_inited_ = true;
  }
  if (OB_UNLIKELY(!is_inited_)) {
    reset();
  }
  return ret;
}

int ObTabletMeta::init(
    const common::ObTabletID &tablet_id,
    const common::ObTabletID &data_tablet_id,
    const share::SCN create_scn,
    const int64_t snapshot_version,
    const ObTabletTableStoreFlag &table_store_flag,
    const int64_t create_schema_version,
    const share::SCN &clog_checkpoint_scn,
    const share::SCN &mds_checkpoint_scn,
    const bool micro_index_clustered,
    const bool has_truncate_info,
    const share::ObForkTabletInfo &fork_info)
{
  int ret = OB_SUCCESS;

  if (OB_UNLIKELY(is_inited_)) {
    ret = OB_INIT_TWICE;
    LOG_WARN("init twice", K(ret), K_(is_inited));
  } else if (OB_UNLIKELY(!tablet_id.is_valid())
      || OB_UNLIKELY(!data_tablet_id.is_valid())
      //|| OB_UNLIKELY(create_scn <= OB_INVALID_TIMESTAMP)
      || OB_UNLIKELY(OB_INVALID_VERSION == snapshot_version)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid args", K(ret), K(tablet_id), K(data_tablet_id),
        K(create_scn), K(snapshot_version), K(clog_checkpoint_scn));
  } else if (OB_FAIL(restore_state_.init_status())) {
    LOG_WARN("failed to init ha status", K(ret));
  } else {
    version_ = TABLET_META_VERSION;
    tablet_id_ = tablet_id;
    data_tablet_id_ = data_tablet_id;
    create_scn_ = create_scn;
    create_schema_version_ = create_schema_version;
    micro_index_clustered_ = micro_index_clustered;
    start_scn_ = INIT_CLOG_CHECKPOINT_SCN;
    clog_checkpoint_scn_ = clog_checkpoint_scn.is_valid() ? clog_checkpoint_scn : INIT_CLOG_CHECKPOINT_SCN;
    mds_checkpoint_scn_ = mds_checkpoint_scn.is_valid() ? mds_checkpoint_scn : INIT_CLOG_CHECKPOINT_SCN;

    ddl_checkpoint_scn_ = INIT_CLOG_CHECKPOINT_SCN;
    snapshot_version_ = snapshot_version;
    multi_version_start_ = snapshot_version;
    table_store_flag_ = table_store_flag;
    ddl_commit_scn_.set_min();
    ddl_snapshot_version_ = 0;
    max_sync_storage_schema_version_ = create_schema_version;
    ddl_data_format_version_ = 0;
    mds_checkpoint_scn_ = mds_checkpoint_scn.is_valid() ? mds_checkpoint_scn : INIT_CLOG_CHECKPOINT_SCN;
    fork_info_ = fork_info;
    has_truncate_info_ = has_truncate_info;
    report_status_.merge_snapshot_version_ = snapshot_version;
    report_status_.cur_report_version_ = snapshot_version;
    report_status_.data_checksum_ = 0;
    report_status_.row_count_ = 0;

    if (OB_FAIL(ret)) {
    } else {
      /* for shared nothing mode, ddl start scn default is min() */
      ddl_start_scn_.set_min();
      ddl_execution_id_ = -1;
    }
    if (OB_FAIL(ret)) {
    } else {
      if (tablet_id_.is_ls_inner_tablet()) {
        last_persisted_committed_tablet_status_.tablet_status_ = ObTabletStatus::NORMAL;
        last_persisted_committed_tablet_status_.data_type_ = ObTabletMdsUserDataType::CREATE_TABLET;
      } else {
        last_persisted_committed_tablet_status_.reset();
      }
      is_inited_ = true;      
    }

    if (OB_FAIL(ret)) {
    } else if (fork_info.is_valid()) {
      // Background:
      // If source/target tablets minor-freeze concurrently, their latest minor sstable may have the
      // same SCN range, which leads to SCN overlap during merge. It's forbidden for fork table,
      // so we pin start_scn_/clog_checkpoint_scn_ to fork_snapshot_version to avoid overlap.
      // Note: mds_checkpoint_scn_ should remain INIT_CLOG_CHECKPOINT_SCN for fork table,
      // because MDS data will not be forked.
      share::SCN fork_snapshot_scn;
      if (OB_FAIL(fork_snapshot_scn.convert_for_tx(fork_info.get_fork_snapshot_version()))) {
        LOG_WARN("failed to convert fork_snapshot_version to SCN", K(ret), K(fork_info));
      } else {
        start_scn_ = fork_snapshot_scn;
        clog_checkpoint_scn_ = fork_snapshot_scn;
        mds_checkpoint_scn_ = INIT_CLOG_CHECKPOINT_SCN;
      }
    }
  }

  if (OB_UNLIKELY(!is_inited_)) {
    reset();
  }
  return ret;
}

int ObTabletMeta::init(
    const ObTabletMeta &old_tablet_meta,
    const int64_t snapshot_version,
    const int64_t multi_version_start,
    const int64_t max_sync_storage_schema_version,
    const share::SCN &clog_checkpoint_scn,
    const share::SCN &mds_checkpoint_scn,
    const share::ObForkTabletInfo &fork_info)
{
  int ret = OB_SUCCESS;

  if (OB_UNLIKELY(is_inited_)) {
    ret = OB_INIT_TWICE;
    LOG_WARN("init twice", K(ret), K_(is_inited));
  } else if (OB_UNLIKELY(!old_tablet_meta.is_valid())
      || OB_UNLIKELY(OB_INVALID_VERSION == max_sync_storage_schema_version)
      || OB_UNLIKELY(!clog_checkpoint_scn.is_valid())
      || OB_UNLIKELY(!mds_checkpoint_scn.is_valid())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid args", K(ret), K(old_tablet_meta), K(max_sync_storage_schema_version), 
        K(clog_checkpoint_scn), K(mds_checkpoint_scn));
  } else {
    version_ = TABLET_META_VERSION;
    tablet_id_ = old_tablet_meta.tablet_id_;
    data_tablet_id_ = old_tablet_meta.data_tablet_id_;
    ref_tablet_id_ = old_tablet_meta.ref_tablet_id_;
    create_scn_ = old_tablet_meta.create_scn_;
    create_schema_version_ = old_tablet_meta.create_schema_version_;
    micro_index_clustered_ = old_tablet_meta.micro_index_clustered_;
    start_scn_ = old_tablet_meta.start_scn_;
    ddl_start_scn_ = old_tablet_meta.ddl_start_scn_;
    ddl_commit_scn_ = old_tablet_meta.ddl_commit_scn_;
    clog_checkpoint_scn_ = SCN::max(clog_checkpoint_scn, old_tablet_meta.clog_checkpoint_scn_);
    restore_state_ = old_tablet_meta.restore_state_;
    report_status_ = old_tablet_meta.report_status_;

    snapshot_version_ = MAX(snapshot_version, old_tablet_meta.snapshot_version_);
    multi_version_start_ = MIN(MAX(multi_version_start, old_tablet_meta.multi_version_start_), snapshot_version_);
    table_store_flag_ = old_tablet_meta.table_store_flag_;
    max_sync_storage_schema_version_ = max_sync_storage_schema_version;
    max_serialized_medium_scn_ = 0; // abandoned
    ddl_checkpoint_scn_ = old_tablet_meta.ddl_checkpoint_scn_;
    ddl_snapshot_version_ = old_tablet_meta.ddl_snapshot_version_;
    ddl_execution_id_ = old_tablet_meta.ddl_execution_id_;
    ddl_data_format_version_ = old_tablet_meta.ddl_data_format_version_;
    mds_checkpoint_scn_ = old_tablet_meta.mds_checkpoint_scn_;
    extra_medium_info_ = old_tablet_meta.extra_medium_info_;
    space_usage_ = old_tablet_meta.space_usage_;
    has_truncate_info_ = old_tablet_meta.has_truncate_info_;
    fork_info_ = fork_info;
    if (OB_FAIL(last_persisted_committed_tablet_status_.assign(old_tablet_meta.last_persisted_committed_tablet_status_))) {
      LOG_WARN("fail to init last_persisted_committed_tablet_status from old tablet meta", K(ret),
          "last_persisted_committed_tablet_status", old_tablet_meta.last_persisted_committed_tablet_status_);
    }
  }

  if (OB_SUCC(ret)) {
    is_inited_ = true;
  }

  if (OB_UNLIKELY(!is_inited_)) {
    reset();
  }
  return ret;
}

int ObTabletMeta::init(
    const ObTabletMeta &old_tablet_meta,
    const share::SCN &flush_scn)
{
  int ret = OB_SUCCESS;

  if (OB_UNLIKELY(is_inited_)) {
    ret = OB_INIT_TWICE;
    LOG_WARN("init twice", K(ret), K_(is_inited));
  } else if (OB_UNLIKELY(!old_tablet_meta.is_valid())
      || OB_UNLIKELY(!flush_scn.is_valid())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid args", K(ret), K(old_tablet_meta), K(flush_scn));
  } else {
    version_ = TABLET_META_VERSION;
    tablet_id_ = old_tablet_meta.tablet_id_;
    data_tablet_id_ = old_tablet_meta.data_tablet_id_;
    ref_tablet_id_ = old_tablet_meta.ref_tablet_id_;
    create_scn_ = old_tablet_meta.create_scn_;
    create_schema_version_ = old_tablet_meta.create_schema_version_;
    micro_index_clustered_ = old_tablet_meta.micro_index_clustered_;
    start_scn_ = old_tablet_meta.start_scn_;
    clog_checkpoint_scn_ = old_tablet_meta.clog_checkpoint_scn_;
    ddl_checkpoint_scn_ = old_tablet_meta.ddl_checkpoint_scn_;
    snapshot_version_ = old_tablet_meta.snapshot_version_;
    multi_version_start_ = old_tablet_meta.multi_version_start_;
    restore_state_ = old_tablet_meta.restore_state_;
    report_status_ = old_tablet_meta.report_status_;
    table_store_flag_ = old_tablet_meta.table_store_flag_;
    ddl_start_scn_ = old_tablet_meta.ddl_start_scn_;
    ddl_commit_scn_ = old_tablet_meta.ddl_commit_scn_;
    ddl_execution_id_ = old_tablet_meta.ddl_execution_id_;
    ddl_data_format_version_ = old_tablet_meta.ddl_data_format_version_;
    ddl_snapshot_version_ = old_tablet_meta.ddl_snapshot_version_;
    max_sync_storage_schema_version_ = old_tablet_meta.max_sync_storage_schema_version_;
    max_serialized_medium_scn_ = old_tablet_meta.max_serialized_medium_scn_;
    mds_checkpoint_scn_ = SCN::max(flush_scn, old_tablet_meta.mds_checkpoint_scn_);
    extra_medium_info_ = old_tablet_meta.extra_medium_info_;
    space_usage_ = old_tablet_meta.space_usage_;
    has_truncate_info_ = old_tablet_meta.has_truncate_info_;
    fork_info_ = old_tablet_meta.fork_info_;
    if (OB_FAIL(last_persisted_committed_tablet_status_.assign(old_tablet_meta.last_persisted_committed_tablet_status_))) {
      LOG_WARN("fail to init last_persisted_committed_tablet_status from old tablet meta", K(ret),
          "last_persisted_committed_tablet_status", old_tablet_meta.last_persisted_committed_tablet_status_);
    }
  }

  if (OB_SUCC(ret)) {
    is_inited_ = true;
  }

  if (OB_UNLIKELY(!is_inited_)) {
    reset();
  }

  return ret;
}

int ObTabletMeta::assign(const ObTabletMeta &other)
{
  int ret = OB_SUCCESS;

  if (OB_UNLIKELY(is_inited_)) {
    ret = OB_INIT_TWICE;
    LOG_WARN("init twice", K(ret), K_(is_inited));
  } else if (OB_UNLIKELY(!other.is_valid())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid args", K(ret), K(other));
  } else {
    version_ = other.version_;
    length_ = other.length_;
    tablet_id_ = other.tablet_id_;
    data_tablet_id_ = other.data_tablet_id_;
    ref_tablet_id_ = other.ref_tablet_id_;
    create_scn_ = other.create_scn_;
    start_scn_ = other.start_scn_;
    clog_checkpoint_scn_ = other.clog_checkpoint_scn_;
    ddl_checkpoint_scn_ = other.ddl_checkpoint_scn_;
    snapshot_version_ = other.snapshot_version_;
    multi_version_start_ = other.multi_version_start_;
    restore_state_ = other.restore_state_;
    report_status_ = other.report_status_;
    table_store_flag_ = other.table_store_flag_;
    ddl_start_scn_ = other.ddl_start_scn_;
    ddl_snapshot_version_ = other.ddl_snapshot_version_;
    max_sync_storage_schema_version_ = other.max_sync_storage_schema_version_;
    ddl_execution_id_ = other.ddl_execution_id_;
    ddl_data_format_version_ = other.ddl_data_format_version_;
    max_serialized_medium_scn_ = other.max_serialized_medium_scn_;
    ddl_commit_scn_ = other.ddl_commit_scn_;
    mds_checkpoint_scn_ = other.mds_checkpoint_scn_;
    extra_medium_info_ = other.extra_medium_info_;
    space_usage_ = other.space_usage_;
    create_schema_version_ = other.create_schema_version_;
    has_next_tablet_ = other.has_next_tablet_;
    is_empty_shell_ = other.is_empty_shell_;
    micro_index_clustered_ = other.micro_index_clustered_;
    has_truncate_info_ = other.has_truncate_info_;
    fork_info_ = other.fork_info_;
    if (OB_FAIL(last_persisted_committed_tablet_status_.assign(other.last_persisted_committed_tablet_status_))) {
      LOG_WARN("fail to init last_persisted_committed_tablet_status from old tablet meta", K(ret),
          "last_persisted_committed_tablet_status", other.last_persisted_committed_tablet_status_);
    }
  }

  if (OB_SUCC(ret)) {
    is_inited_ = true;
  }

  if (OB_UNLIKELY(!is_inited_)) {
    reset();
  }
  return ret;
}

void ObTabletMeta::reset()
{
  version_ = 0;
  tablet_id_.reset();
  data_tablet_id_.reset();
  ref_tablet_id_.reset();
  has_next_tablet_ = false;
  create_scn_ = ObTabletMeta::INVALID_CREATE_SCN;
  create_schema_version_ = 0;
  micro_index_clustered_ = false;
  start_scn_.reset();
  clog_checkpoint_scn_.reset();
  ddl_checkpoint_scn_.set_min();
  snapshot_version_ = OB_INVALID_TIMESTAMP;
  multi_version_start_ = OB_INVALID_TIMESTAMP;
  restore_state_.reset();
  report_status_.reset();
  table_store_flag_.reset();
  ddl_start_scn_.set_min();
  ddl_commit_scn_.set_min();
  ddl_snapshot_version_ = OB_INVALID_TIMESTAMP;
  max_sync_storage_schema_version_ = 0;
  max_serialized_medium_scn_ = 0;
  ddl_execution_id_ = -1;
  ddl_data_format_version_ = 0;
  mds_checkpoint_scn_.reset();
  is_empty_shell_ = false;
  extra_medium_info_.reset();
  last_persisted_committed_tablet_status_.reset();
  space_usage_.reset();
  has_truncate_info_ = false;
  fork_info_.reset();
  is_inited_ = false;
}

bool ObTabletMeta::is_valid() const
{
  // TODO: add more check
  return tablet_id_.is_valid()
      && data_tablet_id_.is_valid()
      && create_scn_ != INVALID_CREATE_SCN
      && multi_version_start_ >= 0
      && multi_version_start_ <= snapshot_version_
      && snapshot_version_ >= 0
      && snapshot_version_ != INT64_MAX
      && max_sync_storage_schema_version_ >= 0
      && max_serialized_medium_scn_ >= 0
      && restore_state_.is_valid()
      && last_persisted_committed_tablet_status_.is_valid()
      && (restore_state_.is_restore_status_pending()
          || (!restore_state_.is_restore_status_pending()
              && clog_checkpoint_scn_ >= INIT_CLOG_CHECKPOINT_SCN
              && start_scn_ >= INIT_CLOG_CHECKPOINT_SCN
              && start_scn_ <= clog_checkpoint_scn_))
      && create_schema_version_ >= 0
      && space_usage_.is_valid();
}

int ObTabletMeta::serialize(char *buf, const int64_t len, int64_t &pos) const
{
  int ret = OB_SUCCESS;
  int64_t new_pos = pos;
  const int64_t length = get_serialize_size();

  if (OB_UNLIKELY(!is_inited_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("not inited", K(ret), K_(is_inited));
  } else if (OB_ISNULL(buf)
      || OB_UNLIKELY(len <= 0)
      || OB_UNLIKELY(pos < 0)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid args", K(ret), K(buf), K(len), K(pos));
  } else if (OB_UNLIKELY(!is_valid())) {
    ret = OB_ERR_UNEXPECTED;
    LOG_ERROR("tablet meta is invalid", K(ret), K(*this));
  } else if (TABLET_META_VERSION != version_) {
    ret = OB_ERR_UNEXPECTED;
    LOG_ERROR("invalid version", K(ret), K_(version));
  } else if (OB_UNLIKELY(length > len - pos)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("buffer's length is not enough", K(ret), K(length), K(len - new_pos));
  } else if (OB_FAIL(serialization::encode_i32(buf, len, new_pos, version_))) {
    LOG_WARN("failed to serialize tablet meta's version", K(ret), K(len), K(new_pos), K_(version));
  } else if (new_pos - pos < length && OB_FAIL(serialization::encode_i32(buf, len, new_pos, length))) {
    LOG_WARN("failed to serialize tablet meta's length", K(ret), K(len), K(new_pos), K(length));
  } else if (new_pos - pos < length && OB_FAIL(tablet_id_.serialize(buf, len, new_pos))) {
    LOG_WARN("failed to serialize tablet id", K(ret), K(len), K(new_pos));
  } else if (new_pos - pos < length && OB_FAIL(data_tablet_id_.serialize(buf, len, new_pos))) {
    LOG_WARN("failed to serialize data tablet id", K(ret), K(len), K(new_pos));
  } else if (new_pos - pos < length && OB_FAIL(ref_tablet_id_.serialize(buf, len, new_pos))) {
    LOG_WARN("failed to serialize ref tablet id", K(ret), K(len), K(new_pos));
  } else if (new_pos - pos < length && OB_FAIL(serialization::encode_bool(buf, len, new_pos, has_next_tablet_))) {
    LOG_WARN("failed to serialize has next tablet", K(ret), K(len), K(new_pos), K_(has_next_tablet));
  } else if (new_pos - pos < length && OB_FAIL(create_scn_.fixed_serialize(buf, len, new_pos))) {
    LOG_WARN("failed to serialize create scn", K(ret), K(len), K(new_pos), K_(create_scn));
  } else if (new_pos - pos < length && OB_FAIL(start_scn_.fixed_serialize(buf, len, new_pos))) {
    LOG_WARN("failed to serialize start scn", K(ret), K(len), K(new_pos), K_(start_scn));
  } else if (new_pos - pos < length && OB_FAIL(clog_checkpoint_scn_.fixed_serialize(buf, len, new_pos))) {
    LOG_ERROR("failed to serialize clog checkpoint ts", K(ret), K(len), K(new_pos), K_(clog_checkpoint_scn));
  } else if (new_pos - pos < length && OB_FAIL(ddl_checkpoint_scn_.fixed_serialize(buf, len, new_pos))) {
    LOG_ERROR("failed to serialize ddl checkpoint ts", K(ret), K(len), K(new_pos), K_(ddl_checkpoint_scn));
  } else if (new_pos - pos < length && OB_FAIL(serialization::encode_i64(buf, len, new_pos, snapshot_version_))) {
    LOG_WARN("failed to serialize snapshot version", K(ret), K(len), K(new_pos), K_(snapshot_version));
  } else if (new_pos - pos < length && OB_FAIL(serialization::encode_i64(buf, len, new_pos, multi_version_start_))) {
    LOG_WARN("failed to serialize multi version start", K(ret), K(len), K(new_pos), K_(multi_version_start));
  } else if (new_pos - pos < length && OB_FAIL(restore_state_.serialize(buf, len, new_pos))) {
    LOG_WARN("failed to serialize ha status", K(ret), K(len), K(new_pos));
  } else if (new_pos - pos < length && OB_FAIL(report_status_.serialize(buf, len, new_pos))) {
    LOG_WARN("failed to serialize report status", K(ret), K(len), K(new_pos));
  } else if (new_pos - pos < length && OB_FAIL(table_store_flag_.serialize(buf, len, new_pos))) {
    LOG_WARN("failed to serialize table store flag", K(ret), K(len), K(new_pos));
  } else if (new_pos - pos < length && OB_FAIL(ddl_start_scn_.fixed_serialize(buf, len, new_pos))) {
    LOG_ERROR("failed to serialize ddl start log ts", K(ret), K(len), K(new_pos), K_(ddl_start_scn));
  } else if (new_pos - pos < length && OB_FAIL(serialization::encode_i64(buf, len, new_pos, ddl_snapshot_version_))) {
    LOG_WARN("failed to serialize ddl snapshot version", K(ret), K(len), K(new_pos), K_(ddl_snapshot_version));
  } else if (new_pos - pos < length && OB_FAIL(serialization::encode_i64(buf, len, new_pos, max_sync_storage_schema_version_))) {
    LOG_WARN("failed to serialize max_sync_storage_schema_version", K(ret), K(len), K(new_pos), K_(max_sync_storage_schema_version));
  } else if (new_pos - pos < length && OB_FAIL(serialization::encode_i64(buf, len, new_pos, ddl_execution_id_))) {
    LOG_WARN("failed to serialize ddl execution id", K(ret), K(len), K(new_pos), K_(ddl_execution_id));
  } else if (new_pos - pos < length && OB_FAIL(serialization::encode_i64(buf, len, new_pos, ddl_data_format_version_))) {
    LOG_WARN("failed to serialize ddl data format version", K(ret), K(len), K(new_pos), K_(ddl_data_format_version));
  } else if (new_pos - pos < length && OB_FAIL(serialization::encode_i64(buf, len, new_pos, max_serialized_medium_scn_))) {
    LOG_WARN("failed to serialize max serialized medium scn", K(ret), K(len), K(new_pos), K_(max_serialized_medium_scn));
  } else if (new_pos - pos < length && OB_FAIL(ddl_commit_scn_.fixed_serialize(buf, len, new_pos))) {
    LOG_ERROR("failed to serialize ddl commit scn", K(ret), K(len), K(new_pos), K_(ddl_commit_scn));
  } else if (new_pos - pos < length && OB_FAIL(mds_checkpoint_scn_.fixed_serialize(buf, len, new_pos))) {
    LOG_ERROR("failed to serialize mds checkpoint ts", K(ret), K(len), K(new_pos), K_(mds_checkpoint_scn));
  } else if (new_pos - pos < length && OB_FAIL(serialization::encode_i64(buf, len, new_pos, create_schema_version_))) {
    LOG_ERROR("failed to serialize create schema version", K(ret), K(len), K(new_pos), K_(create_schema_version));
  } else if (new_pos - pos < length && OB_FAIL(space_usage_.serialize(buf, len, new_pos))) {
    LOG_WARN("failed to serialize tablet space usage", K(ret), K(len), K(new_pos), K_(space_usage));
  } else if (new_pos - pos < length && OB_FAIL(extra_medium_info_.serialize(buf, len, new_pos))) {
    LOG_WARN("failed to serialize extra_medium_info", K(ret), K(len), K(new_pos), K_(extra_medium_info));
  } else if (new_pos - pos < length && OB_FAIL(last_persisted_committed_tablet_status_.serialize(buf, len, new_pos))) {
    LOG_WARN("failed to serialize last_persisted_committed_tablet_status", K(ret), K(len), K(new_pos), K_(last_persisted_committed_tablet_status));
  } else if (new_pos - pos < length && OB_FAIL(serialization::encode_bool(buf, len, new_pos, is_empty_shell_))) {
    LOG_WARN("failed to serialize is_empty_shell", K(ret), K(len), K(new_pos), K_(is_empty_shell));
  } else if (new_pos - pos < length && OB_FAIL(serialization::encode_bool(buf, len, new_pos, micro_index_clustered_))) {
    LOG_ERROR("failed to serialize create schema version", K(ret), K(len), K(new_pos), K_(micro_index_clustered));
  } else if (new_pos - pos < length && OB_FAIL(serialization::encode_bool(buf, len, new_pos, has_truncate_info_))) {
    LOG_WARN("failed to serialize has truncate info", K(ret), K(len), K(new_pos), K_(has_truncate_info));
  } else if (new_pos - pos < length && OB_FAIL(fork_info_.serialize(buf, len, new_pos))) {
    LOG_WARN("failed to serialize fork info", K(ret), K(len), K(new_pos), K_(fork_info));
  } else if (OB_UNLIKELY(length != new_pos - pos)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("tablet meta's length doesn't match standard length", K(ret), K(new_pos), K(pos), K(length), K(length));
  } else {
    pos = new_pos;
    LOG_DEBUG("succeed to serialize tablet meta", KPC(this));
  }

  return ret;
}

int ObTabletMeta::deserialize(
    const char *buf,
    const int64_t len,
    int64_t &pos)
{
  int ret = OB_SUCCESS;
  int64_t new_pos = pos;

  if (OB_UNLIKELY(is_inited_)) {
    ret = OB_INIT_TWICE;
    LOG_WARN("cannot deserialize inited tablet meta", K(ret), K_(is_inited));
  } else if (OB_ISNULL(buf)
      || OB_UNLIKELY(len <= 0)
      || OB_UNLIKELY(pos < 0)
      || OB_UNLIKELY(len <= pos)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid args", K(ret), K(buf), K(len), K(pos));
  } else if (OB_FAIL(serialization::decode_i32(buf, len, new_pos, &version_))) {
    LOG_WARN("failed to deserialize tablet meta's version", K(ret), K(len), K(new_pos));
  } else if (OB_FAIL(serialization::decode_i32(buf, len, new_pos, &length_))) {
    LOG_WARN("failed to deserialize tablet meta's length", K(ret), K(len), K(new_pos));
  } else if (TABLET_META_VERSION == version_) {
    ddl_execution_id_ = 0;
    if (OB_UNLIKELY(length_ > len - pos)) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("buffer's length is not enough", K(ret), K(length_), K(len - new_pos));
    } else if (new_pos - pos < length_ && OB_FAIL(tablet_id_.deserialize(buf, len, new_pos))) {
      LOG_WARN("failed to deserialize tablet id", K(ret), K(len), K(new_pos));
    } else if (new_pos - pos < length_ && OB_FAIL(data_tablet_id_.deserialize(buf, len, new_pos))) {
      LOG_WARN("failed to deserialize data tablet id", K(ret), K(len), K(new_pos));
    } else if (new_pos - pos < length_ && OB_FAIL(ref_tablet_id_.deserialize(buf, len, new_pos))) {
      LOG_WARN("failed to deserialize ref tablet id", K(ret), K(len), K(new_pos));
    } else if (new_pos - pos < length_ && OB_FAIL(serialization::decode_bool(buf, len, new_pos, &has_next_tablet_))) {
      LOG_WARN("failed to deserialize has_next_tablet_", K(ret), K(len), K(new_pos));
    } else if (new_pos - pos < length_ && OB_FAIL(create_scn_.fixed_deserialize(buf, len, new_pos))) {
      LOG_WARN("failed to deserialize create scn", K(ret), K(len), K(new_pos));
    } else if (new_pos - pos < length_ && OB_FAIL(start_scn_.fixed_deserialize(buf, len, new_pos))) {
      LOG_WARN("failed to deserialize start scn", K(ret), K(len), K(new_pos));
    } else if (new_pos - pos < length_ && OB_FAIL(clog_checkpoint_scn_.fixed_deserialize(buf, len, new_pos))) {
      LOG_ERROR("failed to deserialize clog checkpoint ts", K(ret), K(len), K(new_pos));
    } else if (new_pos - pos < length_ && OB_FAIL(ddl_checkpoint_scn_.fixed_deserialize(buf, len, new_pos))) {
      LOG_ERROR("failed to deserialize ddl checkpoint ts", K(ret), K(len), K(new_pos));
    } else if (new_pos - pos < length_ && OB_FAIL(serialization::decode_i64(buf, len, new_pos, &snapshot_version_))) {
      LOG_WARN("failed to deserialize snapshot version", K(ret), K(len));
    } else if (new_pos - pos < length_ && OB_FAIL(serialization::decode_i64(buf, len, new_pos, &multi_version_start_))) {
      LOG_WARN("failed to deserialize multi version start", K(ret), K(len));
    } else if (new_pos - pos < length_ && OB_FAIL(restore_state_.deserialize(buf, len, new_pos))) {
      LOG_ERROR("failed to deserialize restore status", K(ret), K(len), K(new_pos));
    } else if (new_pos - pos < length_ && OB_FAIL(report_status_.deserialize(buf, len, new_pos))) {
      LOG_WARN("failed to deserialize report status", K(ret), K(len), K(new_pos));
    } else if (new_pos - pos < length_ && OB_FAIL(table_store_flag_.deserialize(buf, len, new_pos))) {
      LOG_WARN("failed to deserialize table store flag", K(ret), K(len), K(new_pos));
    } else if (new_pos - pos < length_ && OB_FAIL(ddl_start_scn_.fixed_deserialize(buf, len, new_pos))) {
      LOG_ERROR("failed to deserialize ddl start log ts", K(ret), K(len), K(new_pos));
    } else if (new_pos - pos < length_ && OB_FAIL(serialization::decode_i64(buf, len, new_pos, &ddl_snapshot_version_))) {
      LOG_WARN("failed to deserialize ddl snapshot version", K(ret), K(len), K(new_pos));
    } else if (new_pos - pos < length_ && OB_FAIL(serialization::decode_i64(buf, len, new_pos, &max_sync_storage_schema_version_))) {
      LOG_WARN("failed to deserialize max_sync_storage_schema_version", K(ret), K(len), K(new_pos));
    } else if (new_pos - pos < length_ && OB_FAIL(serialization::decode_i64(buf, len, new_pos, &ddl_execution_id_))) {
      LOG_WARN("failed to deserialize ddl execution id", K(ret), K(len), K(new_pos));
    } else if (new_pos - pos < length_ && OB_FAIL(serialization::decode_i64(buf, len, new_pos, &ddl_data_format_version_))) {
      LOG_WARN("failed to deserialize ddl data format version", K(ret), K(len), K(new_pos));
    } else if (new_pos - pos < length_ && OB_FAIL(serialization::decode_i64(buf, len, new_pos, &max_serialized_medium_scn_))) {
      LOG_WARN("failed to deserialize max serialized medium scn", K(ret), K(len), K(new_pos));
    } else if (new_pos - pos < length_ && OB_FAIL(ddl_commit_scn_.fixed_deserialize(buf, len, new_pos))) {
      LOG_ERROR("failed to deserialize ddl commit scn", K(ret), K(len), K(new_pos));
    } else if (new_pos - pos < length_ && OB_FAIL(mds_checkpoint_scn_.fixed_deserialize(buf, len, new_pos))) {
      LOG_ERROR("failed to deserialize mds checkpoint ts", K(ret), K(len), K(new_pos));
    } else if (new_pos - pos < length_ && OB_FAIL(serialization::decode_i64(buf, len, new_pos, &create_schema_version_))) {
      LOG_ERROR("failed to deserialize create schema version", K(ret), K(len), K(new_pos));
    } else if (new_pos - pos < length_ && OB_FAIL(space_usage_.deserialize(buf, len, new_pos))) {
      LOG_WARN("failed to deserialize tablet space usage", K(ret), K(len), K(new_pos));
    } else if (new_pos - pos < length_ && OB_FAIL(extra_medium_info_.deserialize(buf, len, new_pos))) {
      LOG_WARN("failed to deserialize extra_medium_info", K(ret), K(len), K(new_pos));
    } else if (new_pos - pos < length_ && OB_FAIL(last_persisted_committed_tablet_status_.deserialize(buf, len, new_pos))) {
      LOG_WARN("failed to deserialize last_persisted_committed_tablet_status", K(ret), K(len), K(new_pos));
    } else if (new_pos - pos < length_ && OB_FAIL(serialization::decode_bool(buf, len, new_pos, &is_empty_shell_))) {
      LOG_WARN("failed to deserialize is_empty_shell", K(ret), K(len), K(new_pos));
    } else if (new_pos - pos < length_ && OB_FAIL(serialization::decode_bool(buf, len, new_pos, &micro_index_clustered_))) {
      LOG_WARN("failed to deserialize micro_index_clustered", K(ret), K(len));
    } else if (new_pos - pos < length_ && OB_FAIL(serialization::decode_bool(buf, len, new_pos, &has_truncate_info_))) {
      LOG_WARN("failed to deserialize has_truncate_info", K(ret), K(len));
    } else if (new_pos - pos < length_ && OB_FAIL(fork_info_.deserialize(buf, len, new_pos))) {
      LOG_WARN("failed to deserialize fork info", K(ret), K(len), K(new_pos));
    } else if (OB_UNLIKELY(length_ != new_pos - pos)) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("tablet's length doesn't match standard length", K(ret), K(new_pos), K(pos), K_(length));
    } else {
      pos = new_pos;
      is_inited_ = true;
    }
    LOG_DEBUG("succeed to deserialize tablet meta", KPC(this));
  } else {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("invalid version", K(ret), K_(version));
  }

  return ret;
}

int64_t ObTabletMeta::get_serialize_size() const
{
  int64_t size = 0;
  size += serialization::encoded_length_i32(version_);
  size += serialization::encoded_length_i32(length_);
  size += tablet_id_.get_serialize_size();
  size += data_tablet_id_.get_serialize_size();
  size += ref_tablet_id_.get_serialize_size();
  size += serialization::encoded_length_bool(has_next_tablet_);
  size += create_scn_.get_fixed_serialize_size();
  size += start_scn_.get_fixed_serialize_size();
  size += clog_checkpoint_scn_.get_fixed_serialize_size();
  size += ddl_checkpoint_scn_.get_fixed_serialize_size();
  size += serialization::encoded_length_i64(snapshot_version_);
  size += serialization::encoded_length_i64(multi_version_start_);
  size += restore_state_.get_serialize_size();
  size += report_status_.get_serialize_size();
  size += table_store_flag_.get_serialize_size();
  size += ddl_start_scn_.get_fixed_serialize_size();
  size += serialization::encoded_length_i64(ddl_snapshot_version_);
  size += serialization::encoded_length_i64(max_sync_storage_schema_version_);
  size += serialization::encoded_length_i64(ddl_execution_id_);
  size += serialization::encoded_length_i64(ddl_data_format_version_);
  size += serialization::encoded_length_i64(max_serialized_medium_scn_);
  size += ddl_commit_scn_.get_fixed_serialize_size();
  size += mds_checkpoint_scn_.get_fixed_serialize_size();
  size += serialization::encoded_length_i64(create_schema_version_);
  size += space_usage_.get_serialize_size();
  size += extra_medium_info_.get_serialize_size();
  size += last_persisted_committed_tablet_status_.get_serialize_size();
  size += serialization::encoded_length_bool(is_empty_shell_);
  size += serialization::encoded_length_bool(micro_index_clustered_);
  size += serialization::encoded_length_bool(has_truncate_info_);
  size += fork_info_.get_serialize_size();
  return size;
}

int ObTabletMeta::init_report_info(
    const blocksstable::ObSSTable *sstable,
    const int64_t report_version,
    ObTabletReportStatus &report_status)
{
  int ret = OB_SUCCESS;

  if (OB_ISNULL(sstable) || !sstable->is_major_sstable() || report_version < 0) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("get invalid arguments", K(ret), K(sstable), K(report_version));
  } else if (sstable->get_snapshot_version() < report_status.merge_snapshot_version_) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("get unexpected merge snapshot version", K(ret), K(report_status), KPC(sstable));
  } else if (sstable->get_snapshot_version() == report_status.merge_snapshot_version_) {
  } else {
    report_status.reset();
    report_status.cur_report_version_ = report_version;
    report_status.merge_snapshot_version_ = sstable->get_snapshot_version();
    report_status.row_count_ = sstable->get_row_count();
    report_status.data_checksum_ = sstable->get_data_checksum();
  }
  return ret;
}

int ObTabletMeta::init_report_info(
    const blocksstable::ObMajorChecksumInfo &major_ckm_info,
    const int64_t report_version,
    ObTabletReportStatus &report_status)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(!major_ckm_info.is_valid() || report_version < 0)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("get invalid arguments", K(ret), K(major_ckm_info), K(report_version));
  } else if (major_ckm_info.get_compaction_scn() < report_status.merge_snapshot_version_) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("get unexpected merge snapshot version", K(ret), K(report_status), K(major_ckm_info));
  } else if (major_ckm_info.get_compaction_scn() == report_status.merge_snapshot_version_) {
  } else {
    report_status.reset();
    report_status.cur_report_version_ = report_version;
    report_status.merge_snapshot_version_ = major_ckm_info.get_compaction_scn();
    report_status.row_count_ = major_ckm_info.get_row_count();
    report_status.data_checksum_ = major_ckm_info.get_data_checksum();
    LOG_INFO("success to init report_status from major ckm info", KR(ret), K(report_status), K(major_ckm_info));
  }
  return ret;
}

SCN ObTabletMeta::get_ddl_sstable_start_scn() const
{
  return ddl_start_scn_.is_valid_and_not_min () ? share::SCN::scn_dec(ddl_start_scn_) : ddl_start_scn_;
}

share::SCN ObTabletMeta::get_max_replayed_scn() const
{
  return share::SCN::max(
      share::SCN::max(clog_checkpoint_scn_, mds_checkpoint_scn_),
      ddl_checkpoint_scn_);
}

void ObTabletMeta::update_extra_medium_info(
    const compaction::ObMergeType merge_type,
    const int64_t finish_medium_scn)
{
  if (is_major_merge_type(merge_type)) {
    extra_medium_info_.last_compaction_type_ = is_major_merge(merge_type) ? compaction::ObMediumCompactionInfo::MAJOR_COMPACTION : compaction::ObMediumCompactionInfo::MEDIUM_COMPACTION;
    extra_medium_info_.last_medium_scn_ = finish_medium_scn;
    extra_medium_info_.wait_check_flag_ = false;
  }
}

void ObTabletMeta::update_extra_medium_info(
    const compaction::ObExtraMediumInfo &local_extra_info,
    const compaction::ObExtraMediumInfo &remote_extra_info,
    const int64_t last_major_snapshot)
{
  const int64_t local_last_medium_scn = local_extra_info.last_medium_scn_;
  const int64_t remote_last_medium_scn = remote_extra_info.last_medium_scn_;

  if (local_last_medium_scn < remote_last_medium_scn || last_major_snapshot < local_last_medium_scn) {
    extra_medium_info_.last_compaction_type_ = remote_extra_info.last_compaction_type_;
    extra_medium_info_.last_medium_scn_ = remote_last_medium_scn;
    extra_medium_info_.wait_check_flag_ = false;
  } else { // use local extra medium info
    extra_medium_info_ = local_extra_info;
  }
}

int ObTabletMeta::update_meta_last_persisted_committed_tablet_status(
    const ObTabletTxMultiSourceDataUnit &tx_data,
    const share::SCN &create_commit_scn,
    ObTabletCreateDeleteMdsUserData &last_persisted_committed_tablet_status)
{
  int ret = OB_SUCCESS;
  if (tx_data.is_in_tx()) {
    last_persisted_committed_tablet_status.on_init();
  } else {
    ObTabletCreateDeleteMdsUserData user_data;
    user_data.tablet_status_ = tx_data.tablet_status_;
    user_data.create_commit_scn_ = create_commit_scn;
    user_data.create_commit_version_ = tx_data.tx_scn_.get_val_for_tx();
    if (ObTabletStatus::DELETED == tx_data.tablet_status_) {
      //TODO(bizhu) check deleted trans scn
      user_data.delete_commit_scn_ = tx_data.tx_scn_;
      //user_data.delete_commit_version_ = tx_data.tx_scn_;
    }
    if (OB_FAIL(last_persisted_committed_tablet_status.assign(user_data))) {
      LOG_WARN("failed to set last_persisted_committed_tablet_status", K(ret), K(user_data));
    }
  }
  return ret;
}

} // namespace storage
} // namespace oceanbase
