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

#include "palf_base_info.h"

namespace oceanbase
{
namespace palf
{

LogInfo::LogInfo()
    : version_(-1),
      log_id_(OB_INVALID_LOG_ID),
      lsn_(),
      scn_(),
      log_proposal_id_(INVALID_PROPOSAL_ID),
      accum_checksum_(-1)
{}

LogInfo::~LogInfo()
{
  reset();
}

void LogInfo::reset()
{
  version_ = -1;
  log_id_ = OB_INVALID_LOG_ID;
  lsn_.reset();
  scn_.reset();
  log_proposal_id_ = INVALID_PROPOSAL_ID;
  accum_checksum_ = -1;
}

void LogInfo::operator=(const LogInfo &log_info)
{
  this->version_ = log_info.version_;
  this->log_id_ = log_info.log_id_;
  this->lsn_ = log_info.lsn_;
  this->scn_ = log_info.scn_;
  this->log_proposal_id_ = log_info.log_proposal_id_;
  this->accum_checksum_ = log_info.accum_checksum_;
}


bool LogInfo::operator!=(const LogInfo &log_info) const
{
  return false == (*this == log_info);
}

bool LogInfo::operator==(const LogInfo &log_info) const
{
  return log_id_ == log_info.log_id_ &&
         lsn_ == log_info.lsn_ &&
         scn_ == log_info.scn_ &&
         log_proposal_id_ == log_info.log_proposal_id_ &&
         accum_checksum_ == log_info.accum_checksum_;
}

bool LogInfo::is_valid() const
{
  return (lsn_.is_valid());
}

void LogInfo::generate_by_default()
{
  reset();
  version_ = LOG_INFO_VERSION;
  log_id_ = 0;
  LSN default_prev_lsn(PALF_INITIAL_LSN_VAL);
  lsn_ = default_prev_lsn;
  scn_.set_min();
  log_proposal_id_ = INVALID_PROPOSAL_ID;
  accum_checksum_ = -1;
}

OB_SERIALIZE_MEMBER(LogInfo, version_, log_id_, scn_, lsn_, log_proposal_id_, accum_checksum_);

PalfBaseInfo::PalfBaseInfo()
    : version_(-1),
      prev_log_info_(),
      curr_lsn_()
{}

PalfBaseInfo::~PalfBaseInfo()
{
  reset();
}

void PalfBaseInfo::reset()
{
  version_ = -1;
  prev_log_info_.reset();
  curr_lsn_.reset();
}

bool PalfBaseInfo::is_valid() const
{
  // NB: prev_log_info_ and curr_lsn_ are expected to be valid values, other fields may be invalid (scenario of the first global log)
  return (prev_log_info_.is_valid() && curr_lsn_.is_valid() && curr_lsn_ >= prev_log_info_.lsn_);
}

void PalfBaseInfo::operator=(const PalfBaseInfo &base_info)
{
  version_ = base_info.version_;
  prev_log_info_ = base_info.prev_log_info_;
  curr_lsn_ = base_info.curr_lsn_;
}

void PalfBaseInfo::generate_by_default()
{
  reset();
  version_ = PALF_BASE_INFO_VERSION;
  LSN default_prev_lsn(PALF_INITIAL_LSN_VAL);
  prev_log_info_.generate_by_default();
  curr_lsn_ = default_prev_lsn;
}

int PalfBaseInfo::generate(const LSN &lsn, const LogInfo &prev_log_info)
{
  int ret = OB_SUCCESS;
  if (false == prev_log_info.is_valid() ||
      false == lsn.is_valid() ||
      lsn < prev_log_info.lsn_) {
    ret = OB_INVALID_ARGUMENT; 
    PALF_LOG(WARN, "invalid argument", K(lsn), K(prev_log_info));
  } else {
    version_ = PALF_BASE_INFO_VERSION;
    curr_lsn_ = lsn;
    prev_log_info_ = prev_log_info;
  }
  return ret;
}
OB_SERIALIZE_MEMBER(PalfBaseInfo, version_, prev_log_info_, curr_lsn_);

} // end namespace palf
} // end namespace oceanbase
