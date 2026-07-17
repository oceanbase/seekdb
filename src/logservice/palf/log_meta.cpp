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

#include "log_meta.h"

namespace oceanbase
{
namespace palf
{
using namespace common;
using namespace share;
LogMeta::LogMeta() : version_(-1),
                     log_mode_meta_(),
                     log_snapshot_meta_()
{
}

LogMeta::~LogMeta() { reset(); }

LogMeta::LogMeta(const LogMeta &rmeta) { *this = rmeta; }

int LogMeta::generate_by_palf_base_info(const PalfBaseInfo &palf_base_info,
                                        const AccessMode &access_mode)
{
  int ret = OB_SUCCESS;
  if (false == is_valid_access_mode(access_mode) || false == palf_base_info.is_valid()) {
    ret = OB_INVALID_ARGUMENT;
    PALF_LOG(INFO, "invalid argument", KPC(this), K(access_mode), K(palf_base_info));
  } else if (OB_FAIL(log_snapshot_meta_.generate(palf_base_info.curr_lsn_, palf_base_info.prev_log_info_, palf_base_info.curr_lsn_))) {
    PALF_LOG(WARN, "generate snapshot_meta failed", K(ret), K(palf_base_info));
  } else {
    const SCN &prev_scn = palf_base_info.prev_log_info_.scn_;
    const SCN init_ref_scn = (prev_scn.is_valid() ? prev_scn: SCN::min_scn());
    version_ = LOG_META_VERSION;
    log_mode_meta_.generate(access_mode, init_ref_scn);
    PALF_LOG(INFO, "generate_by_palf_base_info success", KPC(this));
  }
  return ret;
}

int LogMeta::load(const char *buf, int64_t buf_len)
{
  int ret = OB_SUCCESS;
  int64_t pos = 0;
  if (NULL == buf || 0 >= buf_len) {
    ret = OB_INVALID_ARGUMENT;
    PALF_LOG(ERROR, "Invalid argument!!!", K(ret), K(buf), K(buf_len));
  } else if (OB_FAIL(this->deserialize(buf, buf_len, pos))) {
    PALF_LOG(ERROR, "deserialize failed", K(ret));
  }
  return ret;
}

bool LogMeta::is_valid() const
{
  return LOG_META_VERSION == version_
         && true == log_mode_meta_.is_valid()
         && true == log_snapshot_meta_.is_valid();
}

void LogMeta::reset()
{
  version_ = -1;
  log_mode_meta_.reset();
  log_snapshot_meta_.reset();
  version_ = -1;
}

int LogMeta::update_log_snapshot_meta(const LogSnapshotMeta &log_snapshot_meta)
{
  int ret = OB_SUCCESS;
  if (false == log_snapshot_meta.is_valid()) {
    ret = OB_INVALID_ARGUMENT;
    PALF_LOG(ERROR, "Invalid argument!!!", K(ret), K(log_snapshot_meta));
  } else {
    log_snapshot_meta_ = log_snapshot_meta;
  }
  return ret;
}

void LogMeta::operator=(const LogMeta &log_meta)
{
  version_ = log_meta.version_;
  log_mode_meta_ = log_meta.log_mode_meta_;
  log_snapshot_meta_ = log_meta.log_snapshot_meta_;
}

DEFINE_SERIALIZE(LogMeta)
{
  int ret = OB_SUCCESS;
  int64_t new_pos = pos;
  if (OB_UNLIKELY(NULL == buf || buf_len < 0)) {
    ret = OB_INVALID_ARGUMENT;
  } else if (OB_FAIL(serialization::encode_i64(buf, buf_len, new_pos, version_))
             || OB_FAIL(log_mode_meta_.serialize(buf, buf_len, new_pos))
             || OB_FAIL(log_snapshot_meta_.serialize(buf, buf_len, new_pos))) {
    PALF_LOG(ERROR, "LogMeta serialize failed", K(ret), K(buf), K(buf_len), K(pos), K(new_pos));
  } else {
    pos = new_pos;
    PALF_LOG(INFO, "LogMeta serialize", K(*this), K(buf), KP(buf), K(pos));
  }
  return ret;
}

DEFINE_DESERIALIZE(LogMeta)
{
  int ret = OB_SUCCESS;
  int64_t new_pos = pos;
  if (OB_UNLIKELY(NULL == buf || data_len < 0)) {
    ret = OB_INVALID_ARGUMENT;
  } else if (OB_FAIL(serialization::decode_i64(buf, data_len, new_pos, &version_))) {
    PALF_LOG(ERROR, "decode LogMeta version failed", K(ret), K(data_len), K(new_pos));
  } else if (LOG_META_VERSION != version_) {
    ret = OB_NOT_SUPPORTED;
    PALF_LOG(ERROR, "unsupported LogMeta version", K(ret), K_(version));
  } else if (OB_FAIL(log_mode_meta_.deserialize(buf, data_len, new_pos))
             || OB_FAIL(log_snapshot_meta_.deserialize(buf, data_len, new_pos))) {
    PALF_LOG(ERROR, "LogMeta deserialize failed", K(ret), K(buf), K(data_len), K(pos), K(new_pos));
  } else {
    PALF_LOG(INFO, "LogMeta deserialize", K(buf), K(buf + pos), K(pos), K(new_pos), K(*this));
    pos = new_pos;
  }
  return ret;
}

DEFINE_GET_SERIALIZE_SIZE(LogMeta)
{
  int64_t size = 0;
  size += serialization::encoded_length_i64(version_);
  size += log_mode_meta_.get_serialize_size();
  size += log_snapshot_meta_.get_serialize_size();
  return size;
}
} // end namespace palf
} // end namespace oceanbase
