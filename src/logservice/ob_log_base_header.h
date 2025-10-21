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

#ifndef OCEANBASE_LOGSERVICE_OB_LOG_BASE_HEADER_
#define OCEANBASE_LOGSERVICE_OB_LOG_BASE_HEADER_

#include "lib/ob_define.h"
#include <stdint.h>                           // int64_t...
#include <string.h>                           // strncmp...
#include "ob_log_base_type.h"

namespace oceanbase
{
namespace logservice
{
// ObReplayBarrierType is the barrier type for follower log replay, divided into the following three categories
// 1. STRICT_BARRIER:
//    The prerequisite condition for replaying this log is that all logs with a log ts smaller than this log have already been replayed,
//    And before the replay of this log is complete, logs with a log ts greater than this log will not be replayed.
// 2. PRE_BARRIER:
//    The prerequisite condition for replaying this log is that all logs with a log ts smaller than this log have already been replayed,
//    But the replay of this log can be completed before logs with a log ts greater than this log are replayed.
// 3. NO_NEED_BARRIER:
//    This log has no special replay conditions and will not affect the replay of any other logs
enum ObReplayBarrierType
{
  INVALID_BARRIER = 0,
  STRICT_BARRIER = 1,
  PRE_BARRIER = 2,
  NO_NEED_BARRIER = 3,
};

class ObLogBaseHeader {
public:
  ObLogBaseHeader();
  ObLogBaseHeader(const ObLogBaseType log_type,
                  const enum ObReplayBarrierType replay_barrier_type);
  ObLogBaseHeader(const ObLogBaseType log_type,
                  const enum ObReplayBarrierType replay_barrier_type,
                  const int64_t replay_hint);
  ~ObLogBaseHeader();
public:
  void reset();
  void set_compressed();
  bool is_compressed() const;
  bool need_pre_replay_barrier() const;
  bool need_post_replay_barrier() const;
  ObLogBaseType get_log_type() const;
  int64_t get_replay_hint() const;
  NEED_SERIALIZE_AND_DESERIALIZE;
  TO_STRING_KV("version", version_,
                "log_type", log_type_,
                "flag", flag_,
                "need_pre_replay_barrier", need_pre_replay_barrier(),
                "need_post_replay_barrier", need_post_replay_barrier(),
                "is_compressed", is_compressed(),
                "replay_hint", replay_hint_);
private:
  static const int16_t BASE_HEADER_VERSION = 1;
  static const uint32_t NEED_POST_REPLAY_BARRIER_FLAG = (1 << 31);
  static const uint32_t NEED_PRE_REPLAY_BARRIER_FLAG = (1 << 30);
  static const uint32_t PAYLOAD_IS_COMPRESSED = (1 << 29);
  int16_t version_;
  int16_t log_type_;
  int32_t flag_;
  int64_t replay_hint_;
};

} // namespace logservice
} // namespace oceanbase

#endif // OCEANBASE_LOGSERVICE_OB_LOG_BASE_HEADER_
