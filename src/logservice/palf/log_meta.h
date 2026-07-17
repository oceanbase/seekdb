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

#ifndef OCEANBASE_LOGSERVICE_LOG_META_
#define OCEANBASE_LOGSERVICE_LOG_META_

#include "log_meta_info.h"

namespace oceanbase
{
namespace palf
{
// LogMeta is not a mutil version data strucate, therefore,
// we must discard old message
//
// NB: not thread safe
class LogMeta
{
public:
  LogMeta();
  ~LogMeta();
  LogMeta(const LogMeta &rmeta);

public:
  int generate_by_palf_base_info(const PalfBaseInfo &palf_base_info,
                                 const AccessMode &access_mode);

  int load(const char *buf, int64_t buf_len);
  bool is_valid() const;
  void reset();

  LogModeMeta get_log_mode_meta() const { return log_mode_meta_; }
  LogSnapshotMeta get_log_snapshot_meta() const { return log_snapshot_meta_; }
  void operator=(const LogMeta &log_meta);

  // The follow functions used to set few fields of this object
  int update_log_snapshot_meta(const LogSnapshotMeta &log_snapshot_meta);

  TO_STRING_KV(K_(version), K_(log_snapshot_meta),
      K_(log_mode_meta));
  NEED_SERIALIZE_AND_DESERIALIZE;

private:
  int64_t version_;
  LogModeMeta log_mode_meta_;
  LogSnapshotMeta log_snapshot_meta_;
  static constexpr int64_t LOG_META_VERSION = 4;
};
} // end namespace palf
} // end namespace oceanbase

#endif
