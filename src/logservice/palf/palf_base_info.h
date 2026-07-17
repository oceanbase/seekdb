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

#ifndef OCEANBASE_LOGSERVICE_PALF_BASE_INFO_
#define OCEANBASE_LOGSERVICE_PALF_BASE_INFO_

#include "lib/utility/ob_unify_serialize.h"                    // OB_UNIS_VERSION
#include "share/scn.h"
#include "lsn.h"

namespace oceanbase
{
namespace palf
{
struct LogInfo
{
  OB_UNIS_VERSION(1);
public:
  LogInfo();
  ~LogInfo();
  void reset();
  bool is_valid() const;
  void operator=(const LogInfo &log_info);
  bool operator==(const LogInfo &log_info) const;
  bool operator!=(const LogInfo &log_info) const;
  void generate_by_default();

  TO_STRING_KV(K_(log_id), K_(lsn), K_(scn), K_(log_proposal_id), K_(accum_checksum));

  static constexpr int64_t LOG_INFO_VERSION = 1;
  int64_t version_;
  int64_t log_id_;
  LSN lsn_;
  share::SCN scn_;
  int64_t log_proposal_id_;
  int64_t accum_checksum_;
};

struct PalfBaseInfo
{
  OB_UNIS_VERSION(1);
public:
  PalfBaseInfo();
  ~PalfBaseInfo();
  void reset();
  bool is_valid() const;
  void operator=(const PalfBaseInfo &base_info);
  void generate_by_default();
  int generate(const LSN &lsn, const LogInfo &prev_log_info);

  TO_STRING_KV(K_(prev_log_info), K_(curr_lsn));

  static constexpr int64_t PALF_BASE_INFO_VERSION = 1;
  int64_t version_;
  LogInfo prev_log_info_;
  LSN curr_lsn_;  // It's equal to base_lsn.
};
} // end namespace palf
} // end namespace oceanbase
#endif  // OCEANBASE_LOGSERVICE_PALF_BASE_INFO_
