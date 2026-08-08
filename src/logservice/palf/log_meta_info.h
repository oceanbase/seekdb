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

#ifndef OCEANBASE_LOGSERVICE_LOG_META_INFO_
#define OCEANBASE_LOGSERVICE_LOG_META_INFO_

#include "lib/ob_define.h"
#include "lib/utility/ob_print_utils.h" // Print*
#include "share/scn.h"                        // SCN
#include "share/log/palf/lsn.h"                        // LSN
#include "share/log/palf/palf_base_info.h"             // LogInfo
#include "share/log/palf/palf_options.h"               // AccessMode
#include "share/log/palf/log_define.h"

namespace oceanbase
{
namespace palf
{

struct LogModeMeta {
public:
  LogModeMeta();
  ~LogModeMeta();
  int generate(const AccessMode &access_mode, const share::SCN &ref_scn);
  bool is_valid() const;
  void reset();
  void operator=(const LogModeMeta &mode_meta);
  TO_STRING_KV(K_(access_mode), K_(ref_scn));
  NEED_SERIALIZE_AND_DESERIALIZE;
public:
  AccessMode access_mode_;
  // scn lower bound
  // after switching over, scn of all submitted log should be bigger than ref_scn_
  share::SCN ref_scn_;

};

// Garbage collect controller
struct LogSnapshotMeta
{
public:
  LogSnapshotMeta();
  ~LogSnapshotMeta();

public:
  int generate(const LSN &base_lsn,
               const LogInfo &prev_log_info,
               const LSN &prev_log_tail_lsn);
  bool is_valid() const;
  void reset();
  int get_prev_log_info(const LSN &curr_lsn,
                        LogInfo &log_info,
                        LSN &tail_lsn) const;
  void operator=(const LogSnapshotMeta &log_snapshot_meta);
  TO_STRING_KV(K_(version), K_(base_lsn), K_(prev_log_info), K_(prev_log_tail_lsn));
  NEED_SERIALIZE_AND_DESERIALIZE;

  int64_t version_;
  LSN base_lsn_;
  // prev_log_info_ is invalid by default. Physical restore persists it when advancing
  // base_lsn so restart recovery does not need a discarded prefix log block.
  LogInfo prev_log_info_;
  LSN prev_log_tail_lsn_;

  static const int64_t LOG_SNAPSHOT_META_VERSION;
};

} // end namespace palf
} // end namespace oceanbase

#endif
