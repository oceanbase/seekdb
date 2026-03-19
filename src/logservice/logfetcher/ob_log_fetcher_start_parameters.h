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

#ifndef OCEANBASE_LOG_FETCHER_START_PARAMETERS_H_
#define OCEANBASE_LOG_FETCHER_START_PARAMETERS_H_

#include "lib/ob_define.h"
#include "logservice/palf/lsn.h"
#include "ob_log_data_dictionary_in_log_table.h"  // DataDictionaryInLogInfo

namespace oceanbase
{
namespace logfetcher
{
class ObLogFetcherStartParameters
{
public:
  ObLogFetcherStartParameters() { reset(); }

  void reset()
  {
    start_tstamp_ns_ = common::OB_INVALID_TIMESTAMP;
    start_lsn_.reset();
    end_tstamp_ns_ = common::OB_INVALID_TIMESTAMP;
    end_lsn_.reset();
    data_dict_in_log_info_.reset();
  }

  // For Physical Standby
  void reset(
      const int64_t start_tstamp_ns,
      const palf::LSN &start_lsn,
      const int64_t proposal_id)
  {
    reset();
    start_tstamp_ns_ = start_tstamp_ns;
    start_lsn_ = start_lsn;
    proposal_id_ = proposal_id;
  }

  // For OBCDC
  void reset(
      const int64_t start_tstamp_ns,
      const palf::LSN &start_lsn)
  {
    reset();
    start_tstamp_ns_ = start_tstamp_ns;
    start_lsn_ = start_lsn;
  }

  // This interface is called to get the baseline data and incremental data of the data dictionary at startup time.
  // For OBCDC
  void reset(
      const int64_t start_tstamp_ns,
      const int64_t end_tstamp_ns,
      const DataDictionaryInLogInfo &data_dict_in_log_info)
  {
    start_tstamp_ns_ = start_tstamp_ns;
    end_tstamp_ns_ = end_tstamp_ns;
    data_dict_in_log_info_ = data_dict_in_log_info;
  }

  inline int64_t get_start_tstamp_ns() const { return start_tstamp_ns_; }
  inline const palf::LSN &get_start_lsn() const { return start_lsn_; }
  inline int64_t get_end_tstamp_ns() const { return end_tstamp_ns_; }
  inline const palf::LSN &get_end_lsn() const { return end_lsn_; }

  inline int64_t get_proposal_id() const { return proposal_id_; }

  inline const DataDictionaryInLogInfo &get_data_dict_in_log_info() const { return data_dict_in_log_info_; }

  inline void set_end_lsn(const palf::LSN &end_lsn) { end_lsn_ = end_lsn; }

  ObLogFetcherStartParameters &operator=(const ObLogFetcherStartParameters &other)
  {
    start_tstamp_ns_ = other.start_tstamp_ns_;
    start_lsn_ = other.start_lsn_;
    end_tstamp_ns_ = other.end_tstamp_ns_;
    end_lsn_ = other.end_lsn_;
    proposal_id_ = other.proposal_id_;
    data_dict_in_log_info_ = other.data_dict_in_log_info_;

    return *this;
  }

  TO_STRING_KV(
      K_(start_tstamp_ns),
      K_(start_lsn),
      K_(end_tstamp_ns),
      K_(end_lsn),
      K_(proposal_id),
      K_(data_dict_in_log_info));

private:
  int64_t start_tstamp_ns_;
  palf::LSN start_lsn_;
  int64_t end_tstamp_ns_;
  palf::LSN end_lsn_;
  int64_t proposal_id_;
  DataDictionaryInLogInfo data_dict_in_log_info_;
};
} // namespace logfetcher
} // namespace oceanbase

#endif
