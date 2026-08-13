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

#include "log_meta_info.h"

namespace oceanbase
{
using namespace common;
using namespace share;
namespace palf
{

LogModeMeta::LogModeMeta()
    : access_mode_(AccessMode::INVALID_ACCESS_MODE),
      ref_scn_()
{}

LogModeMeta::~LogModeMeta()
{
  reset();
}

int LogModeMeta::generate(const AccessMode &access_mode,
                          const SCN &ref_scn)
{
  int ret = OB_SUCCESS;
  if (false == is_valid_access_mode(access_mode) ||
      !ref_scn.is_valid()) {
    ret = OB_INVALID_ARGUMENT;
  } else {
    access_mode_ = access_mode;
    ref_scn_ = ref_scn;
  }
  return ret;
}

bool LogModeMeta::is_valid() const
{
  return is_valid_access_mode(access_mode_) &&
         ref_scn_.is_valid();
}

void LogModeMeta::reset()
{
  access_mode_ = AccessMode::INVALID_ACCESS_MODE;
  ref_scn_.reset();
}

void LogModeMeta::operator=(const LogModeMeta &mode_meta)
{
  this->access_mode_ = mode_meta.access_mode_;
  this->ref_scn_ = mode_meta.ref_scn_;
}

DEFINE_SERIALIZE(LogModeMeta)
{
  int ret = OB_SUCCESS;
  int64_t new_pos = pos;
  if (NULL == buf || 0 >= buf_len) {
    ret = OB_INVALID_ARGUMENT;
  } else if (buf_len - new_pos < get_serialize_size()) {
    ret = OB_BUF_NOT_ENOUGH;
  } else if (OB_FAIL(serialization::encode_i64(buf, buf_len, new_pos, static_cast<int64_t>(access_mode_))) ||
             OB_FAIL(ref_scn_.fixed_serialize(buf, buf_len, new_pos))) {
    PALF_LOG(ERROR, "LogModeMeta serialize failed", K(ret), K(new_pos));
  } else {
    pos = new_pos;
  }
  return ret;
}

DEFINE_DESERIALIZE(LogModeMeta)
{
  int ret = OB_SUCCESS;
  int64_t new_pos = pos;
  if (NULL == buf || 0 >= data_len) {
    ret = OB_INVALID_ARGUMENT;
  } else if (OB_FAIL(serialization::decode_i64(buf, data_len, new_pos, reinterpret_cast<int64_t *>(&access_mode_))) ||
             OB_FAIL(ref_scn_.fixed_deserialize(buf, data_len, new_pos))) {
    PALF_LOG(ERROR, "LogModeMeta deserialize failed", K(ret), K(new_pos));
  } else {
    pos = new_pos;
  }
  return ret;
}

DEFINE_GET_SERIALIZE_SIZE(LogModeMeta)
{
  int64_t size = 0;
  size += serialization::encoded_length_i64(static_cast<int64_t>(access_mode_));
  size += ref_scn_.get_fixed_serialize_size();
  return size;
}

constexpr int64_t LogSnapshotMeta::LOG_SNAPSHOT_META_VERSION = 2;

LogSnapshotMeta::LogSnapshotMeta() : version_(-1), base_lsn_(), prev_log_info_(), prev_log_tail_lsn_()
{}

LogSnapshotMeta::~LogSnapshotMeta()
{
  reset();
}

int LogSnapshotMeta::generate(const LSN &lsn,
                              const LogInfo &prev_log_info,
                              const LSN &prev_log_tail_lsn)
{
  int ret = OB_SUCCESS;
  if (!lsn.is_valid() || !prev_log_info.is_valid() || !prev_log_tail_lsn.is_valid()) {
    ret = OB_INVALID_ARGUMENT;
  } else {
    version_ = LOG_SNAPSHOT_META_VERSION;
    base_lsn_ = lsn;
    prev_log_info_ = prev_log_info;
    prev_log_tail_lsn_ = prev_log_tail_lsn;
  }
  return ret;
}

bool LogSnapshotMeta::is_valid() const
{
  return LOG_SNAPSHOT_META_VERSION == version_
      && base_lsn_.is_valid()
      && prev_log_info_.is_valid()
      && prev_log_tail_lsn_.is_valid();
}

void LogSnapshotMeta::reset()
{
  base_lsn_.reset();
  prev_log_info_.reset();
  prev_log_tail_lsn_.reset();
  version_ = -1;
}

int LogSnapshotMeta::get_prev_log_info(const LSN &curr_lsn,
                                       LogInfo &log_info,
                                       LSN &tail_lsn) const
{
  int ret = OB_SUCCESS;
  log_info.reset();
  tail_lsn.reset();
  if (LOG_SNAPSHOT_META_VERSION != version_) {
    ret = OB_NOT_SUPPORTED;
  } else if (curr_lsn != prev_log_tail_lsn_) {
    ret = OB_ENTRY_NOT_EXIST;
  } else {
    log_info = prev_log_info_;
    tail_lsn = prev_log_tail_lsn_;
  }
  return ret;
}

void LogSnapshotMeta::operator=(const LogSnapshotMeta &log_snapshot_meta)
{
  this->version_ = log_snapshot_meta.version_;
  this->base_lsn_ = log_snapshot_meta.base_lsn_;
  this->prev_log_info_ = log_snapshot_meta.prev_log_info_;
  this->prev_log_tail_lsn_ = log_snapshot_meta.prev_log_tail_lsn_;
}

DEFINE_SERIALIZE(LogSnapshotMeta)
{
  int ret = OB_SUCCESS;
  int64_t new_pos = pos;
  if (OB_UNLIKELY(nullptr == buf || buf_len < 0 || pos < 0 || pos > buf_len)) {
    ret = OB_INVALID_ARGUMENT;
  } else if (LOG_SNAPSHOT_META_VERSION != version_) {
    ret = OB_VERSION_NOT_MATCH;
    PALF_LOG(ERROR, "log snapshot metadata format version mismatch",
             K(ret), K_(version), K(LOG_SNAPSHOT_META_VERSION));
  } else if (buf_len - new_pos < get_serialize_size()) {
    ret = OB_BUF_NOT_ENOUGH;
  } else if (OB_FAIL(serialization::encode_i64(buf, buf_len, new_pos, version_)) ||
             OB_FAIL(base_lsn_.serialize(buf, buf_len, new_pos)) ||
             OB_FAIL(prev_log_info_.serialize(buf, buf_len, new_pos)) ||
             OB_FAIL(prev_log_tail_lsn_.serialize(buf, buf_len, new_pos))) {
    PALF_LOG(ERROR, "LogSnapshotMeta serialize failed", K(ret), K(new_pos));
  } else {
    pos = new_pos;
  }
  return ret;
}

DEFINE_DESERIALIZE(LogSnapshotMeta)
{
  int ret = OB_SUCCESS;
  int64_t new_pos = pos;
  int64_t decoded_version = -1;
  if (OB_UNLIKELY(nullptr == buf || data_len < 0 || pos < 0 || pos > data_len)) {
    ret = OB_INVALID_ARGUMENT;
  } else if (data_len - pos < serialization::encoded_length_i64(decoded_version)) {
    ret = OB_BUF_NOT_ENOUGH;
  } else if (OB_FAIL(serialization::decode_i64(buf, data_len, new_pos, &decoded_version))) {
  } else if (LOG_SNAPSHOT_META_VERSION != decoded_version) {
    ret = OB_VERSION_NOT_MATCH;
    PALF_LOG(ERROR, "log snapshot metadata format version mismatch",
             K(ret), K(decoded_version), K(LOG_SNAPSHOT_META_VERSION));
  } else if (FALSE_IT(version_ = decoded_version)) {
  } else if (OB_FAIL(base_lsn_.deserialize(buf, data_len, new_pos)) ||
             OB_FAIL(prev_log_info_.deserialize(buf, data_len, new_pos)) ||
             OB_FAIL(prev_log_tail_lsn_.deserialize(buf, data_len, new_pos))) {
    PALF_LOG(ERROR, "LogSnapshotMeta deserialize failed", K(ret), K(new_pos));
  } else if (LOG_SNAPSHOT_META_VERSION != version_) {
    ret = OB_NOT_SUPPORTED;
    PALF_LOG(ERROR, "unsupported LogSnapshotMeta version", K(ret), K_(version));
  } else {
    pos = new_pos;
  }
  return ret;
}

DEFINE_GET_SERIALIZE_SIZE(LogSnapshotMeta)
{
  int64_t size = 0;
  size += serialization::encoded_length_i64(version_);
  size += base_lsn_.get_serialize_size();
  size += prev_log_info_.get_serialize_size();
  size += prev_log_tail_lsn_.get_serialize_size();
  return size;
}

} // end namespace palf
} // end namespace oceanbase
