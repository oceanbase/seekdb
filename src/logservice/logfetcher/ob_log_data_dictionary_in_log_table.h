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

#ifndef OCEANBASE_LOG_FETCHER_DATA_DICT_IN_LOG_TABLE_H_
#define OCEANBASE_LOG_FETCHER_DATA_DICT_IN_LOG_TABLE_H_

#include "logservice/palf/lsn.h"

namespace oceanbase
{
namespace logfetcher
{
// About OB_ALL_VIRTUAL_DATA_DICTIONARY_IN_LOG_TNAME
struct DataDictionaryInLogInfo
{
  DataDictionaryInLogInfo() { reset(); }

  bool is_valid() const
  {
    return common::OB_INVALID_TIMESTAMP != snapshot_scn_
      && common::OB_INVALID_TIMESTAMP != end_scn_
      && start_lsn_.is_valid()
      && end_lsn_.is_valid();
  }

  void reset()
  {
    snapshot_scn_ = common::OB_INVALID_TIMESTAMP;
    end_scn_ = common::OB_INVALID_TIMESTAMP;
    start_lsn_.reset();
    end_lsn_.reset();
  }

  void reset(
      const int64_t snapshot_scn,
      const int64_t end_scn,
      const palf::LSN &start_lsn,
      const palf::LSN &end_lsn)
  {
    snapshot_scn_ = snapshot_scn;
    end_scn_ = end_scn;
    start_lsn_ = start_lsn;
    end_lsn_ = end_lsn;
  }

  DataDictionaryInLogInfo &operator=(const DataDictionaryInLogInfo &other)
  {
    snapshot_scn_ = other.snapshot_scn_;
    end_scn_ = other.end_scn_;
    start_lsn_ = other.start_lsn_;
    end_lsn_ = other.end_lsn_;
    return *this;
  }

  TO_STRING_KV(
      K_(snapshot_scn),
      K_(end_scn),
      K_(start_lsn),
      K_(end_lsn));

  int64_t snapshot_scn_;
  int64_t end_scn_;
  palf::LSN start_lsn_;
  palf::LSN end_lsn_;
};

} // namespace logfetcher
} // namespace oceanbase

#endif
