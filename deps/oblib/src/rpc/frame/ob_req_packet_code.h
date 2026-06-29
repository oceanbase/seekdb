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

#ifndef OCEANBASE_RPC_FRAME_OB_REQ_PACKET_CODE_H_
#define OCEANBASE_RPC_FRAME_OB_REQ_PACKET_CODE_H_

// Self-contained packet-code tags. The obcall RPC transport (packet /
// packet-list / packet-set) has been deleted; nothing dispatches on these codes
// any more. They survive only as compile-time tags for the dead Table-API / CDC
// type bindings and for diagnostics that still record a pcode. Values mirror the
// historical obcall packet-list ids so any persisted logs/diagnostics stay
// numerically stable.

#include <stdint.h>
#include "lib/string/ob_string.h"
// These two were previously surfaced transitively via the deleted
// ob_call_packet.h (which this header replaces in the universally-included
// thread.h). Keep providing them so existing files that relied on the old
// transitive include surface (ObCompressorType in share/ob_define.h,
// ObCurTraceId in low-level allocators) keep compiling.
#include "lib/compress/ob_compressor.h"
#include "lib/profile/ob_trace_id.h"

namespace oceanbase
{
// The obcall namespace still holds the in-process arg/result data structs.
// The deleted ob_call_packet.h used to open it transitively via thread.h, which
// made bare 'using namespace oceanbase::obcall;' directives valid codebase-wide.
// Keep that surface alive (this header replaced ob_call_packet.h in thread.h).
namespace obcall {}
namespace rpc
{
namespace frame
{

enum ObReqPacketCode : int32_t
{
  OB_INVALID_RPC_CODE = 0,

  // 0x002, 0x225, 0x276-0x27A, and 0x27C are reserved for removed packet codes.

  // CDC / logfetcher type-binding tags
  OB_LS_FETCH_MISSING_LOG  = 0x851,
  OB_LS_FETCH_LOG2         = 0x853,
  OB_LOG_REQ_START_LSN_BY_TS = 0x855,
  OB_CDC_FETCH_RAW_LOG     = 0x863,

  // 0x1101-0x1128 are reserved for removed packet codes.
};


// ---------------------------------------------------------------------------
// RPC checksum-check level. Still LIVE: driven by the _rpc_checksum sys config
// (ob_config_helper / ob_server_reload_config). Relocated here from the deleted
// obcall transport header so the config path keeps compiling. The accessor names
// keep the rpc_checksum wording because they mirror the _rpc_checksum config.
// ---------------------------------------------------------------------------
enum class ObReqCheckSumCheckLevel
{
  INVALID,
  FORCE,
  OPTIONAL,
  DISABLE
};

extern ObReqCheckSumCheckLevel g_rpc_checksum_check_level;

inline void set_rpc_checksum_check_level(
  const ObReqCheckSumCheckLevel rpc_checksum_check_level)
{
  g_rpc_checksum_check_level = rpc_checksum_check_level;
}

inline ObReqCheckSumCheckLevel get_rpc_checksum_check_level()
{
  return g_rpc_checksum_check_level;
}

inline ObReqCheckSumCheckLevel get_rpc_checksum_check_level_from_string(
  const common::ObString &string)
{
  ObReqCheckSumCheckLevel ret_type = ObReqCheckSumCheckLevel::INVALID;
  if (0 == string.case_compare("Force")) {
    ret_type = ObReqCheckSumCheckLevel::FORCE;
  } else if (0 == string.case_compare("Optional")) {
    ret_type = ObReqCheckSumCheckLevel::OPTIONAL;
  } else if (0 == string.case_compare("Disable")) {
    ret_type = ObReqCheckSumCheckLevel::DISABLE;
  }
  return ret_type;
}

} // namespace frame
} // namespace rpc
} // namespace oceanbase

#endif // OCEANBASE_RPC_FRAME_OB_REQ_PACKET_CODE_H_
